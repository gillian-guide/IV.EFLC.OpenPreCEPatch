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


// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.

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
}

namespace
{


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






}

// Registered like every other arm: the publish tier reads the site's original
// bytes, the install tier owns the gate.
// ---------------------------------------------------------------------------
// OverrideGTADatPath, moved from the load-time module.
// ---------------------------------------------------------------------------

namespace
{

    // OverrideGTADatPath's sibling key: the path the game's gta.dat push is
    // retargeted to. The original's buffer is 64 bytes and its static initializer
    // holds the stock path; the loader overwrites it from OverrideGTADatPathTo
    // with strcpy_s, so a value of 64 bytes or more faults through the CRT's
    // invalid-parameter path (preserved). Absent or "0" leaves the initializer.
    char zpatch_dat_path[64] = "common:/data/gta.dat";
}

namespace
{
namespace
{
    void CopyOverrideDatPath()
    {
        const std::string fallback("0");
        const auto& value = Feather::Value(Config::Ini(), "Options", "OverrideGTADatPathTo", fallback);
        if (value == "0")
            return;

        strcpy_s(zpatch_dat_path, sizeof(zpatch_dat_path), value.c_str());
    }
}

    // OverrideGTADatPath: the anchor is the gta.dat loader's `push <char*>`, and
    // the original rewrites just that immediate (the anchor's +1).
    void InstallOverrideGTADatPath()
    {
        if (!Loader::Enabled("OverrideGTADatPath"))
            return;

        auto pattern = find_pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(1),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zpatch_dat_path)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(1)));
        }
    }
}


// ---------------------------------------------------------------------------
// RestoreDeathMusic, AllowHashesForIDE and PedModelsInCutscenesFix, moved
// from the audio module with their state, wrappers and bodies.
// ---------------------------------------------------------------------------

extern "C"
{
    // dword_10159C0C, the game's model-index lookup shared.ixx publishes: the
    // PedModels body reads it through the same slot (the C name resolves across
    // module units; a second definition would be a second scan).
    extern uintptr_t zpatch_model_lookup;

    // The shared resolver's address, for the thunk below. The original's body calls
    // sub_10003E50 with one direct `E8`; ours cannot name it: the resolver is
    // `static` in shared.ixx (internal linkage), and C2488 forbids giving a naked
    // function external linkage in a module interface unit, so no importer can use
    // it as a link-time call target. The body calls this module's one-instruction
    // thunk instead, which jumps through this slot -- the single deviation from the
    // original's instruction stream in this body (docs/parity.md).
    uintptr_t zpatch_audio_value_resolver = 0;

    // The "player" model name the PedModels body pushes (original 0x1013A038). A
    // copy rather than shared.ixx's literal: a module-attached literal is not
    // visible to an importer, and the proof compares the push as a slot anyway.
    const char zpatch_audio_player_name[] = "player";
}

// RestoreDeathMusic's published state. The gate body reads the original's option
// word and three hash salts; the install body keeps the site it resolves once and
// the clock word its seed guard compares against. Every salt below is a copy of
// one of the native layer's ten resolved words, taken with `Native::Salt(ref)` at
// registration time and never written as a literal: on 1.0.8.0 the probe hits and
// the shipped `.data` values stand, while 1.0.0.0 and 1.0.4.0 miss and the
// preamble's fallback set applies (source/native.ixx, docs/parity.md).
extern "C"
{
    // byte_10159B86: the option word the gate body tests. The original's loader
    // preamble stores it from the INI before any body runs.
    uint8_t zpatch_audio_death_music_flag = 0;

    // dword_101A84B8: the `6A 06 68 ? ? ? ? 8D 8C` site the install body resolves
    // once. The original's other word, dword_101A84C0, is the guard for its CRT
    // clock/seed block; the rebuild does not carry that block (docs/parity.md), so
    // the slot is not published.
    uint32_t zpatch_audio_death_music_site = 0;

    // The five preamble salts this key's bodies and wrappers read:
    //   dword_1015805C / dword_101580A8 / dword_10158050 -- sub_10050A50
    //   dword_10158054 -- the two-argument wrapper sub_1005B1E0
    //   dword_10158028 -- the byte wrapper sub_1005B490
    uint32_t zpatch_audio_salt_10158054 = 0;
    uint32_t zpatch_audio_salt_10158050 = 0;
    uint32_t zpatch_audio_salt_101580A8 = 0;
    uint32_t zpatch_audio_salt_1015805C = 0;
    uint32_t zpatch_audio_salt_10158028 = 0;

    // The native-layer pieces the three wrappers call, reached through
    // one-instruction `jmp` forwarders for the reason stability.ixx's
    // BadPlayerModelCrashFix forwarders exist: a module interface cannot export a
    // naked function, and a C entry's decorated name is not nameable from inline
    // asm. Filled from the body registry by each piece's recovered VA.
    uintptr_t zpatch_audio_context_init = 0;
    uintptr_t zpatch_audio_invoke_entry = 0;
    uintptr_t zpatch_audio_cleanup_dword = 0;
    uintptr_t zpatch_audio_cleanup_bool = 0;
    uintptr_t zpatch_audio_call_hash = 0;
}

