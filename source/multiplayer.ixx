module;

#include <common.hxx>
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

export module multiplayer;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import native;
import patch;
import shared;

// The MULTIPLAYER category: the MP crash fixes, the spawn probe, the network sync
// paths, the model-check crash payload and the mod-check stubs. One arm per key,
// each anchored to its own recovered signature (or to the recovered per-build
// stub rows).

static const char* const kBadPlayerModelNames[] = {
#include "model_names.inc"
};
static_assert(sizeof(kBadPlayerModelNames) / sizeof(kBadPlayerModelNames[0]) == 0x9637,
    "the recovered model-name table is the original's 38455 entries");

extern "C"
{
    uint32_t zpatch_group_hack_resume = 0;  // dword_1015AD60
    uint32_t zpatch_path_node_resume_ok = 0;    // dword_1015BDFC
    uint32_t zpatch_path_node_resume_fail = 0;  // dword_1015BE00
    uint32_t zpatch_blackscreen_resume = 0;     // dword_1015AD58
    uint32_t zpatch_blackscreen_value_ptr = 0;  // dword_1015AD5C
    uint32_t zpatch_crash_msg_getter = 0;       // dword_1015AD68

    // BadPlayerModelCrashFix: the trampoline's saved object and model index, the
    // resume the arm publishes (dword_10159CB8 = match+10) and the two game
    // pointers the original's loader preamble resolves on every run -- the ped
    // model array (dword_1015AD78) and the model-class lookup the fallback calls
    // (dword_10159C74).
    uint32_t zpatch_bpm_obj = 0;            // dword_10159C8C
    uint32_t zpatch_bpm_model_index = 0;    // dword_10159CA4
    uint32_t zpatch_bpm_resume = 0;         // dword_10159CB8
    uint32_t zpatch_bpm_ped_array = 0;      // dword_1015AD78 (a game address)
    uint32_t zpatch_bpm_fn_59c74 = 0;       // dword_10159C74 (a game address)

    // The two hash salts this arm's own bodies read. These are the original's
    // on-disk .data values, which are also its runtime values on every build whose
    // build probe hits (1.0.8.0 included); the probe-miss branch of the original's
    // loader preamble substitutes the fallback set, reproduced by the arm. The
    // third salt the payload's fallback wrapper used, dword_101580B8, now comes
    // from the native layer's resolved slot (source/native.ixx), because that
    // layer's own copy of sub_1005B3F0 reads it.
    uint32_t zpatch_bpm_salt_c8 = 0x570F5725u;  // dword_101580C8
    uint32_t zpatch_bpm_salt_40 = 0x4BF36A32u;  // dword_10158040

    // The original's IAT slot for GetModuleHandleW (0x10063074), which its
    // GTAC.dll test calls through.
    uint32_t zpatch_bpm_getmodulehandle = 0;

    // The pieces of the native-call layer the transcribed wrappers call: their
    // addresses are published here by the arm, because a module interface cannot
    // export a naked function (see RegisteredBody).
    uint32_t zpatch_bpm_context_init = 0;   // sub_100038C0 (source/native.ixx)
    uint32_t zpatch_bpm_cleanup_dword = 0;  // sub_1005DE40 (source/native.ixx)
    uint32_t zpatch_bpm_call_hash = 0;      // sub_1005B150 (source/native.ixx)
    uint32_t zpatch_bpm_call_bool = 0;      // sub_1005B3F0 (source/native.ixx)
    uint32_t zpatch_bpm_resolver = 0;       // sub_10003E50 (source/shared.ixx)

    // The bounded formatter's two data pieces. `sub_10001230` answers the UCRT
    // printf options block the original keeps at 0x101A84B0, which is zero in its
    // .data; `sub_10001240` reaches the UCRT's own printf engine through the IAT
    // slot 0x1006336C, and this module links the static CRT, so the engine is
    // reached through this slot and the call keeps the original's `call ds:[...]`
    // shape. Both are filled unconditionally in RegisterStability: the formatter is
    // reached from every module that formats a bounded string, so it cannot sit
    // behind a key's gate.
    uint32_t zpatch_bpm_format_options[2] = { 0, 0 };  // 0x101A84B0
    uint32_t zpatch_bpm_stdio_common_vsprintf = 0;     // 0x1006336C

    // sub_10001280's address, for the modules whose inline asm names a local
    // formatter. They enter this body through a six-byte `jmp` forwarder, because a
    // module interface cannot export a naked function; the slot is a plain dword so
    // the forwarder's `jmp DWORD PTR [...]` keeps the original's shape.
    uint32_t zpatch_format_entry = 0;       // sub_10001280 (this module)

    // The entries those two modules already publish: the guarded resolve-and-invoke
    // entry (sub_100038E0) and the game's model-index lookup (dword_10159C0C).
    extern uintptr_t zpatch_native_invoke_entry;
    extern uintptr_t zpatch_model_lookup;
}

// The literals the transcribed bodies push, at the original's own text: the wide
// module name the GTAC.dll test asks for (0x1012C71C), the diagnostic format
// (0x1012CDD8), the fallback model name the payload hashes (0x100A0E78) and the
// name table's miss answer (0x10128D20).

extern "C"
{
    const wchar_t zpatch_bpm_gtac[] = L"GTAC.dll";
    const char zpatch_bpm_format[] = "%s used a non-ped model (%s)";
    const char zpatch_bpm_multiplayer[] = "m_y_multiplayer";
    const char zpatch_bpm_unknown[] = "**UNKNOWN**";
}

    // The collision body's node-walk test and the deadlock counter it compares.
    uint32_t zpatch_collision_should_walk = 0;  // dword_1015BDD4
    uint32_t zpatch_collision_iterations = 0;   // dword_1015BDD8

// The SuperLODFix pair's published state: the two resumes the shim tails into
// (dword_101580AC and dword_101580B0) and the lookup the model helper calls
// (dword_101580B4). The original keeps them in .bss beside its code; the
// transcribed asm names the slots directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_super_lod_fail = 0;    // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;      // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;  // dword_101580B4
}

