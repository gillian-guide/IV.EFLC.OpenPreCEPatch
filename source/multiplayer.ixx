module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h),
// the variadic list BadPlayerModelCrashFix's rebuilt formatter takes, and the CRT
// calls the diagnostic makes.
#include <intrin.h>
#include <algorithm>
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
import options;
import patch;
import shared;

// MULTIPLAYER: the shipped INI's [MULTIPLAYER] section. The old handler modules
// had these keys spread across `stability` (the branch shims, the model crash
// fix), `vehicles` (FreezeCarFix, CollisionDeadlockFix, TeleportProtection, the
// spawn probe), `audio` (BetterMPSync, IncreasePtrNodes), `misc` (the mod-check
// stubs), `loading` (the GFWL/RGSC gates) and `graphics` (SuperLODFix).
export namespace Multiplayer
{
    void Prepare();

    void PublishGroupHackFix();
    void PublishPathNodeCrashFix();
    void PublishBlackscreenFix();
    void PublishCrashMsgFix();
    void PublishBadPlayerModelCrashFix();
    void PublishCollisionDeadlockFix();
    void PublishFreezeCarFix();
    void PublishPatch4RemoveModChecks();
    void PublishSuperLODFix();

    void InstallMPNikoCrashFix();
    void InstallBlackscreenFix();
    void InstallGroupHackFix();
    void InstallPathNodeCrashFix();
    void InstallCrashMsgFix();
    void InstallRemoveAllWeaponsProtection();
    void InstallAimProjectileCrashFix();
    void InstallBadPlayerModelCrashFix();
    void InstallCollisionDeadlockFix();
    void InstallFreezeCarFix();
    void InstallTeleportProtection();
    void InstallPatch6and7RemoveModChecks();
    void InstallPatch4RemoveModChecks();
    void InstallRemoveGFWLUpdatePopup();
    void InstallRemoveRGSCLoginRequirement();
    void InstallSuperLODFix();
    void InstallBetterMPSync();
    void InstallIncreasePtrNodes();
}

// BadPlayerModelCrashFix's diagnostic hashes the model it could not resolve
// against the original ASI's own 38,455-entry name table (0x100DE770) and prints
// the matching name, so the names are behaviour. The list is recovered data in the
// table's own order; source/model_names.inc carries the provenance and the
// extraction recipe. The transcribed lookup (BpmModelNameBody, 0x10003F00) reads
// this array through the original's indexed absolute operand, and the arm registers
// the array with Bodies::RegisterTable so the whole-body proof compares the table's
// contents instead of its displacement.
extern const char* const kBadPlayerModelNames[0x9637] = {
#include "model_names.inc"
};
static_assert(sizeof(kBadPlayerModelNames) / sizeof(kBadPlayerModelNames[0]) == 0x9637,
    "the recovered model-name table is the original's 38455 entries");

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).
extern "C"
{
    uint32_t zpatch_group_hack_resume = 0;      // dword_1015AD60
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
    // loader preamble substitutes the fallback set, reproduced by the arm.
    uint32_t zpatch_bpm_salt_c8 = 0x570F5725u;  // dword_101580C8
    uint32_t zpatch_bpm_salt_40 = 0x4BF36A32u;  // dword_10158040

    // The original's IAT slot for GetModuleHandleW (0x10063074), which its
    // GTAC.dll test calls through.
    uint32_t zpatch_bpm_getmodulehandle = 0;

    // The pieces of the native-call layer the transcribed wrappers call: their
    // addresses are published here by the arm, because a module interface cannot
    // export a naked function.
    uint32_t zpatch_bpm_context_init = 0;   // sub_100038C0 (source/native.ixx)
    uint32_t zpatch_bpm_cleanup_dword = 0;  // sub_1005DE40 (source/native.ixx)
    uint32_t zpatch_bpm_call_hash = 0;      // sub_1005B150 (source/native.ixx)
    uint32_t zpatch_bpm_call_bool = 0;      // sub_1005B3F0 (source/native.ixx)
    uint32_t zpatch_bpm_resolver = 0;       // sub_10003E50 (source/shared.ixx)

    extern uintptr_t zpatch_native_invoke_entry;
    extern uintptr_t zpatch_model_lookup;

    // The collision body's node-walk test and the deadlock counter it compares.
    uint32_t zpatch_collision_should_walk = 0;  // dword_1015BDD4
    uint32_t zpatch_collision_iterations = 0;   // dword_1015BDD8

    // Patch4RemoveModChecks: dword_10159C18, the continuation (match + 12) of the
    // game's date arithmetic. The RemoveGFWLUpdatePopup arm carries the same
    // original slot, and each arm publishes its own copy because the original's
    // handler writes the same word from its two branches.
    uint32_t zpatch_patch4_resume = 0;
}

