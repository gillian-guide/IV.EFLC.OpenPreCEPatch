// SPDX-License-Identifier: GPL-3.0-or-later
//
// vehicles
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

#include <cerrno>

#include <bit>
export module opp.vehicles;

import opp.config;
import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points vehicles installs.
//
// Each body keeps the original site's calling convention and register
// contract; the comment above it names the original destination VA. They are
// 32-bit only (the x64 parity dump resolves names to the original VAs).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{
constexpr size_t kVehicleSteeringOffset = 0x10D8;
constexpr size_t kVehicleBoneOffset = 0xE18;
constexpr size_t kVehicleLockOwnerOffset = 0x84;
constexpr size_t kVehicleRpmOffset = 0x10C8;
constexpr size_t kVehicleFlagsOffset = 0xF64;
constexpr size_t kVehicleEngineStateOffset = 0x1354;
constexpr size_t kVehicleSuspensionTravelOffset = 0x1F3C;
constexpr size_t kVehiclePitchOffset = 0x1F08;
constexpr size_t kVehicleRollOffset = 0x1F04;
constexpr size_t kVehicleHeaveOffset = 0x1F00;

// HighFPSBikePhysicsFix helper (original sub_1001EF30): clamp zpatch_bike_value
// below 0.006666666665 to 0x3BDA740E. SSE in the original.
void abi_bike_physics_helper() noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &zpatch_bike_value, sizeof(value));
    if (0.006666666665 > static_cast<double>(value))
    {
        // 0x3BDA740E is the float 0.0066666667: the floor the original clamps
        // the bike physics scale to (its own literal bit pattern).
        constexpr uint32_t kBikeFloorBits = 0x3BDA740Eu;
        zpatch_bike_value = kBikeFloorBits;
    }
}

// PopupHeadlightsFlag checker (original sub_10044310), called between
// pushad/popad: look up 0xEB9F21D3 ("popuplights"), walk the +popuplights list,
// then copy the last matching [PopupLightAngles] angle (starts at pi/2). A
// failed lookup leaves -1, which matches a model of -1, as in the original.
namespace
{
// PopupAngleMemo: the [PopupLightAngles] table is loaded once at startup, so the
// angle a matching model maps to is remembered instead of rescanning all 512
// entries on every matching vehicle. The original takes the *last* matching
// entry, which the forward scan below keeps.
uint32_t PopupAngleFor(uint32_t hash) noexcept
{
    struct AngleMemo final
    {
        uint32_t hash;
        uint32_t bits;
        uint8_t valid;
    };
    static AngleMemo memo[64] = {};
    const size_t slot = (hash * 2654435761u) >> 26;
    if (memo[slot].valid != 0u && memo[slot].hash == hash)
        return memo[slot].bits;
    uint32_t bits = 0x3FC90FDBu;  // pi/2: the caller's default when nothing matches
    for (uint32_t k = 0; k < 512; ++k)
    {
        if (zpatch_popup_hashes[k] != 0u && zpatch_popup_hashes[k] == hash)
            std::memcpy(&bits, &zpatch_popup_angles[k], sizeof(bits));
    }
    memo[slot].hash = hash;
    memo[slot].bits = bits;
    memo[slot].valid = 1u;
    return bits;
}
}  // namespace