// ---- FreezeCarFix / DebugPrintSpawns ----------------------------------------
//
// The original's handler 0x10037C70 installs sixteen `E8` detours, at seventeen
// rows (the primary and the short prefix name the same site) across six recovered
// signatures. The guard, the ten factory-dispatch thunks and the 394-instruction
// spawn probe are 0x100372F0 / 0x10037330..0x100374E0 / 0x10037510; the probe's
// diagnostic calls its own name/who/print helpers, which are compiler output and
// are rebuilt in C++ below. The arm is gated exactly like the original's: on the
// primary signature (absent on 1.0.0.0 / 1.0.4.0 / 1.0.0.4, so those builds write
// nothing), and the DebugPrintSpawns flag is read *inside* this gate.
//
// The state words keep the original's on-disk seeds: the selector starts at -1,
// the suppress flag at 0, and the six resume words are published before the sites
// are written.
extern "C"
{
    // 0x10158058: -1 means "no spawn kind chosen yet"; any other value is the
    // kind the body picked. Signed, because the value is a sentinel and a small
    // enumerator, not a bit mask.
    int32_t zpatch_spawns_kind = -1;
    uint32_t zpatch_spawns_class = 0;           // dword_10159C80 (captured arg2 byte)
    uint32_t zpatch_spawns_skip = 0;            // dword_10159CB4
    uint32_t zpatch_spawns_orig_a = 0;          // dword_10159CB0 (probe resume)
    uint32_t zpatch_spawns_orig_b = 0;          // dword_10159C98 (guard resume)
    uint32_t zpatch_spawns_case0_resume = 0;    // dword_10159CBC (site +0x24)
    uint32_t zpatch_spawns_case1_resume = 0;    // dword_10159C9C (site +0x3D)
    uint32_t zpatch_spawns_case2_resume = 0;    // dword_10159CA0 (site +0x56)
    uint32_t zpatch_spawns_case3_resume = 0;    // dword_10159CA8 (site +0x6F)
    uint32_t zpatch_spawns_case4_resume = 0;    // dword_10159C7C (site +0x88)
    uint32_t zpatch_spawns_case5_resume = 0;    // dword_10159C88 (site +0x9E)
    uint32_t zpatch_spawns_case6_resume = 0;    // dword_10159CAC (site +0xB4)
    uint32_t zpatch_spawns_case7_resume = 0;    // dword_10159C90 (site +0xCA)
    uint32_t zpatch_spawns_case8_resume = 0;    // dword_10159C94 (site +0xE0)
    uint32_t zpatch_spawns_case9_resume = 0;    // dword_10159C84 (site +0xF6)

    // The original's IAT slot for GetModuleHandleW (0x10063074), which the probe's
    // GTAC.dll test calls through; filled unconditionally by the publish arm.
    uint32_t zpatch_spawns_getmodulehandlew = 0;
    // byte_10159C15: DebugPrintSpawns, read only inside the FreezeCarFix gate
    // (`movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1; mov [0x10159C15],cl`).
    uint8_t zpatch_spawns_debug = 0;
    // sub_10003E50 (source/shared.ixx), the original's own model-name hash.
    uint32_t zpatch_spawns_resolver = 0;
    // dword_1015AD78: the ped-model object array the probe walks. The original's
    // loader preamble resolves it on every run, so this arm resolves it under its
    // own gate too (the BadPlayerModelCrashFix arm carries the same slot for its
    // own bodies).
    uint32_t zpatch_spawns_ped_array = 0;

    // The probe's diagnostic text, at the original's recovered strings: the two
    // names, the module test, and the format each spawn class prints.
    const wchar_t zpatch_spawns_gtac[] = L"GTAC.dll";
    const char zpatch_spawns_fmt_car[] = "%s spawned a car (%s)";
    const char zpatch_spawns_fmt_noncar[] = "%s spawned a non-car car (%s)";
    const char zpatch_spawns_fmt_heli[] = "%s spawned a heli (%s)";
    const char zpatch_spawns_fmt_nonheli[] = "%s spawned a non-heli heli (%s)";
    const char zpatch_spawns_fmt_boat[] = "%s spawned a boat (%s)";
    const char zpatch_spawns_fmt_nonboat[] = "%s spawned a non-boat boat (%s)";
    const char zpatch_spawns_fmt_bike[] = "%s spawned a bike (%s)";
    const char zpatch_spawns_fmt_nonbike[] = "%s spawned a non-bike bike (%s)";
    const char zpatch_spawns_fmt_train[] = "%s spawned a train (%s)";
    const char zpatch_spawns_fmt_nontrain[] = "%s spawned a non-train train (%s)";
    const char zpatch_spawns_fmt_plane[] = "%s spawned a plane (%s)";
    const char zpatch_spawns_fmt_nonplane[] = "%s spawned a non-plane plane (%s)";
    const char zpatch_spawns_fmt_ped[] = "%s spawned a ped (%s)";
    const char zpatch_spawns_fmt_nonped[] = "%s spawned a non-ped ped (%s)";
    const char zpatch_spawns_fmt_vehicleobject[] = "%s spawned a vehicle object (%s)";
    const char zpatch_spawns_fmt_object[] = "%s spawned an object (%s)";
    const char zpatch_spawns_fmt_generic[] = "%s spawned (%s)";
    const char zpatch_spawns_fmt_used_nonped[] = "%s used a non-ped model (%s)";
}

// RemoveGFWLUpdatePopup, as the original handler sub_10005500 applies it: the
// update window's date arithmetic at game 0x7CBF70 is replaced by a stub that
// first disables the four xlive.dll update-window flag slots, then replays the
// four instructions the jump overwrites and resumes at the anchor's +12.
// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).

extern "C"
{
    uint32_t zpatch_gfwl_resume = 0;  // dword_10159C18
    uint32_t zpatch_xlive_base = 0;   // dword_10159C10
}


// Patch4RemoveModChecks' continuation (match + 12) of the game's date arithmetic.
extern "C"
{
    uint32_t zpatch_patch4_resume = 0;
}

namespace
{
    // The address of a body another module transcribed, by its recovered VA. The
    // native layer publishes only its C entries, and a module interface cannot
    // export a naked function, so the body registry is what makes its pieces
    // reachable here.
    uint32_t RegisteredBody(uint32_t reference)
    {
        for (const auto& entry : Bodies::All())
        {
            if (entry.reference == reference)
                return static_cast<uint32_t>(entry.address);
        }
        return 0;
    }

    // A rel32 target inside the match: the displacement dword at `displacement`,
    // measured from the instruction's last byte at `next`.
    uint32_t Rel32Target(std::byte* match, std::ptrdiff_t displacement, std::ptrdiff_t next)
    {
        const auto displacementValue = *reinterpret_cast<const int32_t*>(match + displacement);
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(match + next) + static_cast<uint32_t>(displacementValue));
    }

    // The thunk the CrashMsgFix anchor's -4 points at. The operand is a 32-bit VA of
    // the image *as loaded*: the loader has already applied the image's relocations
    // before this runs, so it is a live address and the cast is the whole
    // conversion. The offline dump relocates its own copy (tests/dump_main.cpp), so
    // the same identity holds there; rebasing through the preferred base would move
    // the thunk by the run's whole relocation distance.
    std::byte* CrashMsgSite()
    {
        auto pattern = find_pattern("68 6A 10 4E 31");
        if (pattern.empty())
            return nullptr;

        return reinterpret_cast<std::byte*>(
            static_cast<uintptr_t>(*pattern.get_first<uint32_t>(-4)));
    }
}

static void __cdecl BpmZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

// The 38,455-entry name table the probe's name helper walks. This module needs its
// own copy: source/stability.ixx owns the same recovered array for
// BadPlayerModelCrashFix, and that arm registers it with the body registry only
// when its own key is on, so FreezeCarFix cannot reach it. The data is recovered
// (source/model_names.inc) and the lookup below is a C++ rebuild of sub_10003F00,
// whose blocking instruction is the indexed absolute `push [0x100DE770 + esi*4]`
// in a compiler-generated /GS frame.
static const char* const kSpawnsModelNames[] = {
#include "model_names.inc"
};
static_assert(sizeof(kSpawnsModelNames) / sizeof(kSpawnsModelNames[0]) == 0x9637,
    "the recovered model-name table is the original's 38455 entries");