// RemoveGFWLUpdatePopup's published state: dword_10159C18 (the stub's resume) and
// dword_10159C10 (the xlive.dll module the original's loader captures).
extern "C"
{
    uint32_t zpatch_gfwl_resume = 0;
    uint32_t zpatch_xlive_base = 0;
}

// FreezeCarFix / DebugPrintSpawns' published state. The state words keep the
// original's on-disk seeds: the selector starts at -1, the suppress flag at 0, and
// the six resume words are published before the sites are written.
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
    // byte_10159C15: DebugPrintSpawns, read only inside the FreezeCarFix gate.
    uint8_t zpatch_spawns_debug = 0;
    // sub_10003E50 (source/shared.ixx), the original's own model-name hash.
    uint32_t zpatch_spawns_resolver = 0;
    // dword_1015AD78: the ped-model object array the probe walks.
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

// SuperLODFix's published state: the two resumes the shim tails into
// (dword_101580AC and dword_101580B0) and the lookup the model helper calls
// (dword_101580B4).
extern "C"
{
    uint32_t zpatch_super_lod_fail = 0;    // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;      // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;  // dword_101580B4

    // The bounded formatter's entry, published by `shared` (sub_10001280). The BPM
    // diagnostic calls it through a forwarder because a module interface cannot
    // name another unit's naked body.
    extern uint32_t zpatch_format_entry;
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

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void AimProjectileShim();
static void GroupHackShim();
static void PathNodeShim();
static void BlackscreenShim();
static void CrashMsgShim();
static void BpmFormatEntry();
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
static void CollisionDeadlockShim();
static void Patch4StubBody();
static void GfwlStubBody();
static void RgscLoginRemovedBody();
static void SuperLodModel();
static void SuperLodShim();
static void BetterMPSyncBody();

void __cdecl BpmZeroBlock(void* destination, int value, unsigned int count);

// The block zeroing the wrappers open with. The original calls the CRT `memset`
// through its IAT thunk (0x10060D6E) with the same argument order; the inline
// assembler rejects a direct call to the intrinsic's name, so the call names this
// cdecl forwarder, which is still one `E8`.
static void __cdecl BpmZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

namespace
{
    // The thunk the CrashMsgFix anchor's -4 points at. The operand is a 32-bit VA of
    // the image *as loaded*: the loader has already applied the image's relocations
    // before this runs, so it is a live address and the cast is the whole
    // conversion. The offline dump relocates its own copy (tests/dump_main.cpp), so
    // the same identity holds there.
    std::byte* CrashMsgSite()
    {
        auto pattern = find_pattern("68 6A 10 4E 31");
        if (pattern.empty())
            return nullptr;

        return reinterpret_cast<std::byte*>(
            static_cast<uintptr_t>(*pattern.get_first<uint32_t>(-4)));
    }

    // The three simple branch arms whose recovered bodies live here. The old shared
    // table is split by key: each section module installs its own arms and the
    // registration driver keeps the table's original order.
    struct BranchArm
    {
        const char* key;
        const char* signature;
        int offset;
        void (*body)();
        uint32_t reference;
    };

    constexpr BranchArm kBlackscreenArm{
        "BlackscreenFix", "75 54 8B 48 28 8B 40 2C", 81, &BlackscreenShim, 0x10036EE0u };
    constexpr BranchArm kGroupHackArm{
        "GroupHackFix", "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04", 0, &GroupHackShim,
        0x10036E90u };
    constexpr BranchArm kPathNodeArm{
        "PathNodeCrashFix", "8B 04 B1 0F B7 D0 81 FA FF FF 00 00", 53, &PathNodeShim,
        0x1003D7B0u };

    void InstallBranchArm(const BranchArm& arm)
    {
        if (!Loader::Enabled(arm.key))
            return;
        auto pattern = find_pattern(arm.signature);
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(arm.body), arm.reference);
        injector::MakeJMP(pattern.get_first(arm.offset), arm.body, true);
    }

    // A rel32 target inside the match: the displacement dword at `displacement`,
    // measured from the instruction's last byte at `next`.
    uint32_t Rel32Target(std::byte* match, std::ptrdiff_t displacement, std::ptrdiff_t next)
    {
        const auto displacementValue = *reinterpret_cast<const int32_t*>(match + displacement);
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(match + next) + static_cast<uint32_t>(displacementValue));
    }

    // OpenPreCEPatch-only keys have no recovered catalogue row (the 99-key table
    // is the original's INI), so they are read directly. The read keeps the
    // original's gate: the stored value must be exactly 1.
    bool OpenOptionEnabled(std::string_view key, std::string_view fallback)
    {
        return Config::Integer(Config::Ini(), key, fallback) == 1;
    }

    // RemoveModChecks is OpenPreCEPatch's unified key: it replaces the three
    // recovered per-patch keys without making an old INI unreadable.
    bool ModChecksEnabled()
    {
        return Loader::Enabled("Patch4RemoveModChecks") ||
               Loader::Enabled("Patch6and7RemoveModChecks") ||
               Loader::Enabled("Patch7RemoveModChecks") ||
               OpenOptionEnabled("RemoveModChecks", "0");
    }

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
    constexpr SuperLodTargets kSuperLodTargets[] = {
        { Build::v1070, 0x4A3D37u, 0x4A3CEFu, 0x58AAE0u },
        { Build::v1080, 0x540E67u, 0x540E1Fu, 0x4DD2D0u },
    };

    // The mod-check stub's write table, per build.
    constexpr uint32_t kModChecksEntryJmp1060 = 0x003A09u;
    constexpr uint32_t kModChecksMidJmp1070 = 0x0038AEu;
    constexpr uint32_t kModChecksCaseJmp1070 = 0x0039E9u;

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

    // The model-name table index used when the `model_index` group is on. The
    // original walks all 38,455 names per diagnostic; the table is static, so
    // the index is built once and binary-searched. The resolver is the same
    // sub_10003E50 forwarder the body calls, so the hashes are identical.
    struct ModelNameIndexEntry
    {
        uint32_t hash;
        const char* name;
    };

    constexpr size_t kModelNameIndexCapacity = 0x9637;
    ModelNameIndexEntry g_modelNameIndex[kModelNameIndexCapacity] = {};
    size_t g_modelNameIndexCount = 0;
    volatile LONG g_modelNameIndexState = 0;  // 0 uninitialised, 1 building, 2 ready
    uintptr_t g_modelNameIndexResolverForTest = 0;

    bool BuildModelNameIndex() noexcept
    {
        for (;;)
        {
            const LONG observed = InterlockedCompareExchange(&g_modelNameIndexState, 1, 0);
            if (observed == 2)
                return g_modelNameIndexCount != 0;
            if (observed == 1)
            {
                Sleep(0);
                continue;
            }

            const auto resolve = reinterpret_cast<uint32_t(__cdecl*)(const char*)>(
                g_modelNameIndexResolverForTest != 0 ? g_modelNameIndexResolverForTest
                                                     : zpatch_bpm_resolver);
            if (resolve == nullptr)
            {
                InterlockedExchange(&g_modelNameIndexState, 0);
                return false;
            }

            const size_t count = sizeof(kBadPlayerModelNames) / sizeof(kBadPlayerModelNames[0]);
            for (size_t index = 0; index < count; ++index)
                g_modelNameIndex[index] = { resolve(kBadPlayerModelNames[index]),
                    kBadPlayerModelNames[index] };
            // Stable: duplicate hashes keep the table's original first-match order.
            std::stable_sort(g_modelNameIndex, g_modelNameIndex + count,
                [](const ModelNameIndexEntry& left, const ModelNameIndexEntry& right)
                { return left.hash < right.hash; });
            g_modelNameIndexCount = count;
            InterlockedExchange(&g_modelNameIndexState, 2);
            return true;
        }
    }

    // sub_10003F00's contract: ECX is the hash, EAX the name. A miss answers the
    // same "**UNKNOWN**" the transcribed body does.
    static const char* __fastcall BpmModelNameCached(uint32_t hash) noexcept
    {
        if (!BuildModelNameIndex())
            return zpatch_bpm_unknown;

        size_t low = 0;
        size_t high = g_modelNameIndexCount;
        while (low < high)
        {
            const size_t middle = low + (high - low) / 2;
            if (g_modelNameIndex[middle].hash < hash)
                low = middle + 1;
            else
                high = middle;
        }
        if (low < g_modelNameIndexCount && g_modelNameIndex[low].hash == hash)
            return g_modelNameIndex[low].name;
        return zpatch_bpm_unknown;
    }

    void ResetModelNameIndexStateForTest() noexcept
    {
        g_modelNameIndexCount = 0;
        g_modelNameIndexResolverForTest = 0;
        InterlockedExchange(&g_modelNameIndexState, 0);
    }

    void SetModelNameIndexResolverForTestImpl(uintptr_t resolver) noexcept
    {
        ResetModelNameIndexStateForTest();
        g_modelNameIndexResolverForTest = resolver;
    }

    const char* ModelNameIndexedForTestImpl(uint32_t hash) noexcept
    {
        return BpmModelNameCached(hash);
    }

