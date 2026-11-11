module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h),
// the variadic list BadPlayerModelCrashFix's rebuilt formatter takes, and the CRT
// calls BpmFormat and BpmZeroBlock make.
#include <intrin.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

export module stability;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;
import shared;

// BadPlayerModelCrashFix's diagnostic hashes the model it could not resolve
// against the original ASI's own 38,455-entry name table (0x100DE770) and prints
// the matching name, so the names are behaviour. The list is recovered data in the
// table's own order; source/model_names.inc carries the provenance and the
// extraction recipe. The transcribed lookup (BpmModelNameBody, 0x10003F00) reads
// this array through the original's indexed absolute operand, and the arm registers
// the array with Bodies::RegisterTable so the whole-body proof compares the table's
// contents instead of its displacement.
static const char* const kBadPlayerModelNames[] = {
#include "model_names.inc"
};
static_assert(sizeof(kBadPlayerModelNames) / sizeof(kBadPlayerModelNames[0]) == 0x9637,
    "the recovered model-name table is the original's 38455 entries");

// Crash fixes. One arm per key, each anchored to its own recovered signature.
// Registration is explicit: the driver calls Register(), so the arm cannot be
// dropped by the linker and the order the arms subscribe in is readable in one
// place.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets below are measured from
// the anchor, which is why they did not change when the strings did.

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).

extern "C"
{
    uint32_t zpatch_freeze_gun_resume = 0;  // dword_1015AD64
    uint32_t zpatch_group_hack_resume = 0;  // dword_1015AD60
    uint32_t zpatch_path_node_resume_ok = 0;    // dword_1015BDFC
    uint32_t zpatch_path_node_resume_fail = 0;  // dword_1015BE00
    uint32_t zpatch_plane_crash_resume_a = 0;   // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;   // dword_1015BE14
    uint32_t zpatch_blackscreen_resume = 0;     // dword_1015AD58
    uint32_t zpatch_blackscreen_value_ptr = 0;  // dword_1015AD5C
    uint32_t zpatch_crash_msg_getter = 0;       // dword_1015AD68
    uint32_t zpatch_wanted_level_resume = 0;      // dword_1015AD6C
    uint32_t zpatch_episode_switch_resume_a = 0;  // dword_10159BBC
    uint32_t zpatch_episode_switch_resume_b = 0;  // dword_10159BB4
    uint32_t zpatch_episode_switch_resume_c = 0;  // dword_10159BB8
    uint32_t zpatch_smpa60_resume_a = 0;          // dword_10159C70
    uint32_t zpatch_smpa60_resume_b = 0;          // dword_10159C68
    uint32_t zpatch_smpa60_resume_c = 0;          // dword_10159C6C

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
    // dword_10159D2C: PlayerAsPedComponentsFix's published resume (source/shared.ixx).
    extern uint32_t zpatch_player_ped_resume;
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
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void AimProjectileShim();
static void Smpa60Shim();
static void Smpa60TailShim();
static void WantedLevelShim();
static void EpisodeSwitchStoreShim();
static void EpisodeSwitchShimB();
static void EpisodeSwitchShimC();
static void EpisodeSwitchSum();
static void EpisodeSwitchVcall();
static void PlaneCrashShim();
static void CrashMsgShim();
static void BlackscreenShim();
static void PathNodeShim();
static void GroupHackShim();
static void FreezeGunShim();
static void BpmFormat();
static void BpmModelNameBody();
static void BpmWhoBody();
static void BpmPrintBody();
static void BpmCheckBody();
static void BpmTrampoline();

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

    // HighFPSSpeedupFix: the nine frame-time clamps inside CTimer::Update. One
    // becomes a short jump (skipping the following test), the other eight are
    // two-byte NOPs.
    struct SpeedupSite final
    {
        ptrdiff_t offset;
        bool shortJump;
    };
    constexpr SpeedupSite kSpeedupSites[] = {
        { -7, false },
        { 3, false },
        { 224, true },
        { 231, false },
        { 367, false },
        { 377, false },
        { 607, false },
        { 614, false },
        { 764, false },
    };

    // The address of a body another module transcribed, by its recovered VA. The
    // native-call layer publishes only its C entries, and a module interface cannot
    // export a naked function, so the body registry is what makes its pieces
    // reachable from here.
    uint32_t RegisteredBody(uint32_t reference)
    {
        for (const auto& entry : Bodies::All())
        {
            if (entry.reference == reference)
                return static_cast<uint32_t>(entry.address);
        }
        return 0;
    }
}