static void AimProjectileShim();
static void CrashMsgShim();
static void BlackscreenShim();
static void PathNodeShim();
static void GroupHackShim();
static void BpmFormat();
static void BpmModelNameBody();
static void BpmWhoBody();
static void BpmPrintBody();
static void BpmCheckBody();
static void BpmTrampoline();
static void SpawnsNameEntry();
static void SpawnsWhoEntry();
static void SpawnsPrintEntry();
static void SpawnsFormatEntry();
static void SpawnsGuardBody();
static void SpawnsCase0x24Body();
static void SpawnsCase0x3DBody();
static void SpawnsCase0x56Body();
static void SpawnsCase0x6FBody();
static void SpawnsCase0x88Body();
static void SpawnsCase0x9EBody();
static void SpawnsCase0xB4Body();
static void SpawnsCase0xCABody();
static void SpawnsCase0xE0Body();
static void SpawnsCase0xF6Body();
static void SpawnsProbeBody();
static void SuperLodModel();
static void SuperLodShim();
static void BetterMPSyncBody();
static void CollisionDeadlockShim();
static void Patch4StubBody();
static void GfwlStubBody();
static void RgscLoginRemovedBody();
    // The per-build set InstallSuperLODFix publishes: the site's two continuations
    // and the hash lookup the model helper calls, each a game RVA from the
    // recovered handler's own base + RVA pairs (the suffix is the build).
    struct SuperLodTargets
    {
        Build build;
        uint32_t fail;
        uint32_t ok;
        uint32_t lookup;
    };

    void PublishGroupHackFix()
    {
        if (!Loader::Enabled("GroupHackFix"))
            return;

        auto pattern = find_pattern("33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04");
        if (pattern.empty())
            return;

        zpatch_group_hack_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 11);
    }

    void PublishPathNodeCrashFix()
    {
        if (!Loader::Enabled("PathNodeCrashFix"))
            return;

        auto pattern = find_pattern("8B 04 B1 0F B7 D0 81 FA FF FF 00 00");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_path_node_resume_ok = static_cast<uint32_t>(match);
        zpatch_path_node_resume_fail = static_cast<uint32_t>(match + 59);
    }

    void PublishBlackscreenFix()
    {
        if (!Loader::Enabled("BlackscreenFix"))
            return;

        auto pattern = find_pattern("75 54 8B 48 28 8B 40 2C");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_blackscreen_resume =
            static_cast<uint32_t>(match + 86 + *pattern.get_first<int32_t>(82));
        zpatch_blackscreen_value_ptr =
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(zpatch_blackscreen_resume) + 1);
    }

    void PublishCrashMsgFix()
    {
        if (!Loader::Enabled("CrashMsgFix"))
            return;

        std::byte* site = CrashMsgSite();
        if (site == nullptr)
            return;

        zpatch_crash_msg_getter = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + 5 +
            *reinterpret_cast<const int32_t*>(site + 1));
    }

    void PublishBadPlayerModelCrashFix()
    {
        if (!Loader::Enabled("BadPlayerModelCrashFix"))
            return;

        auto pattern = find_pattern("83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C");
        if (pattern.empty())
            return;

        zpatch_bpm_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 10);

        // dword_1015AD78: the ped-model object array, the absolute operand four
        // bytes before the original's own anchor (game 0x49EC0F). The operand is a
        // live VA -- the loader has already relocated the image, and the offline
        // dump relocates its copy too -- so the captured dword is the address.
        pattern = find_pattern("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07");
        if (!pattern.empty())
            zpatch_bpm_ped_array = *pattern.get_first<uint32_t>(-4);

        // dword_10159C74: the model-class lookup the fallback calls, the
        // original's anchor - 42 (game 0xB191D0).
        pattern = find_pattern("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23");
        if (!pattern.empty())
        {
            zpatch_bpm_fn_59c74 = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) - 42);
        }

        // The build probe. The original's loader installs its ten fallback hash
        // salts only when this pattern is absent (0x10052E1A jumps the ten stores
        // on a hit), so on 1.0.8.0 the shipped .data values stand. The two salts
        // this key's own bodies read are seeded with those; a missed probe
        // substitutes the recovered fallback set. The third, dword_101580B8, is
        // the native layer's resolved slot (source/native.ixx).
        pattern = find_pattern("68 ? ? ? ? 68 A9 54 14 51 E8");
        if (pattern.empty())
        {
            zpatch_bpm_salt_c8 = 0x406B4B20u;
            zpatch_bpm_salt_40 = 0xC9DBDA90u;
        }

        // 0x10063074: the original's GetModuleHandleW slot, which its GTAC.dll
        // test calls through.
        zpatch_bpm_getmodulehandle = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));

        // The layer's transcribed pieces, by their recovered VAs. The wrapper is
        // the layer's copy of sub_1005B3F0; only one body can carry that recovered
        // VA, so this arm does not register a second one.
        zpatch_bpm_context_init = RegisteredBody(0x100038C0u);
        zpatch_bpm_cleanup_dword = RegisteredBody(0x1005DE40u);
        zpatch_bpm_call_hash = RegisteredBody(0x1005B150u);
        zpatch_bpm_call_bool = RegisteredBody(0x1005B3F0u);
        zpatch_bpm_resolver = static_cast<uint32_t>(Shared::ValueResolver());
    }

    void PublishCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 91);
        zpatch_collision_should_walk =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 95) + displacement);
    }

    constexpr SuperLodTargets kSuperLodTargets[] = {
        { Build::v1070, 0x4A3D37u, 0x4A3CEFu, 0x58AAE0u },
        { Build::v1080, 0x540E67u, 0x540E1Fu, 0x4DD2D0u },
    };

    void PublishSuperLODFix()
    {
        if (!Loader::Enabled("SuperLODFix"))
            return;

        auto pattern = find_pattern("F6 40 ? ? 74 ? 85 ED 0F 85 ? ? ? ? 55 6A ? E8 ? ? ? ? 8B F8");
        if (pattern.empty())
            return;

        const auto base = Game::Get().ImageBase();
        for (const SuperLodTargets& targets : kSuperLodTargets)
        {
            if (targets.build != Loader::Current())
                continue;
            zpatch_super_lod_fail = static_cast<uint32_t>(base + targets.fail);
            zpatch_super_lod_ok = static_cast<uint32_t>(base + targets.ok);
            zpatch_super_lod_lookup = static_cast<uint32_t>(base + targets.lookup);
        }
    }

    void PublishFreezeCarFix()
    {
        if (!Loader::Enabled("FreezeCarFix"))
            return;

        // The original reads DebugPrintSpawns only inside this gate and only raises
        // the flag (`movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1; mov [...],cl`),
        // so a value other than 1 leaves it at the BSS zero.
        zpatch_spawns_debug = Catalogue::Read("DebugPrintSpawns") == 1 ? 1 : 0;
        zpatch_spawns_getmodulehandlew = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));
        zpatch_spawns_resolver = static_cast<uint32_t>(Shared::ValueResolver());

        auto primary = find_pattern("32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0");
        if (primary.empty())
            return;

        auto* primaryMatch = primary.get_first<std::byte>(0);
        zpatch_spawns_orig_a = Rel32Target(primaryMatch, 8, 12);

        // dword_1015AD78, the ped-model object array, is the operand four bytes
        // before the original's own anchor (the same recovered read the
        // BadPlayerModelCrashFix arm makes). It is a live VA: the loader has already
        // relocated the image, and the dump relocates its copy too.
        auto pedArray = find_pattern("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07");
        if (!pedArray.empty())
            zpatch_spawns_ped_array = *pedArray.get_first<uint32_t>(-4);

        auto guard = find_pattern(
            "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B");
        if (!guard.empty())
            zpatch_spawns_orig_b = Rel32Target(guard.get_first<std::byte>(0), 12, 16);

        // The probe's ten jump-table arms: each site's own `call` target is the
        // resume the thunk tail-jumps to, in case order 0..9.
        auto dispatch = find_pattern("E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00");
        if (!dispatch.empty())
        {
            auto* match = dispatch.get_first<std::byte>(0);
            zpatch_spawns_case0_resume = Rel32Target(match, 37, 41);
            zpatch_spawns_case1_resume = Rel32Target(match, 62, 66);
            zpatch_spawns_case2_resume = Rel32Target(match, 87, 91);
            zpatch_spawns_case3_resume = Rel32Target(match, 112, 116);
            zpatch_spawns_case4_resume = Rel32Target(match, 137, 141);
            zpatch_spawns_case5_resume = Rel32Target(match, 159, 163);
            zpatch_spawns_case6_resume = Rel32Target(match, 181, 185);
            zpatch_spawns_case7_resume = Rel32Target(match, 203, 207);
            zpatch_spawns_case8_resume = Rel32Target(match, 225, 229);
            zpatch_spawns_case9_resume = Rel32Target(match, 247, 251);
        }
    }

    void PublishPatch4RemoveModChecks()
    {
        if (!Loader::Enabled("Patch4RemoveModChecks"))
            return;

        auto pattern = find_pattern(
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
            zpatch_patch4_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
    }

    void InstallBetterMPSync()
    {
        if (!Loader::Enabled("BetterMPSync"))
            return;

        auto pattern = find_pattern("8B 90 E8 00 00 00 55 8B CE FF");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&BetterMPSyncBody), 0x100057F0u);
            injector::MakeCALL(pattern.get_first(6),
                reinterpret_cast<void*>(&BetterMPSyncBody), true);
        }
    }

    void InstallIncreasePtrNodes()
    {
        if (!Loader::Enabled("IncreasePtrNodes"))
            return;

        auto pattern = find_pattern("6A 08 ? ? ? ? ? 68 80 38 01 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 320'000u, true);

        pattern = find_pattern("6A 10 ? ? ? ? ? 68 80 3E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 64'000u, true);

        pattern = find_pattern("6A 14 ? ? ? ? ? 68 80 3E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 64'000u, true);
    }

static void GfwlPatch()
{
    for (const uint32_t offset : { 0x66F770u, 0x66F774u, 0x6B55D0u, 0x6B55F0u })
        injector::WriteMemory<uint32_t>(zpatch_xlive_base + offset, 0x10000E83u, true);
}
namespace
{
    // sub_10003F00's payload, rebuilt: first table name whose game hash matches, or
    // "**UNKNOWN**" on a miss (the original answers its own literal at 0x10128D20).
    const char* SpawnsModelName(uint32_t hash)
    {
        using HashFn = uint32_t(__cdecl*)(const char*);
        const auto hash_name = reinterpret_cast<HashFn>(zpatch_spawns_resolver);
        for (uint32_t index = 0; index < 0x9637u; ++index)
        {
            if (hash_name(kSpawnsModelNames[index]) == hash)
                return kSpawnsModelNames[index];
        }
        return "**UNKNOWN**";
    }

    // The original's native call block (sub_1005B290 / sub_1005B530 build it), and
    // the one builder both wrappers share: zero the 0xA0 bytes, point the return and
    // argument words at args, invoke the hash through the guarded entry, copy every
    // pending record out and answer args[0]. The blocked instruction is the
    // compiler's own frame; the block layout is the recovered one.
    struct SpawnsNativeBlock final
    {
        uint32_t p_return;    // +0x00
        uint32_t arg_count;   // +0x04
        uint32_t p_args;      // +0x08
        uint32_t out_count;   // +0x0C
        uint32_t out_ptrs[4]; // +0x10
        struct Record final { uint32_t value[3]; uint32_t padding; };
        Record records[4];    // +0x20
        uint32_t args[17];    // +0x60 (arg 0 is the return slot)
    };

    uint32_t SpawnsScriptCall(uint32_t hash, uint32_t nargs, const uint32_t* arguments)
    {
        SpawnsNativeBlock block{};
        const auto base = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&block.args[0]));
        block.p_return = base;
        block.p_args = base;
        block.arg_count = nargs;
        for (uint32_t index = 0; index < nargs && index < 17u; ++index)
            block.args[index] = arguments[index];
        Native::Invoke(hash, &block);
        while (block.out_count != 0u)
        {
            --block.out_count;
            auto* destination = reinterpret_cast<uint32_t*>(
                static_cast<uintptr_t>(block.out_ptrs[block.out_count]));
            const auto& source = block.records[block.out_count];
            destination[0] = source.value[0];
            destination[1] = source.value[1];
            destination[2] = source.value[2];
        }
        return block.args[0];
    }

    // sub_1005B290 and sub_1005B530, rebuilt: the "who spawned it" script call (the
    // original's hash is dword_101580C8, published by the loader's salt rule) and the
    // chat print (fixed hash 0x79023CFC). Both take their one argument in EDX and are
    // entered through the naked thunks below.
    const char* SpawnsWho(uint32_t id)
    {
        return reinterpret_cast<const char*>(
            static_cast<uintptr_t>(SpawnsScriptCall(Native::Salt(0x101580C8u), 1, &id)));
    }

    void SpawnsPrint(const char* text)
    {
        const auto argument = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(text));
        SpawnsScriptCall(0x79023CFCu, 1, &argument);
    }
}