// Patch4RemoveModChecks: the game's date arithmetic at 1.0.8.0 game 0x7CBF70
// (the `2B C1 1B D6 ...` anchor) is replaced by the original's sub_100054E0, an
// 8-instruction stub that runs the shared flag-gated helper between pusha/popa,
// replays the 64-bit subtraction and its two stack stores, and resumes at
// match + 12.
//
// The helper (sub_10005340, 145 instructions) has two branches. The GFWL one is
// RemoveGFWLUpdatePopup's and lives in this module too; this module owns the
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

namespace Multiplayer
{
    void Prepare()
    {
        // The GFWL stub, the RGSC login body and the Patch4 stub are branch targets
        // of this section's arms, so they carry their recovered VAs from the start.
        // The performance module's allocator vtable also names the RGSC body (the
        // original's table does), so the registration is unconditional.
        Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&Patch4StubBody), 0x100054E0u);
    }
}

namespace Multiplayer
{

    // GroupHackFix's published resume: the anchor's +11 (the loop's `jnz`).
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

    // PathNodeCrashFix's published resumes: the anchor itself (the walk's next
    // iteration) and the anchor's +59 (the `jz` the original test sets the flags
    // for).
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

    // BlackscreenFix's published state: the resume is the tail jump's own target and
    // the value slot is the dword the resume's first instruction loads. Both are read
    // from the site before the install overwrites the jump.
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

