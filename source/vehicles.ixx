// SPDX-License-Identifier: GPL-3.0-or-later
//
// vehicles
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

#include <bit>
#include <cerrno>
#include <numbers>
export module opp.vehicles;

import opp.config;
import opp.patch;
import opp.game;
import opp.common;
import opp.log;

export namespace opp
{
    // The model tables the popup, siren and wheel-angle arms fill, and the size
    // the light-table API exposes them at.
    inline constexpr size_t kModelTableSlots = 512;
}

namespace opp
{
    // The width of the direct-mapped memos below.
    inline constexpr size_t kMemoSlots = 64;

    // GameHash("popuplights"): the one model hash the popup checker and the
    // light-table selector both resolve before anything else.
    inline constexpr uint32_t kPopupLightsModelHash = 0xEB9F21D3u;

    // The light tables store degrees; the bodies and the selector consume radians.
    constexpr float DegreesToRadians(double degrees) noexcept
    {
        return static_cast<float>(degrees * std::numbers::pi / 180.0);
    }
}  // namespace opp

// 32-bit only: the hook bodies and the readable helpers they call. The x64 parity
// dump resolves the body names to the original VAs instead of linking them.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

namespace
{
    // ----- CVehicle / CHandlingData views ------------------------------------
    // The CVehicle prefix these bodies read; 0x0E18 points at the handling block
    // whose steerLock is the steering-lock divisor. The structs are views over
    // live game memory and are never constructed; the offsets live in the
    // padding, where the layout is declared.
    struct VehicleHandling final
    {
        std::byte pad_0000[132];  // steerLock at 0x0084
        float steerLock;  // 0x0084
    };

    struct VehicleView final
    {
        std::byte pad_0000[46];  // modelIndex at 0x002E
        int16_t modelIndex;  // 0x002E
        std::byte pad_0030[3560];  // handling at 0x0E18
        VehicleHandling* handling;  // 0x0E18
        std::byte pad_0E1C[328];  // kind at 0x0F64
        uint32_t kind;  // 0x0F64: 5 = plane
        std::byte pad_0F68[352];  // rpm at 0x10C8
        float rpm;  // 0x10C8
        std::byte pad_10CC[12];  // steerAngle at 0x10D8
        float steerAngle;  // 0x10D8
        std::byte pad_10DC[632];  // engineState at 0x1354
        uint32_t engineState;  // 0x1354
        std::byte pad_1358[2984];  // heave at 0x1F00
        float heave;  // 0x1F00
        float roll;   // 0x1F04
        float pitch;  // 0x1F08
        std::byte pad_1F0C[48];  // suspensionTravel at 0x1F3C
        float suspensionTravel;  // 0x1F3C
    };

    // ----- MovingPlaneParts -------------------------------------------------
    // The component ids the arm hands to the published setter; 96/97 are the
    // pitch pair, 94/95 both negate roll, 93 is heave, and the travel surfaces
    // are called 89, 91, 90, 92 in that order (the original's, not sorted).
    enum : uint8_t
    {
        kPartTravelA = 89,
        kPartTravelB = 91,
        kPartTravelC = 90,
        kPartTravelD = 92,
        kPartHeave = 93,
        kPartRollA = 94,
        kPartRollB = 95,
        kPartPitchUp = 96,
        kPartPitchDown = 97,
        kPartModeAttitude = 3,
        kPartModeTravel = 4,
        kPartModeHeave = 5,
    };

    // One row per call the arm makes. A travel row (attitude == nullptr) scales
    // the script float by the suspension travel; an attitude row takes half the
    // named body angle with the signed gain applied.
    struct PlanePartCall final
    {
        uint8_t part;
        uint8_t mode;
        float VehicleView::* attitude;
        double gain;
        uint8_t flags;  // the setter's fifth argument: 0 for travel, 1 for attitude
    };

    constexpr PlanePartCall kPlanePartCalls[] = {
        {kPartTravelA, kPartModeTravel, nullptr, 1.0, 0},
        {kPartTravelB, kPartModeTravel, nullptr, 1.0, 0},
        {kPartTravelC, kPartModeTravel, nullptr, 1.0, 0},
        {kPartTravelD, kPartModeTravel, nullptr, 1.0, 0},
        {kPartPitchUp, kPartModeAttitude, &VehicleView::pitch, +0.5, 1},
        {kPartPitchDown, kPartModeAttitude, &VehicleView::pitch, -0.5, 1},
        {kPartRollA, kPartModeAttitude, &VehicleView::roll, -0.5, 1},
        {kPartRollB, kPartModeAttitude, &VehicleView::roll, -0.5, 1},
        {kPartHeave, kPartModeHeave, &VehicleView::heave, +0.5, 1},
    };

    // ----- other named offsets ----------------------------------------------
    // strnicmp(text, "wheelangle:", 11), then strtof(text + 11).
    constexpr std::string_view kWheelAnglePrefix = "wheelangle:";

    // ----- Memo -------------------------------------------------------------
    // A direct-mapped memo of a pure key -> value function. The slot is
    // (key * 2654435761) >> (32 - log2(Slots)), so a collision only costs a
    // recomputation. Function-local instances of this type are
    // constant-initialised, which keeps them free of CRT/TLS-backed static init.
    template <typename Value, size_t Slots>
    class Memo final
    {
    public:
        bool Get(uint32_t key, Value& out) const noexcept
        {
            const Slot& slot = slots_[SlotFor(key)];
            if (slot.valid == 0u || slot.key != key)
                return false;
            out = slot.value;
            return true;
        }

        void Put(uint32_t key, const Value& value) noexcept
        {
            Slot& slot = slots_[SlotFor(key)];
            slot.key = key;
            slot.value = value;
            slot.valid = 1u;
        }

    private:
        static_assert(std::has_single_bit(Slots), "Memo needs a power-of-two slot count");
        static constexpr size_t kShift = 32u - static_cast<size_t>(std::bit_width(Slots - 1));

        struct Slot final
        {
            uint32_t key{};
            Value value{};
            uint8_t valid{};
        };

        static size_t SlotFor(uint32_t key) noexcept
        {
            // The Knuth 2^32/phi multiplier: the slot is the top kShift bits.
            return static_cast<size_t>(key * 2654435761u) >> kShift;
        }

        std::array<Slot, Slots> slots_{};
    };
}  // namespace

extern "C"
{

// Hook bodies: the machine-level entry points the arms below install. The
// definitions are in the marked hook-body section at the end of this unit; they
// are declared here because the arms take their addresses.
void abi_popup_headlights();
void abi_popup_headlights_edx();
void abi_steer_detour_eax();
void abi_steer_detour_edx();
void abi_nolivery_a();
void abi_nolivery_b();
void abi_nolivery_c();
void abi_nolivery_d();
void abi_nolivery_e();
void abi_copsirens_a();
void abi_copsirens_b();
void abi_copsirens_ide();
void abi_bike_physics_detour();

// HighFPSBikePhysicsFix helper (original sub_1001EF30): clamp zpatch_bike_value
// below 0.006666666665 to 0x3BDA740E. SSE in the original.
void abi_bike_physics_helper() noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &zpatch_bike_value, sizeof(value));
    // The floor is the float 0.0066666667 (its bits live in opp.common); the
    // recovered threshold is a double, so the comparison widens value.
    if (0.006666666665 > static_cast<double>(value))
        zpatch_bike_value = std::bit_cast<uint32_t>(kBikePhysicsFloor);
}

