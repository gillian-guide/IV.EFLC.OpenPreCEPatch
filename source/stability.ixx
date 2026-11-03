// SPDX-License-Identifier: GPL-3.0-or-later
//
// stability
//
// One function per INI arm, in the original loader order. The recovered C++
// helpers and the game-memory views they read stay with the arms; the
// machine-level bodies are the tail section at the bottom of this file.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

#include <memory>
#include <span>

#include "data/ooc_payload.h"
export module opp.stability;

import opp.patch;
import opp.game;
import opp.common;
import opp.config;
import opp.log;

#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

namespace
{
    // The game objects these hooks read, pinned member by member. The structs are
    // views over live game memory and are never constructed.

    // CEntity prefix: the model index every entity carries.
    constexpr size_t kEntityModelIndex = kModelIndexOffset;  // 0x02E, int16
    struct EntityView final
    {
        std::byte pad_0000[kEntityModelIndex];
        int16_t modelIndex;  // 0x02E
    };
    static_assert(offsetof(EntityView, modelIndex) == kEntityModelIndex);
    static_assert(sizeof(EntityView) == kEntityModelIndex + sizeof(int16_t));

    // CBaseModelInfo prefix: the model's name hash, the key
    // CModelInfoStore::getModelByKey (0x8DD2D0) resolves. BadPlayerModelCrashFix
    // reports it and the garage stores it in its slot.
    constexpr size_t kModelInfoHashKey = 0x3C;
    struct ModelInfoView final
    {
        std::byte pad_0000[kModelInfoHashKey];
        uint32_t hashKey;  // 0x03C
    };
    static_assert(offsetof(ModelInfoView, hashKey) == kModelInfoHashKey);

    // The model-info table the preamble scan publishes: one CBaseModelInfo* per
    // model index.
    using ModelInfoTable = ModelInfoView* const*;

    // CBaseModelInfo::getType (vtable slot 3, offset 0xC). CPedModelInfo answers 6
    // and CVehicleModelInfo 5; only the ped answer keeps the ped the crash fix
    // remaps.
    enum ModelType : uint8_t
    {
        kModelTypePed = 6,
    };

    // CGarage's saved-vehicle slot: 0x48 bytes, the record the game walks two of
    // in sub_9659A0. The original store (sub_10046C80) copies the vehicle's model
    // index and its model-info name hash here; the four hash bytes are scattered
    // over the record, so the hash is assembled little-endian from them.
    struct GarageSlotView final
    {
        std::byte pad_00[16];
        int16_t modelIndex;  // 0x10
        std::byte pad_12[1];
        uint8_t hashByte0;  // 0x13
        std::byte pad_14[28];
        uint16_t savedFlags;  // 0x30
        std::byte pad_32[14];
        uint8_t hashByte1;  // 0x40
        uint8_t hashByte2;  // 0x41
        std::byte pad_42[4];
        uint8_t hashByte3;  // 0x46
        std::byte pad_47[1];
    };
    static_assert(sizeof(GarageSlotView) == 0x48);

    // Bit 0 of savedFlags is set when the game saves the slot (sub_C68B90 ends
    // with `or word [esi+0x30], 1`); the replacement clears it after it stores a
    // substituted model.
    enum : uint16_t
    {
        kGarageSlotSaved = 1u << 0,
    };

    // SpeedupBugFix: the timing function the handler's `50 64 ...` scan finds calls
    // the clock function, which sits 51 bytes before that call.

    // float(0.001), the game's millisecond scale, spelled as its exact decimal so
    // the double arithmetic keeps the recovered value.
    constexpr double kMillisecondScale = 0.001000000047497451;
    static_assert(std::bit_cast<uint32_t>(static_cast<float>(kMillisecondScale)) == 0x3A83126Fu);
}

// Hook bodies: the declarations the installers take the address of; their definitions
// are the tail section at the bottom of this file, after every installer. Each keeps the
// original site's calling convention and register contract; the @exact markers stay here
// for tools/opcep_bodycheck.py.
extern "C"
{
// @exact 0x10005590
void abi_aim_projectile_crash_fix();
// @exact 0x1003d7b0
void abi_path_node_shim();
// @exact 0x1003d870
void abi_plane_crash_shim();
// @exact 0x10050490
void abi_wanted_level_shim();
// @exact 0x10036e90
void abi_group_hack_shim();
// @exact 0x10046750
void abi_smpa60_shim();
// @exact 0x10046780
void abi_smpa60_tail_shim();
// @exact 0x1003d9a0
void abi_player_ped_components_shim();
// @exact 0x10046C80
void abi_garage_body_a();
void abi_garage_entry_a();  // synthetic prologue entry: no original copy
// @exact 0x10046CA0
void abi_garage_body_b();
// @exact 0x10037BA0
void abi_bad_player_model();
// @exact 0x100469E0
void abi_super_lod_fix_shim();
// @exact 0x10036F00
void abi_crash_msg_shim();
// @exact 0x10005650
void abi_benchmark_a();
// @exact 0x10005670
void abi_benchmark_b();
// @exact 0x1001b040
void abi_episode_switch_store_shim();
// @exact 0x1001b060
void abi_episode_switch_shim_b();
// @exact 0x1001b0a0
void abi_episode_switch_vcall();
// @exact 0x1001b0c0
void abi_episode_switch_shim_c();
// @exact 0x1001b080
void abi_episode_switch_sum();
// @exact 0x10005d80
void abi_collision_deadlock_walk();
// @exact 0x10036ee0
void abi_blackscreen_shim();
// @exact 0x1004C3C0
void abi_ooc_branch();

// SuperLODFix's model check (original sub_100469A0): true when the ped is the
// "superlod" model, i.e. the model the fix forces through the LOD path. A missing
// lookup or object answers "not the SuperLOD model", the un-patched answer.
bool abi_super_lod_helper(const void* object) noexcept
{
    int32_t index = -1;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_super_lod_lookup));
    if (zpatch_failsafe_skip("SuperLODFix lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("SuperLODFix object", object == nullptr))
        return false;
    // GameHash("superlod"): the model the fix forces through the LOD path.
    lookup(GameHash("superlod"), &index);  // sub_100469A0's call is unguarded
    return static_cast<const EntityView*>(object)->modelIndex == index;
}

// BadPlayerModelCrashFix's check (original sub_10037AC0): when the model at
// zpatch_bpm_model_index is not a ped, remap it to "m_y_multiplayer". Called
// between pushad/popad, so the GPRs are free; the model table base stays in ECX
// because the original leaves it there for its `call eax`.
void abi_bpm_check() noexcept
{
    const auto models = reinterpret_cast<ModelInfoTable>(
        static_cast<uintptr_t>(zpatch_pre_ped_array));
    if (zpatch_failsafe_skip("BadPlayerModelCrashFix model table", models == nullptr))
        return;  // nothing to remap: the site keeps the model it was given
    ModelInfoView* const model = models[zpatch_bpm_model_index];
    if (model == nullptr)
        return;
    void** const vtable = *reinterpret_cast<void***>(model);
    const auto get_type = reinterpret_cast<char(__thiscall*)(void*)>(vtable[3]);
    const char class_id = get_type(const_cast<void*>(reinterpret_cast<const void*>(models)));
    if (class_id == kModelTypePed)
        return;
    if (GetModuleHandleW(L"GTAC.dll") != nullptr)
    {
        const char* const model_name = zpatch_bpm_model_name(model->hashKey);
        uint32_t entity = zpatch_bpm_obj;
        const char* const who = reinterpret_cast<const char*>(static_cast<uintptr_t>(
            zpatch_script_call(zpatch_salt_c8, 1, &entity)));
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s used a non-ped model (%s)", who, model_name);
        const uint32_t text = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
        zpatch_script_call(kPrintScriptHash, 1, &text);
    }
    const uint32_t hash = GameHash("m_y_multiplayer");
    if (zpatch_script_call_bool(zpatch_salt_b8, 1, &hash) == 0)
    {
        const auto detour = reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(
            static_cast<uintptr_t>(zpatch_pre_fn_59c74));
        if (!zpatch_failsafe_skip("BadPlayerModelCrashFix script detour", detour == nullptr))
            detour(hash, 0);
        zpatch_script_call(zpatch_salt_40, 0, nullptr);
    }
    int32_t new_index = -1;
    const auto lookup = reinterpret_cast<void(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    if (!zpatch_failsafe_skip("BadPlayerModelCrashFix model lookup", lookup == nullptr))
    {
        lookup(hash, &new_index);
        zpatch_bpm_model_index = static_cast<uint32_t>(new_index);
    }
}

// GarageModelFix's replacement lookup (original sub_10046C80's matching half):
// rebuild the model hash the store stashed, resolve it, and answer the model index
// the garage should use. A hash the table answers with an out-of-range index is a
// missing model, so the slot gets the replacement model ("taxi") instead.
int32_t zpatch_garage_lookup_c(uint8_t* object) noexcept
{
    auto* const slot = reinterpret_cast<GarageSlotView*>(object);
    const uint32_t hash = static_cast<uint32_t>(slot->hashByte0) |
                          (static_cast<uint32_t>(slot->hashByte1) << 8) |
                          (static_cast<uint32_t>(slot->hashByte2) << 16) |
                          (static_cast<uint32_t>(slot->hashByte3) << 24);
    // The original's fallback answer is the low hash byte, which is zero on the
    // no-hash path.
    const int32_t hash_low_byte = static_cast<int32_t>(slot->hashByte0);
    if (hash == 0u)
        return hash_low_byte;
    // 0x10046C10 / 0x10046C4A call dword_10159C0C unguarded.
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    if (zpatch_failsafe_skip("GarageModelFix model lookup", lookup == nullptr))
        return hash_low_byte;  // the garage keeps the model it stored
    // A successful resolution is remembered: the hash -> index mapping is static
    // after startup (direct-mapped; a collision just re-resolves). Negative
    // results are never cached, because a model that is not loaded yet must still
    // be found later.
    struct GarageMemo final
    {
        uint32_t hash;
        int32_t index;
    };
    constexpr size_t kGarageMemoSlots = 64;
    constexpr unsigned kGarageMemoShift = 26;  // 32 - log2(kGarageMemoSlots)
    static_assert((1u << (32 - kGarageMemoShift)) == kGarageMemoSlots);
    static GarageMemo memo[kGarageMemoSlots] = {};
    const size_t memo_slot = (hash * 2654435761u) >> kGarageMemoShift;
    int32_t out = -1;
    if (memo[memo_slot].hash == hash && memo[memo_slot].index > 0)
    {
        out = memo[memo_slot].index;
    }
    else
    {
        lookup(hash, &out);
        if (out > 0)
        {
            memo[memo_slot].hash = hash;
            memo[memo_slot].index = out;
        }
    }
    // The replacement only accepts an index below 32000; a failed resolve or the
    // slot's out-of-range marker falls back to the replacement model.
    if (out <= 0 || out >= 32000)
    {
        // The replacement model is a constant hash (GameHash("taxi")), so its index
        // is resolved once; the original looks it up on every miss.
        static int32_t fallback_index = -1;
        if (fallback_index < 0)
            lookup(GameHash("taxi"), &fallback_index);
        out = fallback_index > 0 ? fallback_index : -1;
        slot->modelIndex = static_cast<int16_t>(out);
        slot->savedFlags &= static_cast<uint16_t>(~kGarageSlotSaved);
        // The value the replacement returns for "model substituted" (0xFFFE),
        // which body B pushes as the model index it hands back to the game.
        return 65534;
    }
    slot->modelIndex = static_cast<int16_t>(out);
    return out;
}

// CombinedRadioFreezeFix (original 0x10005ED0): the whole body is a constant
// return, so the compiler emits the original's `mov eax,13h; ret`.
int __cdecl abi_combined_radio_freeze_fix() noexcept
{
    return 19;  // `mov eax,13h`
}

// PedModelsInCutscenesFix (original sub_A23820): replaces the game's "does this
// object use the player model" check. The original compares the local player's
// model against this entity's; the replacement resolves the player model index
// through the published lookup, so a cutscene clone of a player model still
// matches.
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*)
{
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    // `call dword_10159C0C` is unguarded in the original. A missing lookup or a
    // null self answers "not the player model", the un-patched answer.
    if (zpatch_failsafe_skip("PedModelsInCutscenesFix model lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("PedModelsInCutscenesFix object", self == nullptr))
        return false;
    int32_t index = -1;
    lookup(GameHash("player"), &index);
    return static_cast<const EntityView*>(self)->modelIndex == index;
}

// SpeedupBugFix: replaces the game's timing helper (0x10050540, __thiscall on a
// QWORD). It lazily fills the game's eight timing constants (the publish arm
// stores pointers to them) from QueryPerformanceFrequency, then stores a timestamp
// from the game's own clock function (match-51) into *self.
void* __fastcall abi_speedup_body(void* self, int) noexcept
{
    // Mirror sub_10050587's MSVC static-init guard and its clock cache
    // (dword_101A849C). The clock function is located by this signature; the
    // handler's `50 64 ...` scan finds the E9 site, not this function.
    if (zpatch_tls_static_header(zpatch_speedup_scan_guard))
    {
        // The clock function this signature finds sits 51 bytes before the call
        // the handler's `50 64 ...` scan locates.
        zpatch_speedup_clock =
            static_cast<uint32_t>(newfiles_find("1B D9 89 5C 24 24 0F 88"));
        zpatch_tls_static_footer(zpatch_speedup_scan_guard);
    }
    // The original dereferences the timing pointers unconditionally; its one-time
    // test is on the first constant, not on the pointers.
    float* const first = reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t50));
    if (*first == 0.0f)
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const double milliseconds_per_tick = 1000.0 / static_cast<double>(frequency.QuadPart);
        // The four derived values the game keeps, in its own order: milliseconds,
        // microseconds and seconds per tick, then ticks per millisecond.
        const float milliseconds = static_cast<float>(milliseconds_per_tick);
        const float microseconds = static_cast<float>(milliseconds_per_tick * 1000.0);
        const float seconds = static_cast<float>(milliseconds_per_tick * kMillisecondScale);
        const float ticks_per_millisecond =
            static_cast<float>(kMillisecondScale / milliseconds_per_tick);
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t64)) = milliseconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t60)) = microseconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t50)) = seconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t5c)) =
            ticks_per_millisecond;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t68)) = milliseconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t4c)) = microseconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t54)) = seconds;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t58)) =
            ticks_per_millisecond;
    }
    using ClockFn = __int64(__cdecl*)(void);
    // The original calls its resolved clock function unconditionally; a failed
    // scan faults at match-51.
    const __int64 stamp = reinterpret_cast<ClockFn>(
        static_cast<uintptr_t>(zpatch_speedup_clock) - 51)();
    *reinterpret_cast<__int64*>(self) = stamp;
    return self;
}
}  // extern "C"