void abi_popup_check() noexcept
{
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    zpatch_popup_angle = 0x3FC90FDBu;  // dword_101580CC: pi/2 float bits
    zpatch_popup_flag = 0;             // dword_1015BE24
    if (lookup == nullptr)
        return;  // a preamble miss is a clean skip
    // PopupLookupCache: the hash -> model-index mapping is static, so remember
    // the last resolution per hash. Direct-mapped, collision just re-resolves.
    static uint32_t cache_hash[512] = {};
    static int32_t cache_index[512] = {};
    static uint8_t cache_valid[512] = {};
    const auto resolve = [&](uint32_t hash) -> int32_t
    {
        const size_t slot = (hash * 2654435761u) >> 23;
        if (cache_valid[slot] != 0u && cache_hash[slot] == hash)
            return cache_index[slot];
        int32_t resolved = -1;
        lookup(hash, &resolved);  // sub_10044310's call is unguarded by default
        cache_hash[slot] = hash;
        cache_index[slot] = resolved;
        cache_valid[slot] = 1;
        return resolved;
    };
    // The special-script model hash the popup table resolves first
    // (FUN_1001ad20); it is the one hash whose index the original compares.
    constexpr uint32_t kSpecialScriptModelHash = 0xEB9F21D3u;
    int32_t out = resolve(kSpecialScriptModelHash);
    zpatch_popup_index = static_cast<uint32_t>(out);  // dword_1015BE1C
    if (out == static_cast<int32_t>(zpatch_popup_model))
    {
        zpatch_popup_flag = 1;
        return;
    }
    for (uint32_t i = 0; i < 512; ++i)
    {
        const uint32_t hash = zpatch_popuplights_models[i];
        if (hash == 0) return;
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
extern "C" const void* abi_vehicle_component(const void* registry,
                                             const char* name) noexcept
{
    // sub_10003F40 dereferences the registry and each candidate name unguarded;
    // the default-on failsafes below keep a missing table out of the body.
    if (registry == nullptr) return nullptr;
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(registry);
    if (entries == nullptr) return nullptr;
    // The registry's component count is its uint16 at +0x14.
    constexpr size_t kComponentArrayOffset = 0x14;
    const auto count = *reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(registry) + kComponentArrayOffset);
    for (uint16_t i = 0; i < count; ++i)
    {
        // A component entry is 0xE0 bytes.
        constexpr size_t kComponentEntrySize = 0xE0;
        const auto* entry = entries + static_cast<size_t>(i) * kComponentEntrySize;
        const char* candidate = *reinterpret_cast<const char* const*>(entry);
        if (candidate == nullptr) continue;
        if (_stricmp(candidate, name) == 0) return entry;
    }
    return nullptr;
}

// sub_10003980's script-float layout: ctx+0x0E holds the slot offset, the slot
// is at ctx+offset+0x18 and the float at slot+8.
constexpr size_t kScriptOffsetField = 0x0E;
constexpr size_t kScriptSlotBase = 0x18;
constexpr size_t kScriptFloatOffset = 8;

extern "C" uint32_t abi_steer_body() noexcept
{
    auto* const vehicle = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(zpatch_steer_vehicle));
    if (vehicle == nullptr) return 0;

    using BoneFn = uint32_t*(__thiscall*)(void*, uint32_t);
    using SetFn = void(__thiscall*)(uint32_t*, float);
    using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    const auto get_bone = reinterpret_cast<BoneFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59b88));
    const auto set_a = reinterpret_cast<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_5d63c));
    const auto set_b = reinterpret_cast<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59cdc));
    const auto part_call = reinterpret_cast<PartFn>(
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

    // sub_10003980: read the current script float (resolver hash salt_3c).
    const auto script_float = [&]() -> float
    {
        const uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
        if (ctx == 0) return 0.0f;
        const uint32_t offset =
            *reinterpret_cast<const uint32_t*>(
                static_cast<uintptr_t>(ctx) + kScriptOffsetField);
        const uint32_t value = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ctx) + offset + kScriptSlotBase);
        // The script slot can be empty before the script has run; the original
        // dereferences it (fault preserved when the folded guard is off).
        if (value == 0) return 0.0f;
        return *reinterpret_cast<const float*>(
            static_cast<uintptr_t>(value) + kScriptFloatOffset);
    };
    const auto model_of = [vehicle]()
    {
        return *reinterpret_cast<const int16_t*>(vehicle + kModelIndexOffset);
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
        constexpr size_t kVehicleChainFirstOffset = 8;
        constexpr size_t kVehicleChainSecondOffset = 0xB4;
        constexpr size_t kVehicleChainRegistryOffset = 0xC;
        const uint32_t p1 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ped) + kVehicleChainFirstOffset);
        if (p1 == 0u) return nullptr;
        const uint32_t p2 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p1) + kVehicleChainSecondOffset);
        if (p2 == 0u) return nullptr;
        const uint32_t registry = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p2) + kVehicleChainRegistryOffset);
        if (registry == 0u) return nullptr;
        const auto* entry = static_cast<const uint8_t*>(abi_vehicle_component(
            reinterpret_cast<const void*>(static_cast<uintptr_t>(registry)), name));
        if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
        // sub_10004C70 0x10004D4A calls get_bone unguarded (it is null only when
        // the loader's preamble missed).
        // The component entry's bone id is its uint16 at +0x14.
        constexpr size_t kComponentBoneIdOffset = 0x14;
        const uint32_t bone =
            *reinterpret_cast<const uint16_t*>(entry + kComponentBoneIdOffset);
        return get_bone(vehicle, bone);
    };

    // The steering block dereferences the vehicle's lock owner. The default-on
    // failsafe skips it while there is no owner (the original faults); there is
    // no selectable guard-off path in this tree.
    const int16_t steer_model = model_of();
    const uint32_t steer_lock_owner = *reinterpret_cast<const uint32_t*>(
        vehicle + kVehicleBoneOffset);
    if (zpatch_flag_steering && steer_lock_owner != 0u)
    {
        const int16_t model = steer_model;
        const float steering = *reinterpret_cast<const float*>(vehicle + kVehicleSteeringOffset);
        const uint32_t lock_owner = steer_lock_owner;
        const float lock = *reinterpret_cast<const float*>(
            static_cast<uintptr_t>(lock_owner) + kVehicleLockOwnerOffset);
        const float turn = static_cast<float>(static_cast<double>(steering / lock) *
                                              -3.141592653589793);
        float tilt = 0.0f;
        // SteerLookupCache: the wheelangle table is static after startup, so a
        // model's tilt is stable. Direct-mapped by model; a collision re-walks.
        struct SteerTiltCache final
        {
            int16_t model;
            float tilt;
            uint8_t valid;
        };
        static SteerTiltCache tilt_cache[64] = {};
        const size_t tilt_slot = static_cast<uint16_t>(model) & 63u;
        if (tilt_cache[tilt_slot].valid != 0u &&
            tilt_cache[tilt_slot].model == model)
        {
            tilt = tilt_cache[tilt_slot].tilt;
        }
        else
        {
            for (int i = 0; i < 512; ++i)
            {
                const uint32_t hash = zpatch_wheelangle_models[i];
                if (hash == 0u) break;
                int32_t resolved = -1;
                if (lookup == nullptr) break;
                lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
                if (resolved == model)
                {
                    tilt = static_cast<float>(
                        static_cast<double>(zpatch_wheelangle_angles[i]) *
                        3.141592653589793 / 180.0);
                    break;
                }
            }
            tilt_cache[tilt_slot].model = model;
            tilt_cache[tilt_slot].tilt = tilt;
            tilt_cache[tilt_slot].valid = 1;
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
        if (zpatch_engine_last_timestep != script_float())
        {
            const double step = zpatch_engine_descending ? -0.5 : 0.5;
            const float phase = static_cast<float>(
                static_cast<double>(script_float()) * step +
                static_cast<double>(zpatch_engine_phase));
            zpatch_engine_phase = phase;
            uint8_t direction = zpatch_engine_descending;
            if (static_cast<double>(phase) > 0.01) direction = 1;
            if (static_cast<double>(phase) < -0.01) direction = 0;
            zpatch_engine_descending = direction;
        }
        const int16_t model = model_of();
        if (model != -1)
        {
            uint32_t* matrix = bone_matrix(model, "engineblock");
            if (matrix != nullptr)
            {
                identity(matrix);
                const uint32_t flags =
                    *reinterpret_cast<const uint32_t*>(vehicle + kVehicleEngineStateOffset);
                float scale = 0.0f;
                if ((flags & 8u) != 0u)
                {
                    const float rpm = *reinterpret_cast<const float*>(vehicle + kVehicleRpmOffset);
                    scale = static_cast<float>(static_cast<double>(rpm) * 0.75 + 0.25);
                }
                // Bit 4 of the engine-state dword: the original forces the
                // scale to 1.0 when the engine is running (bit 3 is "started").
                constexpr uint32_t kEngineRunningFlag = 0x10u;
                if ((flags & kEngineRunningFlag) != 0u) scale = 1.0f;
                if (set_b) set_b(matrix, scale * zpatch_engine_phase);
            }
        }
    }
    if (zpatch_flag_plane &&
        *reinterpret_cast<const uint32_t*>(vehicle + kVehicleFlagsOffset) == 5u &&
        part_call != nullptr)
    {
        const float travel = *reinterpret_cast<const float*>(vehicle + kVehicleSuspensionTravelOffset);
        part_call(vehicle, 89, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 91, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 90, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 92, 4, script_float() * travel * 100.0f, 0, 0, 0);
        const float pitch = *reinterpret_cast<const float*>(vehicle + kVehiclePitchOffset);
        const float roll = *reinterpret_cast<const float*>(vehicle + kVehicleRollOffset);
        const float heave = *reinterpret_cast<const float*>(vehicle + kVehicleHeaveOffset);
        part_call(vehicle, 96, 3,
                  static_cast<float>(static_cast<double>(pitch) * 0.5), 1, 0, 0);
        part_call(vehicle, 97, 3,
                  static_cast<float>(static_cast<double>(pitch) * -0.5), 1, 0, 0);
        part_call(vehicle, 94, 3,
                  static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
        part_call(vehicle, 95, 3,
                  static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
        part_call(vehicle, 93, 5,
                  static_cast<float>(static_cast<double>(heave) * 0.5), 1, 0, 0);
    }
    // Unconditional tail: snapshot the last script timestep and return its slot.
    const uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
    if (ctx == 0u)
    {
        zpatch_engine_last_timestep = 0.0f;
        return 0;
    }
    const uint32_t offset = *reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(ctx) + kScriptOffsetField);
    const uint32_t value_ptr = *reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(ctx) + offset + kScriptSlotBase);
    if (value_ptr == 0u)
    {
        zpatch_engine_last_timestep = 0.0f;
        return 0;
    }
    const float last = *reinterpret_cast<const float*>(
        static_cast<uintptr_t>(value_ptr) + kScriptFloatOffset);
    zpatch_engine_last_timestep = last;
    return value_ptr;
}