    // CrashMsgFix's published getter: the target of the branch at the thunk's entry,
    // whose displacement is at +1 and is measured from +5.
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

    // BadPlayerModelCrashFix's published state, read before the install overwrites
    // the site: the resume is the site's +10 (the `mov ebx,[ebp+0Ch]` the jump
    // covers). The rest is what the original's unconditional loader preamble
    // resolves on every run and this key's bodies read -- the ped array, the
    // model-class lookup, and the three hash salts whose build probe decides - plus
    // the addresses of the layer pieces the transcribed wrappers call.
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
        zpatch_bpm_context_init = static_cast<uint32_t>(Bodies::AddressFor(0x100038C0u));
        zpatch_bpm_cleanup_dword = static_cast<uint32_t>(Bodies::AddressFor(0x1005DE40u));
        zpatch_bpm_call_hash = static_cast<uint32_t>(Bodies::AddressFor(0x1005B150u));
        zpatch_bpm_call_bool = static_cast<uint32_t>(Bodies::AddressFor(0x1005B3F0u));
        zpatch_bpm_resolver = static_cast<uint32_t>(Shared::ValueResolver());
    }

    // MPNikoCrashFix: a `jz` in the player-model check becomes an unconditional
    // jump, so a model the game cannot resolve no longer falls through.
    void InstallMPNikoCrashFix()
    {
        if (!Loader::Enabled("MPNikoCrashFix"))
            return;

        auto pattern = find_pattern("74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);
    }

    // CrashMsgFix: the thunk loses its branch to the message getter; a call into
    // the shim replaces it, and the shim calls the getter itself, replaces the
    // '~' format markers in place and returns after the thunk's call.
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

    // RemoveAllWeaponsProtection: the 12-byte block the anchor jumps over is deleted,
    // so the weapon-clearing path it guarded is no longer taken.
    void InstallRemoveAllWeaponsProtection()
    {
        if (!Loader::Enabled("RemoveAllWeaponsProtection"))
            return;

        auto pattern = find_pattern("E9 ? ? ? ? 57 8D 8F ? ? 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(5), 12, true);
    }

    // AimProjectileCrashFix: the site jumps into this module's shim (recovered VA
    // 0x10005590), which stores the caller's dwords and derives the flag.
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

        // The payload walks the ped-model array, calls the model-class lookup and
        // the native layer's four transcribed wrappers, and the trampoline
        // tail-jumps through the published resume. Every one is a game or layer
        // address the original's preamble resolved; a miss is a null call or a
        // jump to zero at the first model resolution.
        if (Failsafe::Missing("BadPlayerModelCrashFix model table",
                zpatch_bpm_ped_array == 0) ||
            Failsafe::Missing("BadPlayerModelCrashFix model lookup",
                zpatch_model_lookup == 0) ||
            Failsafe::Missing("BadPlayerModelCrashFix script detour",
                zpatch_bpm_fn_59c74 == 0) ||
            Failsafe::Missing("BadPlayerModelCrashFix layer",
                zpatch_bpm_context_init == 0 || zpatch_bpm_cleanup_dword == 0 ||
                    zpatch_bpm_call_hash == 0 || zpatch_bpm_call_bool == 0 ||
                    zpatch_bpm_resolver == 0 || zpatch_bpm_resume == 0))
            return;

        auto pattern = find_pattern("83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C");
        if (pattern.empty())
            return;
        if (!Failsafe::Body("BadPlayerModelCrashFix body",
                reinterpret_cast<uintptr_t>(&BpmTrampoline)))
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
        // operand provable.
        Bodies::RegisterTable(reinterpret_cast<uintptr_t>(kBadPlayerModelNames),
            0x100DE770u, sizeof(kBadPlayerModelNames));

        injector::MakeJMP(pattern.get_first<std::byte>(0),
            reinterpret_cast<void*>(&BpmTrampoline), true);

        // The diagnostic call inside our own BpmCheckBody is a relative call
        // in this module, not a game-image site. Redirect it to the indexed
        // resolver.
        {
            auto* begin = reinterpret_cast<uint8_t*>(&BpmCheckBody);
            bool patched = false;
            for (size_t offset = 0; offset + 5 <= 0x200 && !patched; ++offset)
            {
                if (begin[offset] != 0xE8)
                    continue;
                int32_t displacement = 0;
                std::memcpy(&displacement, begin + offset + 1, sizeof(displacement));
                const auto target = reinterpret_cast<uintptr_t>(begin + offset + 5) +
                                    static_cast<intptr_t>(displacement);
                if (target != reinterpret_cast<uintptr_t>(&BpmModelNameBody))
                    continue;
                injector::MakeCALL(begin + offset,
                    reinterpret_cast<void*>(&BpmModelNameCached), true);
                patched = true;
            }
            if (!patched)
                Log::Print("model_index: BpmCheckBody call site not found; body scan kept");
        }
    }

    // CollisionDeadlockFix's published walk test: the site's own call target (the
    // disp32 at match+91), read before the install overwrites the entry.
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

    // FreezeCarFix / DebugPrintSpawns: the original's handler 0x10037C70 scans the
    // primary signature first and wraps every other scan in its `if (result)`, so a
    // miss on 1.0.0.0 / 1.0.4.0 / 1.0.0.4 installs nothing at all. The arm keeps
    // that shape: the DebugPrintSpawns flag and the probe's operands are read here,
    // before the install tier writes any site, and every resume is the target of the
    // `E8` the install is about to overwrite.
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

    // CollisionDeadlockFix: the node walk is replaced by its capped twin.
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

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly three
    // bytes -- C2 08 over the opcode and 00 over the byte after the immediate -- and
    // injector::MakeRET is not equivalent: it stores the opcode through a 4-byte int,
    // which would also zero match+3.
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

    // Patch4RemoveModChecks: the anchor's first twelve bytes are the subtraction
    // and the two stores; the stub replays them and resumes at match + 12.
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

    void InstallPatch6and7RemoveModChecks()
    {
        // The original has one handler for two keys: `stoi(ini.get(
        // "Patch6and7RemoveModChecks","0")) == 1 || stoi(ini.get(
        // "Patch7RemoveModChecks","0")) == 1` (0x10053FBD..0x10053FE5), both feeding
        // 0x10044710. Patch7RemoveModChecks is therefore not an alias that does
        // nothing -- it is the alternate trigger for the same full write set, and a
        // Patch7-only INI installs all 16/21 bytes on 1.0.6.0/1.0.7.0.
        if (!ModChecksEnabled())
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

    void InstallPatch4RemoveModChecks()
    {
        // The unified key covers the patch-4 mod-check stub on 1.0.4.0, the build
        // the check exists on; the recovered per-patch key keeps its own gate.
        const bool unified = OpenOptionEnabled("RemoveModChecks", "0") &&
                             Loader::Current() == Build::v1040;
        if (!Loader::Enabled("Patch4RemoveModChecks") && !unified)
            return;

        auto pattern = find_pattern(
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&Patch4StubBody), true);
    }

    // RemoveGFWLUpdatePopup: the anchor's first twelve bytes are the subtraction
    // and its two stores; the stub replays them and resumes at match + 12.
    void InstallRemoveGFWLUpdatePopup()
    {
        if (!Loader::Enabled("RemoveGFWLUpdatePopup"))
            return;

        // dword_10159C10: the xlive.dll module the original's loader captures.
        // xlive.dll can also be Ultimate ASI Loader's proxy; its image is not a
        // GFWL module and the four flag offsets would corrupt the loader.
        const HMODULE xlive = GetModuleHandleA("xlive.dll");
        if (xlive == nullptr || GetProcAddress(xlive, "XLiveInitialize") == nullptr)
            return;
        zpatch_xlive_base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(xlive));

        // The stub replays into xlive.dll through the captured base. Without the
        // module the original writes through zero; keep the game's popup instead.
        if (Failsafe::Missing("RemoveGFWLUpdatePopup xlive.dll", zpatch_xlive_base == 0))
            return;

        auto pattern = find_pattern("2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_gfwl_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 12);
            injector::MakeJMP(match, reinterpret_cast<void*>(&GfwlStubBody), true);
        }
    }

    // RemoveRGSCLoginRequirement: the login-required branch of the RGSC launch
    // path. On 1.0.8.0 the original writes the same five-byte jump twice (the
    // duplicate is kept); the second anchor never matches on that build but does
    // on the ones the key was gated for.
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

    // SuperLODFix's published state: the site's two continuations and the hash lookup
    // the model helper calls, each a game RVA from the recovered handler's own base +
    // RVA pairs (the suffix is the build).
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

    // SuperLODFix: the two-byte pair `test byte [eax+72h], 10h` / `jz` that decides
    // whether the detailed LOD is used is replaced by the model-hash test. The fail
    // path is the `jz` target, the ok path the instruction after the six-byte pair,
    // and the lookup is the game's hash function.
    void InstallSuperLODFix()
    {
        if (!Loader::Enabled("SuperLODFix"))
            return;

        // The body's three published pointers come from the per-build table. A
        // build outside it leaves them zero and the original shim faults on the
        // lookup; the failsafe keeps the game's own LOD test instead.
        if (Failsafe::Missing("SuperLODFix lookup", zpatch_super_lod_lookup == 0) ||
            Failsafe::Missing("SuperLODFix resume",
                zpatch_super_lod_fail == 0 || zpatch_super_lod_ok == 0))
            return;

        auto pattern = find_pattern("F6 40 ? ? 74 ? 85 ED 0F 85 ? ? ? ? 55 6A ? E8 ? ? ? ? 8B F8");
        if (pattern.empty())
            return;
        if (!Failsafe::Site("SuperLODFix site",
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)), 6) ||
            !Failsafe::Body("SuperLODFix body", reinterpret_cast<uintptr_t>(&SuperLodShim)))
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodModel), 0x100469A0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodShim), 0x100469E0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&SuperLodShim), true);
    }

    // BetterMPSync: the sync path's virtual call is the five bytes at the anchor's
    // +6 (`push ebp; mov ecx,esi; call edx`); the call is replaced by a call into
    // the body, which answers 0 and leaves the stack balanced (the argument push
    // and the callee's own pop are both gone).
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

    // IncreasePtrNodes: the three atPool sizes the pointer-node lists allocate.
    // Each site is `push <flags>; push <name>; push <count>`, so the arm rewrites
    // the count at the anchor's +8: 80,000 -> 320,000 for the single-node pool and
    // 16,000 -> 64,000 for the two double-node pools.
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

    void InstallBlackscreenFix() { InstallBranchArm(kBlackscreenArm); }
    void InstallGroupHackFix() { InstallBranchArm(kGroupHackArm); }
    void InstallPathNodeCrashFix() { InstallBranchArm(kPathNodeArm); }

}

