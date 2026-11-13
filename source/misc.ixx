module;

#include <common.hxx>
// The /GS cookie the transcribed second siren check reads and checks
// (`__security_cookie`, `__security_check_cookie` in vcruntime.h).
#include <intrin.h>
#include <cstdlib>
#include <cstring>

export module misc;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import native;
import patch;
import shared;

// The remaining fixes: the bundled MiscFixes table, the mod-check stubs and the
// drive-by FOV. One arm per key, each anchored to its own recovered signature (or,
// for the stubs, to the recovered per-build site table).


// CopSirensFlag: the two model checks (sub_10005F70 and sub_10006260) and the
// shared IDE hook (sub_100040E0), which fills the 512-slot model-hash tables the
// checks walk from the game's IDE parser entries.

extern "C"
{
    // dword_10159C0C, defined in source/shared.ixx: the game's model-index lookup,
    // `int lookup(uint32_t nameHash, int32_t *index)`.
    extern uintptr_t zpatch_model_lookup;

    // dword_1015937C / dword_10159374 (the resumes), dword_10159370 and
    // dword_1015936C (the two model indices the sites computed), dword_10159B80 /
    // dword_10159378 (the two flags), and the IDE parser's arguments and resume
    // (dword_1015BE08 the string, dword_1015BE0C the model object, dword_1015BE10
    // the continuation).
    uint32_t zpatch_copsirens_resume = 0;
    uint32_t zpatch_copsirens_resume_b = 0;
    uint32_t zpatch_copsirens_index = 0;
    uint32_t zpatch_copsirens_index_b = 0;
    uint32_t zpatch_copsirens_flag = 0;
    uint32_t zpatch_copsirens_flag_b = 0;
    uint32_t zpatch_ide_string = 0;
    uint32_t zpatch_ide_model = 0;
    uint32_t zpatch_ide_hook_slot = 0;

    // The 512-slot tables sub_10003FA0 fills: dword_10159380 (sirens),
    // dword_1015CE28 (popuplights), and dword_1015A538 / dword_10159D38
    // (wheelangle models and the float bits of their angles).
    uint32_t zpatch_copsirens_models[512] = {};
    uint32_t zpatch_popuplights_models[512] = {};
    uint32_t zpatch_wheelangle_models[512] = {};
    uint32_t zpatch_wheelangle_angles[512] = {};

    // The names the two siren checks resolve, each the original's own .rdata string
    // and each pushed with `push imm32`: the first check's thirteen built-in police
    // models in its order (0x1013A6B4 ... 0x10129DB4) and the second check's
    // "police2" (0x1013A734, the same string the first check uses second).
    const char zpatch_copsirens_police[] = "police";
    const char zpatch_copsirens_police2[] = "police2";
    const char zpatch_copsirens_police3[] = "police3";
    const char zpatch_copsirens_police4[] = "police4";
    const char zpatch_copsirens_noose[] = "noose";
    const char zpatch_copsirens_pstockade[] = "pstockade";
    const char zpatch_copsirens_nstockade[] = "nstockade";
    const char zpatch_copsirens_polpatriot[] = "polpatriot";
    const char zpatch_copsirens_fbi[] = "fbi";
    const char zpatch_copsirens_n_police[] = "n_police";
    const char zpatch_copsirens_n_police2[] = "n_police2";
    const char zpatch_copsirens_n_polpatriot[] = "n_polpatriot";
    const char zpatch_copsirens_n_pstockade[] = "n_pstockade";

    // The IDE parser's third arm compares the site's string against this literal
    // (original 0x10128DF4, "wheelangle:" and its NUL) and parses the degrees that
    // follow it with the CRT's strtof (original IAT slot 0x100632D4, `call ds:[...]`,
    // which stays a pointer slot because the original's call is memory indirect).
    const char zpatch_ide_wheelangle_prefix[] = "wheelangle:";
    uintptr_t zpatch_ide_strtof = 0;
}

// PopupHeadlightsFlag: the checkbox that makes a vehicle's pop-up headlights
// deserve their own light cone. The two shims (sub_10044530 and its EDX twin
// sub_10044560) run the popuplights checker (sub_10044310) and continue into the
// site's own headlight test.