namespace
{
    // Patch6and7RemoveModChecks (original handler sub_10044710): the packed
    // entry-stub jump targets, per gated build. The handler writes
    // `lea edi,[eax+<site>]; lea ebx,[eax+<target>]; mov byte [edi],0E9h`; the
    // entry target (0x3A09) is a 1.0.6.0 row, the mid and case targets are
    // 1.0.7.0 rows.
    constexpr uint32_t kModChecksEntryJmp1060 = 0x003A09u;
    constexpr uint32_t kModChecksMidJmp1070 = 0x0038AEu;
    constexpr uint32_t kModChecksCaseJmp1070 = 0x0039E9u;

    // One site of the two arms: the byte the handler leaves at the match, or the
    // target it writes a five-byte jump to. Every candidate has exactly one hit at
    // its site on its build.
    enum class ModCheckWrite : uint8_t
    {
        zero,       // clear the stub byte
        jmpOpcode,  // `jmp rel8` (the branch operand already there stays)
        jump,       // `jmp rel32` to `target`
        nopSeven,   // wipe the mod enabler native's seven bytes
    };

    struct ModCheckSite final
    {
        std::string_view signature;
        ModCheckWrite kind;
        uint32_t target{};  // jump only
    };

    // sub_10044710's write order, per build (1.0.6.0 and 1.0.7.0 are the only
    // gated builds; the handler's own `lea edi,[eax+<site>]` rows agree).
    constexpr ModCheckSite kModChecks1060[] = {
        { "01 00 00 00 51 89 5C 24 ? E8 ? ? ? ? 85 C0 75 ? 39 44 24 ? 74",
          ModCheckWrite::zero },
        { "74 ? 8B 45 ? 53 6A ? 50 E8 ? ? ? ? 57 E8 ? ? ? ? 83 C4 ? 68",
          ModCheckWrite::jmpOpcode },
        { "01 00 00 00 75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C ? ? E8",
          ModCheckWrite::zero },
        { "75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C 24 ? E8", ModCheckWrite::jump,
          kModChecksEntryJmp1060 },
        { "74 ? 8B 4D ? 57 6A ? 51 E8 ? ? ? ? 80 3D ? ? ? ? ? 0F",
          ModCheckWrite::jmpOpcode },
        { "C6 05 ? ? ? ? ? 74 ? 8B 40 ? 56 6A ? 6A ? 50 83 CE ? E8",
          ModCheckWrite::nopSeven },
    };

    constexpr ModCheckSite kModChecks1070[] = {
        { "01 00 00 00 51 89 5C 24 ? E8 ? ? ? ? 85 C0 75 ? 39 44 24 ? 74",
          ModCheckWrite::zero },
        { "75 ? 39 44 24 ? 74 ? 8A C3 5F 5E 5D 5B 83 C4 ? C3 8D 54 24 ? 52 8D",
          ModCheckWrite::jump, kModChecksMidJmp1070 },
        { "74 ? 8B 45 ? 53 6A ? 50 E8 ? ? ? ? 57 E8 ? ? ? ? ? ? ? 68",
          ModCheckWrite::jmpOpcode },
        { "01 00 00 00 75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C ? ? E8",
          ModCheckWrite::zero },
        { "75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C 24 ? E8", ModCheckWrite::jump,
          kModChecksCaseJmp1070 },
        { "74 ? 8B 4D ? 57 6A ? 51 E8 ? ? ? ? 80 3D ? ? ? ? ? 0F",
          ModCheckWrite::jmpOpcode },
        { "C6 05 ? ? ? ? ? 74 ? 8B 40 ? 56 6A ? 6A ? 50 83 CE ? E8",
          ModCheckWrite::nopSeven },
    };

    struct ModCheckBuildSites final
    {
        Build build;
        std::span<const ModCheckSite> sites;
    };

    // The build gate: a build without a row writes nothing.
    constexpr ModCheckBuildSites kModChecksSites[] = {
        { Build::v1060, kModChecks1060 },
        { Build::v1070, kModChecks1070 },
    };
}

// Patch4RemoveModChecks: the game's date arithmetic at 1.0.8.0 game 0x7CBF70
// (the `2B C1 1B D6 ...` anchor) is replaced by the original's sub_100054E0, an
// 8-instruction stub that runs the shared flag-gated helper between pusha/popa,
// replays the 64-bit subtraction and its two stack stores, and resumes at
// match + 12.
//
// The helper (sub_10005340, 145 instructions) has two branches. The GFWL one is
// RemoveGFWLUpdatePopup's and already lives in loading.ixx; this module owns the
// Patch4 one: a one-time scan for "33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57"
// (the original's own scanner call), then a zero store to the dword the matched
// code points at (site + 30). That signature has no hit on 1.0.8.0, so on the
// user's build the branch is a scan and nothing else -- the key's visible effect
// there is the stub install itself, exactly as in the original. The scan is kept
// so builds where the target exists behave the same.
//
// The helper is compiler output for a C function (a thread-safe static guard and
// VirtualProtect calls), so it is rebuilt, not transcribed; the stub is
// transcribed and proven whole.
static std::atomic<uint32_t> zpatch_patch4_scan_done{ 0 };
static uint32_t zpatch_patch4_site = 0;