namespace
{
    // ---- OutOfCommissionFix's finale2.sco payload -------------------------
    //
    // The original embeds a compressed "finale2.sco" (163796 bytes at
    // 0x100B6798); its init (sub_1004B750 -> sub_1004B570/sub_1004B150) decodes it
    // into a 179536-byte table, registers it, then sub_1004ACE0 builds an in-memory
    // RPF archive around it. The wrapper mounts that archive with the game's own
    // mount function ("memory:$<ptr>,<size>,0:") and the E9 body widens the game's
    // script-id check to accept its resource magic. The decode below mirrors
    // sub_1004B150's opcode loop.
    constexpr size_t kOocTableSize = 0x2BD50u;  // 179536
    constexpr const unsigned char* kOocPakBlob = zpatch_ooc_blob;
    constexpr std::string_view kOocEntryName = "finale2.sco";
    struct OocPakFormat final
    {
        uint32_t magic;
        uint32_t version;
        uint32_t entries;
        uint16_t header_bytes;
        uint16_t flags;
    };
    constexpr OocPakFormat kOocPak{0x86723DFFu, 3u, 1u, 16u, 0xE9u};
    constexpr uint32_t kOocPageSize = 2048u;

    inline void zpatch_ooc_store(uint32_t* destination, uint32_t value) noexcept
    {
        *reinterpret_cast<volatile uint32_t*>(destination) = value;
    }

    struct OocPakEntry final
    {
        char name_buffer[16];
        uint32_t name_size;
        uint32_t name_capacity;
        uint32_t file_size;
        uint32_t stored_offset;
        uint32_t stored_size;
        uint32_t padding;
        uint32_t data;
    };
    static_assert(sizeof(OocPakEntry) == 44);