namespace
{
    // FreezeGunFix's published resume: the anchor's -12 (the instruction after the
    // `push edi` the body replays). It reads the site the install overwrites, so it
    // belongs to the publish tier.
    void PublishFreezeGunFix()
    {
        if (!Loader::Enabled("FreezeGunFix"))
            return;

        auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
        if (pattern.empty())
            return;

        zpatch_freeze_gun_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) - 12);
    }

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

    // PlaneCrashFix's published resumes: the normal one is the anchor's +14 (after
    // the vtable read the body replays), the null one is the anchor's -705, the head
    // of the same function.
    void PublishPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_plane_crash_resume_b = static_cast<uint32_t>(match + 14);
        zpatch_plane_crash_resume_a = static_cast<uint32_t>(match - 705);
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

    // WantedLevelCrashFix's published resume: the anchor's +12 (the `jz` its
    // `test al,al` feeds).
    void PublishWantedLevelCrashFix()
    {
        if (!Loader::Enabled("WantedLevelCrashFix"))
            return;

        auto pattern = find_pattern("8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00");
        if (pattern.empty())
            return;

        zpatch_wanted_level_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 12);
    }

    // EpisodeSwitchFreezeFix's published resumes: the ECX reader's +7, the archive
    // store's +5 and the EBX reader's +6, read from the sites the install overwrites.
    void PublishEpisodeSwitchFreezeFix()
    {
        if (!Loader::Enabled("EpisodeSwitchFreezeFix"))
            return;

        auto pattern = find_pattern("56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_c = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 7);

        pattern = find_pattern("89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_a = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 5);

        pattern = find_pattern("8B 4B 10 3B 4B 14 7D 0F");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_b = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 6);
    }

    // SMPA60Fix's published resumes: the tail shim's resume is the primary anchor's
    // +136, and the streaming loop's loader call is the second anchor, whose sentinel
    // path and normal path use +28 and +14.
    void PublishSMPA60Fix()
    {
        if (!Loader::Enabled("SMPA60Fix"))
            return;

        auto pattern = find_pattern("01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        if (pattern.empty())
            return;

        zpatch_smpa60_resume_c = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 136);

        pattern = find_pattern("8B 48 70 83 C0 40 50 51 8D 51 10");
        if (pattern.empty())
            return;

        const auto shim = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_smpa60_resume_a = static_cast<uint32_t>(shim + 28);
        zpatch_smpa60_resume_b = static_cast<uint32_t>(shim + 14);
    }

    // PlayerAsPedComponentsFix's published resume: the anchor's +14 (the
    // `add ecx,80h` after the two pushes the shim replays).
    void PublishPlayerAsPedComponentsFix()
    {
        if (!Loader::Enabled("PlayerAsPedComponentsFix"))
            return;

        auto pattern = find_pattern("8B 54 24 10 52 0F B6 90 19 02 00 00");
        if (pattern.empty())
            return;

        Shared::PublishPlayerPedComponentsResume(static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 14));
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
        zpatch_bpm_context_init = RegisteredBody(0x100038C0u);
        zpatch_bpm_cleanup_dword = RegisteredBody(0x1005DE40u);
        zpatch_bpm_call_hash = RegisteredBody(0x1005B150u);
        zpatch_bpm_call_bool = RegisteredBody(0x1005B3F0u);
        zpatch_bpm_resolver = static_cast<uint32_t>(Shared::ValueResolver());
    }
}

namespace
{
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