static void ValueResolverThunk();
static void PedModelsInCutscenesCheckBody();
static void AudioContextInitEntry();
static void AudioInvokeEntry();
static void AudioCleanupDwordEntry();
static void AudioCleanupBoolEntry();
static void AudioCallHashEntry();
static unsigned int __fastcall AudioCallTwoBody(unsigned int, unsigned int);
static unsigned int __fastcall AudioCallZeroBoolBody(unsigned int);
static unsigned int __fastcall AudioCallByteBody(unsigned int, unsigned int);
static bool DeathMusicGateBody();
static char DeathMusicInstallBody();
static void __cdecl AudioZeroBlock(void*, int, unsigned int);

// The CRT `memset` the three wrappers open with. The original calls the CRT's own
// thunk (0x10060D6E) with the same argument order; the inline assembler rejects a
// direct call to the intrinsic's name (C2420), so the call names this cdecl
// forwarder, which is still one `E8`. Same shape as native.ixx's NativeZeroBlock.
static void __cdecl AudioZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

// sub_10050AD0, recovered VA 0x10050AD0 (75 instructions to its `ret`): the
// death-music track install. Called through the game's track-select function
// entry, it asks the gate body first, resolves the `6A 06 68 ? ? ? ? 8D 8C` site
// once, and writes a random track number 1..5 into the push immediate at site+1.
//
// Rebuilt rather than transcribed, so it has no proof row: the original is
// compiler output for a C function with an EH4 frame (prologue 0x10050AD3 ..
// 0x10050AF4, handler 0x100619C1 with its scope table at 0x10154384, epilogue
// 0x10050BAE .. 0x10050BC8), and its clock/seed block calls the static CRT's own
// helpers (0x1005FFC3 and 0x1005FF77), which reach the CRT's per-thread state
// through `fs:[2Ch]` and the TLS index dword_10159328 -- image-internal addresses
// a rebuilt module cannot name (docs/parity.md). The state word it keeps is
// published above, the gate call is the transcribed body, and the write it makes
// is the original's: `rand() % 5 + 1` at site+1.
static char DeathMusicInstallBody()
{
    if (!DeathMusicGateBody())
        return 0;

    if (zpatch_audio_death_music_site == 0)
    {
        auto pattern = find_pattern("6A 06 68 ? ? ? ? 8D 8C");
        if (!pattern.empty())
        {
            zpatch_audio_death_music_site = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
        }
    }

    if (zpatch_audio_death_music_site != 0)
    {
        // The site is `push 6`; the second byte is the immediate the track number
        // replaces. The original writes it unguarded, and so does this.
        auto* site = reinterpret_cast<std::byte*>(
            static_cast<uintptr_t>(zpatch_audio_death_music_site) + 1);
        injector::WriteMemory<std::byte>(site,
            static_cast<std::byte>(std::rand() % 5 + 1), true);
    }
    return 1;
}

    // The address of a body another module transcribed, by its recovered VA. The
    // native layer publishes only its C entries -- a module interface cannot
    // export a naked function -- so the body registry is what makes its pieces
    // reachable from here (the same helper stability.ixx uses).
    uint32_t RegisteredBody(uint32_t reference)
    {
        for (const auto& entry : Bodies::All())
        {
            if (entry.reference == reference)
                return static_cast<uint32_t>(entry.address);
        }
        return 0;
    }

namespace
{