extern "C"
{
    // dword_1015BE20 (the site's model word), dword_1015BE24 (the check's flag),
    // dword_1015BE1C (the popuplights model index), dword_101580CC (the angle the
    // site's `fld` loads), and dword_1015D628 / dword_1015D62C (the two resumes).
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    float zpatch_popup_angle = 1.57079632679489661923f;  // pi/2, dword_101580CC
    uint32_t zpatch_popup_resume_a = 0;
    uint32_t zpatch_popup_resume_b = 0;

    // The two 512-slot [PopupLightAngles] tables the checker walks
    // (dword_1015C628 / dword_1015BE28) are Config::PopupLightAngles()'s arrays, filled
    // by the loader preamble's reader and registered beside this arm. When the section
    // is empty the hashes are all zero, so the checker keeps the pi/2 above -- the
    // original's own default.
}

// ---- VRAMFix -----------------------------------------------------------------
//
// The recovered catalogue attributes this key to the graphics module, but it is a
// single-write install with no other site, so it lives here with the tree's other
// one-off installers. (The reference tree keeps it in graphics.ixx; moving this
// block there is only a move.)
//
// The original's handler 0x1004AAE0 scans
// "8B 44 24 10 D9 2C 24 83 C4 1C C3" and writes an `E9` plus its rel32 to
// 0x1004AAB0 at the match. The handler carries no build gate: the signature is
// present on 1.0.4.0 / 1.0.0.4 / 1.0.6.0 / 1.0.7.0 / 1.1.1.0 / 1.1.2.0 and absent
// on 1.0.0.0 / 1.0.8.0 / 1.1.3.0, which are the original's own clean skips. This
// arm adds no gate either -- the scan is the gate.