// @exact 0x10044530 PopupHeadlightsFlag (pattern "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1",
// live on 1.0.8.0 at game 0xA53A64).
//   movsx eax,word [esi+2Eh]; pushad; mov [dword_1015BE20],eax
//   call sub_10044310; popad; cmp [dword_1015BE24],1; je resume_a
//   cmp eax,[dword_1015BE1C]; jmp [dword_1015D62C]  ; resume B = site+0xA
// resume_a: jmp [dword_1015D628]                   ; resume A = site+0xC
// pushad/popad is essential: the helper may clobber every GPR, and the resumes
// consume ECX/EDX/ESI exactly as the site left them.
static __declspec(naked) void abi_popup_headlights()
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
static __declspec(naked) void abi_popup_headlights_edx()
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
static __declspec(naked) void abi_steer_detour_eax()
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
static __declspec(naked) void abi_steer_detour_edx()
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

// NoLiveryLimit helpers (sub_1003D220 / sub_1003D2F0 / sub_1003D270). The naked
// shims below stash their register inputs in globals, call a compiled helper,
// then tail-jump to the handler-published resume. The count helper calls the
// published function as `__thiscall(this, hash)`.
extern "C" void abi_nolivery_hash_sign()  // sub_1003D220
{
    char buffer[64];
    // Original: sprintf(buffer, 0x40, "%s_sign_%d", name, index + 1).
    zpatch_spawns_sprintf(
        buffer, sizeof(buffer), "%s_sign_%d",
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_name)),
        static_cast<int>(zpatch_nolivery_index) + 1);
    zpatch_nolivery_hash = GameHash(buffer);
    zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name()  // sub_1003D2F0
{
    // push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc; ret.
    // Reads both globals unguarded and has no /GS cookie tail (leaf).
    char* const dst = reinterpret_cast<char*>(static_cast<uintptr_t>(zpatch_nolivery_dest));
    const char* const src =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_bufptr));
    // The original copies into a 16-byte name buffer (sub_1003D2F0 pushes 0x10
    // as the strcpy_s destination size).
    constexpr size_t kNoLiveryNameBytes = 0x10;
    zpatch_secure_strcpy_s(dst, kNoLiveryNameBytes, src);
}
extern "C" void abi_nolivery_count_loop()  // sub_1003D270
{
    using LookupFn = int(__fastcall*)(void* self, int, uint32_t hash);
    const auto fn = reinterpret_cast<LookupFn>(static_cast<uintptr_t>(zpatch_nolivery_fn));
    void* const self = reinterpret_cast<void*>(static_cast<uintptr_t>(zpatch_nolivery_arg));
    const char* const name =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_bufptr));
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
// @exact 0x1003d310: name = &ebx[0x13c], index = eax; resume with (hash, movsx ebx[0x48]).
static __declspec(naked) void abi_nolivery_a()
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
static __declspec(naked) void abi_nolivery_b()
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
static __declspec(naked) void abi_nolivery_c()
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
static __declspec(naked) void abi_nolivery_d()
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
static __declspec(naked) void abi_nolivery_e()
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
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
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
    for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
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
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
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
        for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
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
    const char* const str =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_ide_string));
    const uint32_t model = *reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const void*>(static_cast<uintptr_t>(zpatch_ide_model) + 60u));
    const auto append = [](uint32_t* array, uint32_t value)
    {
        for (int i = 0; i < 512; ++i)
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
    if (prefix_ci(str, "wheelangle:", 11))
    {
        // sub_10003FA0's third arm: strnicmp(...,0xB) then strtof(String1+11). An
        // existing model entry is left untouched (the original's scan only breaks
        // out); the angle is stored only on append.
        const float angle = std::strtof(str + 11, nullptr);
        uint32_t bits = 0;
        std::memcpy(&bits, &angle, sizeof(bits));
        for (int i = 0; i < 512; ++i)
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
// @exact 0x10006310
static __declspec(naked) void abi_copsirens_a()  // 0x10006310
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
static __declspec(naked) void abi_copsirens_b()  // 0x10006330
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
static __declspec(naked) void abi_copsirens_ide()  // 0x100040E0
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
static __declspec(naked) void abi_bike_physics_detour()
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

namespace opp::features
{

    // BikeFeetFix

    void InstallBikeFeetFix(PatchContext& ctx)
    {

        TraceArm("BikeFeetFix");

        // BikeFeetFix: 1 literal
        if (ctx.Option("BikeFeetFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "39 82 54 13 00 00 0F 85 94 04 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 12, 12);
            }
        }

    }


    // BikePhoneAnimsFix

    void InstallBikePhoneAnimsFix(PatchContext& ctx)
    {

        TraceArm("BikePhoneAnimsFix");

        // BikePhoneAnimsFix: 1 literal
        if (ctx.Option("BikePhoneAnimsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 33, 6);
            }
        }

    }


    // publish CopSirensFlag

    void PublishCopSirensFlag(PatchContext& ctx)
    {

        TraceArm("publish CopSirensFlag");

        // CopSirensFlag: 3 slots
        if (ctx.Option("CopSirensFlag", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
            {
                // The resume after the five-byte E8.
                constexpr ptrdiff_t kResumeOffset = 5;
                zpatch_copsirens_resume = MatchAddress(pattern, kResumeOffset);
            }

            if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
            {
                // The second resume 28 bytes into the match.
                constexpr ptrdiff_t kResumeBOffset = 28;
                zpatch_copsirens_resume_b = MatchAddress(pattern, kResumeBOffset);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                // The shared IDE parser hook slot is the byte before the match.
                constexpr ptrdiff_t kIdeSlotOffset = -1;
                zpatch_ide_hook_slot = MatchAddress(pattern, kIdeSlotOffset);
            }
        }

    }


    // CopSirensFlag

    void InstallCopSirensFlag(PatchContext& ctx)
    {

        TraceArm("CopSirensFlag");

        // CopSirensFlag: 3 siginstalls, 2 sigjumps
        if (ctx.Option("CopSirensFlag", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_copsirens_a));
            }

            if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The model-check detour's entry, 22 bytes into the match.
                constexpr ptrdiff_t kCopSirensBodyOffset = 22;
                JumpTo(ctx, match + kCopSirensBodyOffset, OPP_BODY(abi_copsirens_b));
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The shared IDE parser hook is installed 10 bytes before the
                // match (the body rewrites the match-1 slot).
                constexpr ptrdiff_t kCopSirensIdeOffset = -10;
                JumpTo(ctx, match + kCopSirensIdeOffset, OPP_BODY(abi_copsirens_ide));
            }
        }

    }


    // EpisodicVehicleSupport

    void InstallEpisodicVehicleSupport(PatchContext& ctx)
    {

        TraceArm("EpisodicVehicleSupport");

        // EpisodicVehicleSupport: 23 literals, 4 sigwrites. The four pointer
        // stores point at the episode flag; each site was a per-build RVA
        // (0x021500/0x7D91AE on 1.0.7.0, 0x095040/0x684ABE on 1.0.8.0) and is
        // now the pre-store instruction sequence at the site. The build gates
        // stay: each signature also resolves on other builds, where the original
        // did not install this option.
        if (ctx.Option("EpisodicVehicleSupport", "1"))
        {
            const GameImage& image = ctx.image;

            const auto storeEpisodeFlag = [&ctx](hook::pattern& pattern) {
                if (pattern.size() > 0)
                {
                    WriteValue<uint32_t>(ctx, pattern.get(0).get<uint8_t>(),
                                         AddressOf(&zpatch_episode_flag));
                }
            };

            if (ctx.build == GameBuild::v1070)
            {
                if (auto pattern = Pattern(image, "44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
                if (auto pattern = Pattern(image, "44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
            }

            if (ctx.build == GameBuild::v1080)
            {
                if (auto pattern = Pattern(image, "54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
                if (auto pattern = Pattern(image, "54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
            }

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                {
                    // The original NOPs the two recovered sites; a different count
                    // is a view mismatch, so the arm installs nothing.
                    if (auto pattern = PatternAll(image, "7C 18 0F BF 4E 2E"); pattern.size() > 0 &&
                        ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport component window"))
                    {
                        NopAt(ctx, pattern.get(0).get<uint8_t>() - 6, 8);
                        NopAt(ctx, pattern.get(1).get<uint8_t>() - 6, 8);
                    }
                }

                {
                    // The original NOPs the two recovered sites; a different count
                    // is a view mismatch, so the arm installs nothing.
                    if (auto pattern = PatternAll(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08"); pattern.size() > 0 &&
                        ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport episode test"))
                    {
                        NopAt(ctx, pattern.get(0).get<uint8_t>(), 9);
                        NopAt(ctx, pattern.get(1).get<uint8_t>(), 9);
                    }
                }

                if (auto pattern = Pattern(image, "39 35 ? ? ? ? 75 0E 80 3D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 6, 2);
                }

                if (auto pattern = Pattern(image, "74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 2, 9);
                }

                if (auto pattern = Pattern(image, "7C 22 83 F8 28 75 1D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C 0E 05 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 08 3B 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 1F 8B 44 24 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 35 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 38 8B 16"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 11 8B 4E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 1F 8B"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 35 0F BF 43 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 43"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 89 44 24 38"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 11, 6);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 8A 0D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 7);
                    NopAt(ctx, match + 17, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 21, 2);
                }

                if (auto pattern = Pattern(image, "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match - 15, 7);
                    NopAt(ctx, match + 10, 2);
                }
            }
        }

    }


    // EpisodicWeaponSupport

    void InstallEpisodicWeaponSupport(PatchContext& ctx)
    {

        TraceArm("EpisodicWeaponSupport");

        // EpisodicWeaponSupport: 8 literals
        if (ctx.Option("EpisodicWeaponSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "75 5B 83 3D ? ? ? ? 00 75"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 85 8C 01 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 6);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C D5 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 20 8B CD E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 50"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 32 8B 41"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }
            }
        }

    }


    // publish HighFPSBikePhysicsFix

    void PublishHighFPSBikePhysicsFix(PatchContext& ctx)
    {

        TraceArm("publish HighFPSBikePhysicsFix");

        // HighFPSBikePhysicsFix: 1 slot
        if (ctx.Option("HighFPSBikePhysicsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
            {
                zpatch_bike_resume = MatchAddress(pattern, 9);
            }
        }

    }


    // HighFPSBikePhysicsFix

    void InstallHighFPSBikePhysicsFix(PatchContext& ctx)
    {

        TraceArm("HighFPSBikePhysicsFix");

        // HighFPSBikePhysicsFix: 1 sigjump
        if (ctx.Option("HighFPSBikePhysicsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_bike_physics_detour));
            }
        }

    }


    // MovingPlaneParts / MovingSteeringWheelSupport / ShakingEngineBlock are
    // three INI arms over the same five slots: two steering-call resumes with
    // their call targets, and the shared IDE parser hook. The first pattern is
    // the EAX call variant, the second the EDX one; the calls' resumes are 16
    // bytes into the match and the IDE slot is the byte before its match.
    namespace
    {
        constexpr const char* kSteerEaxPattern =
            "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0";
        constexpr const char* kSteerEdxPattern =
            "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2";
        constexpr const char* kIdeHookPattern = "0C 83 88 94 00 00 00 04";
        constexpr ptrdiff_t kSteerResumeOffset = 16;
        constexpr ptrdiff_t kSteerEaxCallOperand = 423;
        constexpr ptrdiff_t kSteerEdxCallOperand = 140;
        constexpr ptrdiff_t kSteerInstallOffset = 2;
        constexpr ptrdiff_t kSteerIdeHookSlotOffset = -1;
        constexpr ptrdiff_t kSteerIdeInstallOffset = -10;

        void PublishSteerSlots(PatchContext& ctx)
        {
            const GameImage& image = ctx.image;
            if (auto pattern = Pattern(image, kSteerEaxPattern); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, kSteerResumeOffset);
                zpatch_steer_vtable_fn = BranchDestination(
                    pattern.get(0).get<uint8_t>(), kSteerEaxCallOperand);
            }
            if (auto pattern = Pattern(image, kSteerEdxPattern); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, kSteerResumeOffset);
                zpatch_steer_vtable_fn = BranchDestination(
                    pattern.get(0).get<uint8_t>(), kSteerEdxCallOperand);
            }
            if (auto pattern = Pattern(image, kIdeHookPattern); pattern.size() > 0)
                zpatch_ide_hook_slot = MatchAddress(pattern, kSteerIdeHookSlotOffset);
        }

        void InstallSteerSlots(PatchContext& ctx)
        {
            const GameImage& image = ctx.image;
            if (auto pattern = Pattern(image, kSteerEaxPattern); pattern.size() > 0)
                JumpTo(ctx, pattern.get(0).get<uint8_t>() + kSteerInstallOffset,
                       OPP_BODY(abi_steer_detour_eax));
            if (auto pattern = Pattern(image, kSteerEdxPattern); pattern.size() > 0)
                JumpTo(ctx, pattern.get(0).get<uint8_t>() + kSteerInstallOffset,
                       OPP_BODY(abi_steer_detour_edx));
            if (auto pattern = Pattern(image, kIdeHookPattern); pattern.size() > 0)
                JumpTo(ctx, pattern.get(0).get<uint8_t>() + kSteerIdeInstallOffset,
                       OPP_BODY(abi_copsirens_ide));
        }
    } // namespace

    // publish MovingPlaneParts

    void PublishMovingPlaneParts(PatchContext& ctx)
    {

        TraceArm("publish MovingPlaneParts");

        // MovingPlaneParts: 5 slots (the shared steering slots).
        if (ctx.Option("MovingPlaneParts", "1"))
            PublishSteerSlots(ctx);

    }


    // MovingPlaneParts

    void InstallMovingPlaneParts(PatchContext& ctx)
    {

        TraceArm("MovingPlaneParts");

        // MovingPlaneParts: 3 siginstalls (the shared steering sites).
        if (ctx.Option("MovingPlaneParts", "1"))
            InstallSteerSlots(ctx);

    }


    // publish MovingSteeringWheelSupport

    void PublishMovingSteeringWheelSupport(PatchContext& ctx)
    {

        TraceArm("publish MovingSteeringWheelSupport");

        // MovingSteeringWheelSupport: 5 slots (the shared steering slots).
        if (ctx.Option("MovingSteeringWheelSupport", "1"))
            PublishSteerSlots(ctx);

    }


    // MovingSteeringWheelSupport

    void InstallMovingSteeringWheelSupport(PatchContext& ctx)
    {

        TraceArm("MovingSteeringWheelSupport");

        // MovingSteeringWheelSupport: 3 siginstalls (the shared steering sites).
        if (ctx.Option("MovingSteeringWheelSupport", "1"))
            InstallSteerSlots(ctx);

    }


    // publish NoLiveryLimit

    void PublishNoLiveryLimit(PatchContext& ctx)
    {

        TraceArm("publish NoLiveryLimit");

        // NoLiveryLimit: 5 slots
        if (ctx.Option("NoLiveryLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
            {
                // The call operand at -6 (its next instruction is at -2); the
                // resume is 90 bytes into the match.
                constexpr ptrdiff_t kNoliveryCallOperandA = -6;
                constexpr ptrdiff_t kNoliveryResumeDOffsetA = 90;
                auto* match = pattern.get(0).get<uint8_t>();
                zpatch_nolivery_fn = BranchDestination(match - 2, kNoliveryCallOperandA);
                zpatch_nolivery_resume_d = MatchAddress(pattern, kNoliveryResumeDOffsetA);
            }

            if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
            {
                // The resume is 13 bytes into the match (after its prologue).
                constexpr ptrdiff_t kNoliveryResumeOffset = 13;
                zpatch_nolivery_resume = MatchAddress(pattern, kNoliveryResumeOffset);
            }

            if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
            {
                // The same call shape as the first row, four bytes earlier.
                constexpr ptrdiff_t kNoliveryCallOperandB = -8;
                constexpr ptrdiff_t kNoliveryResumeDOffsetB = 84;
                auto* match = pattern.get(0).get<uint8_t>();
                zpatch_nolivery_fn = BranchDestination(match - 4, kNoliveryCallOperandB);
                zpatch_nolivery_resume_d = MatchAddress(pattern, kNoliveryResumeDOffsetB);
            }
        }

    }


    // NoLiveryLimit

    void InstallNoLiveryLimit(PatchContext& ctx)
    {

        TraceArm("NoLiveryLimit");

        // NoLiveryLimit: 1 siginstall, 3 sigjumps
        if (ctx.Option("NoLiveryLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The three no-livery body entries relative to their shared match.
                constexpr ptrdiff_t kNoliveryBodyBOffset = -237;
                constexpr ptrdiff_t kNoliveryBodyCOffset = -199;
                JumpTo(ctx, match + kNoliveryBodyBOffset, OPP_BODY(abi_nolivery_b));
                JumpTo(ctx, match + kNoliveryBodyCOffset, OPP_BODY(abi_nolivery_c));
                JumpTo(ctx, match, OPP_BODY(abi_nolivery_a));
            }

            if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                constexpr ptrdiff_t kNoliveryBodyDOffset = 2;
                JumpTo(ctx, match + kNoliveryBodyDOffset, OPP_BODY(abi_nolivery_d));
            }

            if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_nolivery_e));
            }
        }

    }


    // PoliceEpisodicWeaponSupport

    void InstallPoliceEpisodicWeaponSupport(PatchContext& ctx)
    {

        TraceArm("PoliceEpisodicWeaponSupport");

        // PoliceEpisodicWeaponSupport: 1 literal
        if (ctx.Option("PoliceEpisodicWeaponSupport", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "39 2D ? ? ? ? 75 3E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 6, 2);
                }
            }
        }

    }


    // publish PopupHeadlightsFlag

    void PublishPopupHeadlightsFlag(PatchContext& ctx)
    {

        TraceArm("publish PopupHeadlightsFlag");

        // PopupHeadlightsFlag: 5 slots
        if (ctx.Option("PopupHeadlightsFlag", "1"))
        {
            const GameImage& image = ctx.image;

            // The original handler (0x10044590) is `if (pattern A) { ... }
            // else { pattern B ... }`: exactly ONE of the two EAX/EDX sites is
            // ever patched, and the two resume slots belong to that site. Two
            // independent `if`s would install both bodies against whichever
            // pattern matched last, so the first body would tail-jump to the
            // second site's resume.
            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                constexpr ptrdiff_t kResumeBOffset = 10;
                constexpr ptrdiff_t kResumeAOffset = 12;
                zpatch_popup_resume_b = MatchAddress(pattern, kResumeBOffset);
                zpatch_popup_resume_a = MatchAddress(pattern, kResumeAOffset);
            }
            else if (auto patternB = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); patternB.size() > 0)
            {
                constexpr ptrdiff_t kResumeBOffset = 10;
                constexpr ptrdiff_t kResumeAOffset = 12;
                zpatch_popup_resume_b = MatchAddress(patternB, kResumeBOffset);
                zpatch_popup_resume_a = MatchAddress(patternB, kResumeAOffset);
            }

            // The IDE parser hook is independent of the two model tests and is
            // shared with the other arms that install it.
            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                constexpr ptrdiff_t kIdeSlotOffset = -1;
                zpatch_ide_hook_slot = MatchAddress(pattern, kIdeSlotOffset);
            }
        }

    }


    // PopupHeadlightsFlag

    void InstallPopupHeadlightsFlag(PatchContext& ctx)
    {

        TraceArm("PopupHeadlightsFlag");

        // PopupHeadlightsFlag: 3 siginstalls, 1 sigjump
        if (ctx.Option("PopupHeadlightsFlag", "1"))
        {
            const GameImage& image = ctx.image;

            // if / else, like the original: see the publish arm above.
            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The `jz` at +10 and the `fld` operand at +82 that now loads our
                // pi/2 float.
                constexpr ptrdiff_t kBranchOffset = 10;
                constexpr ptrdiff_t kAngleOperand = 82;
                JumpTo(ctx, match, OPP_BODY(abi_popup_headlights));
                WriteValue<uint8_t>(ctx, match + kBranchOffset, opp::ops::kJmpRel8);
                WriteValue<uint32_t>(ctx, match + kAngleOperand,
                                     AddressOf(&zpatch_popup_angle));
            }
            else if (auto patternB = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); patternB.size() > 0)
            {
                auto* match = patternB.get(0).get<uint8_t>();

                constexpr ptrdiff_t kBranchOffset = 10;
                constexpr ptrdiff_t kAngleOperand = 82;
                JumpTo(ctx, match, OPP_BODY(abi_popup_headlights_edx));
                WriteValue<uint8_t>(ctx, match + kBranchOffset, opp::ops::kJmpRel8);
                WriteValue<uint32_t>(ctx, match + kAngleOperand,
                                     AddressOf(&zpatch_popup_angle));
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                constexpr ptrdiff_t kPopupIdeInstallOffset = -10;
                JumpTo(ctx, match + kPopupIdeInstallOffset, OPP_BODY(abi_copsirens_ide));
            }
        }

    }


    // publish ShakingEngineBlock

    void PublishShakingEngineBlock(PatchContext& ctx)
    {

        TraceArm("publish ShakingEngineBlock");

        // ShakingEngineBlock: 5 slots (the shared steering slots).
        if (ctx.Option("ShakingEngineBlock", "1"))
            PublishSteerSlots(ctx);

    }


    // ShakingEngineBlock

    void InstallShakingEngineBlock(PatchContext& ctx)
    {

        TraceArm("ShakingEngineBlock");

        // ShakingEngineBlock: 3 siginstalls (the shared steering sites).
        if (ctx.Option("ShakingEngineBlock", "1"))
            InstallSteerSlots(ctx);

    }


    // TBoGTHeliHeightLimit

    void InstallTBoGTHeliHeightLimit(PatchContext& ctx)
    {

        TraceArm("TBoGTHeliHeightLimit");

        // TBoGTHeliHeightLimit: 1 literal
        if (ctx.Option("TBoGTHeliHeightLimit", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 0A F3 0F"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }
            }
        }

    }

namespace
{
    class VehiclesFeatures
    {
    public:
        VehiclesFeatures()
        {
            PublishEvent().Add(&PublishCopSirensFlag, "PublishCopSirensFlag");
            PublishEvent().Add(&PublishHighFPSBikePhysicsFix, "PublishHighFPSBikePhysicsFix");
            PublishEvent().Add(&PublishNoLiveryLimit, "PublishNoLiveryLimit");
            PublishEvent().Add(&PublishPopupHeadlightsFlag, "PublishPopupHeadlightsFlag");
            PublishEvent().Add(&PublishMovingSteeringWheelSupport, "PublishMovingSteeringWheelSupport");
            PublishEvent().Add(&PublishShakingEngineBlock, "PublishShakingEngineBlock");
            PublishEvent().Add(&PublishMovingPlaneParts, "PublishMovingPlaneParts");
            InstallEvent().Add(&InstallBikeFeetFix, "InstallBikeFeetFix");
            InstallEvent().Add(&InstallBikePhoneAnimsFix, "InstallBikePhoneAnimsFix");
            InstallEvent().Add(&InstallCopSirensFlag, "InstallCopSirensFlag");
            InstallEvent().Add(&InstallEpisodicVehicleSupport, "InstallEpisodicVehicleSupport");
            InstallEvent().Add(&InstallEpisodicWeaponSupport, "InstallEpisodicWeaponSupport");
            InstallEvent().Add(&InstallHighFPSBikePhysicsFix, "InstallHighFPSBikePhysicsFix");
            InstallEvent().Add(&InstallNoLiveryLimit, "InstallNoLiveryLimit");
            InstallEvent().Add(&InstallPoliceEpisodicWeaponSupport, "InstallPoliceEpisodicWeaponSupport");
            InstallEvent().Add(&InstallPopupHeadlightsFlag, "InstallPopupHeadlightsFlag");
            InstallEvent().Add(&InstallTBoGTHeliHeightLimit, "InstallTBoGTHeliHeightLimit");
            InstallEvent().Add(&InstallMovingSteeringWheelSupport, "InstallMovingSteeringWheelSupport");
            InstallEvent().Add(&InstallShakingEngineBlock, "InstallShakingEngineBlock");
            InstallEvent().Add(&InstallMovingPlaneParts, "InstallMovingPlaneParts");
        }
    } vehiclesFeatures;
} // namespace

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
        std::array<VehicleLightValue, 512> popupAngles{};      // radians
        std::array<VehicleLightValue, 512> headlightOffsets{};
    };

    // FUN_100523c0's two 512-slot INI loops. Missing model names leave holes;
    // numeric parsing follows strtof's prefix acceptance.
    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept;

    using ModelIndexResolver = int (*)(uint32_t, void*);

    struct PopupLightSelection
    {
        float angleRadians{1.5707963705062866f};
        bool supported{};
    };

    PopupLightSelection SelectPopupLight(const VehicleLightTables&,
                                         const std::array<uint32_t, 512>& supportedModels,
                                         int modelIndex, ModelIndexResolver,
                                         void* context = nullptr) noexcept;
    float SelectHeadlightOffset(const VehicleLightTables&, int modelIndex, ModelIndexResolver,
                                void* context = nullptr) noexcept;
} // namespace opp





namespace opp
{
    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept
    {
        try
        {
            VehicleLightTables parsed;
            const char* sections[] = {"PopupLightAngles", "HeadlightConeOffsets"};
            const char* suffixes[] = {"Angle", "Offset"};

            for (unsigned table = 0; table < std::size(sections); ++table)
            {
                auto& slots = table == 0 ? parsed.popupAngles : parsed.headlightOffsets;
                for (unsigned i = 0; i < slots.size(); ++i)
                {
                    const std::string prefix = "Car" + std::to_string(i + 1);
                    const std::string name =
                        config.String(sections[table], prefix + "Name", "");
                    if (name.empty())
                        continue;

                    const std::string value =
                        config.String(sections[table], prefix + suffixes[table], "");
                    if (value.empty())
                        return {PatchResultCode::parse_error, "missing vehicle light value"};

                    char* end = nullptr;
                    errno = 0;
                    const float numeric = std::strtof(value.c_str(), &end);
                    if (end == value.c_str() || errno == ERANGE)
                        return {PatchResultCode::parse_error, "invalid vehicle light value"};

                    // The popup table is written in degrees and read in radians.
                    VehicleLightValue& slot = slots[i];
                    slot.modelHash = GameHash(name);
                    slot.populated = true;
                    slot.value = table == 0
                                     ? static_cast<float>(numeric * 3.141592653589793 / 180.0)
                                     : numeric;
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

    PopupLightSelection SelectPopupLight(const VehicleLightTables& tables,
                                         const std::array<uint32_t, 512>& supportedModels,
                                         int modelIndex, ModelIndexResolver resolve,
                                         void* context) noexcept
    {
        PopupLightSelection result;
        if (!resolve || modelIndex < 0)
            return result;

        // The special-script model hash FUN_1001ad20 resolves first; it is the
        // one hash the original compares against the model index.
        constexpr uint32_t kSpecialScriptHash = 0xEB9F21D3u;
        const int specialIndex = resolve(kSpecialScriptHash, context);
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
            return 2.0f;

        // FUN_1001ad20 stops at the first zero hash rather than skipping holes.
        for (const VehicleLightValue& entry : tables.headlightOffsets)
        {
            if (!entry.modelHash)
                break;
            if (entry.populated && resolve(entry.modelHash, context) == modelIndex)
                return entry.value;
        }
        return 2.0f;
    }
} // namespace opp