static void Patch4Step()
{
    if (zpatch_patch4_scan_done.exchange(1u, std::memory_order_acq_rel) == 0)
    {
        auto pattern = find_pattern("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
        if (!pattern.empty())
            zpatch_patch4_site = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
    }

    if (zpatch_patch4_site != 0)
    {
        // 0x10005427 reads *[site + 30] without a null check; the zero store goes
        // through the injector, which is the original's VirtualProtect/write/restore.
        uint32_t target = 0;
        std::memcpy(&target,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(zpatch_patch4_site) + 30u),
            sizeof(target));
        injector::WriteMemory<uint32_t>(target, 0u, true);
    }
}


    void InstallMPNikoCrashFix()
    {
        if (!Loader::Enabled("MPNikoCrashFix"))
            return;

        auto pattern = find_pattern("74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);
    }

    void InstallBlackscreenFix()
    {
        if (!Loader::Enabled("BlackscreenFix"))
            return;

        auto pattern = find_pattern("75 54 8B 48 28 8B 40 2C");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&BlackscreenShim), 0x10036EE0u);
        injector::MakeJMP(pattern.get_first(81), reinterpret_cast<void*>(&BlackscreenShim), true);
    }

    void InstallCrashMsgFix()
    {
        if (!Loader::Enabled("CrashMsgFix"))
            return;

        std::byte* site = CrashMsgSite();
        if (site == nullptr)
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&CrashMsgShim), 0x10036F00u);
        injector::MakeCALL(site, reinterpret_cast<void*>(&CrashMsgShim), true);
    }

    void InstallGroupHackFix()
    {
        if (!Loader::Enabled("GroupHackFix"))
            return;

        auto pattern = find_pattern("33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&GroupHackShim), 0x10036E90u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&GroupHackShim), true);
    }

    void InstallPathNodeCrashFix()
    {
        if (!Loader::Enabled("PathNodeCrashFix"))
            return;

        auto pattern = find_pattern("8B 04 B1 0F B7 D0 81 FA FF FF 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&PathNodeShim), 0x1003D7B0u);
        injector::MakeJMP(pattern.get_first(53), reinterpret_cast<void*>(&PathNodeShim), true);
    }

    void InstallRemoveAllWeaponsProtection()
    {
        if (!Loader::Enabled("RemoveAllWeaponsProtection"))
            return;

        auto pattern = find_pattern("E9 ? ? ? ? 57 8D 8F ? ? 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(5), 12, true);
    }

    void InstallAimProjectileCrashFix()
    {
        if (!Loader::Enabled("AimProjectileCrashFix"))
            return;

        auto pattern = find_pattern("8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&AimProjectileShim), 0x10005590u);
            injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&AimProjectileShim),
                true);
        }
    }

    void InstallBadPlayerModelCrashFix()
    {
        if (!Loader::Enabled("BadPlayerModelCrashFix"))
            return;

        auto pattern = find_pattern("83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&BpmTrampoline), 0x10037BA0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&BpmCheckBody), 0x10037AC0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&BpmWhoBody), 0x1005B290u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&BpmPrintBody), 0x1005B530u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&BpmModelNameBody), 0x10003F00u);

        // The 38,455-entry name table the transcribed lookup reads. Its operand is
        // the original's `[table + esi*4]`, so the table is named here (the
        // original's VA and its length in that image) and the offline proof
        // compares the table's contents through the registration instead of the
        // displacement. Nothing at run time reads this entry; it is what makes the
        // operand provable (docs/rebuilt.md).
        Bodies::RegisterTable(reinterpret_cast<uintptr_t>(kBadPlayerModelNames),
            0x100DE770u, sizeof(kBadPlayerModelNames));

        injector::MakeJMP(pattern.get_first<std::byte>(0),
            reinterpret_cast<void*>(&BpmTrampoline), true);
    }

    void InstallCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&CollisionDeadlockShim), 0x10005D80u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&CollisionDeadlockShim), true);
    }

    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        injector::WriteMemory<std::byte>(match, Bytes::RetImm16, true);
        injector::WriteMemory<std::byte>(match + 1, Bytes::RetImm16High, true);
        injector::WriteMemory<std::byte>(match + 2, Bytes::Zero, true);
    }

    void InstallFreezeCarFix()
    {
        if (!Loader::Enabled("FreezeCarFix"))
            return;

        auto primary = find_pattern("32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0");
        if (primary.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsProbeBody), 0x10037510u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsGuardBody), 0x100372F0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x24Body), 0x10037330u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x6FBody), 0x10037390u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xE0Body), 0x100373C0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x3DBody), 0x100373F0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x56Body), 0x10037420u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x88Body), 0x10037450u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x9EBody), 0x10037480u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xF6Body), 0x100374B0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xB4Body), 0x100374E0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xCABody), 0x10037360u);

        injector::MakeCALL(primary.get_first<std::byte>(7),
            reinterpret_cast<void*>(&SpawnsProbeBody), true);

        auto guard = find_pattern(
            "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B");
        if (!guard.empty())
        {
            injector::MakeCALL(guard.get_first<std::byte>(11),
                reinterpret_cast<void*>(&SpawnsGuardBody), true);
        }

        struct SpawnCase final
        {
            std::ptrdiff_t dispatchOffset;
            void (*body)();
        };
        constexpr SpawnCase kSpawnCases[] = {
            { 0x24, &SpawnsCase0x24Body },
            { 0x3D, &SpawnsCase0x3DBody },
            { 0x56, &SpawnsCase0x56Body },
            { 0x6F, &SpawnsCase0x6FBody },
            { 0x88, &SpawnsCase0x88Body },
            { 0x9E, &SpawnsCase0x9EBody },
            { 0xB4, &SpawnsCase0xB4Body },
            { 0xCA, &SpawnsCase0xCABody },
            { 0xE0, &SpawnsCase0xE0Body },
            { 0xF6, &SpawnsCase0xF6Body },
        };

        auto dispatch = find_pattern("E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00");
        if (!dispatch.empty())
        {
            auto* match = dispatch.get_first<std::byte>(0);
            for (const SpawnCase& spawn : kSpawnCases)
            {
                injector::MakeCALL(match + spawn.dispatchOffset,
                    reinterpret_cast<void*>(spawn.body), true);
            }
        }

        auto probeA = find_pattern("88 87 40 01 00 00 E8");
        if (!probeA.empty())
        {
            injector::MakeCALL(probeA.get_first<std::byte>(6),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        auto probeB = find_pattern("88 5C 24 1F 8B 54 24 28 52 E8");
        if (!probeB.empty())
        {
            injector::MakeCALL(probeB.get_first<std::byte>(9),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
            injector::MakeCALL(probeB.get_first<std::byte>(254),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        auto probeC = find_pattern("38 98 8C 00 00 00 75 2D 57 E8");
        if (!probeC.empty())
        {
            injector::MakeCALL(probeC.get_first<std::byte>(9),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        // The second row of the original's own table: a shorter prefix of the
        // primary, installed at the same +7 site, so the write is the duplicate the
        // original makes (17 rows, 16 distinct sites).
        auto shortPrimary = find_pattern("32 DB 8B 54 24 24 52 E8");
        if (!shortPrimary.empty())
        {
            injector::MakeCALL(shortPrimary.get_first<std::byte>(7),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }
    }

    void InstallSuperLODFix()
    {
        if (!Loader::Enabled("SuperLODFix"))
            return;

        auto pattern = find_pattern("F6 40 ? ? 74 ? 85 ED 0F 85 ? ? ? ? 55 6A ? E8 ? ? ? ? 8B F8");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodModel), 0x100469A0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodShim), 0x100469E0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&SuperLodShim), true);
    }

    void InstallPatch4RemoveModChecks()
    {
        if (!Loader::Enabled("Patch4RemoveModChecks"))
            return;

        auto pattern = find_pattern(
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&Patch4StubBody), true);
    }

    void InstallPatch6and7RemoveModChecks()
    {
        // The original has one handler for two keys: `stoi(ini.get(
        // "Patch6and7RemoveModChecks","0")) == 1 || stoi(ini.get(
        // "Patch7RemoveModChecks","0")) == 1` (0x10053FBD..0x10053FE5), both feeding
        // 0x10044710. Patch7RemoveModChecks is therefore not an alias that does
        // nothing -- it is the alternate trigger for the same full write set, and a
        // Patch7-only INI installs all 16/21 bytes on 1.0.6.0/1.0.7.0.
        if (!Loader::Enabled("Patch6and7RemoveModChecks")
            && !Loader::Enabled("Patch7RemoveModChecks"))
            return;

        for (const auto& row : kModChecksSites)
        {
            if (row.build != Loader::Current())
                continue;

            for (const auto& site : row.sites)
            {
                auto pattern = find_pattern(site.signature);
                if (pattern.empty())
                    continue;

                auto* match = pattern.get_first<std::byte>(0);
                switch (site.kind)
                {
                case ModCheckWrite::zero:
                    injector::WriteMemory<std::byte>(match, Bytes::Zero, true);
                    break;
                case ModCheckWrite::jmpOpcode:
                    injector::WriteMemory<std::byte>(match, Bytes::ShortJmp, true);
                    break;
                case ModCheckWrite::jump:
                    injector::MakeJMP(match,
                        reinterpret_cast<void*>(Game::Get().ImageBase() + site.target), true);
                    break;
                case ModCheckWrite::nopSeven:
                    injector::MakeNOP(match, 7, true);
                    break;
                }
            }
        }
    }

    void InstallRemoveGFWLUpdatePopup()
    {
        if (!Loader::Enabled("RemoveGFWLUpdatePopup"))
            return;

        // dword_10159C10: the xlive.dll module the original's loader captures.
        zpatch_xlive_base =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetModuleHandleA("xlive.dll")));

        auto pattern = find_pattern("2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_gfwl_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 12);
            injector::MakeJMP(match, reinterpret_cast<void*>(&GfwlStubBody), true);
        }
    }

    void InstallRemoveRGSCLoginRequirement()
    {
        if (!Loader::Enabled("RemoveRGSCLoginRequirement"))
            return;

        auto pattern = find_pattern("8B 44 24 ? 85 C0 74 ? F3 0F 7E 05 ? ? ? ? 66 0F D6 00");
        if (Loader::Current() == Build::v1080 && !pattern.empty())
        {
            auto* site = pattern.get_first<std::byte>(0);  // 0x498460
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
        }

        pattern = find_pattern("83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
    }

export void RegisterMultiplayer()
{
    // The bounded formatter's data, filled before any tier runs. The body is
    // reached from every module that formats a bounded string, so neither slot may
    // sit behind a key's gate.
    zpatch_format_entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&BpmFormat));
    zpatch_bpm_stdio_common_vsprintf = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&__stdio_common_vsprintf));

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&AimProjectileShim), 0x10005590u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BlackscreenShim), 0x10036EE0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CrashMsgShim), 0x10036F00u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GroupHackShim), 0x10036E90u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PathNodeShim), 0x1003D7B0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BetterMPSyncBody), 0x100057F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CollisionDeadlockShim), 0x10005D80u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsProbeBody), 0x10037510u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsGuardBody), 0x100372F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x24Body), 0x10037330u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x6FBody), 0x10037390u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xE0Body), 0x100373C0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x3DBody), 0x100373F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x56Body), 0x10037420u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x88Body), 0x10037450u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x9EBody), 0x10037480u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xF6Body), 0x100374B0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xB4Body), 0x100374E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xCABody), 0x10037360u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodModel), 0x100469A0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodShim), 0x100469E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&Patch4StubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);

    // BadPlayerModelCrashFix's name table and payload bodies.
    Bodies::Register(reinterpret_cast<uintptr_t>(&BpmTrampoline), 0x10037BA0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BpmCheckBody), 0x10037AC0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BpmWhoBody), 0x1005B290u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BpmPrintBody), 0x1005B530u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BpmModelNameBody), 0x10003F00u);
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(kBadPlayerModelNames),
        0x100DE770u, sizeof(kBadPlayerModelNames));

    // The arms keep the position they occupied in the original driver.
    Loader::AddPublish(Loader::Entry::Stability, 1, PublishGroupHackFix);
    Loader::AddPublish(Loader::Entry::Stability, 2, PublishPathNodeCrashFix);
    Loader::AddPublish(Loader::Entry::Stability, 4, PublishBlackscreenFix);
    Loader::AddPublish(Loader::Entry::Stability, 5, PublishCrashMsgFix);
    Loader::AddPublish(Loader::Entry::Stability, 10, PublishBadPlayerModelCrashFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 0, PublishCollisionDeadlockFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 11, PublishFreezeCarFix);
    Loader::AddPublish(Loader::Entry::Graphics, 4, PublishSuperLODFix);
    Loader::AddPublish(Loader::Entry::Misc, 2, PublishPatch4RemoveModChecks);

    Loader::AddInstall(Loader::Entry::Stability, 0, InstallMPNikoCrashFix);
    Loader::AddInstall(Loader::Entry::Stability, 1, InstallBlackscreenFix);
    Loader::AddInstall(Loader::Entry::Stability, 2, InstallCrashMsgFix);
    Loader::AddInstall(Loader::Entry::Stability, 4, InstallGroupHackFix);
    Loader::AddInstall(Loader::Entry::Stability, 5, InstallPathNodeCrashFix);
    Loader::AddInstall(Loader::Entry::Stability, 13, InstallRemoveAllWeaponsProtection);
    Loader::AddInstall(Loader::Entry::Stability, 15, InstallAimProjectileCrashFix);
    Loader::AddInstall(Loader::Entry::Stability, 16, InstallBadPlayerModelCrashFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 1, InstallCollisionDeadlockFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 12, InstallTeleportProtection);
    Loader::AddInstall(Loader::Entry::Vehicles, 21, InstallFreezeCarFix);
    Loader::AddInstall(Loader::Entry::Graphics, 14, InstallSuperLODFix);
    Loader::AddInstall(Loader::Entry::Loading, 3, InstallRemoveGFWLUpdatePopup);
    Loader::AddInstall(Loader::Entry::Loading, 4, InstallRemoveRGSCLoginRequirement);
    Loader::AddInstall(Loader::Entry::Audio, 2, InstallBetterMPSync);
    Loader::AddInstall(Loader::Entry::Audio, 3, InstallIncreasePtrNodes);
    Loader::AddInstall(Loader::Entry::Misc, 1, InstallPatch6and7RemoveModChecks);
    Loader::AddInstall(Loader::Entry::Misc, 4, InstallPatch4RemoveModChecks);
}


// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

static __declspec(naked) void BetterMPSyncBody()
{
    __asm
    {
        xor eax, eax
        ret
    }
}

static __declspec(naked) void CollisionDeadlockShim()
{
    __asm
    {
        mov DWORD PTR [zpatch_collision_iterations], 0
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        push ecx
        mov eax, DWORD PTR [ebp + 0xC]
        movzx ecx, WORD PTR [eax + 0x66]
        push ebx
        mov ebx, DWORD PTR [ebp + 8]
        mov edx, DWORD PTR [ebx + 0x70]
        push esi
        push edi
        movzx edi, WORD PTR [edx + ecx * 8]
        cmp di, 0FFFFh
        jz collision_walk_done
    collision_walk_next:
        mov ecx, DWORD PTR [ebx + 0x70]
        movzx eax, di
        movzx edx, WORD PTR [ecx + eax * 8]
        lea esi, [ecx + eax * 8]
        mov eax, DWORD PTR [esi + 4]
        and eax, 0FFFFFFF0h
        cmp dx, 0FFFFh
        jz collision_walk_no_neighbour
        movzx edx, dx
        mov ecx, DWORD PTR [ecx + edx * 8 + 4]
        and ecx, 0FFFFFFF0h
        prefetcht0 BYTE PTR [ecx]
    collision_walk_no_neighbour:
        mov ecx, DWORD PTR [esi + 4]
        and ecx, 3
        mov edx, 1
        shl edx, cl
        push edx
        mov edx, DWORD PTR [ebp + 0xC]
        call DWORD PTR [zpatch_collision_should_walk]
        test al, al
        jnz collision_walk_found
        movzx edi, WORD PTR [esi]
        add DWORD PTR [zpatch_collision_iterations], 1
        cmp DWORD PTR [zpatch_collision_iterations], 2710h
        jz collision_walk_done
        cmp di, 0FFFFh
        jnz collision_walk_next
    collision_walk_done:
        or ax, 0FFFFh
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    collision_walk_found:
        mov ax, di
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    }
}

