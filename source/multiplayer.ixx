// SPDX-License-Identifier: GPL-3.0-or-later
//
// multiplayer
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.
// The machine-level bodies the arms install are the body section at the end.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

#include <algorithm>

export module opp.multiplayer;

import opp.patch;
import opp.game;
import opp.common;
import opp.catalogue_data;
import opp.config;
import opp.log;

namespace
{
    // ----- the spawn dispatch -----------------------------------------------
    // The selector a dispatch case stores in zpatch_spawns_kind, named after the
    // arm the probe's compare chain reports. Cases 1 (+0x3D) and 2 (+0x56) both
    // write 3 in the original bytes, so both rows carry SpawnKind::Ped.
    enum class SpawnKind : uint32_t
    {
        Car = 0,
        Helicopter = 1,
        Boat = 2,
        Ped = 3,
        Object = 4,
        GenericPed = 5,
        Bike = 6,
        Train = 7,
        Plane = 8,
    };

    // One row per dispatch case: `dispatch` is the E8 site inside the jump-table
    // match (36 == 0x24, the suffix on the thunk's name), `kind` the selector the
    // thunk stores, `body` the thunk and `resume` the slot the publish arm fills.
    // The publish pair is derived: the rel32 sits at +1 and the resume at +5.
    struct SpawnCase final
    {
        ptrdiff_t dispatch;
        SpawnKind kind;
        void* body;
        uint32_t* resume;
    };

    constexpr size_t kSpawnCaseCount = 10;

    // The spawner sites FreezeCarFix publishes and installs. Each literal is
    // spelled once: the two spawner calls sit in one function and the dispatch
    // table in another.
    constexpr const char* kSpawnProbePattern = "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0";
    constexpr const char* kSpawnGuardPattern =
        "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B";
    constexpr const char* kSpawnDispatchPattern = "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00";
    constexpr const char* kFreezeGunPattern = "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF";

    // The catalogue's miss answer, shared by the lookup and its two wrappers.
    constexpr std::string_view kUnknownModelName = "**UNKNOWN**";
}  // namespace

// Hook bodies: declarations only, so the arms can take their address through
// OPP_BODY; the definitions are the body section at the end of this file. 32-bit
// only - the x64 parity dump resolves the names to the original VAs instead.
#if !defined(_WIN64)

extern "C"
{

void abi_freeze_gun_shim();
void abi_spawns_guard();
// The ten factory-dispatch thunks (P3 sites), 0x10037330..0x1003750F in dispatch
// order; cases 1 (+0x3D) and 2 (+0x56) both set selector 3 in the original bytes,
// kept 1:1.
void abi_spawns_case_0x24();
void abi_spawns_case_0x3D();
void abi_spawns_case_0x56();
void abi_spawns_case_0x6F();
void abi_spawns_case_0x88();
void abi_spawns_case_0x9E();
void abi_spawns_case_0xB4();
void abi_spawns_case_0xCA();
void abi_spawns_case_0xE0();
void abi_spawns_case_0xF6();
void abi_spawns_probe();

}  // extern "C"

#endif  // !defined(_WIN64)