extern "C"
{
    uint32_t zpatch_vram_arg_a = 0;  // dword_1015D630
    uint32_t zpatch_vram_arg_b = 0;  // dword_1015D634
    uint8_t zpatch_vram_flag = 0;    // byte_1015D638
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void IdeParseBody();
static void CopSirensCheckABody();
static void CopSirensCheckBBody();
static void CopSirensShimABody();
static void CopSirensShimBBody();
static void IdeHookBody();
static void PopupHeadlightsCheckBody();
static void PopupHeadlightsBodyA();
static void PopupHeadlightsBodyB();
static void VramFixShimBody();

// `mov eax, imm32`: the MiscFixes token-activation row writes it. bytes.ixx does
// not carry it, and that module is outside this task's edit scope, so the opcode
// is named here.
// The opcode comes from bytes.ixx; this table only records which row writes it.

namespace
{
    // One site of MiscFixes: the recovered signature, the write shape, the offset
    // from the anchor, and the byte the shape writes (the jump displacement for a
    // short jump, the immediate for the `mov eax` row).
    enum class MiscFixWrite : uint8_t
    {
        byte,
        shortJump,
        nop2,
        movEax,
    };

    struct MiscFixSite final
    {
        std::string_view signature;
        MiscFixWrite kind;
        ptrdiff_t offset;
        std::byte opcode{};  // the byte for `byte`, the jmp opcode for `shortJump`
        uint32_t value{};    // the jump displacement, or the `mov eax` immediate
    };

    // The original handler's eleven literals in its write order (sub_10023DD0). The
    // DFA stage it calls between them is a loader-level arm there, not part of this
    // key's write set. Five rows are other builds' sites and match nothing on
    // 1.0.8.0, which is why the key writes eight places on that image.
    constexpr MiscFixSite kMiscFixSites[] = {
        // data integrity checks VDS102 [v1002 - v1004]
        { "C7 06 ? ? ? ? 74 19", MiscFixWrite::byte, 6, Bytes::ShortJmp },
        // data integrity checks VDS102 [v1005 - v1008]
        { "83 C4 ? 3B 46 20", MiscFixWrite::byte, 12, Bytes::ShortJmp },
        // data integrity checks VDS100 [v1006 - v1008]
        { "75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C", MiscFixWrite::byte, 0,
          Bytes::ShortJmp },
        // token activation [v1006 - v1007]: `jmp +0xC9`
        { "8B 54 24 04 52 A3", MiscFixWrite::shortJump, 0, Bytes::ShortJmp, 0xC9 },
        // token activation [v1006 - v1007]: mov eax,1
        { "83 F8 01 75 30 A3", MiscFixWrite::movEax, 0, Bytes::MovEax, 1 },
        // skip first-time play ask [v1006 - v1008]: `jmp +0x0D`
        { "C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7", MiscFixWrite::shortJump, 0,
          Bytes::ShortJmp, 0x0D },
        // InternetGetConnectedState [v1000 - v1001]
        { "FF 15 ? ? ? ? 85 C0 75 18", MiscFixWrite::nop2, 8 },
        // InternetGetConnectedState [v1002 - v1005]
        { "FF 15 ? ? ? ? 85 C0 75 04 32", MiscFixWrite::nop2, 8 },
        // InternetGetConnectedState [v1006 - v1008]
        { "FF 15 ? ? ? ? 85 C0 75 07 A0", MiscFixWrite::nop2, 8 },
        // health check [v1003 - v1008]
        { "81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8",
          MiscFixWrite::byte, 0, Bytes::Ret },
        // the handler's last write, after its DFA call: force a short jump
        { "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9", MiscFixWrite::byte, 0,
          Bytes::ShortJmp },
    };

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





namespace
{
    // sub_10003E50 (source/shared.ixx), the original's own name resolver/hasher.
    // The checks call it rather than a C++ hash, so the "hash:" and quoted forms
    // resolve exactly as in the original.
    uint32_t ResolveModelName(const char* name)
    {
        return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(Shared::ValueResolver())(name);
    }
}

// sub_1004AA80, rebuilt: the shim's verdict helper. The compiler's own compares
// are `test eax,eax / js set`, `cmp dword_1015D630,0 / jne set` and
// `cmp eax,3E800000h / jge clear` -- a signed compare of the second argument's
// float bits against 0.25f. It is ordinary C++ because the original's frame (/GS
// cookie, saved registers) is compiler output, not a hand-written stream.
static void VramVerdict()
{
    const auto second = static_cast<int32_t>(zpatch_vram_arg_b);
    zpatch_vram_flag = 0;
    if (second < 0 || zpatch_vram_arg_a != 0
        || second < static_cast<int32_t>(0x3E800000u))
    {
        zpatch_vram_flag = 1;
    }
}

namespace
{

    // CopSirensFlag's three sites: the two model checks and the shared IDE hook,
    // installed at the parser anchor's -10 (the seven-byte compare).
    void PublishCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            zpatch_copsirens_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(5)));

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            zpatch_copsirens_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(28)));

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));
    }

    // PopupHeadlightsFlag's sites: the EAX anchor (jump, its +10 branch forced to
    // a short jump, and the `fld` operand at +82 retargeted to our pi/2 float),
    // the shared IDE hook, and the EDX twin when the image has it.
    void PublishPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }
    }
}

namespace
{
    // MiscFixes: the eleven literals, in the handler's order. The original skips
    // the whole key when the XLivelessAddon companion is loaded (it sets its
    // byte_10159B84 from GetModuleHandleA("XLivelessAddon.asi")), so the arm asks
    // the same question first.
    void InstallMiscFixes()
    {
        if (GetModuleHandleA("XLivelessAddon.asi") != nullptr)
            return;
        if (!Loader::Enabled("MiscFixes"))
            return;

        for (const auto& site : kMiscFixSites)
        {
            auto pattern = find_pattern(site.signature);
            if (pattern.empty())
                continue;

            auto* match = pattern.get_first<std::byte>(0);
            switch (site.kind)
            {
            case MiscFixWrite::byte:
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                break;
            case MiscFixWrite::shortJump:
                // A `jmp rel8` written as its two bytes: the injector has no
                // short-jump primitive.
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                injector::WriteMemory<std::byte>(match + site.offset + 1,
                    static_cast<std::byte>(site.value), true);
                break;
            case MiscFixWrite::nop2:
                injector::MakeNOP(match + site.offset, 2, true);
                break;
            case MiscFixWrite::movEax:
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                injector::WriteMemory<uint32_t>(match + site.offset + 1, site.value, true);
                break;
            }
        }
    }