// PopupHeadlightsFlag checker (original sub_10044310), called between
// pushad/popad: look up the popuplights hash, walk the +popuplights list, then
// copy the last matching [PopupLightAngles] angle (starts at pi/2). A failed
// lookup leaves -1, which matches a model of -1, as in the original.
namespace
{
// PopupAngleFor: the [PopupLightAngles] table is loaded once at startup, so the
// angle a matching model maps to is remembered instead of rescanning all 512
// entries on every matching vehicle. The original takes the *last* matching
// entry, which the forward scan below keeps.
uint32_t PopupAngleFor(uint32_t hash) noexcept
{
    static Memo<uint32_t, kMemoSlots> angle_memo;
    uint32_t bits = 0;
    if (angle_memo.Get(hash, bits))
        return bits;
    bits = std::bit_cast<uint32_t>(kHalfPi);  // the caller's default
    for (uint32_t i = 0; i < kModelTableSlots; ++i)
    {
        if (zpatch_popup_hashes[i] != 0u && zpatch_popup_hashes[i] == hash)
            std::memcpy(&bits, &zpatch_popup_angles[i], sizeof(bits));
    }
    angle_memo.Put(hash, bits);
    return bits;
}
}  // namespace

void abi_popup_check() noexcept
{
    const auto lookup =
        Rebased<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup);
    zpatch_popup_angle = std::bit_cast<uint32_t>(kHalfPi);  // dword_101580CC
    zpatch_popup_flag = 0;                                 // dword_1015BE24
    if (lookup == nullptr)
        return;  // a preamble miss is a clean skip
    // PopupLookupCache: a model hash resolves to a model index once, because the
    // registry is static after load. Direct-mapped; a collision re-resolves.
    static Memo<int32_t, kModelTableSlots> resolve_memo;
    const auto resolve = [&](uint32_t hash) -> int32_t
    {
        int32_t resolved = -1;
        if (resolve_memo.Get(hash, resolved))
            return resolved;
        lookup(hash, &resolved);  // sub_10044310's call is unguarded by default
        resolve_memo.Put(hash, resolved);
        return resolved;
    };
    // The popuplights hash is the one the original resolves and compares first
    // (FUN_1001ad20).
    int32_t out = resolve(kPopupLightsModelHash);
    zpatch_popup_index = static_cast<uint32_t>(out);  // dword_1015BE1C
    if (out == static_cast<int32_t>(zpatch_popup_model))
    {
        zpatch_popup_flag = 1;
        return;
    }
    for (uint32_t i = 0; i < kModelTableSlots; ++i)
    {
        const uint32_t hash = zpatch_popuplights_models[i];
        if (hash == 0u) return;
        out = resolve(hash);
        if (out != static_cast<int32_t>(zpatch_popup_model)) continue;
        zpatch_popup_flag = 1;
        zpatch_popup_angle = PopupAngleFor(hash);
        return;
    }
}

// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts.
// abi_vehicle_component is the original sub_10003F40: a name lookup in the
// model registry (entries at [registry], stride 0xE0, count +0x14, name +0,
// bone id +0x14). abi_steer_body is the shared consumer (original
// sub_10004C70), called between pushad/popad by the two detours.
const void* abi_vehicle_component(const void* registry,
                                  const char* name) noexcept
{
    // sub_10003F40 dereferences the registry and each candidate name unguarded;
    // the default-on failsafes below keep a missing table out of the body.
    if (registry == nullptr) return nullptr;
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(registry);
    if (entries == nullptr) return nullptr;
    // The registry's component count is its uint16 at +0x14.
    const auto count = *reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(registry) + 20);
    for (uint16_t i = 0; i < count; ++i)
    {
        // A component entry is 0xE0 bytes.
        const auto* entry = entries + static_cast<size_t>(i) * 224;
        const char* candidate = *reinterpret_cast<const char* const*>(entry);
        if (candidate == nullptr) continue;
        if (_stricmp(candidate, name) == 0) return entry;
    }
    return nullptr;
}

uint32_t abi_steer_body() noexcept
{
    auto* const vehicle = Rebased<VehicleView*>(zpatch_steer_vehicle);
    if (vehicle == nullptr) return 0;

    using BoneFn = uint32_t*(__thiscall*)(void*, uint32_t);
    using SetFn = void(__thiscall*)(uint32_t*, float);
    using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
    const auto lookup =
        Rebased<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup);
    const auto get_bone = GameFunction<BoneFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59b88));
    const auto set_a = GameFunction<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_5d63c));
    const auto set_b = GameFunction<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59cdc));
    const auto part_call = GameFunction<PartFn>(
        static_cast<uintptr_t>(zpatch_steer_vtable_fn));

    // The moving-parts body calls four game functions and the model lookup; a
    // missing preamble pointer or a vehicle whose vtable slot was not published
    // would fault in the original. The clean skip returns 0, which the resume
    // reads as "no component moved this frame".
    if (zpatch_failsafe_skip("moving-parts model lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("moving-parts bone lookup", get_bone == nullptr) ||
        zpatch_failsafe_skip("moving-parts setter A", set_a == nullptr) ||
        zpatch_failsafe_skip("moving-parts setter B", set_b == nullptr) ||
        zpatch_failsafe_skip("moving-parts component call", part_call == nullptr))
        return 0;

    // sub_10003980's script float: the context's +0x0E field holds the slot
    // offset, the slot lives at ctx+offset+0x18 and the float at slot+8. It is
    // resolved on every read as the original does. The slot itself is what the arm
    // returns; an empty slot reads as 0 (the original dereferences it - the folded
    // guard is this tree's failsafe).
    constexpr size_t kScriptFloatOffset = 8;
    const auto script_slot = [&]() -> const uint32_t*
    {
        const uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
        if (ctx == 0u) return nullptr;
        const uint32_t offset = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ctx) + 14);
        const uint32_t slot = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ctx) + offset + 24);
        if (slot == 0u) return nullptr;
        return reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(slot));
    };
    const auto script_float = [&]() -> float
    {
        const uint32_t* slot = script_slot();
        if (slot == nullptr) return 0.0f;
        return *reinterpret_cast<const float*>(
            reinterpret_cast<uintptr_t>(slot) + kScriptFloatOffset);
    };
    // 4x4 identity: the three diagonal entries are the float 1.0.
    constexpr uint32_t kIdentityOne = std::bit_cast<uint32_t>(1.0f);
    static_assert(kIdentityOne == 0x3F800000u);
    const auto identity = [](uint32_t* m)
    {
        m[0] = kIdentityOne;
        m[1] = 0;
        m[2] = 0;
        m[4] = 0;
        m[5] = kIdentityOne;
        m[6] = 0;
        m[8] = 0;
        m[9] = 0;
        m[10] = kIdentityOne;
    };
    const auto bone_matrix = [&](int16_t model, const char* name) -> uint32_t*
    {
        const auto base = static_cast<uintptr_t>(zpatch_pre_ped_array);
        if ((base == 0u || model < 0)) return nullptr;
        const uint32_t ped = *reinterpret_cast<const uint32_t*>(
            base + 4u * static_cast<uint32_t>(model));
        if (ped == 0u) return nullptr;
        // The chain is ped -> [ped+0x8] -> [+0xB4] -> [+0xC]; each link is a
        // pointer the original dereferences unguarded.
        const uint32_t p1 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ped) + 8);
        if (p1 == 0u) return nullptr;
        const uint32_t p2 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p1) + 180);
        if (p2 == 0u) return nullptr;
        const uint32_t registry = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p2) + 12);
        if (registry == 0u) return nullptr;
        const auto* entry = static_cast<const uint8_t*>(abi_vehicle_component(
            reinterpret_cast<const void*>(static_cast<uintptr_t>(registry)), name));
        if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
        // sub_10004C70 0x10004D4A calls get_bone unguarded (it is null only when
        // the loader's preamble missed).
        // The component entry's bone id is its uint16 at +0x14.
        const uint32_t bone = *reinterpret_cast<const uint16_t*>(entry + 20);
        return get_bone(vehicle, bone);
    };

    // The steering block reads the vehicle's handling block; the original
    // dereferences it unguarded, so the default-on check skips the block while
    // there is none. There is no selectable guard-off path in this tree.
    const int16_t steer_model = vehicle->modelIndex;
    const VehicleHandling* const handling = vehicle->handling;
    if (zpatch_flag_steering && handling != nullptr)
    {
        const int16_t model = steer_model;
        const float steering = vehicle->steerAngle;
        const float lock = handling->steerLock;
        const float turn = static_cast<float>(
            static_cast<double>(steering / lock) * -std::numbers::pi);
        // SteerLookupCache: the wheelangle table is static after startup, so a
        // model's tilt is stable. Direct-mapped by model; a collision re-walks.
        static Memo<float, kMemoSlots> tilt_memo;
        float tilt = 0.0f;
        const uint32_t tilt_key = static_cast<uint32_t>(static_cast<uint16_t>(model));
        if (!tilt_memo.Get(tilt_key, tilt))
        {
            for (uint32_t i = 0; i < kModelTableSlots; ++i)
            {
                const uint32_t hash = zpatch_wheelangle_models[i];
                if (hash == 0u) break;
                int32_t resolved = -1;
                lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
                if (resolved == model)
                {
                    tilt = DegreesToRadians(zpatch_wheelangle_angles[i]);
                    break;
                }
            }
            tilt_memo.Put(tilt_key, tilt);
        }
        if (model != -1)
        {
            uint32_t* matrix = bone_matrix(model, "steeringwheel");
            if (matrix != nullptr)
            {
                identity(matrix);
                if (set_a) set_a(matrix, tilt);
                if (set_b) set_b(matrix, turn);
            }
        }
    }
    if (zpatch_flag_engine)
    {
        // Engine-state bits: 3 is "started"; 4 is the original's "running" flag,
        // which forces the block-shake scale to 1. The phase walks with a signed
        // step and a direction deadband; the shake scales from the rpm.
        constexpr double kEnginePhaseStep = 0.5;
        constexpr double kEnginePhaseDeadband = 0.01;
        if (zpatch_engine_last_timestep != script_float())
        {
            const double step =
                zpatch_engine_descending ? -kEnginePhaseStep : kEnginePhaseStep;
            const float phase = static_cast<float>(
                static_cast<double>(script_float()) * step +
                static_cast<double>(zpatch_engine_phase));
            zpatch_engine_phase = phase;
            uint8_t direction = zpatch_engine_descending;
            if (static_cast<double>(phase) > kEnginePhaseDeadband) direction = 1;
            if (static_cast<double>(phase) < -kEnginePhaseDeadband) direction = 0;
            zpatch_engine_descending = direction;
        }
        const int16_t model = vehicle->modelIndex;
        if (model != -1)
        {
            uint32_t* matrix = bone_matrix(model, "engineblock");
            if (matrix != nullptr)
            {
                identity(matrix);
                const uint32_t flags = vehicle->engineState;
                float scale = 0.0f;
                if ((flags & (1u << 3)) != 0u)
                {
                    scale = static_cast<float>(
                        static_cast<double>(vehicle->rpm) * 0.75 + 0.25);
                }
                if ((flags & (1u << 4)) != 0u) scale = 1.0f;
                if (set_b) set_b(matrix, scale * zpatch_engine_phase);
            }
        }
    }
    // MovingPlaneParts: one call per row, in the table's (original) order. The
    // travel rows each resolve the script float; the attitude rows read their
    // body angle when they run. The gate is the kind dword's plane value.
    if (zpatch_flag_plane && vehicle->kind == 5 && part_call != nullptr)
    {
        const float travel = vehicle->suspensionTravel;
        for (const PlanePartCall& part : kPlanePartCalls)
        {
            const float value =
                part.attitude == nullptr
                    ? script_float() * travel * 100.0f
                    : static_cast<float>(
                          static_cast<double>(vehicle->*(part.attitude)) * part.gain);
            part_call(vehicle, part.part, part.mode, value, part.flags, 0, 0);
        }
    }
    // Unconditional tail: snapshot the last script timestep and return its slot.
    const uint32_t* slot = script_slot();
    if (slot == nullptr)
    {
        zpatch_engine_last_timestep = 0.0f;
        return 0;
    }
    const float last = *reinterpret_cast<const float*>(
        reinterpret_cast<uintptr_t>(slot) + kScriptFloatOffset);
    zpatch_engine_last_timestep = last;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(slot));
}