static __declspec(naked) void SpawnsNameEntry()
{
    __asm
    {
        push ecx
        call SpawnsModelName
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsWhoEntry()
{
    __asm
    {
        push edx
        call SpawnsWho
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsPrintEntry()
{
    __asm
    {
        push edx
        call SpawnsPrint
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsFormatEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_format_entry]
    }
}

static __declspec(naked) void SpawnsGuardBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp DWORD PTR [zpatch_spawns_skip], 0
        jne spawns_guard_taken
        push DWORD PTR [ebp + 10h]
        mov eax, DWORD PTR [zpatch_spawns_orig_b]
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        call eax
        mov DWORD PTR [zpatch_spawns_skip], 0
        pop ebp
        ret 0Ch
    spawns_guard_taken:
        xor eax, eax
        mov DWORD PTR [zpatch_spawns_skip], eax
        pop ebp
        ret 0Ch
    }
}

static __declspec(naked) void SpawnsProbeBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 4Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 8], eax
        push esi
        mov esi, DWORD PTR [zpatch_spawns_ped_array]
        xor eax, eax
        push edi
        mov edi, DWORD PTR [ebp + 8]
        __emit 0x0F
        __emit 0x1F
        __emit 0x00
    spawns_walk:
        mov ecx, DWORD PTR [esi + eax * 4]
        test ecx, ecx
        je spawns_walk_next
        cmp DWORD PTR [ecx + 3Ch], edi
        je spawns_found
    spawns_walk_next:
        inc eax
        cmp eax, 7918h
        jl spawns_walk
        or eax, 0FFFFFFFFh
    spawns_dispatch:
        mov ecx, DWORD PTR [zpatch_spawns_kind]
        cwde
        mov DWORD PTR [zpatch_spawns_kind], 0FFFFFFFFh
        mov esi, DWORD PTR [esi + eax * 4]
        cmp eax, -1
        je spawns_original
        test esi, esi
        je spawns_original
        cmp esi, 0Ch
        je spawns_original
        cmp esi, 0CDCDCDCDh
        je spawns_original
        mov eax, DWORD PTR [esi]
        test eax, eax
        je spawns_original
        cmp eax, 0Ch
        je spawns_original
        cmp eax, 0CDCDCDCDh
        je spawns_original
        test ecx, ecx
        jne spawns_case_heli
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_noncar
        cmp DWORD PTR [esi + 6Ch], 0
        jne spawns_noncar
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_car
        jmp spawns_print_spawned
    spawns_found:
        movzx eax, ax
        jmp spawns_dispatch
    spawns_noncar:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_noncar
        jmp spawns_print_plain
    spawns_case_heli:
        cmp ecx, 1
        jne spawns_case_boat
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonheli
        cmp DWORD PTR [esi + 6Ch], 4
        jne spawns_nonheli
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_heli
        jmp spawns_print_spawned
    spawns_nonheli:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonheli
        jmp spawns_print_plain
    spawns_case_boat:
        cmp ecx, 2
        jne spawns_case_bike
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonboat
        cmp DWORD PTR [esi + 6Ch], 2
        jne spawns_nonboat
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_boat
        jmp spawns_print_spawned
    spawns_nonboat:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonboat
        jmp spawns_print_plain
    spawns_case_bike:
        cmp ecx, 6
        jne spawns_case_train
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonbike
        cmp DWORD PTR [esi + 6Ch], 1
        jne spawns_nonbike
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_bike
        jmp spawns_print_spawned
    spawns_nonbike:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonbike
        jmp spawns_print_plain
    spawns_case_train:
        cmp ecx, 7
        jne spawns_case_plane
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nontrain
        cmp DWORD PTR [esi + 6Ch], 3
        jne spawns_nontrain
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_train
        jmp spawns_print_spawned
    spawns_nontrain:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nontrain
        jmp spawns_print_plain
    spawns_case_plane:
        cmp ecx, 8
        jne spawns_case_ped
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonplane
        cmp DWORD PTR [esi + 6Ch], 5
        jne spawns_nonplane
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_plane
        jmp spawns_print_spawned
    spawns_nonplane:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonplane
        jmp spawns_print_plain
    spawns_case_ped:
        cmp ecx, 3
        jne spawns_case_object
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 6
        je spawns_ped_ok
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonped
        jmp spawns_print_plain
    spawns_ped_ok:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_ped
        jmp spawns_print_spawned
    spawns_case_object:
        cmp ecx, 4
        jne spawns_case_vehicleobject
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonvehicleobject
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_vehicleobject
        jmp spawns_print_plain
    spawns_nonvehicleobject:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_object
        jmp spawns_print_spawned
    spawns_case_vehicleobject:
        cmp ecx, 5
        jne spawns_original
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 6
        je spawns_generic
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_used_nonped
    spawns_print_plain:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call SpawnsFormatEntry
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call SpawnsPrintEntry
    spawns_suppress:
        mov DWORD PTR [zpatch_spawns_skip], 1
        xor eax, eax
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 8]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    spawns_generic:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_generic
    spawns_print_spawned:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call SpawnsFormatEntry
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call SpawnsPrintEntry
    spawns_original:
        mov eax, DWORD PTR [zpatch_spawns_orig_a]
        push edi
        call eax
        mov ecx, DWORD PTR [ebp - 8]
        add esp, 4
        xor ecx, ebp
        pop edi
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

#define ZP_SPAWNS_CASE(name, selector, resume)                 \
    static __declspec(naked) void name()                       \
    {                                                          \
        __asm push ebp                                         \
        __asm mov ebp, esp                                     \
        __asm mov ecx, DWORD PTR [ebp + 0Ch]                   \
        __asm movsx eax, cl                                    \
        __asm mov DWORD PTR [zpatch_spawns_kind], selector     \
        __asm mov DWORD PTR [zpatch_spawns_class], eax         \
        __asm mov DWORD PTR [ebp + 0Ch], ecx                   \
        __asm pop ebp                                          \
        __asm jmp DWORD PTR [resume]                           \
    }

ZP_SPAWNS_CASE(SpawnsCase0x24Body, 5, zpatch_spawns_case0_resume)

ZP_SPAWNS_CASE(SpawnsCase0x3DBody, 3, zpatch_spawns_case1_resume)

ZP_SPAWNS_CASE(SpawnsCase0x56Body, 3, zpatch_spawns_case2_resume)

ZP_SPAWNS_CASE(SpawnsCase0x6FBody, 0, zpatch_spawns_case3_resume)

ZP_SPAWNS_CASE(SpawnsCase0x88Body, 4, zpatch_spawns_case4_resume)

ZP_SPAWNS_CASE(SpawnsCase0x9EBody, 6, zpatch_spawns_case5_resume)

ZP_SPAWNS_CASE(SpawnsCase0xB4Body, 7, zpatch_spawns_case6_resume)

ZP_SPAWNS_CASE(SpawnsCase0xCABody, 1, zpatch_spawns_case7_resume)

ZP_SPAWNS_CASE(SpawnsCase0xE0Body, 2, zpatch_spawns_case8_resume)

ZP_SPAWNS_CASE(SpawnsCase0xF6Body, 8, zpatch_spawns_case9_resume)

#undef ZP_SPAWNS_CASE


static __declspec(naked) void SuperLodModel()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        lea eax, [ebp - 8]
        push eax
        push 0AE4B15D6h
        call DWORD PTR [zpatch_super_lod_lookup]
        mov eax, DWORD PTR [ebp + 8]
        add esp, 8
        mov ecx, DWORD PTR [ebp - 4]
        movsx eax, WORD PTR [eax + 2Eh]
        cmp eax, DWORD PTR [ebp - 8]
        setz al
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

static __declspec(naked) void SuperLodShim()
{
    __asm
    {
        jmp slod_main
    slod_fail:
        jmp DWORD PTR [zpatch_super_lod_fail]
    slod_main:
        push eax
        push ecx
        push esi
        call SuperLodModel
        add esp, 4
        pop ecx
        test al, al
        pop eax
        jnz slod_ok
        test byte ptr [eax + 72h], 10h
        jz slod_fail
    slod_ok:
        jmp DWORD PTR [zpatch_super_lod_ok]
    }
}

static __declspec(naked) void Patch4StubBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call Patch4Step
        __emit 0x61  // popa
        sub eax, ecx
        sbb edx, esi
        mov DWORD PTR [esp + 0Ch], eax
        mov DWORD PTR [esp + 10h], edx
        jmp DWORD PTR [zpatch_patch4_resume]
    }
}

static __declspec(naked) void GfwlStubBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call GfwlPatch
        __emit 0x61  // popa
        sub eax, ecx
        sbb edx, esi
        mov DWORD PTR [esp + 0Ch], eax
        mov DWORD PTR [esp + 10h], edx
        jmp DWORD PTR [zpatch_gfwl_resume]
    }
}

static __declspec(naked) void RgscLoginRemovedBody()
{
    __asm
    {
        mov al, 1
        ret
    }
}

static __declspec(naked) void AimProjectileShim()
{
    __asm
    {
        cmp ecx, 0
        je aim_done
        mov eax, DWORD PTR [esp + 8]
        mov DWORD PTR [ecx + 0x20], eax
        mov eax, DWORD PTR [esp + 0xc]
        mov DWORD PTR [ecx + 0x24], eax
        neg eax
        sbb eax, eax
        and eax, DWORD PTR [esp + 4]
        mov DWORD PTR [ecx + 0x1c], eax
    aim_done:
        ret 12
    }
}

static __declspec(naked) void CrashMsgShim()
{
    __asm
    {
        call DWORD PTR [zpatch_crash_msg_getter]
        mov dl, BYTE PTR [eax]
        test dl, dl
        jz crash_msg_done
        mov ecx, eax
        __emit 0x66  // xchg ax, ax
        __emit 0x90
    crash_msg_loop:
        cmp dl, 7Eh
        jnz crash_msg_skip
        mov BYTE PTR [ecx], 20h
    crash_msg_skip:
        mov dl, BYTE PTR [ecx + 1]
        inc ecx
        test dl, dl
        jnz crash_msg_loop
    crash_msg_done:
        ret
    }
}