    void InstallCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&CopSirensShimABody), true);

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(22),
                reinterpret_cast<void*>(&CopSirensShimBBody), true);

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);
    }


    // VRAMFix: the matched function's entry becomes a jump to the shim.
    void InstallVRAMFix()
    {
        if (!Loader::Enabled("VRAMFix"))
            return;

        auto pattern = find_pattern("8B 44 24 10 D9 2C 24 83 C4 1C C3");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&VramFixShimBody), 0x1004AAB0u);
        injector::MakeJMP(pattern.get_first<std::byte>(0),
            reinterpret_cast<void*>(&VramFixShimBody), true);
    }

    void InstallPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyA), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyB), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }
    }
}

// Registered like every other arm: the publish tier reads the site's original
// bytes, the install tier owns the gate.
export void RegisterMisc()
{
    Loader::AddPublish(Loader::Entry::Misc, 3, PublishCopSirensFlag);
    Loader::AddPublish(Loader::Entry::Misc, 4, PublishPopupHeadlightsFlag);

    Loader::AddInstall(Loader::Entry::Misc, 0, InstallMiscFixes);
    Loader::AddInstall(Loader::Entry::Misc, 5, InstallCopSirensFlag);
    Loader::AddInstall(Loader::Entry::Misc, 6, InstallPopupHeadlightsFlag);
    Loader::AddInstall(Loader::Entry::Misc, 7, InstallVRAMFix);

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimABody), 0x10006310u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimBBody), 0x10006330u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&IdeHookBody), 0x100040E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyA), 0x10044530u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyB), 0x10044560u);

    // The second siren check's table: the IDE parser hook's +siren list, which the
    // transcription reads as `[esi + zpatch_copsirens_models]` and as a plain absolute
    // at slot 0. The registration pairs this module's array with the original's VA and
    // the length there (512 words); its tests/bodies.tsv `table` row is the offline
    // projection of this call (docs/rebuilt.md §3). Unlike the wheelangle tables this
    // one is *filled* at run time by the transcribed IDE hook, so a content hash of it
    // compares the shipped all-zero state, not the populated one.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_copsirens_models),
        0x10159380u, sizeof(zpatch_copsirens_models));   // 512 * 4 = 2048

    // The IDE payload's popuplights list, dword_1015CE28: the same 512 words the
    // transcribed payload fills and the popup-headlights checker walks. It is the
    // third of the four tables the payload touches (the siren list and the wheelangle
    // pair are registered beside their own bodies' arms). All-zero at load, like the
    // others, because the original keeps it in `.data`'s uninitialised tail.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popuplights_models),
        0x1015CE28u, sizeof(zpatch_popuplights_models));  // 512 * 4 = 2048

    // The popup-headlights checker's two [PopupLightAngles] arrays: dword_1015C628
    // (the hashes) and dword_1015BE28 (the angles). They are Config's own storage, not
    // copies: the loader preamble's reader fills them, and the checker reads them
    // through eight interleaved columns, which is why the registration names the
    // array's base and the comparator resolves the seven column displacements inside
    // it (docs/rebuilt.md §2f). Both are all-zero at load.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_hashes),
        0x1015C628u, sizeof(zpatch_popup_angle_hashes));  // 512 * 4 = 2048
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_values),
        0x1015BE28u, sizeof(zpatch_popup_angle_values));  // 512 * 4 = 2048

    // The IDE payload's third arm calls the CRT's strtof through the original's IAT
    // slot shape; the static CRT has no IAT, so the call goes through this word.
    zpatch_ide_strtof = reinterpret_cast<uintptr_t>(&strtof);

    // Each stub's inline `call` is invisible to C++; naming the helper here keeps
    // its out-of-line definition in the image.
    [[maybe_unused]] const uintptr_t helpers[] = {
        reinterpret_cast<uintptr_t>(&CopSirensCheckABody),
        reinterpret_cast<uintptr_t>(&CopSirensCheckBBody),
        reinterpret_cast<uintptr_t>(&IdeParseBody),
        reinterpret_cast<uintptr_t>(&PopupHeadlightsCheckBody),
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------



// sub_10003FA0, recovered VA 0x10003FA0 (92 instructions, 314 bytes): the IDE parser
// hook's payload, transcribed instruction for instruction.
//
// Three independent tests on the site's string (dword_1015BE08), in the original's
// order, each of which appends the model word (dword_1015BE0C + 60, read unguarded)
// to a 512-slot table when it is not already there: "popuplights" by three dword
// compares, "siren" by a dword and a word compare, and the "wheelangle:" prefix
// through the CRT's _strnicmp followed by strtof of the text eleven bytes in. The
// first two fall through to the next arm; the third ends the function.
//
// Every one of the four tables is a registered table (docs/rebuilt.md §2b-§2e): the
// popuplights list at 0x1015CE28, the siren list at 0x10159380, and the wheelangle
// model/angle pair at 0x1015A538 / 0x10159D38. Three of them are read through an
// absolute *with* a base and no index (`cmp [eax+table],esi`, the §2d shape) and the
// stores through an indexed absolute (`mov [ecx*4+table],eax`, the §2b shape); the
// slot-0 entry check is a plain absolute and therefore an `abs` slot.
//
// The three loop paddings are emitted as bytes because the inline assembler picks
// its own: `66 90` at 0x10003FDE, `0F 1F 00` at 0x1000402D and
// `66 0F 1F 44 00 00` at 0x1000409A. No exception frame and no TLS static-init
// guard, so nothing blocks the transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void IdeParseBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        mov edx, DWORD PTR [zpatch_ide_string]
        mov eax, 75706F70h            // "popu"
        push esi
        push edi
        mov edi, DWORD PTR [zpatch_ide_model]
        cmp eax, DWORD PTR [edx]
        jne ide_not_popuplights
        mov eax, 67696C70h            // "plig"
        cmp eax, DWORD PTR [edx + 4]
        jne ide_not_popuplights
        mov eax, 737468h              // "hts\0"
        cmp eax, DWORD PTR [edx + 8]
        jne ide_not_popuplights
        xor ecx, ecx
        cmp DWORD PTR [zpatch_popuplights_models], ecx
        je ide_popuplights_append
        mov esi, DWORD PTR [edi + 3Ch]
        xor eax, eax
        __emit 0x66
        __emit 0x90                   // nop (the original's two-byte form)
    ide_popuplights_walk:
        cmp DWORD PTR [eax + zpatch_popuplights_models], esi
        je ide_not_popuplights
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_popuplights_models], 0
        jne ide_popuplights_walk
    ide_popuplights_append:
        mov eax, DWORD PTR [edi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_popuplights_models], eax
    ide_not_popuplights:
        mov eax, 65726973h            // "sire"
        cmp eax, DWORD PTR [edx]
        jne ide_not_siren
        mov eax, 6Eh                  // "n\0"
        cmp ax, WORD PTR [edx + 4]
        jne ide_not_siren
        xor ecx, ecx
        cmp DWORD PTR [zpatch_copsirens_models], ecx
        je ide_siren_append
        mov esi, DWORD PTR [edi + 3Ch]
        xor eax, eax
        __emit 0x0F
        __emit 0x1F
        __emit 0x00                  // nop dword ptr [eax]
    ide_siren_walk:
        cmp DWORD PTR [eax + zpatch_copsirens_models], esi
        je ide_not_siren
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_copsirens_models], 0
        jne ide_siren_walk
    ide_siren_append:
        mov eax, DWORD PTR [edi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_copsirens_models], eax
    ide_not_siren:
        push 0Bh
        push OFFSET zpatch_ide_wheelangle_prefix
        push edx
        call DWORD PTR [zpatch_resolver_strnicmp]
        add esp, 0Ch
        test eax, eax
        jne ide_done
        push eax                      // endptr = the null the compare left in EAX
        mov eax, DWORD PTR [zpatch_ide_string]
        add eax, 0Bh
        push eax
        call DWORD PTR [zpatch_ide_strtof]
        mov esi, DWORD PTR [zpatch_ide_model]
        xor ecx, ecx
        add esp, 8
        fstp DWORD PTR [ebp - 4]
        cmp DWORD PTR [zpatch_wheelangle_models], ecx
        je ide_wheelangle_append
        mov edx, DWORD PTR [esi + 3Ch]
        xor eax, eax
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x44
        __emit 0x00
        __emit 0x00                  // nop word ptr [eax+eax]
    ide_wheelangle_walk:
        cmp DWORD PTR [eax + zpatch_wheelangle_models], edx
        je ide_done
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_wheelangle_models], 0
        jne ide_wheelangle_walk
    ide_wheelangle_append:
        movss xmm0, DWORD PTR [ebp - 4]
        mov eax, DWORD PTR [esi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_wheelangle_models], eax
        movss DWORD PTR [ecx*4 + zpatch_wheelangle_angles], xmm0
    ide_done:
        pop edi
        pop esi
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10005F70, recovered VA 0x10005F70 (191 instructions, 750 bytes): the first
// siren check, transcribed instruction for instruction.
//
// Thirteen built-in police model names are resolved through the original's own
// resolver (sub_10003E50) and the game's model-index lookup, in the original's order;
// the first match sets the flag and leaves through the shared epilogue. Otherwise the
// IDE parser's +siren list is walked: `push dword ptr [edi + 0x10159380]` reads slot N
// of the 512-word table as a base-plus-displacement absolute, `cmove` keeps the flag
// byte the walk read, and the loop ends at the first zero word or past slot 511
// (`cmp edi, 7FCh`). The entry check reads the same table's first word as a plain
// absolute.
//
// The /GS frame is the original's, and the 3-byte `nop dword ptr [eax]` at
// 0x100061ED (0F 1F 00) is emitted as bytes so the assembler cannot choose another
// padding. No exception frame, no TLS static-init guard: nothing blocks the
// transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CopSirensCheckABody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police
        mov DWORD PTR [zpatch_copsirens_flag], 0
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police2
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police3
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police4
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_noose
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_pstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_nstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_polpatriot
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_fbi
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_police
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_police2
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_polpatriot
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_pstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        cmp DWORD PTR [zpatch_copsirens_models], 0
        je copsirens_a_no_walk
        push ebx
        push edi
        xor edi, edi
        lea ebx, [edi + 1]
        // The original's loop alignment: 0F 1F 00 (`nop dword ptr [eax]`).
        __emit 0x0F
        __emit 0x1F
        __emit 0x00
    copsirens_a_walk:
        mov esi, DWORD PTR [zpatch_copsirens_index]
        lea eax, [ebp - 8]
        push eax
        push DWORD PTR [edi + zpatch_copsirens_models]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        add esp, 8
        cmp DWORD PTR [ebp - 8], esi
        cmove eax, ebx
        add edi, 4
        mov DWORD PTR [zpatch_copsirens_flag], eax
        cmp edi, 7FCh
        ja copsirens_a_walk_done
        cmp DWORD PTR [edi + zpatch_copsirens_models], 0
        jne copsirens_a_walk
    copsirens_a_walk_done:
        pop edi
        pop ebx
    copsirens_a_no_walk:
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    copsirens_a_hit:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        mov DWORD PTR [zpatch_copsirens_flag], 1
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10006260, recovered VA 0x10006260 (53 instructions, 174 bytes): the second
// siren check, transcribed instruction for instruction.
//
// It resolves "police2" through the original's own resolver (sub_10003E50) and the
// game's model-index lookup, and answers flag 1 when the index matches. Otherwise it
// walks the IDE parser's +siren list: `push dword ptr [esi + 0x10159380]` reads slot
// N of the 512-word table, an absolute *with* a base register and no index -- the
// shape the table registry canonicalises as `[esi+table:<hash>]`, with the base
// register still compared (docs/rebuilt.md §2d). The walk stops at the first
// zero word (`cmp dword ptr [esi+0x10159380], 0`), and the check before the loop
// reads the same table's first word as a plain absolute.
//
// Its /GS frame is the original's: the cookie is an absolute slot and the check a
// call slot. The body has no exception frame and no TLS static-init guard, so
// nothing blocks a transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CopSirensCheckBBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        push edi
        mov edi, DWORD PTR [zpatch_copsirens_index_b]
        push OFFSET zpatch_copsirens_police2
        mov DWORD PTR [zpatch_copsirens_flag_b], 0
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], edi
        je sirens_hit
        cmp DWORD PTR [zpatch_copsirens_models], 0
        je sirens_done
        xor esi, esi
    sirens_walk:
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push DWORD PTR [esi + zpatch_copsirens_models]
        call DWORD PTR [zpatch_model_lookup]
        add esp, 8
        cmp DWORD PTR [ebp - 8], edi
        je sirens_hit
        add esi, 4
        cmp esi, 7FCh
        ja sirens_done
        cmp DWORD PTR [esi + zpatch_copsirens_models], 0
        jne sirens_walk
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    sirens_hit:
        mov DWORD PTR [zpatch_copsirens_flag_b], 1
    sirens_done:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10006310, recovered VA 0x10006310 (8 instructions): the first siren shim.