// Test seams for the model-name index. They live outside the anonymous
// namespace so the module can export them.
export void ResetModelNameIndexForTest() noexcept
{
    ResetModelNameIndexStateForTest();
}

export void SetModelNameIndexResolverForTest(uintptr_t resolver) noexcept
{
    SetModelNameIndexResolverForTestImpl(resolver);
}

export const char* ModelNameIndexedForTest(uint32_t hash) noexcept
{
    return ModelNameIndexedForTestImpl(hash);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// abi_aim_projectile_crash_fix, recovered VA 0x10005590 (sub_10005590): a
// __thiscall that stores the caller's two dwords into the object in ECX, derives a
// flag from the second and returns with `ret 12`. AimProjectileCrashFix jumps here.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
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

// sub_100057F0, recovered VA 0x100057F0 (3 bytes, 2 instructions): the MP sync
// value. The site is the virtual call the sync path makes; the body answers it
// with 0 instead. Naked and internal.
static __declspec(naked) void BetterMPSyncBody()
{
    __asm
    {
        xor eax, eax
        ret
    }
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
            if (hash_name(kBadPlayerModelNames[index]) == hash)
                return kBadPlayerModelNames[index];
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

// sub_10005340's RemoveGFWLUpdatePopup branch: store the "update disabled" dword
// into the four xlive.dll flag slots. `zpatch_xlive_base` is the module the
// loader captured; when xlive.dll is absent it is null and the write lands on the
// low 0x66F770 page, which is the original's fault (preserved, not guarded).
static void GfwlPatch()
{
    for (const uint32_t offset : { 0x66F770u, 0x66F774u, 0x6B55D0u, 0x6B55F0u })
        injector::WriteMemory<uint32_t>(zpatch_xlive_base + offset, 0x10000E83u, true);
}

// abi_crash_msg_shim, recovered VA 0x10036F00: the message getter is called through
// the published slot, and the '~' the game uses as a format marker is turned into a
// space in the string the getter returned, in place. The `xchg ax, ax` is the
// original's loop alignment, emitted as bytes so the assembler cannot shrink it.
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

// abi_blackscreen_shim, recovered VA 0x10036EE0: the savegame-load state check.
// The arm replaces the tail `jmp` at the scan anchor's +81 with a jump here; the
// original 0x6C02A0 is then reached through the published resume, and reads the
// state dword whose address is published separately (its first instruction is
// `mov eax, ds:<dword>`, so the published word is an address, not a value).
// States 8..10 return directly and the load is skipped; every other state takes
// the original path. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails.
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

// abi_path_node_shim, recovered VA 0x1003D7B0: the path node the walk just read is
// rejected when it is the 0x1FFFDF sentinel (the walk continues at the anchor) and
// otherwise carries its own `mov dl,[eax+1Fh]; test dl,1` into the resumed code,
// which is why the flags are left as the original's. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
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

// abi_group_hack_shim, recovered VA 0x10036E90: the group walk's first step, moved
// into the shim so the compare against the 12-byte table is made before the loop
// body. The `cmp [ecx],0` sets the flags the resume's `jnz` consumes, and the
// tail jump keeps them. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails.
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

// The BPM diagnostic's formatter call. The formatter is a `static` naked body in
// `shared`, and a module interface cannot name another unit's naked function as a
// call target, so the body calls this six-byte forwarder, which jumps through the
// published entry. The call stays one `E8` and the formatter is unchanged.
static __declspec(naked) void BpmFormatEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_format_entry]
    }
}