    // RestoreDeathMusic's option word. The original's loader preamble stores it
    // from the INI before any body runs and the gate body tests it at run time;
    // ours is set in the publish tier, once the key is known to be on.
    void PublishRestoreDeathMusic()
    {
        if (!Loader::Enabled("RestoreDeathMusic"))
            return;

        zpatch_audio_death_music_flag = 1;
    }

    // AllowHashesForIDE: the game's model-name hash function becomes the shared
    // resolver (sub_10003E50), so an IDE entry may name a model by `hash:NNNN`.
    // The game calls the hash function with two arguments; the resolver is
    // __cdecl and reads only the first, so its plain `ret` leaves the second for
    // the caller to pop, exactly as the original's body does.
    void InstallAllowHashesForIDE()
    {
        if (!Loader::Enabled("AllowHashesForIDE"))
            return;

        auto pattern = find_pattern("8B 4C 24 08 56 8B 74 24 08 80 3E 22");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(Shared::ValueResolver()), true);
    }


    // PedModelsInCutscenesFix: the cutscene code's player-model check becomes the
    // body, which re-derives the player model's index through the shared resolver
    // and the game lookup and compares that with the object's model word.
    void InstallPedModelsInCutscenesFix()
    {
        if (!Loader::Enabled("PedModelsInCutscenesFix"))
            return;

        auto pattern = find_pattern("56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&PedModelsInCutscenesCheckBody),
                0x10050870u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&PedModelsInCutscenesCheckBody), true);
        }
    }

    // RestoreDeathMusic: the game's track-select function entry and its two
    // callers become calls into the install body and the gate body. All three
    // sites are already `E8` calls in the image, so only the displacement moves.
    void InstallRestoreDeathMusic()
    {
        if (!Loader::Enabled("RestoreDeathMusic"))
            return;

        auto pattern = find_pattern("0F 57 C0 F3 0F 11 86 20 02 00 00");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DeathMusicInstallBody), 0x10050AD0u);
            injector::MakeCALL(pattern.get_first(-13),
                reinterpret_cast<void*>(&DeathMusicInstallBody), true);
        }

        pattern = find_pattern("C7 86 04 02 00 00 04 00 00 00 E9");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DeathMusicGateBody), 0x10050A50u);
            injector::MakeCALL(pattern.get_first(-13),
                reinterpret_cast<void*>(&DeathMusicGateBody), true);
        }

        pattern = find_pattern("E8 ? ? ? ? 84 C0 74 C6 8B BE");
        if (!pattern.empty())
        {
            injector::MakeCALL(pattern.get_first(0),
                reinterpret_cast<void*>(&DeathMusicGateBody), true);
        }
    }

}


// ---------------------------------------------------------------------------
// RemoveUselessChecks, moved from its own module: the recovered per-build
// sweep tables and the one installer.
// ---------------------------------------------------------------------------

namespace
{
    // One single write: `length` bytes of `byte` at game base + rva.
    struct UselessCheckSingle
    {
        uint32_t rva;
        uint8_t length;
        uint8_t byte;
    };

    #include "../data/useless_checks.inc"

    static_assert(std::size(kUselessBuild9Nop5) == 2230u);
    static_assert(std::size(kUselessBuild9MovAl1) == 45u);
    static_assert(std::size(kUselessBuild9XorAlAl) == 56u);
    static_assert(std::size(kUselessBuild8Nop5) == 2288u);
    static_assert(std::size(kUselessBuild5Nop5) == 2466u);
    static_assert(std::size(kUselessSingles9) == 19u);
    static_assert(std::size(kUselessSingles8) == 2u);
    static_assert(std::size(kUselessSingles5) == 2u);

    // The five-byte patterns the build tables write live in bytes.ixx with every
    // other recovered opcode byte; a NOP run uses MakeNOP while every other single
    // is a single byte store.
    void SweepSingles(uintptr_t base, std::span<const UselessCheckSingle> rows)
    {
        for (const auto& row : rows)
        {
            if (row.byte == std::to_integer<uint8_t>(Bytes::Nop))
                injector::MakeNOP(base + row.rva, row.length, true);
            else
                injector::WriteMemory<uint8_t>(base + row.rva, row.byte, true);
        }
    }

    // One protected five-byte write per site.
    void SweepNop5(uintptr_t base, std::span<const uint32_t> rows)
    {
        for (const auto rva : rows)
        {
            injector::WriteMemoryRaw(base + rva,
                const_cast<uint8_t*>(
                    reinterpret_cast<const uint8_t*>(Bytes::FiveNops.data())),
                Bytes::FiveNops.size(), true);
        }
    }