namespace
{
    // The one dispatch table both FreezeCarFix arms read. OPP_BODY is a
    // reinterpret_cast (or, for the parity dump, a runtime resolver), so the
    // table cannot be constexpr and stays a function-local static: no
    // static-init-order dependency.
    const std::array<SpawnCase, kSpawnCaseCount>& SpawnCases() noexcept
    {
        static const std::array<SpawnCase, kSpawnCaseCount> kCases = {{
            {36, SpawnKind::GenericPed, OPP_BODY(abi_spawns_case_0x24),
             &zpatch_spawns_case0_resume},
            {61, SpawnKind::Ped, OPP_BODY(abi_spawns_case_0x3D),
             &zpatch_spawns_case1_resume},
            {86, SpawnKind::Ped, OPP_BODY(abi_spawns_case_0x56),
             &zpatch_spawns_case2_resume},
            {111, SpawnKind::Car, OPP_BODY(abi_spawns_case_0x6F),
             &zpatch_spawns_case3_resume},
            {136, SpawnKind::Object, OPP_BODY(abi_spawns_case_0x88),
             &zpatch_spawns_case4_resume},
            {158, SpawnKind::Bike, OPP_BODY(abi_spawns_case_0x9E),
             &zpatch_spawns_case5_resume},
            {180, SpawnKind::Train, OPP_BODY(abi_spawns_case_0xB4),
             &zpatch_spawns_case6_resume},
            {202, SpawnKind::Helicopter, OPP_BODY(abi_spawns_case_0xCA),
             &zpatch_spawns_case7_resume},
            {224, SpawnKind::Boat, OPP_BODY(abi_spawns_case_0xE0),
             &zpatch_spawns_case8_resume},
            {246, SpawnKind::Plane, OPP_BODY(abi_spawns_case_0xF6),
             &zpatch_spawns_case9_resume},
        }};
        return kCases;
    }
}  // namespace

namespace opp::features
{

    // BetterMPSync