    // FreezeGunFix: the anchor's -21 is the entry of the weapon check, which is
    // replaced by the shim.
    void InstallFreezeGunFix()
    {
        if (!Loader::Enabled("FreezeGunFix"))
            return;

        auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&FreezeGunShim), 0x10036EB0u);
        injector::MakeJMP(pattern.get_first(-21), reinterpret_cast<void*>(&FreezeGunShim), true);
    }

    // GroupHackFix: the group walk's entry becomes a jump into the shim, which
    // repeats the anchor's first three instructions and the table compare.
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

    // PathNodeCrashFix: the `mov dl,[eax+1Fh]; test dl,1` at the anchor's +53 is
    // replaced by the shim, which rejects the sentinel.
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

    // PlaneCrashFix: the anchor's +3 is the state store the body replays after its
    // null check.
    void InstallPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&PlaneCrashShim), 0x1003D870u);
        injector::MakeJMP(pattern.get_first(3), reinterpret_cast<void*>(&PlaneCrashShim), true);
    }

    // BlackscreenFix: the tail jump at the anchor's +81 becomes a jump into the shim.
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

    // CrashMsgFix: the thunk loses its branch to the message getter; a call into the
    // shim replaces it, and the shim calls the getter itself, replaces the '~'
    // format markers in place and returns to the instruction after the thunk's call.
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

    // WantedLevelCrashFix: the update's entry becomes the shim, which skips it for a
    // null object.
    void InstallWantedLevelCrashFix()
    {
        if (!Loader::Enabled("WantedLevelCrashFix"))
            return;

        auto pattern = find_pattern("8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&WantedLevelShim), 0x10050490u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&WantedLevelShim), true);
    }

    // EpisodeSwitchFreezeFix: five sites, rewritten in the original's order: the ECX
    // reader, the archive store, the stream position, the EBX reader, the virtual
    // call.
    void InstallEpisodeSwitchFreezeFix()
    {
        if (!Loader::Enabled("EpisodeSwitchFreezeFix"))
            return;

        auto pattern = find_pattern("56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05");
        if (pattern.empty())
            return;
        auto* shimC = pattern.get_first<std::byte>(0);

        pattern = find_pattern("89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84");
        if (pattern.empty())
            return;
        auto* store = pattern.get_first<std::byte>(0);

        pattern = find_pattern("8B 4B 10 3B 4B 14 7D 0F");
        if (pattern.empty())
            return;
        auto* shimB = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimC), 0x1001B0C0u);
        injector::MakeJMP(shimC, reinterpret_cast<void*>(&EpisodeSwitchShimC), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchStoreShim), 0x1001B040u);
        injector::MakeJMP(store, reinterpret_cast<void*>(&EpisodeSwitchStoreShim), true);

        pattern = find_pattern("8B 41 10 03 41 0C C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchSum), 0x1001B080u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchSum), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimB), 0x1001B060u);
        injector::MakeJMP(shimB, reinterpret_cast<void*>(&EpisodeSwitchShimB), true);

        pattern = find_pattern("8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchVcall), 0x1001B0A0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchVcall), true);
    }

    // SMPA60Fix: two bodies. The `jz` at the primary anchor's -38 becomes an
    // unconditional short jump, the tail body replaces the primary anchor's +123 and
    // the second body the streaming loop's loader call.
    void InstallSMPA60Fix()
    {
        if (!Loader::Enabled("SMPA60Fix"))
            return;

        auto pattern = find_pattern("01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        if (pattern.empty())
            return;

        auto* primary = pattern.get_first<std::byte>(0);

        pattern = find_pattern("8B 48 70 83 C0 40 50 51 8D 51 10");
        if (pattern.empty())
            return;

        auto* shim = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60TailShim), 0x10046780u);
        injector::MakeJMP(primary + 123, reinterpret_cast<void*>(&Smpa60TailShim), true);
        injector::WriteMemory<std::byte>(primary - 38, Bytes::ShortJmp, true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60Shim), 0x10046750u);
        injector::MakeJMP(shim, reinterpret_cast<void*>(&Smpa60Shim), true);
    }

    // PlayerAsPedComponentsFix: the anchor's +5 is the `movzx edx,[eax+219h]` whose
    // result becomes CPedVariationMgr::SetVariation's `bool` argument; the site
    // becomes a jump into the shared shim (recovered VA 0x1003D9A0, hosted in
    // shared.ixx), which pushes the computed "this is the player model" flag and the
    // `push ecx` at the anchor's +13 instead.
    void InstallPlayerAsPedComponentsFix()
    {
        if (!Loader::Enabled("PlayerAsPedComponentsFix"))
            return;

        // The shared shim calls the model lookup and jumps through the published
        // resume. A failed preamble scan leaves either zero; the un-patched
        // variation flag is what the game would have computed itself.
        if (Failsafe::Missing("PlayerAsPedComponentsFix model lookup",
                zpatch_model_lookup == 0) ||
            Failsafe::Missing("PlayerAsPedComponentsFix resume",
                zpatch_player_ped_resume == 0))
            return;

        auto pattern = find_pattern("8B 54 24 10 52 0F B6 90 19 02 00 00");
        if (pattern.empty())
            return;
        if (!Failsafe::Body("PlayerAsPedComponentsFix body",
                Shared::PlayerPedComponentsShim()))
            return;

        injector::MakeJMP(pattern.get_first(5),
            reinterpret_cast<void*>(Shared::PlayerPedComponentsShim()), true);
    }

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped. The `sco_anchor`
    // group instead scans a signature that ends on the `jz` itself; a build where
    // the old -32 distance does not hold would otherwise flip an unrelated `jz`.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        // `mov ds:X,edi` (6 bytes) + `test ebp,ebp` (2) put the `jz` opcode
        // at +8. The signature ends on the branch it flips, instead of scanning
        // a downstream anchor and relying on a fixed -32 distance.
        auto anchored = find_pattern("89 3D ? ? ? ? 85 ED 74 1E");
        if (anchored.empty())
            return;
        injector::WriteMemory<std::byte>(
            anchored.get_first<std::byte>(8), Bytes::ShortJmp, true);
    }

    // TryToSkipAllErrors: the error handler returns immediately instead of walking
    // its own path.
    void InstallTryToSkipAllErrors()
    {
        if (!Loader::Enabled("TryToSkipAllErrors"))
            return;

        auto pattern = find_pattern("81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::Ret, true);
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

    void InstallHighFPSSpeedupFix()
    {
        if (!Loader::Enabled("HighFPSSpeedupFix"))
            return;

        auto pattern = find_pattern("0F 2F C1 76 03 0F 28 C8 F3 0F 10 15");
        if (pattern.empty())
            return;

        for (const auto& site : kSpeedupSites)
        {
            if (site.shortJump)
                injector::WriteMemory<std::byte>(pattern.get_first(site.offset), Bytes::ShortJmp, true);
            else
                injector::MakeNOP(pattern.get_first(site.offset), 2, true);
        }
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

    // BadPlayerModelCrashFix: the game's model-resolution entry becomes a jump to
    // the trampoline. The jump is made only after the resume above is published,
    // because the resume is read from the site the jump overwrites.
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
        // operand provable (docs/rebuilt.md).
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

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterStability()
{
    // The bounded formatter's data, filled before any tier runs. The body is
    // reached from three modules, so neither slot may sit behind a key's gate: a
    // run with BadPlayerModelCrashFix off still formats livery, OOC and DLC paths.
    zpatch_format_entry = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&BpmFormat));
    zpatch_bpm_stdio_common_vsprintf = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&__stdio_common_vsprintf));

    Loader::PublishEvent() += []
    {
        PublishFreezeGunFix();
        PublishGroupHackFix();
        PublishPathNodeCrashFix();
        PublishPlaneCrashFix();
        PublishBlackscreenFix();
        PublishCrashMsgFix();
        PublishWantedLevelCrashFix();
        PublishEpisodeSwitchFreezeFix();
        PublishSMPA60Fix();
        PublishPlayerAsPedComponentsFix();
        PublishBadPlayerModelCrashFix();
    };
    Loader::InstallEvent() += []
    {
        InstallMPNikoCrashFix();
        InstallBlackscreenFix();
        InstallCrashMsgFix();
        InstallFreezeGunFix();
        InstallGroupHackFix();
        InstallPathNodeCrashFix();
        InstallPlaneCrashFix();
        InstallPlayerAsPedComponentsFix();
        InstallWantedLevelCrashFix();
        InstallEpisodeSwitchFreezeFix();
        InstallSMPA60Fix();
        InstallSCOSignatureFix();
        InstallTryToSkipAllErrors();
        InstallRemoveAllWeaponsProtection();
        InstallHighFPSSpeedupFix();
        InstallAimProjectileCrashFix();
        InstallBadPlayerModelCrashFix();
    };
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