// The forwarders into the layer's transcribed pieces. A module interface cannot
// export a naked function and a C entry's decorated name is not nameable from
// inline asm, so each call names a six-byte `jmp` through a slot the arm fills from
// the body registry by the piece's recovered VA. The stack and registers reach the
// piece untouched: these instructions touch nothing.
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

// The one-argument native wrapper the fallback calls is the native layer's own
// transcription of sub_1005B3F0 (source/native.ixx), reached through this
// forwarder: the layer resolves the salt the wrapper reads (dword_101580B8) with
// the preamble's probe rule, so a second copy here would resolve the same word a
// second time. This is a call target only -- sub_1005B3F0 is never written into
// the game image -- so the forwarder needs no body-registry entry of its own.
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

// sub_10003F00, recovered VA 0x10003F00 (62 bytes, 21 instructions): the
// name-table walk behind the GTAC.dll diagnostic. The hash arrives in ECX; the body
// scans the original ASI's own 38,455-entry `char*` table (0x100DE770) from index
// 0, hashes each name with sub_10003E50 and answers the first match, or
// "**UNKNOWN**" on a miss.
//
// The two table operands keep the original's absolute indexed shape
// (`push [table + esi*4]`, `mov eax, [table + esi*4]`): the table is recovered
// data (source/model_names.inc, in the table's own order) and this module reads
// its copy. The whole-body proof resolves such an operand through
// Bodies::RegisterTable - registered by the BadPlayerModelCrashFix arm with the
// original's VA and length - and compares the table's contents instead of its
// displacement. Every other instruction is the original's; the body has no frame
// and no /GS cookie.
//
// The 10-byte `nop word ptr [eax+eax+0]` at 0x10003F06 (`66 66 0F 1F 84 00 00 00
// 00 00`) is the original's loop padding. The inline assembler's `nop` is one byte,
// and the body's internal branch targets are proved by their offset from the body
// start, so the original's bytes are emitted.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// sub_1005B290, recovered VA 0x1005B290 (61 instructions): build the 0xA0-byte
// native call block, call the native whose hash is dword_101580C8 with the one
// argument the caller passed in EDX, copy every pending output record out and
// answer args[0] (the "who" name the diagnostic prints). __fastcall-shaped: the
// first register argument is unused and the payload travels in EDX. The /GS frame
// is the original's.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// sub_1005B530, recovered VA 0x1005B530 (45 instructions): the chat print. Same
// block, fixed native hash 0x79023CFC, the message pointer from EDX as its one
// argument, and the dword cleanup (0x1005DE40) instead of the inline copy-out.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// sub_10037AC0, recovered VA 0x10037AC0 (65 instructions): the payload. It reads
// the model the caller named through the ped array, asks the model's own class id
// (the vtable slot at +0Ch) and returns when it is a ped. Otherwise: when GTAC.dll
// is loaded it prints the diagnostic (name lookup, "who" call, formatter, chat
// print -- all behind the original's own module test), then it asks the fixed
// native whether the fallback is needed, calls the model-class lookup when it is
// not, and resolves "m_y_multiplayer" through the shared model lookup, writing the
// index back into the trampoline's slot. Every read is as unguarded as the
// original's; the /GS prologue and epilogue are its own.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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
        call BpmFormatEntry
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