    // The 45/56 tables protect each byte separately: this must not collapse into
    // one five-byte write, or the write set differs.
    void SweepBytes(uintptr_t base, std::span<const uint32_t> rows,
        std::span<const std::byte> bytes)
    {
        for (const auto rva : rows)
        {
            for (size_t index = 0; index < bytes.size(); ++index)
                injector::WriteMemory<uint8_t>(
                    base + rva + index, std::to_integer<uint8_t>(bytes[index]), true);
        }
    }

    struct BuildSweep final
    {
        Build build;
        std::span<const UselessCheckSingle> singles;
        std::span<const uint32_t> nop5;
        std::span<const uint32_t> first;
        std::span<const std::byte> firstPattern;
        std::span<const uint32_t> second;
        std::span<const std::byte> secondPattern;
    };

    constexpr BuildSweep kSweeps[] = {
        { Build::v1080, kUselessSingles9, kUselessBuild9Nop5, kUselessBuild9MovAl1, Bytes::MovAl1, kUselessBuild9XorAlAl, Bytes::XorAlAl },
        { Build::v1070, kUselessSingles8, kUselessBuild8Nop5, {}, {}, {}, {} },
        { Build::v1040, kUselessSingles5, kUselessBuild5Nop5, {}, {}, {}, {} },
    };
}

export namespace UselessChecks
{
    void Install()
    {
        if (!Loader::Enabled("RemoveUselessChecks"))
            return;

        const auto base = Game::Get().ImageBase();
        for (const auto& sweep : kSweeps)
        {
            if (sweep.build != Loader::Current())
                continue;

            SweepSingles(base, sweep.singles);
            SweepNop5(base, sweep.nop5);
            SweepBytes(base, sweep.first, sweep.firstPattern);
            SweepBytes(base, sweep.second, sweep.secondPattern);
            return;
        }
    }
}


// ---------------------------------------------------------------------------
// PlayerAsPedComponentsFix, moved from the stability module; its shim and
// helper bodies live in shared.ixx.
// ---------------------------------------------------------------------------

namespace
{
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

    // PlayerAsPedComponentsFix: the anchor's +5 is the `movzx edx,[eax+219h]` whose
    // result becomes CPedVariationMgr::SetVariation's `bool` argument; the site
    // becomes a jump into the shared shim (recovered VA 0x1003D9A0, hosted in
    // shared.ixx), which pushes the computed "this is the player model" flag and the
    // `push ecx` at the anchor's +13 instead.
    void InstallPlayerAsPedComponentsFix()
    {
        if (!Loader::Enabled("PlayerAsPedComponentsFix"))
            return;

        auto pattern = find_pattern("8B 54 24 10 52 0F B6 90 19 02 00 00");
        if (pattern.empty())
            return;

        injector::MakeJMP(pattern.get_first(5),
            reinterpret_cast<void*>(Shared::PlayerPedComponentsShim()), true);
    }
}