// tramp_SMPA60Fix, recovered VA 0x10046750: the streaming load's argument setup,
// replayed around the sentinel that says the loader slot is unused. Naked and
// internal: an external-linkage naked function is written to the module and every
// importer fails.
static __declspec(naked) void Smpa60Shim()
{
    __asm
    {
        cmp ecx, 0DDDDDDDDh
        jz smpa60_shim_tail
        mov ecx, DWORD PTR [eax + 70h]
        add eax, 40h
        push eax
        push ecx
        lea edx, [ecx + 10h]
        push edx
        push ecx
        push ecx
        jmp DWORD PTR [zpatch_smpa60_resume_b]
    smpa60_shim_tail:
        jmp DWORD PTR [zpatch_smpa60_resume_a]
    }
}

// sub_10046780, recovered VA 0x10046780: the block's link fixup, which is skipped
// when the next block is absent or is the 0xCDCDCDCD fill. Naked and internal.
static __declspec(naked) void Smpa60TailShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 14h]
        test ecx, ecx
        jz smpa60_tail_resume
        cmp ecx, 0CDCDCDCDh
        jz smpa60_tail_resume
        mov edx, DWORD PTR [eax + 10h]
        mov DWORD PTR [ecx + 10h], edx
    smpa60_tail_resume:
        jmp DWORD PTR [zpatch_smpa60_resume_c]
    }
}