    void zpatch_ooc_register_entry(std::span<const std::byte> table)
    {
        auto* begin = reinterpret_cast<OocPakEntry*>(
            static_cast<uintptr_t>(zpatch_ooc_vector_begin));
        auto* end = reinterpret_cast<OocPakEntry*>(
            static_cast<uintptr_t>(zpatch_ooc_vector_end));
        auto* capacity = reinterpret_cast<OocPakEntry*>(
            static_cast<uintptr_t>(zpatch_ooc_vector_capacity));
        const size_t size = begin == nullptr ? 0u : static_cast<size_t>(end - begin);
        const size_t cap = begin == nullptr ? 0u : static_cast<size_t>(capacity - begin);
        bool reallocated = false;
        if (size == cap)
        {
            const size_t new_capacity =
                (cap + cap / 2u > size) ? cap + cap / 2u : size + 1u;
            auto* replacement = static_cast<OocPakEntry*>(
                ::operator new(new_capacity * sizeof(OocPakEntry)));
            if (size != 0u)
                std::memcpy(replacement, begin, size * sizeof(OocPakEntry));
            if (begin != nullptr)
                ::operator delete(begin);
            begin = replacement;
            end = begin + size;
            capacity = begin + new_capacity;
            reallocated = true;
        }

        OocPakEntry entry{};
        std::memcpy(entry.name_buffer, kOocEntryName.data(), kOocEntryName.size() + 1u);
        entry.name_size = static_cast<uint32_t>(kOocEntryName.size());
        entry.name_capacity = 15u;  // the game's std::string small-buffer capacity
        entry.file_size = static_cast<uint32_t>(kOocTableSize);
        entry.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table.data()));
        std::memcpy(end, &entry, sizeof(entry));
        ++end;
        if (reallocated)
        {
            zpatch_ooc_store(&zpatch_ooc_vector_begin,
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(begin)));
            zpatch_ooc_store(&zpatch_ooc_vector_end,
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)));
            zpatch_ooc_store(&zpatch_ooc_vector_capacity,
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(capacity)));
        }
        else
        {
            zpatch_ooc_store(&zpatch_ooc_vector_end,
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)));
        }
    }

    // The format's shared radix and Adler values; the opcode set and its biases
    // live in OocStream::step, next to the branches that use them.
    constexpr uint32_t kOocByteBase = 256u;
    constexpr uint32_t kOocWordBase = 65536u;
    constexpr uint32_t kOocAdlerModulus = 0xFFF1u;
    constexpr size_t kOocAdlerBlock = 1872u;

    struct OocStream final
    {
        size_t cursor = 0;  // the original's dword_101A8430
        unsigned char* dest = nullptr;

        void literal(size_t count, size_t from) noexcept
        {
            if (cursor + count <= kOocTableSize)
            {
                // The original's source bound (a1 + n >= dword_101887D4 = blob base
                // - 16) is constant-true: the stream starts past the base pointer.
                std::memcpy(dest + cursor, kOocPakBlob + from, count);
                cursor += count;
            }
            else
            {
                cursor = kOocTableSize + 1;
            }
        }
        void backref(size_t distance, size_t count) noexcept
        {
            if (cursor + count > kOocTableSize || distance > cursor)
            {
                cursor = kOocTableSize + 1;
                return;
            }
            unsigned char* out = dest + cursor;
            const unsigned char* in = out - distance;
            for (size_t i = 0; i < count; ++i)
                out[i] = in[i];  // overlap propagates
            cursor += count;
        }
        size_t step(size_t pc) noexcept
        {
            // The finale2 stream's opcode format; the code values are part of the
            // resource format, not arbitrary numbers. Hex in the original: the
            // boundaries are 0x20, 0x18, 0x10, 0x08, 0x07, 0x06, 0x04, 0x40 and
            // 0x80, the distance biases 0x17FFFF/0xFFFFF/0x3FFF, and the literal/
            // shortest biases 31/30 and 127.
            const unsigned char op = kOocPakBlob[pc];
            if (op < 32)
            {
                if (op >= 24)
                {
                    backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
                                kOocWordBase * op - 1572863u,
                            kOocPakBlob[pc + 3] + 1u);
                    return pc + 4;
                }
                if (op >= 16)
                {
                    backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
                                kOocWordBase * op - 1048575u,
                            (kOocPakBlob[pc + 3] << 8) + kOocPakBlob[pc + 4] + 1u);
                    return pc + 5;
                }
                if (op >= 8)
                {
                    const size_t count =
                        kOocByteBase * op + kOocPakBlob[pc + 1] - 2047u;
                    literal(count, pc + 2);
                    return pc + 2 + count;
                }
                if (op == 7)
                {
                    const size_t count =
                        kOocByteBase * kOocPakBlob[pc + 1] + kOocPakBlob[pc + 2] + 1u;
                    literal(count, pc + 3);
                    return pc + 3 + count;
                }
                if (op == 6)
                {
                    backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
                                kOocWordBase * kOocPakBlob[pc + 1] + 1u,
                            kOocPakBlob[pc + 4] + 1u);
                    return pc + 5;
                }
                if (op == 4)
                {
                    backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
                                kOocWordBase * kOocPakBlob[pc + 1] + 1u,
                            (kOocPakBlob[pc + 4] << 8) + kOocPakBlob[pc + 5] + 1u);
                    return pc + 6;
                }
                return pc;  // no-progress opcode terminates the stream
            }
            if (op < 128)
            {
                if (op >= 64)
                {
                    backref(kOocPakBlob[pc + 1] + kOocByteBase * op - 16383u,
                            kOocPakBlob[pc + 2] + 1u);
                    return pc + 3;
                }
                literal(op - 31, pc + 1);
                return pc + (op - 30);
            }
            backref(kOocPakBlob[pc + 1] + 1u, op - 127);
            return pc + 2;
        }
    };

    // The original's Adler-32 variant (a = 1, b = 0, 1872-byte partition, 32-bit
    // wrapping); reproduced because the blob's trailer was built with it.
    uint32_t zpatch_ooc_adler32(std::span<const std::byte> data) noexcept
    {
        uint32_t a = 1u;
        uint32_t b = 0u;
        for (size_t block = 0; block < data.size(); block += kOocAdlerBlock)
        {
            const size_t end = (block + kOocAdlerBlock < data.size())
                                   ? block + kOocAdlerBlock
                                   : data.size();
            for (size_t i = block; i < end; ++i)
            {
                a += static_cast<uint32_t>(static_cast<unsigned char>(data[i]));
                b += a;
            }
            a %= kOocAdlerModulus;
            b %= kOocAdlerModulus;
        }
        return (b << 16) | a;
    }

    bool zpatch_ooc_decode(std::span<std::byte> table) noexcept
    {
        // sub_1004B570: decode until the no-progress opcode, then require the
        // 05 FA terminator, the exact destination size and the Adler trailer.
        const auto table_begin = reinterpret_cast<uintptr_t>(table.data());
        zpatch_ooc_store(&zpatch_ooc_input_bound,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kOocPakBlob) - 16u));
        zpatch_ooc_store(&zpatch_ooc_output_begin, static_cast<uint32_t>(table_begin));
        zpatch_ooc_store(&zpatch_ooc_output_cursor, static_cast<uint32_t>(table_begin));
        zpatch_ooc_store(&zpatch_ooc_output_end,
                         static_cast<uint32_t>(table_begin + kOocTableSize));
        OocStream stream{0, reinterpret_cast<unsigned char*>(table.data())};
        const auto publish_cursor = [&]() {
            zpatch_ooc_store(&zpatch_ooc_output_cursor,
                             static_cast<uint32_t>(table_begin + stream.cursor));
        };
        size_t pc = 0;
        bool terminated = false;
        while (stream.cursor <= kOocTableSize)
        {
            const size_t previous = pc;
            const size_t prior_cursor = stream.cursor;
            pc = stream.step(pc);
            if (stream.cursor != prior_cursor)
                publish_cursor();
            if (pc == previous)
            {
                terminated = true;
                break;
            }
        }
        if (!terminated || stream.cursor != kOocTableSize)
            return false;
        // The pak trailer is the 5 (end-of-stream opcode) and 0xFA (pack marker)
        // byte pair, followed by the big-endian Adler-32 the fixture stores.
        if (kOocPakBlob[pc] != 5u || kOocPakBlob[pc + 1] != 0xFAu)
            return false;
        const uint32_t trailer = (static_cast<uint32_t>(kOocPakBlob[pc + 2]) << 24) |
                                 (static_cast<uint32_t>(kOocPakBlob[pc + 3]) << 16) |
                                 (static_cast<uint32_t>(kOocPakBlob[pc + 4]) << 8) |
                                 static_cast<uint32_t>(kOocPakBlob[pc + 5]);
        return zpatch_ooc_adler32(table) == trailer;
    }

    // sub_1004ACE0 for the single registered entry ("finale2.sco", table, 179536).
    // 20-byte header, one 16-byte record, the name, then the payload at offset 2048
    // followed by the zero padding to the next 2048 boundary. The table is freed
    // like the original; `final_offset` is the original's dword_10159C78.
    std::unique_ptr<unsigned char, opp::FreeDeleter> zpatch_ooc_build_archive(
        std::span<const std::byte> table, uint32_t& final_offset) noexcept
    {
        const uint32_t size = static_cast<uint32_t>(table.size());
        const uint32_t remainder = size & (kOocPageSize - 1u);
        const uint32_t pages = size / kOocPageSize + (remainder != 0u ? 1u : 0u);
        const uint32_t total = pages * kOocPageSize + kOocPageSize;
        // The original does not check this allocation and faults while filling it;
        // this tree returns a null archive so the caller skips the mount (the
        // default-on failsafe policy; the unchecked fill stays 1:1 otherwise).
        auto* archive = static_cast<unsigned char*>(std::malloc(total));
        if (archive == nullptr)
            return {};
        std::memset(archive, 0, total);
        uint32_t offset = 0;
        const auto append = [&](const void* source, uint32_t bytes) noexcept {
            std::memcpy(archive + offset, source, bytes);
            offset += bytes;
            zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
        };
        zpatch_ooc_store(&zpatch_ooc_archive_offset, 0);
        const uint32_t name_bytes = static_cast<uint32_t>(kOocEntryName.size() + 1u);
        struct OocHeader final
        {
            uint32_t fields[4];
            uint16_t words[2];
        };
        static_assert(sizeof(OocHeader) == 20);
        // The builder's header: pak magic, version, one entry, header size 0x10 and
        // the 0xE9 flag word (the format's fixed layout).
        const OocHeader header{{kOocPak.magic, kOocPak.version, kOocPak.entries,
                                name_bytes + 16u},
                               {kOocPak.header_bytes, kOocPak.flags}};
        append(&header, sizeof(header));
        unsigned char record[16] = {};
        std::memcpy(record, &size, 4);  // physical size (entry[0x20])
        const uint32_t index = 1u;
        std::memcpy(record + 8, &index, 4);
        const uint16_t record_pages = static_cast<uint16_t>(pages);
        std::memcpy(record + 12, &record_pages, 2);  // sub_1004ABB0
        if (remainder != 0u)
        {
            const uint16_t padding = static_cast<uint16_t>(kOocPageSize - remainder);
            // Bits 0..10 hold the padding; bits 11..15 are zero in this format
            // (the 0xF800 mask documents the field width the original packs).
            const uint16_t packed =
                static_cast<uint16_t>((static_cast<uint16_t>(0u) & 63488u) | padding);
            std::memcpy(record + 14, &packed, 2);  // sub_1004AB90
        }
        append(record, 16);
        append(kOocEntryName.data(), name_bytes);
        offset = kOocPageSize;  // the original resets dword_10159C78
        zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
        append(table.data(), size);
        // The caller owns the table (the original frees it inside this loop).
        if (remainder != 0u)
            offset += kOocPageSize - remainder;  // zero padding (already memset)
        if (remainder != 0u)
            zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
        final_offset = offset;
        return std::unique_ptr<unsigned char, opp::FreeDeleter>(archive);
    }

    // sub_1004B750: the one-shot init the wrapper runs before mounting. Its guard
    // dword_10159C00 has no writer in the original, so it re-runs (and leaks the
    // archive) on every call - kept 1:1. Both mallocs are unchecked like the
    // original; this rebuild decodes once and reuses the mounted archive for the
    // process lifetime (same resulting state, one allocation instead of one per
    // call).
    uint32_t zpatch_ooc_lookup() noexcept
    {
        static uint32_t cached_archive = 0;
        static uint32_t cached_state = 0;
        if (cached_archive != 0u)
        {
            zpatch_ooc_store(&zpatch_ooc_state, cached_state);
            return cached_archive;
        }

        const std::unique_ptr<unsigned char, opp::FreeDeleter> table(
            static_cast<unsigned char*>(std::malloc(kOocTableSize)));
        if (!table)
            return 0;
        const std::span<std::byte> table_bytes(reinterpret_cast<std::byte*>(table.get()),
                                               kOocTableSize);
        if (!zpatch_ooc_decode(table_bytes))
            return 0;
        zpatch_ooc_store(&zpatch_ooc_table_size, static_cast<uint32_t>(kOocTableSize));
        zpatch_ooc_register_entry(table_bytes);
        uint32_t final_offset = 0;
        std::unique_ptr<unsigned char, opp::FreeDeleter> archive =
            zpatch_ooc_build_archive(table_bytes, final_offset);
        if (!archive)
            return 0;
        zpatch_ooc_store(&zpatch_ooc_vector_end, zpatch_ooc_vector_begin);
        zpatch_ooc_store(&zpatch_ooc_state, final_offset);
        cached_archive =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive.release()));
        cached_state = final_offset;
        return cached_archive;
    }

    uint32_t zpatch_ooc_fnv1a(std::span<const std::byte> data) noexcept
    {
        uint32_t hash = 2166136261u;
        for (const std::byte byte : data)
        {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(byte));
            hash *= 16777619u;
        }
        return hash;
    }
}  // namespace

extern "C"
{
// Offline self-test for OutOfCommissionFix (native tests only): the decode must
// reproduce the blob's own Adler trailer and the archive must match the golden
// layout/hash.
bool zpatch_ooc_selftest() noexcept
{
    // The fixture's expected values, produced by this builder: Adler-32
    // 0x41E794AB, FNV-1a 0xB5AF7B10, 182272 bytes (89 pages of 2048).

    std::vector<std::byte> table(kOocTableSize);
    const bool decoded = zpatch_ooc_decode(table);
    const uint32_t table_adler = zpatch_ooc_adler32(table);
    uint32_t final_offset = 0;
    const std::unique_ptr<unsigned char, opp::FreeDeleter> archive =
        zpatch_ooc_build_archive(table, final_offset);
    if (!archive)
        return false;
    const unsigned char* const bytes = archive.get();
    const auto archive_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes), final_offset);
    const uint32_t header[4] = {kOocPak.magic, kOocPak.version, kOocPak.entries, 28u};
    bool header_ok = true;
    for (int i = 0; i < 4; ++i)
        header_ok = header_ok && std::memcmp(bytes + 4 * i, &header[i], 4) == 0;
    // Header size 0x10, then the 0xE9 flag word.
    header_ok = header_ok && bytes[16] == kOocPak.header_bytes && bytes[17] == 0u &&
                bytes[18] == kOocPak.flags && bytes[19] == 0u;
    const uint32_t record_size = static_cast<uint32_t>(kOocTableSize);
    const uint32_t record_index = 1u;
    bool record_ok = std::memcmp(bytes + 20, &record_size, 4) == 0;
    const uint32_t zero = 0u;
    record_ok = record_ok && std::memcmp(bytes + 24, &zero, 4) == 0 &&
                std::memcmp(bytes + 28, &record_index, 4) == 0;
    // The fixture's packed page count (88 = 0x58) and the 0x02B0 tail word.
    record_ok = record_ok && bytes[32] == 88u && bytes[33] == 0u && bytes[34] == 0xB0u &&
                bytes[35] == 0x02u;
    const bool name_ok =
        std::memcmp(bytes + 36, kOocEntryName.data(), kOocEntryName.size() + 1u) == 0;
    return decoded && final_offset == 182272u && table_adler == 1105695915u &&
           header_ok && record_ok && name_ok &&
           zpatch_ooc_fnv1a(archive_bytes) == 3048176400u;
}