export void RegisterMisc()
{

    Loader::AddPublish(Loader::Entry::Loading, 1, CopyOverrideDatPath);
    Loader::AddInstall(Loader::Entry::Loading, 8, InstallOverrideGTADatPath);
    // RestoreDeathMusic's published words, filled before any tier runs.
    zpatch_audio_value_resolver = Shared::ValueResolver();
    zpatch_audio_salt_10158054 = Native::Salt(0x10158054u);
    zpatch_audio_salt_10158050 = Native::Salt(0x10158050u);
    zpatch_audio_salt_101580A8 = Native::Salt(0x101580A8u);
    zpatch_audio_salt_1015805C = Native::Salt(0x1015805Cu);
    zpatch_audio_salt_10158028 = Native::Salt(0x10158028u);
    zpatch_audio_context_init = RegisteredBody(0x100038C0u);
    zpatch_audio_invoke_entry = RegisteredBody(0x100038E0u);
    zpatch_audio_cleanup_dword = RegisteredBody(0x1005DE40u);
    zpatch_audio_cleanup_bool = RegisteredBody(0x1005DE80u);
    zpatch_audio_call_hash = RegisteredBody(0x1005B150u);
    Loader::AddPublish(Loader::Entry::Audio, 0, PublishRestoreDeathMusic);
    Loader::AddInstall(Loader::Entry::Audio, 0, InstallAllowHashesForIDE);
    Loader::AddInstall(Loader::Entry::Audio, 9, InstallPedModelsInCutscenesFix);
    Loader::AddInstall(Loader::Entry::Audio, 10, InstallRestoreDeathMusic);
    Loader::AddInstall(Loader::Entry::UselessChecks, 0, UselessChecks::Install);
    Loader::AddPublish(Loader::Entry::Stability, 9, PublishPlayerAsPedComponentsFix);
    Loader::AddInstall(Loader::Entry::Stability, 7, InstallPlayerAsPedComponentsFix);
    Loader::AddInstall(Loader::Entry::Misc, 0, InstallMiscFixes);

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates.

    // The second siren check's table: the IDE parser hook's +siren list, which the
    // transcription reads as `[esi + zpatch_copsirens_models]` and as a plain absolute
    // at slot 0. The registration pairs this module's array with the original's VA and
    // the length there (512 words); its tests/bodies.tsv `table` row is the offline
    // projection of this call (docs/rebuilt.md Â§3). Unlike the wheelangle tables this
    // one is *filled* at run time by the transcribed IDE hook, so a content hash of it
    // compares the shipped all-zero state, not the populated one.

    // The IDE payload's popuplights list, dword_1015CE28: the same 512 words the
    // transcribed payload fills and the popup-headlights checker walks. It is the
    // third of the four tables the payload touches (the siren list and the wheelangle
    // pair are registered beside their own bodies' arms). All-zero at load, like the
    // others, because the original keeps it in `.data`'s uninitialised tail.

    // The popup-headlights checker's two [PopupLightAngles] arrays: dword_1015C628
    // (the hashes) and dword_1015BE28 (the angles). They are Config's own storage, not
    // copies: the loader preamble's reader fills them, and the checker reads them
    // through eight interleaved columns, which is why the registration names the
    // array's base and the comparator resolves the seven column displacements inside
    // it (docs/rebuilt.md Â§2f). Both are all-zero at load.

}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------






// A one-instruction thunk onto the shared resolver (`jmp DWORD PTR [slot]`, six
// bytes): the `jmp` pushes nothing, so the resolver sees the same stack and flags
// the original's `call sub_10003E50` gives it, and its `ret` returns to the body.
// The slot's declaration above records why the call cannot name the resolver
// directly.
static __declspec(naked) void ValueResolverThunk()
{
    __asm
    {
        jmp DWORD PTR [zpatch_audio_value_resolver]
    }
}

// sub_10050870, recovered VA 0x10050870 (75 bytes, 26 instructions): the "does
// this object use the player model" check the cutscene code calls. It resolves
// "player" through the shared resolver, asks the game's model lookup for that
// model's index and compares it with the object's model word (+2Eh). The /GS
// prologue and epilogue are the original's, including the CRT check call.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PedModelsInCutscenesCheckBody()
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
        push OFFSET zpatch_audio_player_name
        mov esi, ecx
        call ValueResolverThunk
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        movsx eax, WORD PTR [esi + 2Eh]
        add esp, 0Ch
        cmp eax, DWORD PTR [ebp - 8]
        mov ecx, DWORD PTR [ebp - 4]
        setz al
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// The forwarders into the native layer's transcribed pieces. Each is one `jmp`
// through a slot the registration fills from the body registry; they touch no
// register, no stack slot and no flag, so the piece sees exactly the call the
// original's `E8` gives it.
static __declspec(naked) void AudioContextInitEntry()
{
    __asm { jmp DWORD PTR [zpatch_audio_context_init] }
}

static __declspec(naked) void AudioInvokeEntry()
{
    __asm { jmp DWORD PTR [zpatch_audio_invoke_entry] }
}

static __declspec(naked) void AudioCleanupDwordEntry()
{
    __asm { jmp DWORD PTR [zpatch_audio_cleanup_dword] }
}

static __declspec(naked) void AudioCleanupBoolEntry()
{
    __asm { jmp DWORD PTR [zpatch_audio_cleanup_bool] }
}

static __declspec(naked) void AudioCallHashEntry()
{
    __asm { jmp DWORD PTR [zpatch_audio_call_hash] }
}