// abi_wanted_level_shim, recovered VA 0x10050490: the wanted-level update is
// skipped when there is no player object, and the null path returns immediately.
// The body leaves the flags of its own `test al,al` for the resume's conditional.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void WantedLevelShim()
{
    __asm
    {
        cmp ecx, 0
        jz wanted_level_null
        mov eax, DWORD PTR [ecx]
        mov edx, DWORD PTR [eax + 128h]
        call edx
        test al, al
        jmp DWORD PTR [zpatch_wanted_level_resume]
    wanted_level_null:
        pop ebx
        ret
    }
}

// abi_episode_switch_store_shim, recovered VA 0x1001B040: the episode switch's
// archive store, replayed so a failed open returns the empty result instead of
// running the tail of the function. Naked and internal.
static __declspec(naked) void EpisodeSwitchStoreShim()
{
    __asm
    {
        mov DWORD PTR [ebx + 20h], eax
        cmp eax, 0
        jz episode_switch_store_null
        mov edx, DWORD PTR [eax]
        jmp DWORD PTR [zpatch_episode_switch_resume_a]
    episode_switch_store_null:
        xor al, al
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 10h
    }
}

// sub_1001B060, recovered VA 0x1001B060: the reader's index check, replayed around
// a null test. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimB()
{
    __asm
    {
        cmp ebx, 0
        jz episode_switch_b_null
        mov ecx, DWORD PTR [ebx + 10h]
        cmp ecx, DWORD PTR [ebx + 14h]
        jmp DWORD PTR [zpatch_episode_switch_resume_b]
    episode_switch_b_null:
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

// sub_1001B0C0, recovered VA 0x1001B0C0: the same check for the caller that passes
// its object in ECX. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimC()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_c_null
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 14h], 0
        jmp DWORD PTR [zpatch_episode_switch_resume_c]
    episode_switch_c_null:
        mov eax, 0
        ret
    }
}

// tramp_EpisodeSwitchFreezeFix_b, recovered VA 0x1001B080: the byte-stream position,
// returned as 0 when there is no stream. Naked and internal.
static __declspec(naked) void EpisodeSwitchSum()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_sum_null
        mov eax, DWORD PTR [ecx + 10h]
        add eax, DWORD PTR [ecx + 0Ch]
        ret
    episode_switch_sum_null:
        mov eax, 0
        ret
    }
}

// sub_1001B0A0, recovered VA 0x1001B0A0: the virtual call the episode switch makes
// through the object's vtable, skipped for a null object. Naked and internal.
static __declspec(naked) void EpisodeSwitchVcall()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_vcall_null
        mov eax, ecx
        mov ecx, DWORD PTR [eax]
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [eax + 4]
        mov edx, DWORD PTR [edx + 30h]
        push eax
        call edx
        ret
    episode_switch_vcall_null:
        mov eax, 0
        ret
    }
}

// abi_plane_crash_shim, recovered VA 0x1003D870: the plane's destruction tail. The
// `mov [eax+26h],bl` at the anchor's +3 is replaced, so the body replays it after a
// null check and takes the null state's path (the anchor's -705, the head of the
// same function) instead of dereferencing a missing vehicle. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
static __declspec(naked) void PlaneCrashShim()
{
    __asm
    {
        cmp eax, 0
        jz plane_crash_null
        mov BYTE PTR [eax + 26h], bl
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 0F4h]
        jmp DWORD PTR [zpatch_plane_crash_resume_b]
    plane_crash_null:
        jmp DWORD PTR [zpatch_plane_crash_resume_a]
    }
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