// It stores the site's model index (EDX), runs the check between pusha/pushfd and
// popa/popfd, and resumes with EAX = the flag, which the site's `shl al, 6;
// xor al, [esi+0F6Fh]` continues on. pusha/popa are emitted as bytes: the inline
// assembler renders both with a 0x66 operand-size prefix.
static __declspec(naked) void CopSirensShimABody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index], edx
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckABody
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        jmp DWORD PTR [zpatch_copsirens_resume]
    }
}

// sub_10006330, recovered VA 0x10006330 (8 instructions): the second siren shim.
// It is installed at the anchor's +22 over the first five bytes of a six-byte
// `cmp`, and its own `cmp [flag], 1` is what the resumed `je` at the anchor's +28
// consumes; the replaced comparison's sixth byte is dead.
static __declspec(naked) void CopSirensShimBBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index_b], eax
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckBBody
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_copsirens_flag_b], 1
        jmp DWORD PTR [zpatch_copsirens_resume_b]
    }
}

// sub_100040E0, recovered VA 0x100040E0 (9 instructions): the shared IDE hook.
// After the payload it replays the site's `mov ecx,7; xor edx,edx; repe cmpsb`
// (the in-place copies are skipped by the resume at the anchor's -1), so the
// resumed `jne` still sees the compare's flags. `repe cmpsb` is emitted as bytes
// so the assembler cannot choose another form.
static __declspec(naked) void IdeHookBody()
{
    __asm
    {
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_ide_string], edi
        mov DWORD PTR [zpatch_ide_model], eax
        call IdeParseBody
        __emit 0x61  // popa
        mov ecx, 7
        xor edx, edx
        __emit 0xF3
        __emit 0xA6  // repe cmpsb
        jmp DWORD PTR [zpatch_ide_hook_slot]
    }
}