    class BetterMPSync
    {
    public:
        BetterMPSync()
        {
            InstallEvent() += []
            {
                TraceArm("InstallBetterMPSync");

                // BetterMPSync: 1 siginstall
                if (Game::Option("BetterMPSync", "0"))
                {
                    if (auto pattern = find_pattern("8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
                    {
                        // The five bytes at +6 (push ebp; mov ecx,esi; call edx) become
                        // the direct call to the no-op body.
                        injector::MakeCALL(pattern.get_first<uint8_t>(6), OPP_BODY(abi_better_mp_sync), true);
                    }
                }
            };
        }
    } betterMPSync;


    // FreezeCarFix

    class FreezeCarFix
    {
    public:
        FreezeCarFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishFreezeCarFix");

                // FreezeCarFix: 12 slots
                if (!Game::Option("FreezeCarFix", "1"))
                    return;
                // The two spawner calls' rel32 operands (the E8s at +7 and +11 of the
                // probe match). The probe site is also this arm's gate: no probe, no
                // publish, as in the original.
                auto probeSites = find_pattern(kSpawnProbePattern);
                if (probeSites.size() == 0)
                    return;

                zpatch_spawns_orig_a = BranchDestination(probeSites.get_first<uint8_t>(), 8);

                if (auto pattern = find_pattern(kSpawnGuardPattern); pattern.size() > 0)
                {
                    zpatch_spawns_orig_b = BranchDestination(pattern.get_first<uint8_t>(), 12);
                }

                if (auto pattern = find_pattern(kSpawnDispatchPattern); pattern.size() > 0)
                {
                    // The dispatch jump table's rel32 entries, one row per case. The
                    // original writes the ten resume slots in the order (9, 6, 1, 2, 4, 7,
                    // 3, 5, 8, 0), not ascending: that order is part of the transcription.
                    constexpr size_t kPublishOrder[] = {9, 6, 1, 2, 4, 7, 3, 5, 8, 0};
                    for (const size_t index : kPublishOrder)
                    {
                        const SpawnCase& spawn = SpawnCases()[index];
                        *spawn.resume =
                            MatchTarget(pattern, spawn.dispatch + 1, spawn.dispatch + 5);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallFreezeCarFix");

                // FreezeCarFix: 7 siginstalls
                if (!Game::Option("FreezeCarFix", "1"))
                    return;
                // The E8 opcodes the original placed at each spawner call.
                constexpr ptrdiff_t kProbeCallOffset = 7;

                auto probeSites = find_pattern(kSpawnProbePattern);
                if (probeSites.size() == 0)
                    return;

                injector::MakeCALL(probeSites.get_first<uint8_t>() + kProbeCallOffset, OPP_BODY(abi_spawns_probe), true);

                if (auto pattern = find_pattern(kSpawnGuardPattern); pattern.size() > 0)
                {
                    injector::MakeCALL(pattern.get_first<uint8_t>(11), OPP_BODY(abi_spawns_guard), true);
                }

                if (auto pattern = find_pattern(kSpawnDispatchPattern); pattern.size() > 0)
                {
                    auto* match = pattern.get_first<uint8_t>();

                    for (const SpawnCase& spawn : SpawnCases())
                        injector::MakeCALL(match + spawn.dispatch, spawn.body, true);
                }

                if (auto pattern = find_pattern("88 87 40 01 00 00 E8"); pattern.size() > 0)
                {
                    // The call after the six-byte `mov [edi+0x140], al`.
                    injector::MakeCALL(pattern.get_first<uint8_t>(6), OPP_BODY(abi_spawns_probe), true);
                }

                if (auto pattern = find_pattern("88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
                {
                    // The two spawn probes in the function (the first at +9, the
                    // second 245 bytes later).
                    injector::MakeCALL(pattern.get_first<uint8_t>(9), OPP_BODY(abi_spawns_probe), true);
                    injector::MakeCALL(pattern.get_first<uint8_t>(254), OPP_BODY(abi_spawns_probe), true);
                }

                if (auto pattern = find_pattern("38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
                {
                    injector::MakeCALL(pattern.get_first<uint8_t>(9), OPP_BODY(abi_spawns_probe), true);
                }

                // The probe site's prefix, without its `8B F0` tail.
                if (auto pattern = find_pattern("32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get_first<uint8_t>();

                    injector::MakeCALL(match + kProbeCallOffset, OPP_BODY(abi_spawns_probe), true);
                }
            };
        }
    } freezeCarFix;


    // FreezeGunFix

    class FreezeGunFix
    {
    public:
        FreezeGunFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishFreezeGunFix");

                // FreezeGunFix: 1 slot
                if (Game::Option("FreezeGunFix", "1"))
                {
                    if (auto pattern = find_pattern(kFreezeGunPattern); pattern.size() > 0)
                    {
                        // The shim resumes at the instruction 12 bytes before the match
                        // (the body it replaces sits at match-21 and is 9 bytes long).
                        zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallFreezeGunFix");

                // FreezeGunFix: 1 siginstall
                if (Game::Option("FreezeGunFix", "1"))
                {
                    if (auto pattern = find_pattern(kFreezeGunPattern); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // The shim replaces the nine bytes at match-21 that end where
                        // the resume starts.
                        injector::MakeJMP(match - 21, OPP_BODY(abi_freeze_gun_shim), true);
                    }
                }
            };
        }
    } freezeGunFix;


    // RemoveAllWeaponsProtection

    class RemoveAllWeaponsProtection
    {
    public:
        RemoveAllWeaponsProtection()
        {
            InstallEvent() += []
            {
                TraceArm("InstallRemoveAllWeaponsProtection");

                // RemoveAllWeaponsProtection: 1 literal
                if (Game::Option("RemoveAllWeaponsProtection", "1"))
                {
                    if (auto pattern = find_pattern("E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
                    {
                        // The five-byte jump is followed by the twelve-byte call the
                        // original NOPs out.
                        injector::MakeNOP(pattern.get_first<uint8_t>(5), 12, true);
                    }
                }
            };
        }
    } removeAllWeaponsProtection;


    // TeleportProtection

    class TeleportProtection
    {
    public:
        TeleportProtection()
        {
            InstallEvent() += []
            {
                TraceArm("InstallTeleportProtection");

                // TeleportProtection: 1 literal
                if (Game::Option("TeleportProtection", "1"))
                {
                    if (auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // `ret 8`: C2 08 00, through the guarded helper.
                        injector::MakeRET(match, 8, true);
                    }
                }
            };
        }
    } teleportProtection;


} // namespace opp::features

// The multiplayer model-name catalogue (folded in from catalogue.*): the hash
// index over the generated table in opp.catalogue_data. The spawn diagnostics
// print names from here; the index is built once.

export namespace opp
{
    size_t ModelCatalogSize() noexcept;
    std::string_view ModelCatalogName(size_t index) noexcept;

    // 0x10003F00: first match in the original table order; kUnknownModelName on
    // a miss.
    std::string_view ModelNameFromHash(uint32_t hash) noexcept;
}

namespace opp
{
    namespace
    {
        // ModelNameIndex: sorted (hash, first table index) pairs. The original
        // scans all 38,455 names per diagnostic lookup; the table is static, so
        // build the index once. A fixed array keeps this free of CRT/TLS-backed
        // static initialisation, which is unsafe before the ASI's CRT is ready.
        constexpr size_t kModelIndexCapacity = 40000;

        struct ModelHashEntry
        {
            uint32_t hash;
            uint32_t index;
        };

        ModelHashEntry g_modelIndex[kModelIndexCapacity];
        size_t g_modelIndexCount = 0;
        volatile LONG g_modelIndexState = 0;  // 0 uninitialised, 1 building, 2 ready

        bool BuildModelHashIndex() noexcept
        {
            for (;;)
            {
                const LONG observed =
                    InterlockedCompareExchange(&g_modelIndexState, 1, 0);
                if (observed == 2)
                    return g_modelIndexCount != 0;
                if (observed == 1)
                {
                    Sleep(0);
                    continue;
                }
                const size_t count = ModelNameCount();
                if (count > kModelIndexCapacity)
                {
                    InterlockedExchange(&g_modelIndexState, 2);
                    return false;
                }
                for (size_t i = 0; i < count; ++i)
                    g_modelIndex[i] = {GameHash(ModelNameAt(i)),
                                       static_cast<uint32_t>(i)};
                std::sort(g_modelIndex, g_modelIndex + count,
                          [](const ModelHashEntry& left, const ModelHashEntry& right) {
                              if (left.hash != right.hash) return left.hash < right.hash;
                              return left.index < right.index;
                          });
                g_modelIndexCount = count;
                InterlockedExchange(&g_modelIndexState, 2);
                return true;
            }
        }
    } // namespace

    size_t ModelCatalogSize() noexcept
    {
        return ModelNameCount();
    }

    std::string_view ModelCatalogName(size_t index) noexcept
    {
        const char* name = ModelNameAt(index);
        return name != nullptr ? std::string_view(name) : kUnknownModelName;
    }

    std::string_view ModelNameFromHash(uint32_t hash) noexcept
    {
        if (BuildModelHashIndex())
        {
            size_t low = 0;
            size_t high = g_modelIndexCount;
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                if (g_modelIndex[mid].hash < hash)
                    low = mid + 1;
                else
                    high = mid;
            }
            if (low < g_modelIndexCount && g_modelIndex[low].hash == hash)
            {
                const char* name = ModelNameAt(g_modelIndex[low].index);
                return name != nullptr ? std::string_view(name) : kUnknownModelName;
            }
            return kUnknownModelName;
        }

        // Index unavailable (catalogue grew past the fixed capacity): keep the
        // original lookup as the fallback.
        const size_t count = ModelNameCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (GameHash(ModelNameAt(i)) == hash)
                return ModelNameAt(i);
        }
        return kUnknownModelName;
    }
} // namespace opp

// Body section: the machine-level entry points the arms install, after every
// readable installer. Each keeps the original site's calling convention and
// register contract (its comment names the destination VA); its @exact marker
// names the original VA and tools/opcep_bodycheck.py compares the instruction
// stream against it. The bodies whose address an arm takes have external
// linkage; the shims only the asm here calls stay static.
#if !defined(_WIN64)

extern "C"
{

// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770); single-player never reaches
// this path (the caller is gated on GTAC.dll).
const char* zpatch_bpm_model_name(uint32_t hash) noexcept
{
    // Return the first table name whose GameHash matches; "**UNKNOWN**" on a miss.
    return opp::ModelNameFromHash(hash).data();
}

// FreezeCarFix / DebugPrintSpawns helper shims: the original's helpers take
// their single argument in ECX or EDX. The probe body calls these by name.
static const char* zpatch_spawns_name_ecx();  // sub_10003F00 twin, arg in ECX
static __declspec(naked) const char* zpatch_spawns_name_ecx()
{
    __asm {
        push ecx
        call zpatch_bpm_model_name
        add  esp, 4
        ret
    }
}

static const char* zpatch_spawns_who_edx();  // sub_1005B290 twin, arg in EDX
static const char* zpatch_spawns_who_c(uint32_t id) noexcept
{
    // The original reads the native hash from [0x101580C8] (zpatch_salt_c8); the
    // loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
    return opp::Rebased<const char*>(zpatch_script_call(zpatch_salt_c8, 1, &id));
}
static __declspec(naked) const char* zpatch_spawns_who_edx()
{
    __asm {
        push edx
        call zpatch_spawns_who_c
        add  esp, 4
        ret
    }
}

static void zpatch_spawns_print_edx();  // sub_1005B530 twin, arg in EDX
static void zpatch_spawns_print_c(const char* text) noexcept
{
    const uint32_t arg = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(text));
    zpatch_script_call(kPrintScriptHash, 1, &arg);
}
static __declspec(naked) void zpatch_spawns_print_edx()
{
    __asm {
        push edx
        call zpatch_spawns_print_c
        add  esp, 4
        ret
    }
}

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer).
struct SpawnsGetModuleHandleInit final
{
    SpawnsGetModuleHandleInit() noexcept
    {
        zpatch_spawns_getmodulehandlew = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));
    }
};
SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// @exact 0x100057f0 BetterMPSync: xor eax,eax / ret.
int __cdecl abi_better_mp_sync() { return 0; }

// @exact 0x10036eb0 FreezeGunFix: eax = object; [eax+38h] is the ammo/clip
// pointer and [eax+40h] the handle. If the handle is valid (not the debug-heap
// poison 0CDCDCDCDh, not -1, not 0) push edi and tail-jump into the original
// path, else return 0. The null path pops esi and does ret 8 because the site
// sits just after the enclosing function's push esi.
__declspec(naked) void abi_freeze_gun_shim()
{
    __asm {
        mov ecx, DWORD PTR [eax + 38h]
        test ecx, ecx
        mov esi, DWORD PTR [eax + 40h]
        cmp esi, 0CDCDCDCDh
        je freeze_gun_null
        cmp esi, -1
        je freeze_gun_null
        cmp esi, 0
        je freeze_gun_null
        push edi
        jmp DWORD PTR [zpatch_freeze_gun_resume]
    freeze_gun_null:
        xor eax, eax
        pop esi
        ret 8
    }
}

// FreezeCarFix / DebugPrintSpawns bodies: the sub_100372F0 guard, the ten
// factory-dispatch thunks and the sub_10037510 spawn probe (installer 0x10037C70;
// the FreezeCarFix loader arm gates the install).
// @exact 0x100372F0
__declspec(naked) void abi_spawns_guard()
{
    // Keeps the original callee's EBP frame and ret 0Ch cleanup, so it stays naked.
    __asm {
        push ebp
        mov ebp, esp
        cmp dword ptr [zpatch_spawns_skip], 0
        jne spawns_guard_taken
        push dword ptr [ebp + 10h]
        mov eax, dword ptr [zpatch_spawns_orig_b]
        push dword ptr [ebp + 0Ch]
        push dword ptr [ebp + 8]
        call eax
        mov dword ptr [zpatch_spawns_skip], 0
        pop ebp
        ret 0Ch
    spawns_guard_taken:
        xor eax, eax
        mov dword ptr [zpatch_spawns_skip], eax
        pop ebp
        ret 0Ch
        __emit 0CCh
    }
}

// The ten factory-dispatch thunks (P3 sites). NOTE: case 1 (+0x3D) and case 2
// (+0x56) both set selector 3 in the original bytes; kept 1:1.

// @exact 0x10037330
// Spawn probes: the game's 10 class cases all do the same thing. The macro
// keeps the shim body in one place; only the selector and resume differ. The
// selector stays a plain literal because the inline assembler resolves neither
// `SpawnKind::` nor any other qualified constant, so the static_assert ties each
// literal to the enum row the dispatch table uses.
#define SPAWNS_CASE(name, selector, kind, resume)                              \
    static_assert(static_cast<uint32_t>(SpawnKind::kind) == selector);         \
    __declspec(naked) void name() {                                            \
        __asm push ebp                                                        \
        __asm mov ebp, esp                                                    \
        __asm mov ecx, dword ptr [ebp + 0Ch]                                  \
        __asm movsx eax, cl                                                   \
        __asm mov dword ptr [zpatch_spawns_kind], selector                    \
        __asm mov dword ptr [zpatch_spawns_class], eax                        \
        __asm mov dword ptr [ebp + 0Ch], ecx                                  \
        __asm pop ebp                                                         \
        __asm jmp dword ptr [resume]                                          \
    }

// The macro's body-name suffix is the original dispatch offset (0x24..0xF6); the
// second argument is the selector value the case stores in [zpatch_spawns_kind].
// Cases 1 and 2 both write 3 in the original bytes, kept 1:1.
SPAWNS_CASE(abi_spawns_case_0x24, 5, GenericPed, zpatch_spawns_case0_resume)
SPAWNS_CASE(abi_spawns_case_0x3D, 3, Ped, zpatch_spawns_case1_resume)
SPAWNS_CASE(abi_spawns_case_0x56, 3, Ped, zpatch_spawns_case2_resume)
SPAWNS_CASE(abi_spawns_case_0x6F, 0, Car, zpatch_spawns_case3_resume)
SPAWNS_CASE(abi_spawns_case_0x88, 4, Object, zpatch_spawns_case4_resume)
SPAWNS_CASE(abi_spawns_case_0x9E, 6, Bike, zpatch_spawns_case5_resume)
SPAWNS_CASE(abi_spawns_case_0xB4, 7, Train, zpatch_spawns_case6_resume)
SPAWNS_CASE(abi_spawns_case_0xCA, 1, Helicopter, zpatch_spawns_case7_resume)
SPAWNS_CASE(abi_spawns_case_0xE0, 2, Boat, zpatch_spawns_case8_resume)
SPAWNS_CASE(abi_spawns_case_0xF6, 8, Plane, zpatch_spawns_case9_resume)
#undef SPAWNS_CASE

// sub_10037510, the spawn probe (P1/P4/P5/P6/P7 sites): byte-exact naked body,
// 394 instructions, 0x10037510..0x10037ABC. ECX at the [vtable+0xC] call is the
// selector, exactly like the original - do not "fix" it.
//
// Field map, for reading the transcription: EDI is the requested model index,
// ESI walks the ped array (count 0x7918); the probe stores the selector in
// [zpatch_spawns_kind] and the class byte in [zpatch_spawns_class]; the object's
// class is the vtable[+0xC]() byte (5 = vehicle, 6 = ped), the object type is
// [esi+0x6C] (0..5) and the "non-ped" diagnostic is the [esi+0x3C] model check.
// The ten dispatch cases in the original share one body shape, so the repeated
// blocks below are the transcription, not a missed helper.
// @exact 0x10037510
__declspec(naked) void abi_spawns_probe()
{
    __asm {

        push ebp
        mov ebp, esp
        sub esp, 4Ch
        mov eax, dword ptr [__security_cookie]
        xor eax, ebp
        mov dword ptr [ebp - 8], eax
        push esi
        mov esi, dword ptr [zpatch_pre_ped_array]
        xor eax, eax
        push edi
        mov edi, dword ptr [ebp + 8]
        __emit 0Fh
        __emit 1Fh
        __emit 00h
L_10037530:
        mov ecx, dword ptr [esi + eax*4]
        test ecx, ecx
        je L_10037540
        cmp dword ptr [ecx + 3Ch], edi
        je L_100375FA
L_10037540:
        inc eax
        cmp eax, 7918h
        jl L_10037530
        or eax, 0FFFFFFFFh
L_1003754B:
        mov ecx, dword ptr [zpatch_spawns_kind]
        cwde
        mov dword ptr [zpatch_spawns_kind], 0FFFFFFFFh
        mov esi, dword ptr [esi + eax*4]
        cmp eax, -1
        je L_10037AA2
        test esi, esi
        je L_10037AA2
        cmp esi, 0Ch
        je L_10037AA2
        cmp esi, 0CDCDCDCDh
        je L_10037AA2
        mov eax, dword ptr [esi]
        test eax, eax
        je L_10037AA2
        cmp eax, 0Ch
        je L_10037AA2
        cmp eax, 0CDCDCDCDh
        je L_10037AA2
        test ecx, ecx
        jne L_10037635
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_10037602
        cmp dword ptr [esi + 6Ch], 0
        jne L_10037602
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_car
        jmp L_10037A8C
L_100375FA:
        movzx eax, ax
        jmp L_1003754B
L_10037602:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_car
        jmp L_10037A27
L_10037635:
        cmp ecx, 1
        jne L_100376C0
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_1003768D
        cmp dword ptr [esi + 6Ch], 4
        jne L_1003768D
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_heli
        jmp L_10037A8C
L_1003768D:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_heli
        jmp L_10037A27
L_100376C0:
        cmp ecx, 2
        jne L_1003774B
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_10037718
        cmp dword ptr [esi + 6Ch], 2
        jne L_10037718
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_boat
        jmp L_10037A8C
L_10037718:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_boat
        jmp L_10037A27
L_1003774B:
        cmp ecx, 6
        jne L_100377D6
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100377A3
        cmp dword ptr [esi + 6Ch], 1
        jne L_100377A3
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_bike
        jmp L_10037A8C
L_100377A3:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_bike
        jmp L_10037A27
L_100377D6:
        cmp ecx, 7
        jne L_10037861
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_1003782E
        cmp dword ptr [esi + 6Ch], 3
        jne L_1003782E
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_train
        jmp L_10037A8C
L_1003782E:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_train
        jmp L_10037A27
L_10037861:
        cmp ecx, 8
        jne L_100378EC
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100378B9
        cmp dword ptr [esi + 6Ch], 5
        jne L_100378B9
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_plane
        jmp L_10037A8C
L_100378B9:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_plane
        jmp L_10037A27
L_100378EC:
        cmp ecx, 3
        jne L_1003796D
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 6
        je L_1003792D
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_ped
        jmp L_10037A27
L_1003792D:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_ped
        jmp L_10037A8C
L_1003796D:
        cmp ecx, 4
        jne L_100379EB
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100379AB
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_vehicle_object
        jmp L_10037A27
L_100379AB:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_object
        jmp L_10037A8C
L_100379EB:
        cmp ecx, 5
        jne L_10037AA2
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 6
        je L_10037A59
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_used_nonped
L_10037A27:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call zpatch_spawns_sprintf
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call zpatch_spawns_print_edx
L_10037A3D:
        mov dword ptr [zpatch_spawns_skip], 1
        xor eax, eax
        pop edi
        pop esi
        mov ecx, dword ptr [ebp - 8]
        xor ecx, ebp
        call zpatch_spawns_cookie_check_ecx
        mov esp, ebp
        pop ebp
        ret
L_10037A59:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_generic
L_10037A8C:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call zpatch_spawns_sprintf
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call zpatch_spawns_print_edx
L_10037AA2:
        mov eax, dword ptr [zpatch_spawns_orig_a]
        push edi
        call eax
        mov ecx, dword ptr [ebp - 8]
        add esp, 4
        xor ecx, ebp
        pop edi
        pop esi
        call zpatch_spawns_cookie_check_ecx
        mov esp, ebp
        pop ebp
        ret
        __emit 0CCh
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)