// abi_freeze_gun_shim, recovered VA 0x10036EB0: the freeze-gun path is entered
// only when the weapon's model word at +40h is a real value; the three sentinels
// (0xCDCDCDCD, -1, 0) return an empty result instead. The site is the anchor's
// -21: the five-byte jump covers `mov ecx,[eax+38h]` and `test ecx,ecx`, so the
// body re-runs both; `mov esi,[eax+40h]` is jumped over and re-run as well, and
// the `push edi` that follows is the body's own. The resume is the published
// anchor-12, the instruction after that `push edi`.
//
// The null path's `xor eax,eax; pop esi; ret 8` is the enclosing function's stack
// contract, not a bare return: the site sits just after that function's `push
// esi`, so the path must pop the saved register and then the two arguments
// (`ret 8`) before returning to the caller.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void FreezeGunShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 38h]
        test ecx, ecx
        mov esi, DWORD PTR [eax + 40h]
        cmp esi, 0CDCDCDCDh
        jz freeze_gun_null
        cmp esi, 0FFFFFFFFh
        jz freeze_gun_null
        cmp esi, 0
        jz freeze_gun_null
        push edi
        jmp DWORD PTR [zpatch_freeze_gun_resume]
    freeze_gun_null:
        xor eax, eax
        pop esi
        ret 8
    }
}

// ---------------------------------------------------------------------------
// BadPlayerModelCrashFix
//
// The game resolves a model index at 0x7CF803, three bytes into a function whose
// frame is already open (so EBP is live). The original replaces that entry with a
// 16-instruction trampoline (0x10037BA0) over a 65-instruction payload
// (0x10037AC0): when the model the caller named is not a ped (class id 6), the
// payload hashes "m_y_multiplayer", asks the game which model that is and writes
// the answer back into the caller's own argument slot, so the game never resolves
// a model that does not exist.
//
// Everything the hook runs is transcribed here: the trampoline, the payload, the
// two native wrappers of its GTAC.dll diagnostic (0x1005B290, 0x1005B530), the
// one-argument wrapper its fallback calls (0x1005B3F0) and the model-name lookup
// (0x10003F00). The pieces those wrappers share with the native-call layer stay
// where they are transcribed, in source/native.ixx and source/shared.ixx, and are
// reached through the six-byte forwarders below. The bounded formatter
// (0x10001280 over 0x10001240/0x10001230) is transcribed as well.
//
// The name lookup is the one piece here with an indexed absolute table operand.
// It is transcribed rather than rebuilt because the tree's body registry now names
// such tables (Bodies::RegisterTable): the operand keeps the original's
// `[table + esi*4]` shape and the proof compares the table's *contents* through
// the registration instead of its displacement. docs/rebuilt.md §2b records it.
// ---------------------------------------------------------------------------

// sub_10001230, recovered VA 0x10001230 (2 instructions): the UCRT printf options
// block the engine reads. The original answers the address of its own zeroed .data
// word at 0x101A84B0; this module answers its copy, and the instruction is the
// original's. Transcribed, with a proof row (docs/rebuilt.md).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void BpmFormatOptions()
{
    __asm
    {
        mov eax, OFFSET zpatch_bpm_format_options
        ret
    }
}

// sub_10001240, recovered VA 0x10001240 (21 instructions): the printf engine call
// itself, with the original's standard-snprintf option bit (`or ecx, 2`) and its
// "every negative answer becomes -1" tail. The original's `call ds:0x1006336C` is
// its IAT slot for `__stdio_common_vsprintf`; the static CRT has no IAT, so the
// engine is reached through the slot above and the instruction keeps that shape.
// Both the option block and the engine are what the original calls, so the rebuild
// this replaces (`_vsnprintf_s` with `_TRUNCATE`) is gone: the semantics are now the
// original's own CRT entry point rather than an equivalent one.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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

// sub_10001280, recovered VA 0x10001280 (13 instructions): the original's bounded
// `vsnprintf` wrapper, `(char* buffer, unsigned size, const char* format, ...)`,
// with the arg list's address taken from the frame (`[ebp+14h]`) exactly as the
// compiled original does. Transcribed and proven, so the C++ `_vsnprintf_s` rebuild
// the tree carried is gone. The other modules whose inline asm names a local
// formatter (vehicles' livery helpers, the OOC wrapper, the DLC discovery) enter
// this body through a six-byte `jmp` forwarder on `zpatch_format_entry`, so the
// original's single formatter is single here too.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
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