// NoLiveryLimit helpers (sub_1003D220 / sub_1003D2F0 / sub_1003D270). The naked
// shims stash their register inputs in globals, call a compiled helper, then
// tail-jump to the handler-published resume. The count helper calls the
// published function as `__thiscall(this, hash)`.
extern "C" void abi_nolivery_hash_sign()  // sub_1003D220
{
    char buffer[64];
    // Original: sprintf(buffer, 0x40, "%s_sign_%d", name, index + 1).
    zpatch_spawns_sprintf(
        buffer, sizeof(buffer), "%s_sign_%d",
        Rebased<const char*>(zpatch_nolivery_name),
        static_cast<int>(zpatch_nolivery_index) + 1);
    zpatch_nolivery_hash = GameHash(buffer);
    zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name()  // sub_1003D2F0
{
    // push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc; ret.
    // Reads both globals unguarded and has no /GS cookie tail (leaf).
    char* const dst = Rebased<char*>(zpatch_nolivery_dest);
    const char* const src = Rebased<const char*>(zpatch_nolivery_bufptr);
    // The original copies into a 16-byte name buffer (sub_1003D2F0 pushes 0x10
    // as the strcpy_s destination size).
    zpatch_secure_strcpy_s(dst, 16, src);
}
extern "C" void abi_nolivery_count_loop()  // sub_1003D270
{
    using LookupFn = int(__fastcall*)(void* self, int, uint32_t hash);
    const auto fn = GameFunction<LookupFn>(static_cast<uintptr_t>(zpatch_nolivery_fn));
    void* const self = Rebased<void*>(zpatch_nolivery_arg);
    const char* const name = Rebased<const char*>(zpatch_nolivery_bufptr);
    zpatch_nolivery_count = 0;
    for (uint32_t i = 1;; ++i)
    {
        char buffer[64];
        zpatch_spawns_sprintf(buffer, sizeof(buffer), "%s_sign_%d", name,
                              static_cast<int>(i));
        const int result = fn ? fn(self, 0, GameHash(buffer)) : 0;
        if (result == 0) break;
        zpatch_nolivery_count = i;
    }
    zpatch_cookie_tail();
}

// CopSirensFlag helpers (sub_10005F70 / sub_10006260) and the reimplemented IDE
// parser (sub_10003FA0). The shims stash the model index, run the check and tail-
// jump to the resume; the IDE hook fills the model-hash tables the checks read.
extern "C" int abi_copsirens_check_a()  // sub_10005F70
{
    static const char* const kPolice[] =
    {
            "police",     "police2",    "police3",  "police4",   "noose",      "pstockade",
            "nstockade",  "polpatriot", "fbi",      "n_police",  "n_police2",  "n_polpatriot",
            "n_pstockade", nullptr};
    const auto lookup =
        Rebased<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup);
    const int32_t index = static_cast<int32_t>(zpatch_copsirens_index);
    zpatch_copsirens_flag = 0;
    // sub_10005F70's `call dword_10159C0C` is unguarded. A missing lookup
    // answers "not a police car", which leaves the siren decision un-patched.
    if (zpatch_failsafe_skip("CopSirensFlag model lookup", lookup == nullptr))
        return 0;
    for (int i = 0; kPolice[i]; ++i)
    {
        int32_t out = -1;
        lookup(GameHash(kPolice[i]), &out);
        if (out == index)
        {
            zpatch_copsirens_flag = 1;
            return 1;
        }
    }
    // Original sub_10005F70 re-stores the flag before the models loop
    // (mov eax,[0x10159B80]; ...; mov [0x10159B80],eax); keep the store.
    {
        volatile uint32_t* flag = &zpatch_copsirens_flag;
        *flag = *flag;
    }
    for (uint32_t i = 0; i < kModelTableSlots && zpatch_copsirens_models[i]; ++i)
    {
        int32_t out = -1;
        lookup(zpatch_copsirens_models[i], &out);
        if (out == index)
        {
            zpatch_copsirens_flag = 1;
            return 1;
        }
    }
    return 0;
}
extern "C" int abi_copsirens_check_b()  // sub_10006260
{
    const auto lookup =
        Rebased<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup);
    const int32_t index = static_cast<int32_t>(zpatch_copsirens_index_b);
    zpatch_copsirens_flag_b = 0;
    int32_t out = -1;
    // sub_10006260's `call dword_10159C0C` is unguarded.
    if (zpatch_failsafe_skip("CopSirensFlag model lookup (B)", lookup == nullptr))
        return 0;
    lookup(GameHash("police2"), &out);
    if (out != index)
    {
        if (!zpatch_copsirens_models[0]) return 0;
        for (uint32_t i = 0; i < kModelTableSlots && zpatch_copsirens_models[i]; ++i)
        {
            out = -1;
            lookup(zpatch_copsirens_models[i], &out);
            if (out == index)
            {
                zpatch_copsirens_flag_b = 1;
                return 1;
            }
        }
        return 0;
    }
    zpatch_copsirens_flag_b = 1;
    return 1;
}
// _strnicmp equivalent: the original's IDE parser takes the case-insensitive
// prefix branch (strnicmp(v0, "wheelangle:", 0xB)); stops at a NUL like the CRT.
bool prefix_ci(const char* text, const char* prefix, size_t length) noexcept
{
    for (size_t i = 0; i < length; ++i)
    {
        const unsigned char a = static_cast<unsigned char>(text[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (a == 0 || b == 0) return a == b;
        const unsigned char lower_a =
            (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
        const unsigned char lower_b =
            (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
        if (lower_a != lower_b) return false;
    }
    return true;
}

extern "C" void abi_ide_parse()  // sub_10003FA0
{
    // The original tests the string unguarded; a null zpatch_ide_string faults.
    const char* const str = Rebased<const char*>(zpatch_ide_string);
    // The IDE entry's model index is the dword at +0x3C (not the entity's int16
    // at +0x2E, hooks.h kModelIndexOffset).
    const uint32_t model =
        *Rebased<const uint32_t*>(static_cast<uintptr_t>(zpatch_ide_model) + 60);
    const auto append = [](uint32_t* array, uint32_t value)
    {
        for (uint32_t i = 0; i < kModelTableSlots; ++i)
        {
            if (array[i] == value) return;
            if (array[i] == 0)
            {
                array[i] = value;
                return;
            }
        }
    };
    // The original's arms are independent `if`s on the same string (the
    // popuplights block falls through to the siren check), not else-if.
    if (std::strcmp(str, "popuplights") == 0)
    {
        append(zpatch_popuplights_models, model);
    }
    if (std::strcmp(str, "siren") == 0)
    {
        append(zpatch_copsirens_models, model);
    }
    if (prefix_ci(str, kWheelAnglePrefix.data(), kWheelAnglePrefix.size()))
    {
        // sub_10003FA0's third arm: strnicmp(...,11) then strtof(text + 11). An
        // existing model entry is left untouched (the original's scan only breaks
        // out); the angle is stored only on append.
        const float angle = std::strtof(str + kWheelAnglePrefix.size(), nullptr);
        uint32_t bits = 0;
        std::memcpy(&bits, &angle, sizeof(bits));
        for (uint32_t i = 0; i < kModelTableSlots; ++i)
        {
            if (zpatch_wheelangle_models[i] == model) return;
            if (zpatch_wheelangle_models[i] == 0)
            {
                zpatch_wheelangle_models[i] = model;
                zpatch_wheelangle_angles[i] = bits;
                return;
            }
        }
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{
    namespace
    {
        // ----- one literal per recovered site ---------------------------------
        // MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts all
        // hook the same two steering calls, and PopupHeadlightsFlag / CopSirensFlag
        // share the IDE parser slot, so each signature is spelled once.
        constexpr const char* kSteerEaxPattern =
            "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0";
        constexpr const char* kSteerEdxPattern =
            "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2";
        constexpr const char* kIdeHookPattern = "0C 83 88 94 00 00 00 04";
        // The shared IDE parser hook slot is the byte before the match; the arms
        // that install abi_copsirens_ide jump in ten bytes before it.
        constexpr ptrdiff_t kIdeHookSlotOffset = -1;
        constexpr ptrdiff_t kIdeHookInstallOffset = -10;
        constexpr const char* kPopupEaxPattern = "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1";
        constexpr const char* kPopupEdxPattern = "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1";
        constexpr const char* kCopSirensCallPattern = "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00";
        constexpr const char* kCopSirensModelPattern = "0F BF 41 2E 39 05";

        // ----- NOP sites ------------------------------------------------------
        // One NOP the original installs at a recovered site: `offset` is relative
        // to the match, `length` the number of bytes.
        struct NopByteRange final
        {
            ptrdiff_t offset;
            size_t length;
        };

        // One recovered NOP site. `expected` 0 patches the first match; N means
        // PatternAll + ExpectMatchCount(N) (the P13 rule), so a view with a
        // different site count installs nothing. `secondary` is {0, 0} when one
        // NOP is enough, and `countLabel` names a multi-match row in the log.
        struct NopSite final
        {
            const char* signature;
            NopByteRange primary;
            NopByteRange secondary;
            unsigned expected;
            const char* countLabel;
        };

        template <size_t Count>
        void ApplyNopSites(const NopSite (&sites)[Count])
        {
            for (const NopSite& site : sites)
            {
                if (site.expected != 0u)
                {
                    if (auto pattern = find_pattern<0>(site.signature);
                        pattern.size() > 0 &&
                        ExpectMatchCount(pattern, site.expected, site.countLabel))
                    {
                        for (size_t match = 0; match < site.expected; ++match)
                        {
                            injector::MakeNOP(
                                pattern.get(match).get<uint8_t>() + site.primary.offset,
                                site.primary.length, true);
                        }
                    }
                    continue;
                }
                if (auto pattern = find_pattern(site.signature); pattern.size() > 0)
                {
                    auto* match = pattern.get_first<uint8_t>();
                    injector::MakeNOP(match + site.primary.offset, site.primary.length, true);
                    if (site.secondary.length != 0u)
                        injector::MakeNOP(match + site.secondary.offset,
                                          site.secondary.length, true);
                }
            }
        }

        // Every build except 1.0.0.0 and 1.0.4.0: the episodic arms' recovered gate.
        constexpr GameBuild kEpisodicBuilds[] = {
            GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
            GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
            GameBuild::v1130, GameBuild::v12043,
        };

        bool IsEpisodicBuild(GameBuild build) noexcept
        {
            for (const GameBuild candidate : kEpisodicBuilds)
            {
                if (candidate == build) return true;
            }
            return false;
        }

        // The episodes' `cmp [zpatch_episode_flag], 2` pointer stores. Each site was
        // a per-build RVA (0x021500/0x7D91AE on 1.0.7.0, 0x095040/0x684ABE on
        // 1.0.8.0) and is now its pre-store instruction sequence. The build gate
        // stays: both signatures also resolve on builds where the original did not
        // install this option.
        struct FlagStoreSite final
        {
            GameBuild build;
            const char* signature;
        };

        constexpr FlagStoreSite kEpisodeFlagSites[] = {
            {GameBuild::v1070, "44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"},
            {GameBuild::v1070,
             "44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"},
            {GameBuild::v1080, "54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"},
            {GameBuild::v1080,
             "54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"},
        };

        // ----- the arms' NOP tables -------------------------------------------
        // One row per recovered site, in the order its arm installs it.
        constexpr NopSite kBikeFeetFixSites[] = {
            // The `jne` after the engine-state compare. `54 13 00 00` in the
            // signature is CVehicle::engineState (0x1354); a pattern string cannot
            // spell an offset by name.
            {"39 82 54 13 00 00 0F 85 94 04 00 00", {12, 12}, {0, 0}, 0, nullptr},
        };

        constexpr NopSite kBikePhoneAnimsFixSites[] = {
            {"80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE", {33, 6}, {0, 0}, 0, nullptr},
        };

        constexpr NopSite kEpisodicVehicleSites[] = {
            // The two recovered component-window sites, eight bytes before the match.
            {"7C 18 0F BF 4E 2E", {-6, 8}, {0, 0}, 2,
             "EpisodicVehicleSupport component window"},
            // `cmp [zpatch_episode_flag], 2` before the component call.
            {"83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08", {0, 9}, {0, 0}, 2,
             "EpisodicVehicleSupport episode test"},
            {"39 35 ? ? ? ? 75 0E 80 3D", {6, 2}, {0, 0}, 0, nullptr},
            {"74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05", {2, 9}, {0, 0}, 0, nullptr},
            {"7C 22 83 F8 28 75 1D", {0, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 0F 8C 0E 05 00 00", {0, 13}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 08 3B 05", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 1F 8B 44 24 04", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 35 0F BF 46 2E", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 38 8B 16", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 11 8B 4E", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 1F 8B", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 35 0F BF 43 2E", {0, 9}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 43", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E", {0, 13}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 89 44 24 38", {11, 6}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 8A 0D", {0, 7}, {17, 2}, 0, nullptr},
            {"83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C",
             {21, 2}, {0, 0}, 0, nullptr},
            {"8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18", {-15, 7}, {10, 2}, 0, nullptr},
        };

        constexpr NopSite kEpisodicWeaponSites[] = {
            {"75 5B 83 3D ? ? ? ? 00 75", {0, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 0F 85 8C 01 00 00", {7, 6}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 0F 8C D5 00", {0, 13}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 20 8B CD E8", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 75 50", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 32 8B 41", {7, 2}, {0, 0}, 0, nullptr},
            {"83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", {7, 2}, {0, 0}, 0, nullptr},
        };

        constexpr NopSite kPoliceEpisodicSites[] = {
            {"39 2D ? ? ? ? 75 3E", {6, 2}, {0, 0}, 0, nullptr},
        };

        constexpr NopSite kHeliHeightLimitSites[] = {
            {"83 3D ? ? ? ? 02 7C 0A F3 0F", {7, 2}, {0, 0}, 0, nullptr},
        };

        // ----- the shared steering sites --------------------------------------
        void PublishSteerSlots()
        {
            // The first pattern is the EAX call variant, the second the EDX one;
            // the calls' resumes are 16 bytes into the match.
            constexpr ptrdiff_t kSteerResumeOffset = 16;
            // The call each variant makes, as the E8 operand's offset.
            if (auto pattern = find_pattern(kSteerEaxPattern); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, kSteerResumeOffset);
                zpatch_steer_vtable_fn = BranchDestination(
                    pattern.get_first<uint8_t>(), 423);
            }
            if (auto pattern = find_pattern(kSteerEdxPattern); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, kSteerResumeOffset);
                zpatch_steer_vtable_fn = BranchDestination(
                    pattern.get_first<uint8_t>(), 140);
            }
            if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                zpatch_ide_hook_slot = MatchAddress(pattern, kIdeHookSlotOffset);
        }

        void InstallSteerSlots()
        {
            // The E9 replaces the call at match+2.
            constexpr ptrdiff_t kSteerInstallOffset = 2;
            if (auto pattern = find_pattern(kSteerEaxPattern); pattern.size() > 0)
                injector::MakeJMP(pattern.get_first<uint8_t>() + kSteerInstallOffset,
                                  OPP_BODY(abi_steer_detour_eax), true);
            if (auto pattern = find_pattern(kSteerEdxPattern); pattern.size() > 0)
                injector::MakeJMP(pattern.get_first<uint8_t>() + kSteerInstallOffset,
                                  OPP_BODY(abi_steer_detour_edx), true);
            if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                injector::MakeJMP(pattern.get_first<uint8_t>() + kIdeHookInstallOffset,
                                  OPP_BODY(abi_copsirens_ide), true);
        }

        // ----- PopupHeadlightsFlag --------------------------------------------
        // The original handler (0x10044590) is an if/else over the EAX/EDX pair, so
        // exactly one site is ever patched and its two resume slots are the ones
        // the body tail-jumps through.
        void StorePopupResumes(hook::pattern& pattern) noexcept
        {
            // The branch resume, 10 bytes into the match, and the fall-through one.
            zpatch_popup_resume_b = MatchAddress(pattern, 10);
            zpatch_popup_resume_a = MatchAddress(pattern, 12);
        }

        // The jump at the match start, the `jz` at +10 and the `fld` operand at +82
        // that now loads our pi/2 float.
        void WritePopupSite(hook::pattern& pattern, void* body)
        {
            auto* match = pattern.get_first<uint8_t>();
            injector::MakeJMP(match, body, true);
            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(10),
                                           opp::ops::kJmpRel8, true);
            injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(82),
                                            AddressOf(&zpatch_popup_angle), true);
        }

        // ----- CopSirensFlag --------------------------------------------------
    }  // namespace

    // BikeFeetFix

    class BikeFeetFix
    {
    public:
        BikeFeetFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallBikeFeetFix");

                // BikeFeetFix: 1 literal
                if (Game::Option("BikeFeetFix", "1"))
                    ApplyNopSites(kBikeFeetFixSites);
            };
        }
    } bikeFeetFix;


    // BikePhoneAnimsFix

    class BikePhoneAnimsFix
    {
    public:
        BikePhoneAnimsFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallBikePhoneAnimsFix");

                // BikePhoneAnimsFix: 1 literal
                if (Game::Option("BikePhoneAnimsFix", "1"))
                    ApplyNopSites(kBikePhoneAnimsFixSites);
            };
        }
    } bikePhoneAnimsFix;


    // CopSirensFlag

    class CopSirensFlag
    {
    public:
        CopSirensFlag()
        {
            PublishEvent() += []
            {
                TraceArm("PublishCopSirensFlag");

                // CopSirensFlag: 3 slots
                if (Game::Option("CopSirensFlag", "1"))
                {
                    // The resume after the five-byte E8, and the second resume 28 bytes
                    // into the model match.
                    if (auto pattern = find_pattern(kCopSirensCallPattern); pattern.size() > 0)
                    {
                        zpatch_copsirens_resume = MatchAddress(pattern, 5);
                    }

                    if (auto pattern = find_pattern(kCopSirensModelPattern); pattern.size() > 0)
                    {
                        zpatch_copsirens_resume_b = MatchAddress(pattern, 28);
                    }

                    if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                    {
                        // The shared IDE parser hook slot is the byte before the match.
                        zpatch_ide_hook_slot = MatchAddress(pattern, kIdeHookSlotOffset);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallCopSirensFlag");

                // CopSirensFlag: 3 siginstalls, 2 sigjumps
                if (Game::Option("CopSirensFlag", "1"))
                {
                    if (auto pattern = find_pattern(kCopSirensCallPattern); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_copsirens_a), true);
                    }

                    if (auto pattern = find_pattern(kCopSirensModelPattern); pattern.size() > 0)
                    {
                        // The model-check detour's entry, 22 bytes into the match.
                        injector::MakeJMP(pattern.get_first<uint8_t>(22), OPP_BODY(abi_copsirens_b), true);
                    }

                    if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // The shared IDE parser hook is installed 10 bytes before the
                        // match (the body rewrites the match-1 slot).
                        injector::MakeJMP(match + kIdeHookInstallOffset, OPP_BODY(abi_copsirens_ide), true);
                    }
                }
            };
        }
    } copSirensFlag;


    // EpisodicVehicleSupport

    class EpisodicVehicleSupport
    {
    public:
        EpisodicVehicleSupport()
        {
            InstallEvent() += []
            {
                TraceArm("InstallEpisodicVehicleSupport");

                // EpisodicVehicleSupport: the four episode-flag pointer stores and the
                // recovered NOP sites.
                if (Game::Option("EpisodicVehicleSupport", "1"))
                {
                    for (const FlagStoreSite& site : kEpisodeFlagSites)
                    {
                        if (site.build != Game::Get().build) continue;
                        if (auto pattern = find_pattern(site.signature); pattern.size() > 0)
                        {
                            injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(), AddressOf(&zpatch_episode_flag), true);
                        }
                    }

                    if (IsEpisodicBuild(Game::Get().build))
                        ApplyNopSites(kEpisodicVehicleSites);
                }
            };
        }
    } episodicVehicleSupport;


    // EpisodicWeaponSupport

    class EpisodicWeaponSupport
    {
    public:
        EpisodicWeaponSupport()
        {
            InstallEvent() += []
            {
                TraceArm("InstallEpisodicWeaponSupport");

                // EpisodicWeaponSupport: the recovered NOP sites.
                if (Game::Option("EpisodicWeaponSupport", "1") && IsEpisodicBuild(Game::Get().build))
                    ApplyNopSites(kEpisodicWeaponSites);
            };
        }
    } episodicWeaponSupport;


    // HighFPSBikePhysicsFix

    class HighFPSBikePhysicsFix
    {
    public:
        HighFPSBikePhysicsFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishHighFPSBikePhysicsFix");

                // HighFPSBikePhysicsFix: 1 slot
                if (Game::Option("HighFPSBikePhysicsFix", "1"))
                {
                    if (auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
                    {
                        zpatch_bike_resume = MatchAddress(pattern, 9);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallHighFPSBikePhysicsFix");

                // HighFPSBikePhysicsFix: 1 sigjump
                if (Game::Option("HighFPSBikePhysicsFix", "1"))
                {
                    if (auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_bike_physics_detour), true);
                    }
                }
            };
        }
    } highFPSBikePhysicsFix;


    // MovingPlaneParts / MovingSteeringWheelSupport / ShakingEngineBlock are
    // three INI arms over the same five slots (PublishSteerSlots/InstallSteerSlots
    // above).


    // MovingPlaneParts

    class MovingPlaneParts
    {
    public:
        MovingPlaneParts()
        {
            PublishEvent() += []
            {
                TraceArm("PublishMovingPlaneParts");

                // MovingPlaneParts: 5 slots (the shared steering slots).
                if (Game::Option("MovingPlaneParts", "1"))
                    PublishSteerSlots();
            };
            InstallEvent() += []
            {
                TraceArm("InstallMovingPlaneParts");

                // MovingPlaneParts: 3 siginstalls (the shared steering sites).
                if (Game::Option("MovingPlaneParts", "1"))
                    InstallSteerSlots();
            };
        }
    } movingPlaneParts;


    // MovingSteeringWheelSupport

    class MovingSteeringWheelSupport
    {
    public:
        MovingSteeringWheelSupport()
        {
            PublishEvent() += []
            {
                TraceArm("PublishMovingSteeringWheelSupport");

                // MovingSteeringWheelSupport: 5 slots (the shared steering slots).
                if (Game::Option("MovingSteeringWheelSupport", "1"))
                    PublishSteerSlots();
            };
            InstallEvent() += []
            {
                TraceArm("InstallMovingSteeringWheelSupport");

                // MovingSteeringWheelSupport: 3 siginstalls (the shared steering sites).
                if (Game::Option("MovingSteeringWheelSupport", "1"))
                    InstallSteerSlots();
            };
        }
    } movingSteeringWheelSupport;


    // NoLiveryLimit

    class NoLiveryLimit
    {
    public:
        NoLiveryLimit()
        {
            PublishEvent() += []
            {
                TraceArm("PublishNoLiveryLimit");

                // NoLiveryLimit: 5 slots
                if (Game::Option("NoLiveryLimit", "1"))
                {
                    if (auto pattern = find_pattern("74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
                    {
                        // The call operand at -6 (its next instruction is at -2); the
                        // resume is 90 bytes into the match.
                        auto* match = pattern.get_first<uint8_t>();
                        zpatch_nolivery_fn = BranchDestination(match - 2, -6);
                        zpatch_nolivery_resume_d = MatchAddress(pattern, 90);
                    }

                    if (auto pattern = find_pattern("8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
                    {
                        // The resume is 13 bytes into the match (after its prologue).
                        zpatch_nolivery_resume = MatchAddress(pattern, 13);
                    }

                    if (auto pattern = find_pattern("8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
                    {
                        // The same call shape as the first row, four bytes earlier.
                        auto* match = pattern.get_first<uint8_t>();
                        zpatch_nolivery_fn = BranchDestination(match - 4, -8);
                        zpatch_nolivery_resume_d = MatchAddress(pattern, 84);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallNoLiveryLimit");

                // NoLiveryLimit: 1 siginstall, 3 sigjumps
                if (Game::Option("NoLiveryLimit", "1"))
                {
                    // The naked bodies tail-jump through the handler-published resume
                    // and call the published count function. If any publish pattern
                    // missed, those slots are not game addresses; installing the bodies
                    // would jump/call through the stale values. The failsafe skips the
                    // arm instead (the original handler's signature-miss behaviour).
        #if !defined(_WIN64)
                    const auto is_game_address = [](uint32_t va) {
                        const auto base = reinterpret_cast<uintptr_t>(Game::Get().image.base);
                        return base != 0u && va >= base && (va - base) < Game::Get().image.size;
                    };
                    if (zpatch_failsafe_skip("NoLiveryLimit publish",
                                             !is_game_address(zpatch_nolivery_resume) ||
                                                 !is_game_address(zpatch_nolivery_resume_d) ||
                                                 !is_game_address(zpatch_nolivery_fn)))
                        return;
        #endif

                    if (auto pattern = find_pattern("8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // The three no-livery body entries relative to their shared match.
                        injector::MakeJMP(match - 237, OPP_BODY(abi_nolivery_b), true);
                        injector::MakeJMP(match - 199, OPP_BODY(abi_nolivery_c), true);
                        injector::MakeJMP(match, OPP_BODY(abi_nolivery_a), true);
                    }

                    if (auto pattern = find_pattern("74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
                    {
                        injector::MakeJMP(pattern.get_first<uint8_t>(2), OPP_BODY(abi_nolivery_d), true);
                    }

                    if (auto pattern = find_pattern("8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_nolivery_e), true);
                    }
                }
            };
        }
    } noLiveryLimit;


    // PoliceEpisodicWeaponSupport

    class PoliceEpisodicWeaponSupport
    {
    public:
        PoliceEpisodicWeaponSupport()
        {
            InstallEvent() += []
            {
                TraceArm("InstallPoliceEpisodicWeaponSupport");

                // PoliceEpisodicWeaponSupport: 1 literal
                if (Game::Option("PoliceEpisodicWeaponSupport", "0") && IsEpisodicBuild(Game::Get().build))
                    ApplyNopSites(kPoliceEpisodicSites);
            };
        }
    } policeEpisodicWeaponSupport;


    // PopupHeadlightsFlag

    class PopupHeadlightsFlag
    {
    public:
        PopupHeadlightsFlag()
        {
            PublishEvent() += []
            {
                TraceArm("PublishPopupHeadlightsFlag");

                // PopupHeadlightsFlag: 5 slots
                if (Game::Option("PopupHeadlightsFlag", "1"))
                {
                    // The original handler (0x10044590) is `if (pattern A) { ... }
                    // else { pattern B ... }`: exactly ONE of the two EAX/EDX sites is
                    // ever patched, and the two resume slots belong to that site. Two
                    // independent `if`s would write both against whichever pattern
                    // matched last, so the first body would tail-jump to the second
                    // site's resume.
                    if (auto pattern = find_pattern(kPopupEaxPattern); pattern.size() > 0)
                        StorePopupResumes(pattern);
                    else if (auto edx_pattern = find_pattern(kPopupEdxPattern);
                             edx_pattern.size() > 0)
                        StorePopupResumes(edx_pattern);

                    // The IDE parser hook is independent of the two model tests and is
                    // shared with the other arms that install it.
                    if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                        zpatch_ide_hook_slot = MatchAddress(pattern, kIdeHookSlotOffset);
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallPopupHeadlightsFlag");

                // PopupHeadlightsFlag: 3 siginstalls, 1 sigjump
                if (Game::Option("PopupHeadlightsFlag", "1"))
                {
                    // if / else, like the original: see the publish arm above.
                    if (auto pattern = find_pattern(kPopupEaxPattern); pattern.size() > 0)
                        WritePopupSite(pattern, OPP_BODY(abi_popup_headlights));
                    else if (auto edx_pattern = find_pattern(kPopupEdxPattern);
                             edx_pattern.size() > 0)
                        WritePopupSite(edx_pattern, OPP_BODY(abi_popup_headlights_edx));

                    if (auto pattern = find_pattern(kIdeHookPattern); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match + kIdeHookInstallOffset, OPP_BODY(abi_copsirens_ide), true);
                    }
                }
            };
        }
    } popupHeadlightsFlag;


    // ShakingEngineBlock

    class ShakingEngineBlock
    {
    public:
        ShakingEngineBlock()
        {
            PublishEvent() += []
            {
                TraceArm("PublishShakingEngineBlock");

                // ShakingEngineBlock: 5 slots (the shared steering slots).
                if (Game::Option("ShakingEngineBlock", "1"))
                    PublishSteerSlots();
            };
            InstallEvent() += []
            {
                TraceArm("InstallShakingEngineBlock");

                // ShakingEngineBlock: 3 siginstalls (the shared steering sites).
                if (Game::Option("ShakingEngineBlock", "1"))
                    InstallSteerSlots();
            };
        }
    } shakingEngineBlock;


    // TBoGTHeliHeightLimit

    class TBoGTHeliHeightLimit
    {
    public:
        TBoGTHeliHeightLimit()
        {
            InstallEvent() += []
            {
                TraceArm("InstallTBoGTHeliHeightLimit");

                // TBoGTHeliHeightLimit: 1 literal
                if (Game::Option("TBoGTHeliHeightLimit", "0") && IsEpisodicBuild(Game::Get().build))
                    ApplyNopSites(kHeliHeightLimitSites);
            };
        }
    } tBoGTHeliHeightLimit;


} // namespace opp::features

export namespace opp
{
    struct VehicleLightValue
    {
        uint32_t modelHash{};
        float value{};
        bool populated{};
    };

    struct VehicleLightTables
    {
        std::array<VehicleLightValue, kModelTableSlots> popupAngles{};  // radians
        std::array<VehicleLightValue, kModelTableSlots> headlightOffsets{};
    };

    // FUN_100523c0's two 512-slot INI loops. Missing model names leave holes;
    // numeric parsing follows strtof's prefix acceptance.
    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept;

    using ModelIndexResolver = int (*)(uint32_t, void*);

    struct PopupLightSelection
    {
        float angleRadians{kHalfPi};
        bool supported{};
    };

    PopupLightSelection SelectPopupLight(
        const VehicleLightTables&,
        const std::array<uint32_t, kModelTableSlots>& supportedModels, int modelIndex,
        ModelIndexResolver, void* context = nullptr) noexcept;
    float SelectHeadlightOffset(const VehicleLightTables&, int modelIndex, ModelIndexResolver,
                                void* context = nullptr) noexcept;
} // namespace opp





namespace opp
{
    namespace
    {
        // One row per [PopupLightAngles]/[HeadlightConeOffsets] table: the INI
        // section, its key suffix, the table it fills and whether its values are
        // degrees (the popup angles are, the cone offsets are not).
        struct LightTableSection final
        {
            const char* section;
            const char* suffix;
            std::array<VehicleLightValue, kModelTableSlots> VehicleLightTables::* table;
            bool degrees;
        };

        constexpr LightTableSection kLightTableSections[] = {
            {"PopupLightAngles", "Angle", &VehicleLightTables::popupAngles, true},
            {"HeadlightConeOffsets", "Offset", &VehicleLightTables::headlightOffsets, false},
        };
    }  // namespace

    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept
    {
        try
        {
            VehicleLightTables parsed;
            for (const LightTableSection& section : kLightTableSections)
            {
                auto& slots = parsed.*(section.table);
                for (size_t i = 0; i < slots.size(); ++i)
                {
                    const std::string prefix = "Car" + std::to_string(i + 1);
                    const std::string name =
                        config.String(section.section, prefix + "Name", "");
                    if (name.empty())
                        continue;

                    const std::string value =
                        config.String(section.section, prefix + section.suffix, "");
                    if (value.empty())
                        return {PatchResultCode::parse_error, "missing vehicle light value"};

                    char* end = nullptr;
                    errno = 0;
                    const float numeric = std::strtof(value.c_str(), &end);
                    if (end == value.c_str() || errno == ERANGE)
                        return {PatchResultCode::parse_error, "invalid vehicle light value"};

                    VehicleLightValue& slot = slots[i];
                    slot.modelHash = GameHash(name);
                    slot.populated = true;
                    slot.value = section.degrees ? DegreesToRadians(numeric) : numeric;
                }
            }

            output = parsed;
            return {};
        }
        catch (...)
        {
            return {PatchResultCode::invalid_input, "vehicle table allocation failed"};
        }
    }

    PopupLightSelection SelectPopupLight(
        const VehicleLightTables& tables,
        const std::array<uint32_t, kModelTableSlots>& supportedModels, int modelIndex,
        ModelIndexResolver resolve, void* context) noexcept
    {
        PopupLightSelection result;
        if (!resolve || modelIndex < 0)
            return result;

        // The popuplights hash is the one the original resolves and compares first
        // (FUN_1001ad20).
        const int specialIndex = resolve(kPopupLightsModelHash, context);
        if (specialIndex >= 0 && specialIndex == modelIndex)
        {
            result.supported = true;
            return result;
        }

        for (const uint32_t hash : supportedModels)
        {
            if (!hash)
                break;
            if (resolve(hash, context) != modelIndex)
                continue;

            result.supported = true;
            // The original scans all 512 slots; the last duplicate wins.
            for (const VehicleLightValue& entry : tables.popupAngles)
            {
                if (entry.populated && entry.modelHash != 0 && entry.modelHash == hash)
                    result.angleRadians = entry.value;
            }
            break;
        }
        return result;
    }

    float SelectHeadlightOffset(const VehicleLightTables& tables, int modelIndex,
                                ModelIndexResolver resolve, void* context) noexcept
    {
        if (!resolve || modelIndex < 0)
            return kDefaultConeOffset;

        // FUN_1001ad20 stops at the first zero hash rather than skipping holes.
        for (const VehicleLightValue& entry : tables.headlightOffsets)
        {
            if (!entry.modelHash)
                break;
            if (entry.populated && resolve(entry.modelHash, context) == modelIndex)
                return entry.value;
        }
        return kDefaultConeOffset;
    }
} // namespace opp

// ---- hook bodies ----
// The machine-level entry points the arms above install, kept together here
// after the readable installers. Each keeps the original site's calling
// convention and register contract; its comment names the destination VA.
#if !defined(_WIN64)

extern "C"
{

// @exact 0x10044530 PopupHeadlightsFlag (pattern "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1",
// live on 1.0.8.0 at game 0xA53A64).
//   movsx eax,word [esi+2Eh]; pushad; mov [dword_1015BE20],eax
//   call sub_10044310; popad; cmp [dword_1015BE24],1; je resume_a
//   cmp eax,[dword_1015BE1C]; jmp [dword_1015D62C]  ; resume B = site+0xA
// resume_a: jmp [dword_1015D628]                   ; resume A = site+0xC
// pushad/popad is essential: the helper may clobber every GPR, and the resumes
// consume ECX/EDX/ESI exactly as the site left them.
__declspec(naked) void abi_popup_headlights()
{
    __asm {
        movsx eax, word ptr [esi + 2Eh]
        pushad
        mov   dword ptr [zpatch_popup_model], eax
        call  abi_popup_check
        popad
        cmp   dword ptr [zpatch_popup_flag], 1
        je    popup_resume_a
        cmp   eax, dword ptr [zpatch_popup_index]
        jmp   dword ptr [zpatch_popup_resume_b]
    popup_resume_a:
        jmp   dword ptr [zpatch_popup_resume_a]
    }
}
// @exact 0x10044560 PopupHeadlightsFlag: the EDX twin for the second site
// pattern (absent on 1.0.8.0).
__declspec(naked) void abi_popup_headlights_edx()
{
    __asm {
        movsx edx, word ptr [esi + 2Eh]
        pushad
        mov   dword ptr [zpatch_popup_model], edx
        call  abi_popup_check
        popad
        cmp   dword ptr [zpatch_popup_flag], 1
        je    popup_edx_resume_a
        cmp   edx, dword ptr [zpatch_popup_index]
        jmp   dword ptr [zpatch_popup_resume_b]
    popup_edx_resume_a:
        jmp   dword ptr [zpatch_popup_resume_a]
    }
}

// @exact 0x100051B0 MovingSteeringWheelSupport detour: the installer E9s the
// game site at match+2 (game 0xA538A0); the body caches ESI (the vehicle), runs
// the shared consumer between pushad/popad, calls vfunc 296 (al = its result)
// and tail-jumps to the resume (match+16, the jne after the replayed test).
__declspec(naked) void abi_steer_detour_eax()
{
    __asm {
        mov  dword ptr [zpatch_steer_vehicle], esi
        pushad
        call abi_steer_body
        popad
        mov  edx, dword ptr [esi]
        mov  eax, dword ptr [edx + 128h]
        mov  ecx, esi
        call eax
        test al, al
        jmp  dword ptr [zpatch_steer_resume]
    }
}
// @exact 0x100051E0 MovingSteeringWheelSupport fallback detour (pattern absent
// on 1.0.8.0): same, with the vfunc fetched through EAX/EDX instead.
__declspec(naked) void abi_steer_detour_edx()
{
    __asm {
        mov  dword ptr [zpatch_steer_vehicle], esi
        pushad
        call abi_steer_body
        popad
        mov  eax, dword ptr [esi]
        mov  edx, dword ptr [eax + 128h]
        mov  ecx, esi
        call edx
        test al, al
        jmp  dword ptr [zpatch_steer_resume]
    }
}

// @exact 0x1003d310: name = &ebx[0x13c], index = eax; resume with (hash, movsx ebx[0x48]).
__declspec(naked) void abi_nolivery_a()
{
    __asm {
        pushad
        lea ecx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], ecx
        mov DWORD PTR [zpatch_nolivery_index], eax
        call abi_nolivery_hash_sign
        popad
        mov ecx, DWORD PTR [zpatch_nolivery_hash]
        movsx edx, WORD PTR [ebx + 48h]
        push ecx
        push edx
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}
// @exact 0x1003d340: name = &ebx[0x13c], index = ecx; resume with (hash, movsx ebx[0x48]).
__declspec(naked) void abi_nolivery_b()
{
    __asm {
        pushad
        lea edx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], edx
        mov DWORD PTR [zpatch_nolivery_index], ecx
        call abi_nolivery_hash_sign
        popad
        mov edx, DWORD PTR [zpatch_nolivery_hash]
        movsx eax, WORD PTR [ebx + 48h]
        push edx
        push eax
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}
// @exact 0x1003d370: like a, but the original also drops 8 bytes of args first.
__declspec(naked) void abi_nolivery_c()
{
    __asm {
        pushad
        lea edx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], edx
        mov DWORD PTR [zpatch_nolivery_index], eax
        call abi_nolivery_hash_sign
        popad
        mov edx, DWORD PTR [zpatch_nolivery_hash]
        movsx eax, WORD PTR [ebx + 48h]
        add esp, 8
        push edx
        push eax
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}
// @exact 0x1003d3a0: copies the frame name into a 16-byte buffer, counts the
// "<name>_sign_N" lookups, writes the count to [ebp+0xc2], then resumes.
__declspec(naked) void abi_nolivery_d()
{
    __asm {
        pushad
        mov DWORD PTR [zpatch_nolivery_arg], edi
        lea ecx, [ebp + 13Ch]
        mov DWORD PTR [zpatch_nolivery_dest], ecx
        lea ecx, [esp + 60h]
        mov DWORD PTR [zpatch_nolivery_bufptr], ecx
        call abi_nolivery_copy_name
        call abi_nolivery_count_loop
        popad
        push ebx
        mov ebx, DWORD PTR [zpatch_nolivery_count]
        mov BYTE PTR [ebp + 0C2h], bl
        pop ebx
        jmp DWORD PTR [zpatch_nolivery_resume_d]
    }
}
// @exact 0x1003d3e0: the same sequence as 0x1003d3a0 with ESI as the object
// (name = &edi[0x13c], count written to [edi+0xc2]); same resume slot.
__declspec(naked) void abi_nolivery_e()
{
    __asm {
        pushad
        mov DWORD PTR [zpatch_nolivery_arg], esi
        lea ecx, [edi + 13Ch]
        mov DWORD PTR [zpatch_nolivery_dest], ecx
        lea ecx, [esp + 60h]
        mov DWORD PTR [zpatch_nolivery_bufptr], ecx
        call abi_nolivery_copy_name
        call abi_nolivery_count_loop
        popad
        push ebx
        mov ebx, DWORD PTR [zpatch_nolivery_count]
        mov BYTE PTR [edi + 0C2h], bl
        pop ebx
        jmp DWORD PTR [zpatch_nolivery_resume_d]
    }
}

// @exact 0x10006310
__declspec(naked) void abi_copsirens_a()  // 0x10006310
{
    __asm {
        mov DWORD PTR [zpatch_copsirens_index], edx
        pushad
        pushfd
        call abi_copsirens_check_a
        popfd
        popad
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        jmp DWORD PTR [zpatch_copsirens_resume]
    }
}
// @exact 0x10006330
__declspec(naked) void abi_copsirens_b()  // 0x10006330
{
    __asm {
        mov DWORD PTR [zpatch_copsirens_index_b], eax
        pushad
        pushfd
        call abi_copsirens_check_b
        popfd
        popad
        cmp DWORD PTR [zpatch_copsirens_flag_b], 1
        jmp DWORD PTR [zpatch_copsirens_resume_b]
    }
}
// @exact 0x100040E0
__declspec(naked) void abi_copsirens_ide()  // 0x100040E0
{
    __asm {
        pushad
        mov DWORD PTR [zpatch_ide_string], edi
        mov DWORD PTR [zpatch_ide_model], eax
        call abi_ide_parse
        popad
        mov ecx, 7
        xor edx, edx
        __emit 0F3h
        __emit 0A6h               // repe cmpsb
        jmp DWORD PTR [zpatch_ide_hook_slot]
    }
}

// 0x1001ef60 HighFPSBikePhysicsFix: saves the incoming value, clamps it,
// restores the registers, then tail-jumps to the resume (match+9).
// @exact 0x1001EF60
__declspec(naked) void abi_bike_physics_detour()
{
    __asm {
        fld DWORD PTR [ebp + 8]
        fstp DWORD PTR [zpatch_bike_value]
        pushad
        call abi_bike_physics_helper
        popad
        mov DWORD PTR [zpatch_bike_saved], eax
        mov eax, DWORD PTR [zpatch_bike_value]
        mov DWORD PTR [ebp + 8], eax
        mov eax, DWORD PTR [zpatch_bike_saved]
        fld DWORD PTR [ebp + 8]
        push ecx
        mov ecx, esi
        fstp DWORD PTR [esp]
        jmp DWORD PTR [zpatch_bike_resume]
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)