// tramp_BadPlayerModelCrashFix, recovered VA 0x10037BA0 (16 instructions): the
// site's trampoline. The arm's jump replaces the game's `and esp,0FFFFFFF0h`, so
// the body saves the caller's two arguments (entity at [EBP+0Ch], model index at
// [EBP+10h]), runs the payload, writes the possibly-corrected index back into the
// caller's argument slot, replays the prologue the jump overwrote and tail-jumps
// to the published resume (match+10, the `mov ebx,[ebp+0Ch]`).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// The probe's four helper entries. The original calls the compiler-generated
// helpers directly (sub_10003F00 / sub_1005B290 / sub_1005B530 / sub_10001280); a
// module interface cannot export a naked function, so the calls name these
// four-to-six-byte thunks, which reach the rebuilt helper (or the formatter body
// source/shared.ixx already proved) with the stack and registers untouched.
// The call instruction itself stays the original's `E8 rel32`.
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

// sub_100372F0, recovered VA 0x100372F0 (16 instructions): the spawn guard. It
// keeps the original callee's EBP frame and `ret 0Ch` cleanup.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// The ten factory-dispatch thunks, recovered VAs 0x10037330..0x100374E0 (9
// instructions each): the selector the probe dispatches on, the argument's sign
// byte, and the site's own resume. Cases 1 (+0x3D) and 2 (+0x56) both set selector
// 3 in the original bytes; kept 1:1.
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