// OutOfCommissionFix wrapper (original 0x1004C350): register the fake pak, log the
// mount path through the published logger, then call the game function the E8
// replaced with the original arguments. Its register use is cdecl-clean.
uint32_t abi_ooc_wrap(uint32_t arg0, uint32_t arg1) noexcept
{
    const uint32_t archive = zpatch_ooc_lookup();
    if (archive != 0u)
    {
        // The original formats into a 0x44-byte stack buffer with a 0x40 size
        // argument; the characters are the "memory:$<ptr>,<size>,0:" literals its
        // recovered format string consumes.
        char buffer[68];
        zpatch_spawns_sprintf(buffer, 64, zpatch_ooc_fmt_b, zpatch_ooc_fmt_a, ':', '$',
                              static_cast<int>(archive), static_cast<int>(zpatch_ooc_state), '0',
                              ':', static_cast<const char*>(nullptr));
        const auto logger = reinterpret_cast<void(__cdecl*)(char*, int, int)>(
            static_cast<uintptr_t>(zpatch_ooc_logger));
        // sub_1004C350 calls the logger and the original target unguarded.
        logger(buffer, 1, -1);
    }
    const auto original = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(
        static_cast<uintptr_t>(zpatch_ooc_orig));
    return original(arg0, arg1);
}
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{
    namespace
    {
        // A hook site as an arm describes it: the signature that finds it and the
        // byte offset from the hit to the site.
        struct Site final
        {
            std::string_view signature;
            ptrdiff_t offset;
        };

        // A hook site plus the body installed there.
        struct BodySite final
        {
            std::string_view signature;
            ptrdiff_t offset;
            void* body;
        };

        // Installs one body at its site; false when the signature did not resolve
        // (the original handler's own signature-miss behaviour).
        bool InstallBody(PatchContext& ctx, const BodySite& site)
        {
            auto pattern = Pattern(ctx.image, site.signature);
            if (pattern.size() == 0)
                return false;
            JumpTo(ctx, pattern.get(0).get<uint8_t>(site.offset), site.body);
            return true;
        }

        // Installs one body at a call site (E8) instead of a jump site (E9).
        bool CallBody(PatchContext& ctx, const BodySite& site)
        {
            auto pattern = Pattern(ctx.image, site.signature);
            if (pattern.size() == 0)
                return false;
            CallTo(ctx, pattern.get(0).get<uint8_t>(site.offset), site.body);
            return true;
        }

        // Publishes the resume/slot address one signature resolves.
        bool PublishSlot(PatchContext& ctx, const Site& site, uint32_t& slot)
        {
            auto pattern = Pattern(ctx.image, site.signature);
            if (pattern.size() == 0)
                return false;
            slot = MatchAddress(pattern, site.offset);
            return true;
        }

        // The signatures these arms scan. A signature is one literal here so two
        // arms that hook the same function cannot drift apart.
        constexpr std::string_view kBadPlayerModelSite =
            "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C";
        constexpr std::string_view kBenchmarkSite =
            "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20";
        constexpr std::string_view kBlackscreenSite = "75 54 8B 48 28 8B 40 2C";
        constexpr std::string_view kCollisionWalkSite =
            "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66";
        constexpr std::string_view kCrashMsgSite = "68 6A 10 4E 31";
        constexpr std::string_view kEpisodeLoadSite =
            "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84";
        constexpr std::string_view kEpisodeCompareSite = "8B 4B 10 3B 4B 14 7D 0F";
        constexpr std::string_view kEpisodeTouchSite =
            "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05";
        constexpr std::string_view kGarageSaveSite = "8B 57 20 D9 42 34 D9 5E 04 8B 47 20";
        constexpr std::string_view kGarageRequestSite = "0F B7 0E 50 51 E8";
        constexpr std::string_view kGroupHackSite =
            "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04";
        constexpr std::string_view kModelLookupSite =
            "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E";
        constexpr std::string_view kOocBranchSite = "A9 39 44 24 18";
        constexpr std::string_view kOocWrapperSite =
            "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56";
        constexpr std::string_view kPathNodeSite = "8B 04 B1 0F B7 D0 81 FA FF FF 00 00";
        constexpr std::string_view kPlaneCrashSite =
            "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00";
        constexpr std::string_view kPlayerPedSite =
            "8B 54 24 10 52 0F B6 90 19 02 00 00";
        constexpr std::string_view kSmpa60GateSite =
            "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C";
        constexpr std::string_view kSmpa60ShimSite = "8B 48 70 83 C0 40 50 51 8D 51 10";
        constexpr std::string_view kSpeedupSite = "50 64 89 25 00 00 00 00 83 EC 34";
        constexpr std::string_view kSuperLodSite =
            "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8";
        constexpr std::string_view kWantedSite =
            "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00";
        constexpr ptrdiff_t kBenchmarkResumeOffset = 13;
        constexpr ptrdiff_t kBenchmarkCallOffset = 8;
        constexpr ptrdiff_t kGarageEntryDelta = -0x20;
        constexpr ptrdiff_t kGarageEntryResumeDelta = kGarageEntryDelta + 8;
        constexpr ptrdiff_t kGarageScaleOperandDelta = kGarageEntryResumeDelta - 4;
        static_assert(kGarageEntryResumeDelta == -0x18);
        static_assert(kGarageScaleOperandDelta == -0x1C);

        // The finale archive path exists on 1.0.6.0-1.1.3.0; 1.2.0.43 has no
        // verified payload, so the arm stays gated to these builds.
        bool IsFinaleBuild(GameBuild build) noexcept
        {
            return BuildIs(build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                   GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                   GameBuild::v1120, GameBuild::v1130});
        }
    }  // namespace

    void InstallAimProjectileCrashFix(PatchContext& ctx)
    {
        TraceArm("AimProjectileCrashFix");
        if (!ctx.Option("AimProjectileCrashFix", "1"))
            return;
        InstallBody(ctx, {"8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8", 0,
                          OPP_BODY(abi_aim_projectile_crash_fix)});
    }

    void PublishBadPlayerModelCrashFix(PatchContext& ctx)
    {
        TraceArm("publish BadPlayerModelCrashFix");
        if (ctx.Option("BadPlayerModelCrashFix", "1"))
            PublishSlot(ctx, {kBadPlayerModelSite, 10}, zpatch_bpm_resume);
    }

    void InstallBadPlayerModelCrashFix(PatchContext& ctx)
    {
        TraceArm("BadPlayerModelCrashFix");
        if (!ctx.Option("BadPlayerModelCrashFix", "1"))
            return;
        InstallBody(ctx, {kBadPlayerModelSite, 0, OPP_BODY(abi_bad_player_model)});
    }

    void PublishBenchmarkFix(PatchContext& ctx)
    {
        TraceArm("publish BenchmarkFix");
        if (!ctx.Option("BenchmarkFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kBenchmarkSite);
        if (pattern.size() == 0)
            return;
        zpatch_benchmark_resume_a = MatchAddress(pattern, kBenchmarkResumeOffset);
        zpatch_benchmark_resume_b = MatchAddress(pattern, kBenchmarkResumeOffset);
    }

    void InstallBenchmarkFix(PatchContext& ctx)
    {
        TraceArm("BenchmarkFix");
        if (!ctx.Option("BenchmarkFix", "1"))
            return;
        if (!BuildIs(ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004,
                                 GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
                                 GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                                 GameBuild::v12043}))
            return;
        // The original patches exactly the two benchmark entries (bodies a and b);
        // a different count is a view mismatch, so the arm installs nothing.
        auto pattern = PatternAll(ctx.image, kBenchmarkSite);
        if (pattern.size() == 0 ||
            !ExpectMatchCount(pattern, 2, "BenchmarkFix entries"))
            return;
        JumpTo(ctx, pattern.get(0).get<uint8_t>(kBenchmarkCallOffset), OPP_BODY(abi_benchmark_a));
        JumpTo(ctx, pattern.get(1).get<uint8_t>(kBenchmarkCallOffset), OPP_BODY(abi_benchmark_b));
    }

    void PublishBlackscreenFix(PatchContext& ctx)
    {
        TraceArm("publish BlackscreenFix");
        if (!ctx.Option("BlackscreenFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kBlackscreenSite);
        if (pattern.size() == 0)
            return;
        // The branch at +0x52 jumps through the original target two bytes later;
        // the dword one byte past that is the value pointer the body reads.
        constexpr ptrdiff_t kBranchOperand = 82;
        zpatch_blackscreen_resume = MatchTarget(pattern, kBranchOperand, 86);
        const auto address = static_cast<uintptr_t>(
            MatchTarget(pattern, kBranchOperand, 87));
        if (InImage(ctx.image, address))
            zpatch_blackscreen_value_ptr = *reinterpret_cast<const uint32_t*>(address);
    }

    void InstallBlackscreenFix(PatchContext& ctx)
    {
        TraceArm("BlackscreenFix");
        if (!ctx.Option("BlackscreenFix", "1"))
            return;
        // The site is the `jle` 81 bytes into the match.
        InstallBody(ctx, {kBlackscreenSite, 81, OPP_BODY(abi_blackscreen_shim)});
    }

    void PublishCollisionDeadlockFix(PatchContext& ctx)
    {
        TraceArm("publish CollisionDeadlockFix");
        if (!ctx.Option("CollisionDeadlockFix", "1"))
            return;
        // The `call rel32` predicate site at +91 (its next instruction at +95) is
        // the walk function the shim calls.
        if (auto pattern = Pattern(ctx.image, kCollisionWalkSite); pattern.size() > 0)
            zpatch_collision_deadlock_should_walk = MatchTarget(pattern, 91, 95);
    }

    void InstallCollisionDeadlockFix(PatchContext& ctx)
    {
        TraceArm("CollisionDeadlockFix");
        if (!ctx.Option("CollisionDeadlockFix", "1"))
            return;
        InstallBody(ctx, {kCollisionWalkSite, 0, OPP_BODY(abi_collision_deadlock_walk)});
    }

    void InstallCombinedRadioFreezeFix(PatchContext& ctx)
    {
        TraceArm("CombinedRadioFreezeFix");
        if (!ctx.Option("CombinedRadioFreezeFix", "1"))
            return;
        CallBody(ctx, {"E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0", 0,
                       OPP_BODY(abi_combined_radio_freeze_fix)});
    }

    void PublishCrashMsgFix(PatchContext& ctx)
    {
        TraceArm("publish CrashMsgFix");
        if (!ctx.Option("CrashMsgFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kCrashMsgSite);
        if (pattern.size() == 0)
            return;
        // *(match-4) is the call target the site's E8 replaced; the getter's own
        // first instruction is a branch whose rel32 sits at +1.
        constexpr ptrdiff_t kCallOperand = -4;
        constexpr ptrdiff_t kBranchOperand = 1;
        const auto site = static_cast<uintptr_t>(*pattern.get(0).get<uint32_t>(kCallOperand));
        if (InImage(ctx.image, site))
            zpatch_crash_msg_getter =
                BranchDestination(reinterpret_cast<const uint8_t*>(site), kBranchOperand);
    }

    void InstallCrashMsgFix(PatchContext& ctx)
    {
        TraceArm("CrashMsgFix");
        if (!ctx.Option("CrashMsgFix", "1"))
            return;
        // The E8 the body replaces sits four bytes before the match, and its rel32
        // operand is the call target the body's first instruction loads.
        constexpr ptrdiff_t kCallOperand = -4;
        auto pattern = Pattern(ctx.image, kCrashMsgSite);
        if (pattern.size() == 0)
            return;
        CallTo(ctx, MatchPointer(pattern, kCallOperand), OPP_BODY(abi_crash_msg_shim));
    }

    void PublishEpisodeSwitchFreezeFix(PatchContext& ctx)
    {
        TraceArm("publish EpisodeSwitchFreezeFix");
        if (!ctx.Option("EpisodeSwitchFreezeFix", "1"))
            return;
        PublishSlot(ctx, {kEpisodeLoadSite, 5}, zpatch_episode_switch_resume_a);
        PublishSlot(ctx, {kEpisodeCompareSite, 6}, zpatch_episode_switch_resume_b);
        PublishSlot(ctx, {kEpisodeTouchSite, 7}, zpatch_episode_switch_resume_c);
    }

    void InstallEpisodeSwitchFreezeFix(PatchContext& ctx)
    {
        TraceArm("EpisodeSwitchFreezeFix");
        if (!ctx.Option("EpisodeSwitchFreezeFix", "1"))
            return;
        // The five replacement functions, installed in this order.
        const BodySite sites[] = {
            {kEpisodeTouchSite, 0, OPP_BODY(abi_episode_switch_shim_c)},
            {kEpisodeLoadSite, 0, OPP_BODY(abi_episode_switch_store_shim)},
            {"8B 41 10 03 41 0C C3", 0, OPP_BODY(abi_episode_switch_sum)},
            {kEpisodeCompareSite, 0, OPP_BODY(abi_episode_switch_shim_b)},
            {"8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3", 0,
             OPP_BODY(abi_episode_switch_vcall)},
        };
        for (const BodySite& site : sites)
            InstallBody(ctx, site);
    }

    void PublishGarageModelFix(PatchContext& ctx)
    {
        TraceArm("publish GarageModelFix");
        if (!ctx.Option("GarageModelFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kGarageSaveSite);
        if (pattern.size() == 0)
            return;
        zpatch_garage_resume_a = MatchAddress(pattern, 6);
        // The hook goes to the function prologue 0x20 bytes before the anchor; its
        // resume is just past the 8-byte `movss xmm0, ds:X` it skips, and the float
        // to reload is that instruction's operand.
        zpatch_garage_entry_resume_a = MatchAddress(pattern, kGarageEntryResumeDelta);
        zpatch_garage_scale = *reinterpret_cast<const float*>(static_cast<uintptr_t>(
            Field(pattern.get(0).get<uint8_t>(), kGarageScaleOperandDelta)));
        PublishSlot(ctx, {kGarageRequestSite, 5}, zpatch_garage_resume_b);
    }

    void InstallGarageModelFix(PatchContext& ctx)
    {
        TraceArm("GarageModelFix");
        if (!ctx.Option("GarageModelFix", "1"))
            return;
        // Site A is hooked at the prologue, 0x20 bytes before the anchor; the body
        // reloads the xmm0 the skipped instruction sets.
        InstallBody(ctx, {kGarageSaveSite, kGarageEntryDelta, OPP_BODY(abi_garage_entry_a)});
        InstallBody(ctx, {kGarageRequestSite, 0, OPP_BODY(abi_garage_body_b)});
    }

    void PublishGroupHackFix(PatchContext& ctx)
    {
        TraceArm("publish GroupHackFix");
        if (ctx.Option("GroupHackFix", "1"))
            PublishSlot(ctx, {kGroupHackSite, 11}, zpatch_group_hack_resume);
    }

    void InstallGroupHackFix(PatchContext& ctx)
    {
        TraceArm("GroupHackFix");
        if (!ctx.Option("GroupHackFix", "1"))
            return;
        InstallBody(ctx, {kGroupHackSite, 0, OPP_BODY(abi_group_hack_shim)});
    }

    void InstallMPNikoCrashFix(PatchContext& ctx)
    {
        TraceArm("MPNikoCrashFix");
        if (!ctx.Option("MPNikoCrashFix", "1"))
            return;
        // The crash is the conditional jump itself: turning it into an
        // unconditional short jump skips the freed-model dereference.
        auto pattern = Pattern(ctx.image, "74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00");
        if (pattern.size() == 0)
            return;
        WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(), opp::ops::kJmpRel8);
    }

    void PublishOutOfCommissionFix(PatchContext& ctx)
    {
        TraceArm("publish OutOfCommissionFix");
        if (!ctx.Option("OutOfCommissionFix", "1") || !IsFinaleBuild(ctx.build))
            return;
        // The branch body resumes at match+0x11.
        PublishSlot(ctx, {kOocBranchSite, 17}, zpatch_ooc_resume);
        auto pattern = Pattern(ctx.image, kOocWrapperSite);
        if (pattern.size() == 0)
            return;
        // The match is the first E8; the logger is the call whose rel32 operand
        // sits 50 bytes before it (next at -46), and the original call target is
        // the match's own operand (at +1, next at +5).
        zpatch_ooc_logger = MatchTarget(pattern, -50, -46);
        zpatch_ooc_orig = MatchTarget(pattern, 1, 5);
    }

    void InstallOutOfCommissionFix(PatchContext& ctx)
    {
        TraceArm("OutOfCommissionFix");
        if (!ctx.Option("OutOfCommissionFix", "1") || !IsFinaleBuild(ctx.build))
            return;
        InstallBody(ctx, {kOocBranchSite, 7, OPP_BODY(abi_ooc_branch)});
        CallBody(ctx, {kOocWrapperSite, 0, OPP_BODY(abi_ooc_wrap)});
    }

    void PublishPathNodeCrashFix(PatchContext& ctx)
    {
        TraceArm("publish PathNodeCrashFix");
        if (!ctx.Option("PathNodeCrashFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kPathNodeSite);
        if (pattern.size() == 0)
            return;
        zpatch_path_node_resume_ok = MatchAddress(pattern);
        zpatch_path_node_resume_fail = MatchAddress(pattern, 59);
    }

    void InstallPathNodeCrashFix(PatchContext& ctx)
    {
        TraceArm("PathNodeCrashFix");
        if (!ctx.Option("PathNodeCrashFix", "1"))
            return;
        // The shim replaces the `mov dl, [eax+0x1F]` 53 bytes into the match.
        InstallBody(ctx, {kPathNodeSite, 53, OPP_BODY(abi_path_node_shim)});
    }

    void PublishPedModelsInCutscenesFix(PatchContext& ctx)
    {
        TraceArm("publish PedModelsInCutscenesFix");
        if (ctx.Option("PedModelsInCutscenesFix", "1"))
            PublishSlot(ctx, {kModelLookupSite, 0}, zpatch_model_lookup);
    }

    void InstallPedModelsInCutscenesFix(PatchContext& ctx)
    {
        TraceArm("PedModelsInCutscenesFix");
        if (!ctx.Option("PedModelsInCutscenesFix", "1"))
            return;
        InstallBody(ctx, {"56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E", 0,
                          OPP_BODY(abi_ped_models_in_cutscenes_check)});
    }

    void PublishPlaneCrashFix(PatchContext& ctx)
    {
        TraceArm("publish PlaneCrashFix");
        if (!ctx.Option("PlaneCrashFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kPlaneCrashSite);
        if (pattern.size() == 0)
            return;
        zpatch_plane_crash_resume_b = MatchAddress(pattern, 14);
        zpatch_plane_crash_resume_a = MatchAddress(pattern, -705);
    }

    void InstallPlaneCrashFix(PatchContext& ctx)
    {
        TraceArm("PlaneCrashFix");
        if (!ctx.Option("PlaneCrashFix", "1"))
            return;
        // The shim replaces the store 3 bytes into the match.
        InstallBody(ctx, {kPlaneCrashSite, 3, OPP_BODY(abi_plane_crash_shim)});
    }

    void PublishPlayerAsPedComponentsFix(PatchContext& ctx)
    {
        TraceArm("publish PlayerAsPedComponentsFix");
        if (!ctx.Option("PlayerAsPedComponentsFix", "1"))
            return;
        PublishSlot(ctx, {kPlayerPedSite, 14}, zpatch_player_ped_resume);
        PublishSlot(ctx, {kModelLookupSite, 0}, zpatch_model_lookup);
    }

    void InstallPlayerAsPedComponentsFix(PatchContext& ctx)
    {
        TraceArm("PlayerAsPedComponentsFix");
        if (!ctx.Option("PlayerAsPedComponentsFix", "1"))
            return;
        InstallBody(ctx, {kPlayerPedSite, 5, OPP_BODY(abi_player_ped_components_shim)});
    }

    void PublishSMPA60Fix(PatchContext& ctx)
    {
        TraceArm("publish SMPA60Fix");
        if (!ctx.Option("SMPA60Fix", "1"))
            return;
        if (Pattern(ctx.image, kSmpa60GateSite).size() == 0)
            return;
        auto pattern = Pattern(ctx.image, kSmpa60ShimSite);
        if (pattern.size() == 0)
            return;
        zpatch_smpa60_resume_a = MatchAddress(pattern, 28);
        zpatch_smpa60_resume_b = MatchAddress(pattern, 14);
        auto gate = Pattern(ctx.image, kSmpa60GateSite);
        if (gate.size() > 0)
            zpatch_smpa60_resume_c = MatchAddress(gate, 136);
    }

    void InstallSMPA60Fix(PatchContext& ctx)
    {
        TraceArm("SMPA60Fix");
        if (!ctx.Option("SMPA60Fix", "1"))
            return;
        if (Pattern(ctx.image, kSmpa60GateSite).size() == 0)
            return;
        auto site = Pattern(ctx.image, kSmpa60GateSite);
        if (site.size() > 0)
        {
            auto* const match = site.get(0).get<uint8_t>();
            // The A60 tail sits 123 bytes after the anchor; the branch the fix
            // widens is 38 bytes before it.
            JumpTo(ctx, site.get(0).get<uint8_t>(123), OPP_BODY(abi_smpa60_tail_shim));
            WriteValue<uint8_t>(ctx, match - 38, opp::ops::kJmpRel8);
        }
        InstallBody(ctx, {kSmpa60ShimSite, 0, OPP_BODY(abi_smpa60_shim)});
    }

    void PublishSpeedupBugFix(PatchContext& ctx)
    {
        TraceArm("publish SpeedupBugFix");
        if (!ctx.Option("SpeedupBugFix", "1"))
            return;
        auto pattern = Pattern(ctx.image, kSpeedupSite);
        if (pattern.size() == 0)
            return;
        struct SpeedupSlot final
        {
            uint32_t* slot;
            ptrdiff_t operand;
        };
        // The eight timing-constant operands inside the function kSpeedupSite
        // matches, one per published pointer, in the recovered instruction order.
        const SpeedupSlot slots[] = {
            {&zpatch_speedup_t50, 26},  {&zpatch_speedup_t64, 165},
            {&zpatch_speedup_t60, 188}, {&zpatch_speedup_t5c, 226},
            {&zpatch_speedup_t68, 234}, {&zpatch_speedup_t4c, 249},
            {&zpatch_speedup_t54, 264}, {&zpatch_speedup_t58, 276},
        };
        static_assert(std::size(slots) == 8);
        for (const SpeedupSlot& slot : slots)
            *slot.slot = *pattern.get(0).get<uint32_t>(slot.operand);
    }

    void InstallSpeedupBugFix(PatchContext& ctx)
    {
        TraceArm("SpeedupBugFix");
        if (!ctx.Option("SpeedupBugFix", "1"))
            return;
        // The body replaces the 21-byte prologue before the match.
        InstallBody(ctx, {kSpeedupSite, -21, OPP_BODY(abi_speedup_body)});
    }

    void PublishSuperLODFix(PatchContext& ctx)
    {
        TraceArm("publish SuperLODFix");
        if (!ctx.Option("SuperLODFix", "1"))
            return;
        // The shim's three targets are derived from the arm's own site scan and the
        // lookup function's entry scan: the fail/ok blocks sit a fixed distance past
        // the site on both gated builds, and the lookup window is unique (proved
        // with tools/opcep_sigcheck.py). The build gate keeps both scans on the
        // build the original patched.
        if (!BuildIs(ctx.build, {GameBuild::v1070, GameBuild::v1080}))
            return;
        if (auto pattern = Pattern(ctx.image, kSuperLodSite); pattern.size() > 0)
        {
            const auto* const site = pattern.get(0).get<uint8_t>();
            // The fail/ok blocks sit 0x4E and 6 bytes past the site.
            zpatch_super_lod_fail = AddressOf(site + 78);
            zpatch_super_lod_ok = AddressOf(site + 6);
        }
        // The same model-lookup entry kModelLookupSite finds, read one byte past
        // its first match (the scan the original SuperLOD handler used).
        if (auto pattern = Pattern(ctx.image,
                                   "8B 44 24 04 89 44 24 04 66 A1 ?? ?? ?? ?? 66 85 C0 76 3E 8B");
            pattern.size() > 0)
            zpatch_super_lod_lookup = AddressOf(pattern.get(0).get<uint8_t>());
    }

    void InstallSuperLODFix(PatchContext& ctx)
    {
        TraceArm("SuperLODFix");
        if (!ctx.Option("SuperLODFix", "1"))
            return;
        // The recovered handler writes the same site on 1.0.7.0 (RVA 0x4A3CE9) and
        // 1.0.8.0 (RVA 0x540E19); the scan is the instruction pair at the site and
        // resolves on both, so the RVA table is gone. The build gate stays: the
        // same bytes exist on other builds and the original only installed it on
        // these two.
        if (!BuildIs(ctx.build, {GameBuild::v1070, GameBuild::v1080}))
            return;
#if !defined(_WIN64)
        // The shim tail-jumps through the three published slots. If any of them
        // missed (a relocated absolute in the lookup window, or a different
        // build), installing the shim would jump through a null slot; skip the arm
        // instead.
        if (zpatch_failsafe_skip("SuperLODFix targets", zpatch_super_lod_lookup == 0u ||
                                                              zpatch_super_lod_fail == 0u ||
                                                              zpatch_super_lod_ok == 0u))
            return;
#endif
        InstallBody(ctx, {kSuperLodSite, 0, OPP_BODY(abi_super_lod_fix_shim)});
    }

    void InstallTryToSkipAllErrors(PatchContext& ctx)
    {
        TraceArm("TryToSkipAllErrors");
        if (!ctx.Option("TryToSkipAllErrors", "1"))
            return;
        // The arm replaces the function's first byte with a `ret`.
        auto pattern = Pattern(ctx.image, "81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00");
        if (pattern.size() == 0)
            return;
        WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(), 195);  // ret
    }

    void PublishWantedLevelCrashFix(PatchContext& ctx)
    {
        TraceArm("publish WantedLevelCrashFix");
        if (ctx.Option("WantedLevelCrashFix", "1"))
            PublishSlot(ctx, {kWantedSite, 12}, zpatch_wanted_level_resume);
    }

    void InstallWantedLevelCrashFix(PatchContext& ctx)
    {
        TraceArm("WantedLevelCrashFix");
        if (!ctx.Option("WantedLevelCrashFix", "1"))
            return;
        InstallBody(ctx, {kWantedSite, 0, OPP_BODY(abi_wanted_level_shim)});
    }

    namespace
    {
        class StabilityFeatures
        {
        public:
            StabilityFeatures()
            {
                PublishEvent().Add(&PublishBadPlayerModelCrashFix, "PublishBadPlayerModelCrashFix");
                PublishEvent().Add(&PublishBlackscreenFix, "PublishBlackscreenFix");
                PublishEvent().Add(&PublishCollisionDeadlockFix, "PublishCollisionDeadlockFix");
                PublishEvent().Add(&PublishEpisodeSwitchFreezeFix,
                                   "PublishEpisodeSwitchFreezeFix");
                PublishEvent().Add(&PublishGroupHackFix, "PublishGroupHackFix");
                PublishEvent().Add(&PublishPathNodeCrashFix, "PublishPathNodeCrashFix");
                PublishEvent().Add(&PublishPedModelsInCutscenesFix,
                                   "PublishPedModelsInCutscenesFix");
                PublishEvent().Add(&PublishPlaneCrashFix, "PublishPlaneCrashFix");
                PublishEvent().Add(&PublishPlayerAsPedComponentsFix,
                                   "PublishPlayerAsPedComponentsFix");
                PublishEvent().Add(&PublishSMPA60Fix, "PublishSMPA60Fix");
                PublishEvent().Add(&PublishSpeedupBugFix, "PublishSpeedupBugFix");
                PublishEvent().Add(&PublishWantedLevelCrashFix, "PublishWantedLevelCrashFix");
                PublishEvent().Add(&PublishSuperLODFix, "PublishSuperLODFix");
                PublishEvent().Add(&PublishCrashMsgFix, "PublishCrashMsgFix");
                PublishEvent().Add(&PublishBenchmarkFix, "PublishBenchmarkFix");
                PublishEvent().Add(&PublishGarageModelFix, "PublishGarageModelFix");
                PublishEvent().Add(&PublishOutOfCommissionFix, "PublishOutOfCommissionFix");
                InstallEvent().Add(&InstallAimProjectileCrashFix, "InstallAimProjectileCrashFix");
                InstallEvent().Add(&InstallBadPlayerModelCrashFix, "InstallBadPlayerModelCrashFix");
                InstallEvent().Add(&InstallBlackscreenFix, "InstallBlackscreenFix");
                InstallEvent().Add(&InstallCollisionDeadlockFix, "InstallCollisionDeadlockFix");
                InstallEvent().Add(&InstallCombinedRadioFreezeFix, "InstallCombinedRadioFreezeFix");
                InstallEvent().Add(&InstallEpisodeSwitchFreezeFix,
                                   "InstallEpisodeSwitchFreezeFix");
                InstallEvent().Add(&InstallGroupHackFix, "InstallGroupHackFix");
                InstallEvent().Add(&InstallMPNikoCrashFix, "InstallMPNikoCrashFix");
                InstallEvent().Add(&InstallPathNodeCrashFix, "InstallPathNodeCrashFix");
                InstallEvent().Add(&InstallPedModelsInCutscenesFix,
                                   "InstallPedModelsInCutscenesFix");
                InstallEvent().Add(&InstallPlaneCrashFix, "InstallPlaneCrashFix");
                InstallEvent().Add(&InstallPlayerAsPedComponentsFix,
                                   "InstallPlayerAsPedComponentsFix");
                InstallEvent().Add(&InstallSMPA60Fix, "InstallSMPA60Fix");
                InstallEvent().Add(&InstallSpeedupBugFix, "InstallSpeedupBugFix");
                InstallEvent().Add(&InstallTryToSkipAllErrors, "InstallTryToSkipAllErrors");
                InstallEvent().Add(&InstallWantedLevelCrashFix, "InstallWantedLevelCrashFix");
                InstallEvent().Add(&InstallSuperLODFix, "InstallSuperLODFix");
                InstallEvent().Add(&InstallCrashMsgFix, "InstallCrashMsgFix");
                InstallEvent().Add(&InstallBenchmarkFix, "InstallBenchmarkFix");
                InstallEvent().Add(&InstallGarageModelFix, "InstallGarageModelFix");
                InstallEvent().Add(&InstallOutOfCommissionFix, "InstallOutOfCommissionFix");
            }
        } stabilityFeatures;
    }  // namespace
}  // namespace opp::features

// ---- hook bodies ----
//
// The machine-level entry points stability installs: every __asm/__declspec(naked)
// definition, in one block after the installers. Each keeps the original site's calling
// convention and register contract; the C++ helpers they call stay above, in the readable
// part. 32-bit only - the x64 parity dump resolves names to the original VAs.

#if !defined(_WIN64)

// The compiled helpers defined above, called here by name from the shims.
extern "C" bool abi_super_lod_helper(const void* object) noexcept;
extern "C" void abi_bpm_check() noexcept;
extern "C" int32_t zpatch_garage_lookup_c(uint8_t* object) noexcept;

extern "C"
{

// PlayerAsPedComponentsFix helper (original sub_1003D930): compare the object's
// model (+0x2E) against the "player" model index. The shim stores the object
// (EAX) first; C linkage so the shim's asm can call it by name.

int32_t abi_player_ped_helper() noexcept
{
    int32_t index = -1;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    // 0x1003D960 `call dword_10159C0C` is unguarded, so a null lookup faults.
    zpatch_player_ped_computed = 0;  // original writes 0 before the lookup
    if (!zpatch_failsafe_skip("PlayerAsPedComponentsFix model lookup", lookup == nullptr))
        lookup(GameHash("player"), &index);
    const auto* object = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(zpatch_player_ped_object));
    // The object is the argument the shim stashed; a clean skip keeps the
    // original's "no match" answer (model -1) instead of dereferencing null.
    const int16_t model =
        zpatch_failsafe_skip("PlayerAsPedComponentsFix object", object == nullptr)
            ? static_cast<int16_t>(-1)
            : *reinterpret_cast<const int16_t*>(object + kModelIndexOffset);
    zpatch_player_ped_computed = (index == model) ? 1u : 0u;
    // Tail contract: EDX=1 (the cmove source), ECX=cookie via the /GS check,
    // EAX=the object's model (the movsx eax,[eax+2Eh] the resume sees).
    __asm { mov edx, 1 }
    zpatch_cookie_tail();
    return model;
}

// GarageModelFix store (original sub_10046C80): ECX = the garage slot, EDX = the
// vehicle; the vehicle's model index and its model-info name hash are copied into
// the slot (GarageSlotView in the interface unit: modelIndex at +0x10, the four
// hash bytes at +0x13/+0x40/+0x41/+0x46). A missing model table means nothing is
// stashed, so the lookup side sees hash 0 and leaves the model alone.
static __declspec(naked) void zpatch_garage_store_model()
{
    __asm {
        cmp  BYTE PTR [zpatch_citizen_present], 0
        push esi
        mov  esi, ecx
        jne  garage_done
        cmp  DWORD PTR [zpatch_pre_ped_array], 0
        je   garage_done
        movsx edx, WORD PTR [edx + 2Eh]
        mov  eax, DWORD PTR [zpatch_pre_ped_array]
        mov  eax, DWORD PTR [eax + edx * 4]
        mov  eax, DWORD PTR [eax + 3Ch]
        mov  ecx, eax
        shr  ecx, 8
        mov  BYTE PTR [esi + 40h], cl
        mov  ecx, eax
        shr  ecx, 10h
        mov  BYTE PTR [esi + 13h], al
        shr  eax, 18h
        mov  BYTE PTR [esi + 41h], cl
        mov  BYTE PTR [esi + 46h], al
    garage_done:
        pop  esi
        ret
    }
}
// GarageModelFix lookup: the compiled helper (interface unit) rebuilds the hash
// and returns the replacement model index; the tail keeps the /GS check the
// original body performs before it returns.
static __declspec(naked) int32_t zpatch_garage_lookup_model()
{
    __asm {
        push ecx
        call zpatch_garage_lookup_c
        add  esp, 4
        push eax
        mov ecx, dword ptr [__security_cookie]
        call zpatch_spawns_cookie_check_ecx
        pop eax
        ret
    }
}

// @exact 0x10005590 AimProjectileCrashFix: __thiscall (this in ECX), three
// stack arguments, callee pops 12 bytes; MSVC rejects __thiscall on free
// functions, so the original instruction sequence is reproduced directly.
// Field map: [ecx+0x20] = arg2, [ecx+0x24] = arg3, [ecx+0x1c] = arg1 masked by
// the sign of arg3 (the original's `neg/sbb/and`); [esp+8]/[esp+0xC]/[esp+4]
// are those three stack arguments.
__declspec(naked) void abi_aim_projectile_crash_fix()
{
    __asm {
        cmp ecx, 0
        je skip
        mov eax, [esp + 8]
        mov [ecx + 20h], eax
        mov eax, [esp + 0Ch]
        mov [ecx + 24h], eax
        neg eax
        sbb eax, eax
        and eax, [esp + 4]
        mov [ecx + 1Ch], eax
    skip:
        ret 12
    }
}

// Crash-fix shims, transcribed from the original bodies' machine code: the C
// translation hid argument pushes, register setup and flags the resumed code
// consumes. Registers the resumed code depends on (AL, DL, flags) must survive
// intact; each shim ends in a tail jump to the published resume.

// @exact 0x1003d7b0 PathNodeCrashFix: eax = node index; the sentinel is
// 0x1FFFDF. Otherwise dl = [eax+0x1F] and its low bit feed the failure resume.
__declspec(naked) void abi_path_node_shim()
{
    __asm {
        cmp eax, 1FFFDFh
        je ok
        mov dl, BYTE PTR [eax + 1Fh]
        test dl, 1
        jmp DWORD PTR [zpatch_path_node_resume_fail]
    ok:
        jmp DWORD PTR [zpatch_path_node_resume_ok]
    }
}

// @exact 0x1003d870 PlaneCrashFix: eax = object; null takes the null resume.
// The normal path stores bl at +0x26 and loads eax from [[esi]+0xF4].
__declspec(naked) void abi_plane_crash_shim()
{
    __asm {
        cmp eax, 0
        je null_path
        mov BYTE PTR [eax + 26h], bl
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 0F4h]
        jmp DWORD PTR [zpatch_plane_crash_resume_b]
    null_path:
        jmp DWORD PTR [zpatch_plane_crash_resume_a]
    }
}

// @exact 0x10050490 WantedLevelCrashFix: ecx = object; null returns 0. The
// normal path calls vtable+0x128; the resume consumes al and the following
// test's flags.
__declspec(naked) void abi_wanted_level_shim()
{
    __asm {
        cmp ecx, 0
        je null_path
        mov eax, DWORD PTR [ecx]
        mov edx, DWORD PTR [eax + 128h]
        call edx
        test al, al
        jmp DWORD PTR [zpatch_wanted_level_resume]
    null_path:
        pop ebx
        ret
    }
}

// @exact 0x10036e90 GroupHackFix: eax = 0, ecx += 4, edx = 7, then the resume
// tests the flags of [ecx] against 0. The 0xC bound is the group table's
// twelve-byte stride check the original performs before that test.
__declspec(naked) void abi_group_hack_shim()
{
    __asm {
        xor eax, eax
        add ecx, 4
        lea edx, [eax + 7]
        cmp ecx, 0Ch
        je null_path
        cmp DWORD PTR [ecx], 0
        jmp DWORD PTR [zpatch_group_hack_resume]
    null_path:
        ret
    }
}

// @exact 0x10046750 SMPA60Fix: ecx = this; the sentinel 0xDDDDDDDD (the MSVC
// debug-CRT poison value the original compares against) tails into the match+28
// resume. The normal path pushes five arguments for the match+14 resume:
// [eax+0x70] (a pointer), eax+0x40 (the sub-object), [ecx+0x10], ecx, ecx.
__declspec(naked) void abi_smpa60_shim()
{
    __asm {
        cmp ecx, 0DDDDDDDDh
        je sentinel
        mov ecx, DWORD PTR [eax + 70h]
        add eax, 40h
        push eax
        push ecx
        lea edx, [ecx + 10h]
        push edx
        push ecx
        push ecx
        jmp DWORD PTR [zpatch_smpa60_resume_b]
    sentinel:
        jmp DWORD PTR [zpatch_smpa60_resume_a]
    }
}

// @exact 0x10046780 SMPA60Fix tail shim: copies [eax+0x10] into [[eax+0x14]+0x10]
// unless that pointer is null or the 0xCDCDCDCD debug-heap poison value, then
// tail-jumps to the published resume (match+136).
__declspec(naked) void abi_smpa60_tail_shim()
{
    __asm {
        mov ecx, DWORD PTR [eax + 14h]
        test ecx, ecx
        je skip
        cmp ecx, 0CDCDCDCDh
        je skip
        mov edx, DWORD PTR [eax + 10h]
        mov DWORD PTR [ecx + 10h], edx
    skip:
        jmp DWORD PTR [zpatch_smpa60_resume_c]
    }
}

// @exact 0x1003d9a0 PlayerAsPedComponentsFix: save eax, run the helper (stores
// 1/0 in zpatch_player_ped_computed), restore, then push the result and ecx and
// tail-jump to the resume (match+14).
__declspec(naked) void abi_player_ped_components_shim()
{
    __asm {
        pushad
        mov DWORD PTR [zpatch_player_ped_object], eax
        call abi_player_ped_helper
        popad
        push DWORD PTR [zpatch_player_ped_computed]
        push ecx
        jmp DWORD PTR [zpatch_player_ped_resume]
    }
}

// @exact 0x10046C80 GarageModelFix body A (installed at game 0xC68BB0): pushad;
// ecx=object(esi), edx=vehicle(edi); call the store helper; popad; replay
// mov edx,[edi+0x20]; fld [edx+0x34]; tail-jump to the resume (match+6).
[[maybe_unused]] __declspec(naked) void abi_garage_body_a()
{
    __asm {
        pushad
        mov  ecx, esi
        mov  edx, edi
        call zpatch_garage_store_model
        popad
        mov  edx, dword ptr [edi + 20h]
        fld  dword ptr [edx + 34h]
        jmp  dword ptr [zpatch_garage_resume_a]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
    }
}
// Opt-in prologue form of body A (garage_entry, installed at game 0xC68B90):
// the function entry is the mid-body site minus 0x20 and starts with an 8-byte
// `movss xmm0, ds:X`. The hook skips that instruction, runs the same store
// helper with the prologue's own arguments (ECX=this/object, [esp+4]=vehicle),
// restores every GPR, reloads xmm0 and resumes after the instruction (+8). The
// prologue's ECX/arg0 are the object/vehicle the mid-body site reads from
// ESI/EDI, so the store sees the same pair; the store writes object+0x13/+0x40/
// +0x41/+0x46, which the copier that follows never touches.
__declspec(naked) void abi_garage_entry_a()
{
    __asm {
        pushad
        mov  ecx, dword ptr [esp + 18h]   // ECX at entry = this (object)
        mov  edx, dword ptr [esp + 24h]   // [esp+4] at entry = vehicle
        call zpatch_garage_store_model
        popad
        push eax
        mov  eax, OFFSET zpatch_garage_scale
        __emit 0F3h
        __emit 00Fh
        __emit 010h
        __emit 000h                       // movss xmm0, DWORD PTR [eax]
        pop  eax
        jmp  dword ptr [zpatch_garage_entry_resume_a]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable)
    }
}

// @exact 0x10046CA0 GarageModelFix body B (installed at game 0x9659C3): pushad;
// call the lookup helper (object in ECX); popad; replay
// movzx ecx,[esi]; push eax; push ecx; tail-jump to the resume (match+5).
__declspec(naked) void abi_garage_body_b()
{
    __asm {
        pushad
        call zpatch_garage_lookup_model
        popad
        movzx ecx, word ptr [esi]
        push eax
        push ecx
        jmp  dword ptr [zpatch_garage_resume_b]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
    }
}

// @exact 0x10037BA0 BadPlayerModelCrashFix trampoline: the handler E9s the game
// function at pattern "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C" (game 0x7CF803,
// 3 bytes in) and publishes match+0xA. The body captures [ebp+0xC] (entity) and
// [ebp+0x10] (model index), runs the check between pushad/popad, writes the
// corrected model index back to [ebp+0x10], then replays the overwritten
// prologue (and esp,0xF0; sub esp,0x54; xorps xmm0,xmm0; push ebx) and tails.
// EBP is the game function's live frame.
__declspec(naked) void abi_bad_player_model()
{
    __asm {
        push edi
        mov  edi, dword ptr [ebp + 0Ch]
        mov  dword ptr [zpatch_bpm_obj], edi
        mov  edi, dword ptr [ebp + 10h]
        mov  dword ptr [zpatch_bpm_model_index], edi
        pushad
        call abi_bpm_check
        popad
        mov  edi, dword ptr [zpatch_bpm_model_index]
        mov  dword ptr [ebp + 10h], edi
        pop  edi
        and  esp, 0FFFFFFF0h
        sub  esp, 54h
        xorps xmm0, xmm0
        push ebx
        jmp  dword ptr [zpatch_bpm_resume]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
    }
}

// @exact 0x100469E0 SuperLODFix: calls a compiled helper, so this is the shim
// contract rather than a C-level transcription. The fail path resumes via
// dword_101580AC, the ok path via dword_101580B0; both are handler-published
// base+rva values, build-gated.
__declspec(naked) void abi_super_lod_fix_shim()
{
    __asm {
        jmp slod_main
    slod_fail:
        jmp DWORD PTR [zpatch_super_lod_fail]
    slod_main:
        push eax
        push ecx
        push esi
        call abi_super_lod_helper
        add esp, 4
        pop ecx
        test al, al
        pop eax
        jne slod_ok
        test byte ptr [eax + 72h], 10h  ; bit 4 of the byte at +72h = has LOD
        je slod_fail
    slod_ok:
        jmp DWORD PTR [zpatch_super_lod_ok]
    }
}

// @exact 0x10036F00 CrashMsgFix: calls the game's message getter through the
// handler-published pointer (the site's original call target, *(match-4)) and
// replaces every '~' with a space. Entered via the handler's E8, so it returns
// normally. The original's two nops are omitted (not byte-exact).
__declspec(naked) void abi_crash_msg_shim()
{
    __asm {
        call DWORD PTR [zpatch_crash_msg_getter]
        mov dl, BYTE PTR [eax]
        test dl, dl
        je cms_done
        mov ecx, eax
        __emit 066h
        __emit 090h
    cms_loop:
        cmp dl, 7Eh
        jne cms_skip
        mov BYTE PTR [ecx], 20h
    cms_skip:
        mov dl, BYTE PTR [ecx + 1]
        inc ecx
        test dl, dl
        jne cms_loop
    cms_done:
        ret
    }
}

// BenchmarkFix: two match-site shims (E9 at match[0]/[1]+8). Each checks eax,
// runs the interposed callback (esi with edi), then tails to the published
// resume, or unwinds via the enclosing function's tail when eax is null.
// @exact 0x10005650
__declspec(naked) void abi_benchmark_a()
{
    __asm {
        test eax, eax
        je bf_a_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_a]
    bf_a_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}
// @exact 0x10005670
__declspec(naked) void abi_benchmark_b()
{
    __asm {
        test eax, eax
        je bf_b_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_b]
    bf_b_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// @exact 0x1001b040 EpisodeSwitchFreezeFix: [ebx+0x20] is the episode-load
// object slot; stores eax there, null returns 0 (pop edi/esi/ebx, ret 0x10),
// else tails to match+5.
__declspec(naked) void abi_episode_switch_store_shim()
{
    __asm {
        mov DWORD PTR [ebx + 20h], eax
        cmp eax, 0
        je ep_store_null
        mov edx, DWORD PTR [eax]
        jmp DWORD PTR [zpatch_episode_switch_resume_a]
    ep_store_null:
        xor al, al
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 16
    }
}
// @exact 0x1001b060: [ebx+0x10] and [ebx+0x14] are the load object's
// begin/end pointers the original compares.
__declspec(naked) void abi_episode_switch_shim_b()
{
    __asm {
        cmp ebx, 0
        je ep_b_null
        mov ecx, DWORD PTR [ebx + 10h]
        cmp ecx, DWORD PTR [ebx + 14h]
        jmp DWORD PTR [zpatch_episode_switch_resume_b]
    ep_b_null:
        mov BYTE PTR [edi], 0
        pop edi
        pop esi
        mov eax, ebp
        pop ebp
        pop ebx
        pop ecx
        ret
    }
}
// @exact 0x1001b0a0: [eax] is the vtable, [eax+4] the first argument, and
// vtable+0x30 the virtual the original calls.
__declspec(naked) void abi_episode_switch_vcall()
{
    __asm {
        cmp ecx, 0
        je ep_vc_null
        mov eax, ecx
        mov ecx, DWORD PTR [eax]
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [eax + 4]
        mov edx, DWORD PTR [edx + 30h]
        push eax
        call edx
        ret
    ep_vc_null:
        mov eax, 0
        ret
    }
}
// @exact 0x1001b0c0: [esi+0x14] is the pointer the resume tests for null.
__declspec(naked) void abi_episode_switch_shim_c()
{
    __asm {
        cmp ecx, 0
        je ep_c_null
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 14h], 0
        jmp DWORD PTR [zpatch_episode_switch_resume_c]
    ep_c_null:
        mov eax, 0
        ret
    }
}

// @exact 0x10005d80 CollisionDeadlockFix: full replacement of the node-walk
// function the handler matches (prologue 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48
// 66, rewritten with E9). The per-node predicate published in dword_1015BDD4 is
// NOT __stdcall: it takes the aligned entry pointer in EAX, a2 in EDX and the
// shift mask on the stack, and pops its argument (ret 4). The original also
// restores esp from ebp, discarding the predicate's cleanup. The iteration
// counter (dword_1015BDD8, zpatch_collision_deadlock_iterations) bounds the walk
// at 10000 steps.
// Field map: [ebp+0x8] = the graph, [ebp+0xC] = the start entry (its uint16
// index at +0x66), [graph+0x70] = the node table; a node is 8 bytes (uint16
// next at +0, pointer at +4 with its low 4 bits as flags); 0xFFFF is the
// end-of-chain index and 0xFFFFFFF0 the pointer mask. The 0x2710 comparison is
// the recovered 10 000-step iteration bound.
__declspec(naked) void abi_collision_deadlock_walk()
{
    __asm {
        mov DWORD PTR [zpatch_collision_deadlock_iterations], 0
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        push ecx
        mov eax, DWORD PTR [ebp + 0Ch]
        movzx ecx, WORD PTR [eax + 66h]
        push ebx
        mov ebx, DWORD PTR [ebp + 08h]
        mov edx, DWORD PTR [ebx + 70h]
        push esi
        push edi
        movzx edi, WORD PTR [edx + ecx*8]
        cmp di, 0FFFFh
        je coll_return_minus1
    coll_loop:
        mov ecx, DWORD PTR [ebx + 70h]
        movzx eax, di
        movzx edx, WORD PTR [ecx + eax*8]
        lea esi, [ecx + eax*8]
        mov eax, DWORD PTR [esi + 4]
        and eax, 0FFFFFFF0h
        cmp dx, 0FFFFh
        je coll_skip_prefetch
        movzx edx, dx
        mov ecx, DWORD PTR [ecx + edx*8 + 4]
        and ecx, 0FFFFFFF0h
        prefetcht0 BYTE PTR [ecx]
    coll_skip_prefetch:
        mov ecx, DWORD PTR [esi + 4]
        and ecx, 3
        mov edx, 1
        shl edx, cl
        push edx
        mov edx, DWORD PTR [ebp + 0Ch]
        call DWORD PTR [zpatch_collision_deadlock_should_walk]
        test al, al
        jne coll_return_di
        movzx edi, WORD PTR [esi]
        add DWORD PTR [zpatch_collision_deadlock_iterations], 1
        cmp DWORD PTR [zpatch_collision_deadlock_iterations], 10000  ; the recovered 10 000-step bound
        je coll_return_minus1
        cmp di, 0FFFFh
        jne coll_loop
    coll_return_minus1:
        or ax, 0FFFFh
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    coll_return_di:
        mov ax, di
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    }
}

// @exact 0x10036ee0 BlackscreenFix shim: the handler publishes the original call
// target (dword_1015AD58) and a pointer to the value the game reads
// (dword_1015AD5C). Values 8..10 pass through unchanged; others run the original.
__declspec(naked) void abi_blackscreen_shim()
{
    __asm {
        mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
        mov eax, DWORD PTR [eax]
        cmp eax, 7
        jle tail
        cmp eax, 11
        jl keep
    tail:
        jmp DWORD PTR [zpatch_blackscreen_resume]
    keep:
        ret
    }
}

// @exact 0x1001b080 EpisodeSwitchFreezeFix sum: __thiscall, no stack arguments,
// returns [ecx+10h] + [ecx+0Ch] (0 when this is null).
__declspec(naked) void abi_episode_switch_sum()
{
    __asm {
        cmp ecx, 0
        je skip
        mov eax, [ecx + 10h]
        add eax, [ecx + 0Ch]
        ret
    skip:
        mov eax, 0
        ret
    }
}

// @exact 0x1004C3C0 OutOfCommissionFix branch body (E9 target of game
// 0xA22E97): replay `cmp [esp+0x30], eax` + `movq xmm0, [esp+0x30]`; when the
// comparison was unequal, re-compare against the original's 0x86723DFF magic so
// the game's following `jz` also accepts the injected value; resume at match+0x11.
__declspec(naked) void abi_ooc_branch()
{
    __asm {
        cmp  dword ptr [esp + 30h], eax
        movq xmm0, qword ptr [esp + 30h]
        jne  ooc_branch_check
        jmp  dword ptr [zpatch_ooc_resume]
    ooc_branch_check:
        mov  eax, 86723DFFh
        cmp  dword ptr [esp + 30h], eax
        jmp  dword ptr [zpatch_ooc_resume]
        __emit 0CCh
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)