// sub_10044310, recovered VA 0x10044310 (125 instructions, 534 bytes): the
// popup-headlights checker, transcribed instruction for instruction.
//
// It seeds the site's angle slot (dword_101580CC) with pi/2 as a *dword* store
// (`mov dword ptr [0x101580CC], 3FC90FDBh`), asks the game's model store for the
// model the original names by the literal hash 0xEB9F21D3 (`push 0EB9F21D3h` at
// 0x1004432E), and answers flag 1 when that is the site's model word. Otherwise it
// walks the +popuplights list the IDE payload filled: the same lookup per slot, a
// bounded `cmp edi,7FCh` walk that ends at the first zero word, and on a match flag 1
// plus the column scan below. The lookup slot is unguarded, as the original's
// `call dword_10159C0C` is.
//
// The column scan is the reason this body needed the table registry extended: it
// reads the [PopupLightAngles] tables as **eight interleaved columns** of 64 rows,
// `mov ecx,ds:[eax+0x1015C628]` through `[eax+0x1015C644]` and the same eight at
// 0x1015BE28, with EAX stepping 0x20 (`add eax,20h` / `cmp eax,800h` / `jl`). Seven
// of each eight displacements are therefore four bytes into the table rather than at
// its base; the registry resolves a displacement inside a registered table to that
// table plus the offset, and the offset is compared (docs/rebuilt.md §2f). Each column
// keeps the last matching row's angle, which is what the original's in-place
// overwrite does.
//
// The /GS frame is the original's (cookie slot, check call slot). The two loop
// paddings are emitted as bytes because the inline assembler picks its own:
// `66 0F 1F 84 00 00 00 00 00` at 0x10044387 and `0F 1F 80 00 00 00 00` at
// 0x100443E9. No exception frame, no TLS static-init guard.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PopupHeadlightsCheckBody()
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
        mov DWORD PTR [zpatch_popup_angle], 3FC90FDBh   // pi/2, the .data seed
        push eax
        push 0EB9F21D3h
        mov DWORD PTR [zpatch_popup_flag], 0
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [ebp - 8]
        add esp, 8
        mov DWORD PTR [zpatch_popup_index], eax
        cmp eax, DWORD PTR [zpatch_popup_model]
        jne popup_walk
        mov DWORD PTR [zpatch_popup_flag], 1
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    popup_walk:
        push esi
        xor esi, esi
        cmp DWORD PTR [zpatch_popuplights_models], esi
        je popup_no_list
        push edi
        xor edi, edi
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x84
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00                  // nop word ptr [eax+eax]
    popup_search_loop:
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push DWORD PTR [edi + zpatch_popuplights_models]
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [zpatch_popup_model]
        add esp, 8
        cmp eax, DWORD PTR [ebp - 8]
        je popup_search_hit
        add edi, 4
        inc esi
        cmp edi, 7FCh
        ja popup_walk_done
        cmp DWORD PTR [edi + zpatch_popuplights_models], 0
        jne popup_search_loop
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    popup_search_hit:
        mov DWORD PTR [zpatch_popup_flag], 1
        xor eax, eax
        __emit 0x0F
        __emit 0x1F
        __emit 0x80
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00                  // nop dword ptr [eax]
    popup_column_0:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes]
        test ecx, ecx
        je popup_column_1
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_1
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_1:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 4]
        test ecx, ecx
        je popup_column_2
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_2
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 4]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_2:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 8]
        test ecx, ecx
        je popup_column_3
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_3
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 8]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_3:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 0Ch]
        test ecx, ecx
        je popup_column_4
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_4
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 0Ch]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_4:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 10h]
        test ecx, ecx
        je popup_column_5
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_5
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 10h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_5:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 14h]
        test ecx, ecx
        je popup_column_6
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_6
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 14h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_6:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 18h]
        test ecx, ecx
        je popup_column_7
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_7
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 18h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_7:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 1Ch]
        test ecx, ecx
        je popup_column_done
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_done
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 1Ch]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_done:
        add eax, 20h
        cmp eax, 800h
        jl popup_column_0
    popup_walk_done:
        pop edi
    popup_no_list:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10044530, recovered VA 0x10044530 (10 instructions): the EAX site's shim.