// sub_10037510, recovered VA 0x10037510 (394 instructions, 0x10037510..0x10037ABC):
// the spawn probe, transcribed instruction for instruction from the original. It
// is reached through nine of the arm's detours; ECX at the vtable +0Ch call is the
// selector, exactly as the original leaves it.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// abi_collision_deadlock_walk, recovered VA 0x10005D80: the whole node-walk of
// sub_4E6250 with an iteration cap in it. The walk is the game's own, instruction
// for instruction -- the only additions are the counter word (zeroed at entry,
// incremented per node, compared against 0x2710) and the early exit when it
// trips. The walk test is called through the published pointer (dword_1015BDD4)
// instead of a direct call, because the call's target is the site's own operand.
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

// sub_100054E0, recovered VA 0x100054E0 (8 instructions). `pusha`/`popa` are
// emitted as bytes: the inline assembler renders both with a 0x66 operand-size
// prefix, which the original does not have.
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

// sub_100054E0, recovered VA 0x100054E0 (8 instructions): the shared
// Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub. It runs the flag-gated
// helper between pusha/popa, replays the 64-bit ECX:ESI subtraction from EDX:EAX
// the site held (and the two stores of its result into the caller's argument
// slots), then tail-jumps to the replayed site's continuation. `pusha`/`popa` are
// emitted as bytes: the inline assembler renders both with a 0x66 operand-size
// prefix, which the original does not have.
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

// sub_10038550, recovered VA 0x10038550 (2 instructions): the RGSC login check's
// replacement. It sets only AL, so the caller sees "already signed in" and the
// upper 24 bits of EAX stay whatever the caller had, exactly as the original.
static __declspec(naked) void RgscLoginRemovedBody()
{
    __asm
    {
        mov al, 1
        ret
    }
}

// sub_100469A0, recovered VA 0x100469A0: the model test the shim calls with the
// ESI argument still on the stack. It resolves the constant 0xAE4B15D6 through
// the published lookup and compares the object's signed word at +0x2E with the
// answer. The frame is the body's own /GS frame: the cookie word and the cookie
// check are the module's, exactly as a compiled function of this module has
// them, and both are absolute/slot operands in the proof.
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

// sub_100469E0, recovered VA 0x100469E0: the body the SuperLODFix arm installs at
// the `test byte [eax+72h], 10h` / `jz` pair. EAX is the object and ESI the model
// the test reads. A model that resolves to the published hash continues at the
// site's continuation (ok); otherwise the object's 0x10 flag decides, and the
// `jz` it would have taken is the site's branch target (fail).
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