// sub_1005B1E0, recovered VA 0x1005B1E0 (48 instructions): the two-argument native
// wrapper. The hash is the salt dword_10158054, the two arguments travel in EDX
// and on the stack, and the answer is args[0]. __fastcall-shaped, and the first
// register argument (ECX) is unused exactly as in the original. The /GS frame is
// the original's.
static __declspec(naked) unsigned int __fastcall AudioCallTwoBody(unsigned int, unsigned int)
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
        mov eax, DWORD PTR [zpatch_audio_salt_10158054]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call AudioZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
    audio_two_records:
        mov ecx, edi
        call AudioContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz audio_two_records
        mov ecx, DWORD PTR [esp + 0Ch]
        lea eax, [esp + 70h]
        mov DWORD PTR [esp + 18h], eax
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 10h], eax
        mov eax, DWORD PTR [ebp + 8]
        mov DWORD PTR [esp + 1Ch], esi
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 74h], eax
        mov DWORD PTR [esp + 14h], 2
        call AudioInvokeEntry
        lea ecx, [esp + 10h]
        call AudioCleanupDwordEntry
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

// sub_1005B360, recovered VA 0x1005B360 (44 instructions): the zero-argument
// wrapper that answers a byte. The hash arrives in ECX and the answer is the low
// byte of args[0].
static __declspec(naked) unsigned int __fastcall AudioCallZeroBoolBody(unsigned int)
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
        mov ebx, ecx
        push 0
        push eax
        call AudioZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
        // The original's loop alignment (0F 1F 40 00), emitted as bytes so the
        // assembler cannot choose a shorter nop.
        __emit 0x0F
        __emit 0x1F
        __emit 0x40
        __emit 0x00
    audio_zero_records:
        mov ecx, edi
        call AudioContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz audio_zero_records
        lea eax, [esp + 70h]
        mov DWORD PTR [esp + 14h], esi
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov ecx, ebx
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        call AudioInvokeEntry
        lea ecx, [esp + 10h]
        call AudioCleanupBoolEntry
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

// sub_1005B490, recovered VA 0x1005B490 (46 instructions): the one-argument
// wrapper whose answer is a byte. The argument travels in EDX, the hash is the
// salt dword_10158028.
static __declspec(naked) unsigned int __fastcall AudioCallByteBody(unsigned int, unsigned int)
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
        mov eax, DWORD PTR [zpatch_audio_salt_10158028]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call AudioZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
    audio_byte_records:
        mov ecx, edi
        call AudioContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz audio_byte_records
        mov ecx, DWORD PTR [esp + 0Ch]
        lea eax, [esp + 70h]
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 14h], 1
        call AudioInvokeEntry
        lea ecx, [esp + 10h]
        call AudioCleanupBoolEntry
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

// sub_10050A50, recovered VA 0x10050A50 (41 instructions): the death-music gate.
// It answers 0 when the option word is clear, then asks the game three questions
// through the hash salts: the current "death music enabled" state (a zero-argument
// call, an out-parameter read by the two-argument call, then a byte answer), and
// the two flags dword_101580A8 and dword_10158050. The /GS frame is the original's.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) bool DeathMusicGateBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        cmp BYTE PTR [zpatch_audio_death_music_flag], 0
        jne death_music_enabled
    death_music_answer_zero:
        // The original's shared "no" epilogue: both `je`s below jump back here
        // (0x10050AA9/0x10050ABA to 0x10050A69), so it sits ahead of the enabled
        // path exactly as it does in the original.
        xor al, al
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    death_music_enabled:
        mov ecx, DWORD PTR [zpatch_audio_salt_1015805C]
        call AudioCallHashEntry
        lea ecx, [ebp - 8]
        mov edx, eax
        push ecx
        call AudioCallTwoBody
        mov edx, DWORD PTR [ebp - 8]
        add esp, 4
        call AudioCallByteBody
        test al, al
        jne death_music_answer_one
        mov ecx, DWORD PTR [zpatch_audio_salt_101580A8]
        call AudioCallZeroBoolBody
        test al, al
        je death_music_answer_zero
        mov ecx, DWORD PTR [zpatch_audio_salt_10158050]
        call AudioCallZeroBoolBody
        test al, al
        je death_music_answer_zero
    death_music_answer_one:
        mov ecx, DWORD PTR [ebp - 4]
        mov al, 1
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}