// It stores the site's model word, runs the check between pusha/popa, then takes
// the site's second branch when the model is a popuplights model, or falls into
// the anchor's +10 short jump (which the arm rewrites to unconditional) with the
// site's own `cmp eax, [index]` replayed first. pusha/popa are emitted as bytes
// because the inline assembler adds a 0x66 prefix.
static __declspec(naked) void PopupHeadlightsBodyA()
{
    __asm
    {
        movsx eax, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], eax
        call PopupHeadlightsCheckBody
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_a_resume_a
        cmp eax, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_a_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}

// sub_10044560, recovered VA 0x10044560 (10 instructions): the EDX twin for the
// second site pattern, absent on 1.0.8.0 but present on the builds the key was
// gated for.
static __declspec(naked) void PopupHeadlightsBodyB()
{
    __asm
    {
        movsx edx, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], edx
        call PopupHeadlightsCheckBody
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_b_resume_a
        cmp edx, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_b_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}

// sub_1004AAB0, recovered VA 0x1004AAB0 (13 instructions): the argument/verdict
// shim. It stashes two of the replaced function's arguments, runs the verdict under
// pushad/popad, returns INT_MAX when the verdict is set, then reproduces the
// original tail whose `fldcw [esp]` reads the E9 site's return address as the FPU
// control word (the preserved quirk).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void VramFixShimBody()
{
    __asm
    {
        mov eax, DWORD PTR [esp + 14h]
        mov DWORD PTR [zpatch_vram_arg_a], eax
        mov eax, DWORD PTR [esp + 10h]
        mov DWORD PTR [zpatch_vram_arg_b], eax
        __emit 0x60  // pushad
        call VramVerdict
        __emit 0x61  // popad
        cmp BYTE PTR [zpatch_vram_flag], 0
        je vram_skip
        mov eax, 7FFFFFFFh
    vram_skip:
        fldcw WORD PTR [esp]
        add esp, 1Ch
        ret
    }
}