static __declspec(naked) void BlackscreenShim()
{
    __asm
    {
        mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
        mov eax, DWORD PTR [eax]
        cmp eax, 7
        jle blackscreen_resume
        cmp eax, 0Bh
        jl blackscreen_return
    blackscreen_resume:
        jmp DWORD PTR [zpatch_blackscreen_resume]
    blackscreen_return:
        ret
    }
}

static __declspec(naked) void PathNodeShim()
{
    __asm
    {
        cmp eax, 1FFFDFh
        jz path_node_ok
        mov dl, BYTE PTR [eax + 1Fh]
        test dl, 1
        jmp DWORD PTR [zpatch_path_node_resume_fail]
    path_node_ok:
        jmp DWORD PTR [zpatch_path_node_resume_ok]
    }
}

static __declspec(naked) void GroupHackShim()
{
    __asm
    {
        xor eax, eax
        add ecx, 4
        lea edx, [eax + 7]
        cmp ecx, 0Ch
        jz group_hack_empty
        cmp DWORD PTR [ecx], 0
        jmp DWORD PTR [zpatch_group_hack_resume]
    group_hack_empty:
        ret
    }
}

static __declspec(naked) void BpmFormatOptions()
{
    __asm
    {
        mov eax, OFFSET zpatch_bpm_format_options
        ret
    }
}

static __declspec(naked) void BpmFormatEngine()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        push DWORD PTR [ebp + 0Ch]
        push 0
        push DWORD PTR [ebp + 8]
        push edx
        push ecx
        call BpmFormatOptions
        mov ecx, DWORD PTR [eax]
        push DWORD PTR [eax + 4]
        or ecx, 2
        push ecx
        call DWORD PTR [zpatch_bpm_stdio_common_vsprintf]
        or ecx, 0FFFFFFFFh
        add esp, 1Ch
        test eax, eax
        cmovs eax, ecx
        mov esp, ebp
        pop ebp
        ret
    }
}

static __declspec(naked) void BpmFormat()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        mov ecx, DWORD PTR [ebp + 8]
        lea eax, [ebp + 14h]
        mov edx, DWORD PTR [ebp + 0Ch]
        push eax
        push DWORD PTR [ebp + 10h]
        call BpmFormatEngine
        add esp, 8
        pop ecx
        pop ebp
        ret
    }
}

static __declspec(naked) void BpmContextInitEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_bpm_context_init]
    }
}

static __declspec(naked) void BpmInvokeEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_invoke_entry]
    }
}

static __declspec(naked) void BpmCleanupDwordEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_bpm_cleanup_dword]
    }
}

static __declspec(naked) void BpmCallHashEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_bpm_call_hash]
    }
}

static __declspec(naked) void BpmCallBoolEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_bpm_call_bool]
    }
}

static __declspec(naked) void BpmResolverEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_bpm_resolver]
    }
}

static __declspec(naked) void BpmModelNameBody()
{
    __asm
    {
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        __emit 0x66
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x84
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
    bpm_name_walk:
        push DWORD PTR [kBadPlayerModelNames + esi * 4]
        call BpmResolverEntry
        add esp, 4
        cmp eax, edi
        jz bpm_name_found
        inc esi
        cmp esi, 9637h
        jb bpm_name_walk
        pop edi
        mov eax, OFFSET zpatch_bpm_unknown
        pop esi
        ret
    bpm_name_found:
        mov eax, DWORD PTR [kBadPlayerModelNames + esi * 4]
        pop edi
        pop esi
        ret
    }
}

static __declspec(naked) void BpmWhoBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0ACh
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 0A8h], eax
        mov eax, DWORD PTR [zpatch_bpm_salt_c8]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call BpmZeroBlock
        add esp, 0Ch
        lea esi, [esp + 30h]
        mov edi, 4
    bpm_who_records:
        mov ecx, esi
        call BpmContextInitEntry
        add esi, 10h
        sub edi, 1
        jnz bpm_who_records
        mov ecx, DWORD PTR [esp + 0Ch]
        lea eax, [esp + 70h]
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], edi
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 14h], 1
        call BpmInvokeEntry
        mov edx, DWORD PTR [esp + 1Ch]
        test edx, edx
        je bpm_who_done
    bpm_who_copy:
        dec edx
        mov DWORD PTR [esp + 1Ch], edx
        mov ecx, DWORD PTR [esp + edx * 4 + 20h]
        add edx, edx
        mov eax, DWORD PTR [esp + edx * 8 + 30h]
        mov DWORD PTR [ecx], eax
        mov eax, DWORD PTR [esp + edx * 8 + 34h]
        mov DWORD PTR [ecx + 4], eax
        mov eax, DWORD PTR [esp + edx * 8 + 38h]
        mov DWORD PTR [ecx + 8], eax
        mov edx, DWORD PTR [esp + 1Ch]
        test edx, edx
        jne bpm_who_copy
    bpm_who_done:
        mov ecx, DWORD PTR [esp + 0B4h]
        mov eax, DWORD PTR [esp + 70h]
        pop edi
        pop esi
        pop ebx
        xor ecx, esp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

static __declspec(naked) void BpmPrintBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0ACh
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 0A8h], eax
        push ebx
        push esi
        push edi
        push 0A0h
        lea eax, [esp + 14h]
        mov ebx, edx
        push 0
        push eax
        call BpmZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
        // The original's loop alignment (0F 1F 40 00), emitted as bytes so the
        // assembler cannot choose a shorter nop.
        __emit 0x0F
        __emit 0x1F
        __emit 0x40
        __emit 0x00
    bpm_print_records:
        mov ecx, edi
        call BpmContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz bpm_print_records
        lea eax, [esp + 70h]
        mov DWORD PTR [esp + 1Ch], esi
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov ecx, 79023CFCh
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 14h], 1
        call BpmInvokeEntry
        lea ecx, [esp + 10h]
        call BpmCleanupDwordEntry
        mov ecx, DWORD PTR [esp + 0B4h]
        pop edi
        pop esi
        pop ebx
        xor ecx, esp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

static __declspec(naked) void BpmCheckBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 48h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov ecx, DWORD PTR [zpatch_bpm_ped_array]
        mov eax, DWORD PTR [zpatch_bpm_model_index]
        push esi
        mov esi, DWORD PTR [ecx + eax * 4]
        test esi, esi
        je bpm_check_done
        mov eax, DWORD PTR [esi]
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 6
        je bpm_check_done
        push OFFSET zpatch_bpm_gtac
        call DWORD PTR [zpatch_bpm_getmodulehandle]
        test eax, eax
        je bpm_check_multiplayer
        mov ecx, DWORD PTR [esi + 3Ch]
        call BpmModelNameBody
        mov edx, DWORD PTR [zpatch_bpm_obj]
        mov esi, eax
        call BpmWhoBody
        push esi
        push eax
        push OFFSET zpatch_bpm_format
        lea eax, [ebp - 44h]
        push 40h
        push eax
        call BpmFormat
        add esp, 14h
        lea edx, [ebp - 44h]
        call BpmPrintBody
    bpm_check_multiplayer:
        push OFFSET zpatch_bpm_multiplayer
        call BpmResolverEntry
        add esp, 4
        mov esi, eax
        mov edx, esi
        call BpmCallBoolEntry
        test al, al
        jne bpm_check_lookup
        push 0
        push esi
        call DWORD PTR [zpatch_bpm_fn_59c74]
        mov ecx, DWORD PTR [zpatch_bpm_salt_40]
        add esp, 8
        call BpmCallHashEntry
    bpm_check_lookup:
        lea eax, [ebp - 48h]
        mov DWORD PTR [ebp - 48h], 0FFFFFFFFh
        push eax
        push esi
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [ebp - 48h]
        add esp, 8
        mov DWORD PTR [zpatch_bpm_model_index], eax
    bpm_check_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

static __declspec(naked) void BpmTrampoline()
{
    __asm
    {
        push edi
        mov edi, DWORD PTR [ebp + 0Ch]
        mov DWORD PTR [zpatch_bpm_obj], edi
        mov edi, DWORD PTR [ebp + 10h]
        mov DWORD PTR [zpatch_bpm_model_index], edi
        __emit 0x60  // pushad (pushal)
        call BpmCheckBody
        __emit 0x61  // popad (popal)
        mov edi, DWORD PTR [zpatch_bpm_model_index]
        mov DWORD PTR [ebp + 10h], edi
        pop edi
        and esp, 0FFFFFFF0h
        sub esp, 54h
        xorps xmm0, xmm0
        push ebx
        jmp DWORD PTR [zpatch_bpm_resume]
    }
}
