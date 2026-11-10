module;

#include <common.hxx>

// Declares the /GS cookie the transcribed body's prologue reads and the check its
// epilogue calls (`__security_cookie`, `__security_check_cookie` in vcruntime.h),
// and the two C routines the transcribed wrappers forward to.
#include <intrin.h>
#include <cstdlib>
#include <cstring>

export module audio;

import bodies;
import bytes;
import common;
import game;
import improvements;
import loader;
import native;
import patch;
import shared;

// The fixes the original groups with the radio and the world: the radio freeze,
// the episode gates the weapons and the helicopter ceiling sit behind, the input
// behaviour (mouse, minimize), the pointer-node pool sizes, the MP sync, the
// physics sleep, the model-hash hook and the cutscene player-model check. One arm
// per key, each anchored to its own recovered signature.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets below are measured from
// the anchor, which is why they did not change when the strings did.

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).
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

// MouseFix's published state. Two pointers the game's mouse reader stores its axes
// through, the game byte that flips the axis sign, the game's own frame-time
// function, and the two resumes the site bodies tail-jump to. The floats and
// doubles are the original ASI's literal pool: recovered `.data`, kept as raw words
// so the bit patterns are exactly the original's.
extern "C"
{
    uintptr_t zpatch_mf_x = 0;       // dword_10159C50 -> the game's mouse-X int
    uintptr_t zpatch_mf_y = 0;       // dword_10159C3C -> the game's mouse-Y int
    uintptr_t zpatch_mf_flag = 0;    // dword_10159C5C -> the game's invert byte
    uintptr_t zpatch_mf_time = 0;    // dword_10159C34 -> the game's frame clock
    uint32_t zpatch_mf_resume_a = 0; // dword_10159C60 -> the angle site's match
    uint32_t zpatch_mf_resume_b = 0; // dword_10159C40 -> the advance site's +0x756

    // The game's VirtualProtect import, called the way the original's advance
    // helper calls it: `call dword ptr [slot]`.
    uintptr_t zpatch_mf_virtual_protect = 0;

    // The eight xmm words the two site bodies save and restore around their helper
    // (the original's scratch dwords at 0x10159C30..0x10159C64).
    float zpatch_mf_xmm_save[8] = {};

    // The scalar constants the site bodies read with mulsd/movsd QWORD PTR [slot]
    // and movss DWORD PTR [slot]. They are the original's .rdata values written as
    // the numbers they are; three of them are not the mathematical constants they
    // resemble, so each of those says so. No explicit alignment: a scalar load does
    // not need one on x86, and the compiler gives doubles and floats their natural
    // alignment anyway.
    double zpatch_mf_scale = 0.0078125;
    double zpatch_mf_scale_255 = 255.0;
    double zpatch_mf_time_mul = 0.075000003;    // 0.075f printed to 9 digits, read back as a double
    double zpatch_mf_time_add = 0.025;
    double zpatch_mf_mul_neg1_d = -1.0;
    double zpatch_mf_pi = 3.1415927;            // not pi: the original's value
    double zpatch_mf_two_pi = 6.2831855;        // not 2*pi, same reason
    double zpatch_mf_neg_pi = -3.1415927;
    float zpatch_mf_sign = 127.5f;
    float zpatch_mf_mul_neg1 = -1.0f;
    float zpatch_mf_step_scale = 0.1f;

    // The two xorps masks: four single-precision sign bits (0x101523F0) and two
    // double-precision ones (0x101523E0). Both are 16 bytes in the original and
    // `xorps` reads sixteen, so they must stay 16-byte aligned here too.
    alignas(16) uint32_t zpatch_mf_single_sign[4] = { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u };
    alignas(16) uint64_t zpatch_mf_double_sign[2] = { 0x8000000000000000ull, 0x8000000000000000ull };
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void CombinedRadioFreezeFixBody();
static void BetterMPSyncBody();
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
static void MfAxisFloatBody();
static void MfAxisNegBody();
static void MfAxisIntBody();
static void MfAxisFlagBody();
static void MfAngleStepBody();
static void MfAngleBody();
static void MfAdvanceWindowsBody();
static void MfAdvanceBody();

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

namespace
{
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

    // The game waits ten ticks when the window is minimized; the original writes
    // six, so the arm does too.
    constexpr std::byte kMinimizePauseTick{ 0x06 };

    // One site of the episode gate: the recovered signature, the offset from its
    // anchor the NOP starts at, and how many bytes it covers. A row NOPs either
    // the comparison itself or the branch after the six-byte `cmp [episode],2`.
    struct EpisodeGateSite final
    {
        std::string_view signature;
        ptrdiff_t offset;
        size_t length;
    };

    // EpisodicWeaponSupport: the eight episode gates in front of the episodic
    // weapons. The key's build mask is the original's ten-build set (options.inc),
    // so the arm needs no gate of its own.
    constexpr EpisodeGateSite kEpisodicWeaponSites[] = {
        { "75 5B 83 3D ? ? ? ? 00 75", 0, 2 },
        { "83 3D ? ? ? ? 02 0F 85 8C 01 00 00", 7, 6 },
        { "83 3D ? ? ? ? 02 0F 8C D5 00", 0, 13 },
        { "83 3D ? ? ? ? 02 75 20 8B CD E8", 7, 2 },
        { "83 3D ? ? ? ? 02 75 50", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 32 8B 41", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", 7, 2 },
    };

    // The two movd replacements: `cvtsi2ss xmmN, eax` becomes `movd xmmN, eax`,
    // which moves the stored int bit-for-bit. Four bytes each; the bytes around
    // them are untouched.
    constexpr std::array<uint8_t, 4> kMovdXmm0Eax{ 0x66, 0x0F, 0x6E, 0xC0 };
    constexpr std::array<uint8_t, 4> kMovdXmm1Eax{ 0x66, 0x0F, 0x6E, 0xC8 };

    // RestoreDeathMusic's option word. The original's loader preamble stores it
    // from the INI before any body runs and the gate body tests it at run time;
    // ours is set in the publish tier, once the key is known to be on.
    void PublishRestoreDeathMusic()
    {
        if (!Loader::Enabled("RestoreDeathMusic"))
            return;

        zpatch_audio_death_music_flag = 1;
    }

    // MouseFix's published words, read before the sites are written: the two axis
    // pointers, the invert byte, the game's frame clock and the two resumes. The
    // first three are absolute operands of the sites the install tier overwrites,
    // which is why the publish tier owns them. Each operand is a live VA (the
    // loader has already applied the image's relocations; the offline dump
    // relocates its copy), so the captured dword is the address and is stored as
    // read.
    void PublishMouseFix()
    {
        if (!Loader::Enabled("MouseFix"))
            return;

        auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C");
        if (!pattern.empty())
        {
            zpatch_mf_x = *pattern.get_first<uint32_t>(13);
            zpatch_mf_y = *pattern.get_first<uint32_t>(20);
        }

        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D");
        if (!pattern.empty())
            zpatch_mf_flag = *pattern.get_first<uint32_t>(20);

        pattern = find_pattern("83 EC 08 DB 05 ? ? ? ? 68");
        if (!pattern.empty())
        {
            zpatch_mf_time =
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        }

        pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46");
        if (!pattern.empty())
        {
            const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
            zpatch_mf_resume_a = static_cast<uint32_t>(match);
            zpatch_mf_resume_b = static_cast<uint32_t>(match + 1878);
        }

        // The game's VirtualProtect, which the advance helper calls through its
        // IAT slot in the original (`FF 15`), so it stays an indirect call here.
        zpatch_mf_virtual_protect = reinterpret_cast<uintptr_t>(&VirtualProtect);
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

    // CombinedRadioFreezeFix: the call at the anchor is the radio update; it
    // becomes a call into the body, which returns the freeze value instead of
    // running the update.
    void InstallCombinedRadioFreezeFix()
    {
        if (!Loader::Enabled("CombinedRadioFreezeFix"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&CombinedRadioFreezeFixBody), 0x10005ED0u);
            injector::MakeCALL(pattern.get_first(0),
                reinterpret_cast<void*>(&CombinedRadioFreezeFixBody), true);
        }
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

    // NoPhysicsSleep: the physics sleep threshold is the float the anchor's -38
    // operand names (0.005f at game 0x1090B18, RVA 0xC90B18), so the arm writes a
    // zero there. The operand is a live VA -- the loader has already relocated the
    // image, and the offline dump relocates its copy -- so it is used as it was
    // captured; rebasing it through the preferred base would move the write by the
    // run's whole relocation distance.
    void InstallNoPhysicsSleep()
    {
        if (!Loader::Enabled("NoPhysicsSleep"))
            return;

        auto pattern = find_pattern("0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00");
        if (pattern.empty())
            return;

        injector::WriteMemory<uint32_t>(*pattern.get_first<uint32_t>(-38), 0u, true);
    }

    // DoNotPauseOnMinimize: the wait's tick count drops to six, and the branch
    // that pauses the game when the window loses focus is NOPed.
    void InstallDoNotPauseOnMinimize()
    {
        if (!Loader::Enabled("DoNotPauseOnMinimize"))
            return;

        auto pattern = find_pattern("6A 0A 56 50 FF D2 A1");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(1), kMinimizePauseTick, true);

        pattern = find_pattern("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 2, true);
    }

    void InstallEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("EpisodicWeaponSupport"))
            return;

        for (const auto& site : kEpisodicWeaponSites)
        {
            auto pattern = find_pattern(site.signature);
            if (!pattern.empty())
                injector::MakeNOP(pattern.get_first(site.offset), site.length, true);
        }
    }

    // PoliceEpisodicWeaponSupport: the police-vehicle episode gate; the two-byte
    // branch after the `cmp [episode],2` at the anchor is NOPed.
    void InstallPoliceEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("PoliceEpisodicWeaponSupport"))
            return;

        auto pattern = find_pattern("39 2D ? ? ? ? 75 3E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);
    }

    // TBoGTHeliHeightLimit: the helicopter ceiling's episode gate -- the branch
    // after the `cmp [episode],2` at the anchor is NOPed, so the ceiling applies
    // on every episode.
    void InstallTBoGTHeliHeightLimit()
    {
        if (!Loader::Enabled("TBoGTHeliHeightLimit"))
            return;

        auto pattern = find_pattern("83 3D ? ? ? ? 02 7C 0A F3 0F");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // PedModelsInCutscenesFix: the cutscene code's player-model check becomes the
    // body, which re-derives the player model's index through the shared resolver
    // and the game lookup and compares that with the object's model word.
    void InstallPedModelsInCutscenesFix()
    {
        if (!Loader::Enabled("PedModelsInCutscenesFix"))
            return;

        // The body resolves "player" and calls the game's model lookup. A missed
        // preamble scan is a null call inside every cutscene object check.
        if (Failsafe::Missing("PedModelsInCutscenesFix model lookup",
                zpatch_model_lookup == 0))
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

    // MouseFix: the pair of axis readers, the three `cvtsi2ss` sites and the two
    // angle sites. Every write is the original handler's: an `E9` at the two axis
    // readers and the flagged reader, the movd pair at +5/+33 with the two-byte
    // nops at +3/+31, the movd single at +5 with its two-byte nop at +3, the two
    // `E8`s at +51/+90 of the int-axis site, and the two `E9`s at the angle site's
    // -15 and +1862.
    void InstallMouseFix()
    {
        if (!Loader::Enabled("MouseFix"))
            return;

        auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisFloatBody), 0x10024060u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisFloatBody), true);
        }

        // The negated-axis reader's anchor differs from the flagged one only in
        // the instruction after the call, so it is its own signature.
        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisNegBody), 0x100240C0u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisNegBody), true);
        }

        constexpr std::string_view kMovdSites[] = {
            "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
            "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07",
        };
        for (const auto site : kMovdSites)
        {
            pattern = find_pattern(site);
            if (pattern.empty())
                continue;

            injector::WriteMemoryRaw(pattern.get_first(5),
                const_cast<uint8_t*>(kMovdXmm0Eax.data()), kMovdXmm0Eax.size(), true);
            injector::WriteMemoryRaw(pattern.get_first(33),
                const_cast<uint8_t*>(kMovdXmm0Eax.data()), kMovdXmm0Eax.size(), true);
            injector::MakeNOP(pattern.get_first(3), 2, true);
            injector::MakeNOP(pattern.get_first(31), 2, true);
        }

        pattern = find_pattern("83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46");
        if (!pattern.empty())
        {
            injector::WriteMemoryRaw(pattern.get_first(5),
                const_cast<uint8_t*>(kMovdXmm1Eax.data()), kMovdXmm1Eax.size(), true);
            injector::MakeNOP(pattern.get_first(3), 2, true);
        }

        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisFlagBody), 0x10024170u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisFlagBody), true);
        }

        // The int-axis helper is called from two places of one function (the
        // anchor's +51 and +90), so both calls are retargeted from the one scan.
        pattern = find_pattern("56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisIntBody), 0x10024120u);
            injector::MakeCALL(pattern.get_first(51),
                reinterpret_cast<void*>(&MfAxisIntBody), true);
            injector::MakeCALL(pattern.get_first(90),
                reinterpret_cast<void*>(&MfAxisIntBody), true);
        }

        // The two site bodies: the angle hook 15 bytes before the anchor and the
        // advance hook 1862 bytes after it.
        pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAngleBody), 0x10024560u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAdvanceBody), 0x100243A0u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAdvanceWindowsBody), 0x100241E0u);
            injector::MakeJMP(pattern.get_first(-15),
                reinterpret_cast<void*>(&MfAngleBody), true);
            injector::MakeJMP(pattern.get_first(1862),
                reinterpret_cast<void*>(&MfAdvanceBody), true);
        }
    }
}

// One of this module's keys is deliberately not installed:
//
//   * OutOfCommissionFix: the key's payload is the original's embedded, compressed
//     finale2.sco (163,796 bytes at 0x100B6798 in .rdata), decoded into a
//     179,536-byte table by sub_1004B570 and then registered and mounted by
//     sub_1004B750 / sub_1004C3F0 (16,311 bytes of C++ with EH4 frames). Its
//     7-instruction branch body and its 29-instruction wrapper are implementable,
//     but the branch only accepts the archive magic the missing table produces, so
//     installing it would widen a check nothing fills. docs/parity.md records what
//     is missing.
//
// It stays open rather than being stubbed.

// Registered like every other arm: the publish tier reads the sites the install
// tier overwrites, and the install tier owns the gate and the writes.
export void RegisterAudio()
{
    // The thunk's slot: published once, before any arm can install the body.
    zpatch_audio_value_resolver = Shared::ValueResolver();

    // The five preamble salts the death-music bodies read, taken from the layer's
    // resolved words rather than written as literals: a hit build keeps the
    // shipped values and 1.0.0.0/1.0.4.0 get the fallback set (source/native.ixx).
    zpatch_audio_salt_10158054 = Native::Salt(0x10158054u);
    zpatch_audio_salt_10158050 = Native::Salt(0x10158050u);
    zpatch_audio_salt_101580A8 = Native::Salt(0x101580A8u);
    zpatch_audio_salt_1015805C = Native::Salt(0x1015805Cu);
    zpatch_audio_salt_10158028 = Native::Salt(0x10158028u);

    // The native-layer pieces the wrappers call, by their recovered VAs. Only one
    // body can carry a recovered VA, so these are the layer's transcriptions
    // reached through audio's forwarders.
    zpatch_audio_context_init = RegisteredBody(0x100038C0u);
    zpatch_audio_invoke_entry = RegisteredBody(0x100038E0u);
    zpatch_audio_cleanup_dword = RegisteredBody(0x1005DE40u);
    zpatch_audio_cleanup_bool = RegisteredBody(0x1005DE80u);
    zpatch_audio_call_hash = RegisteredBody(0x1005B150u);

    Loader::PublishEvent() += []
    {
        PublishRestoreDeathMusic();
        PublishMouseFix();
    };

    Loader::InstallEvent() += []
    {
        InstallAllowHashesForIDE();
        InstallCombinedRadioFreezeFix();
        InstallBetterMPSync();
        InstallIncreasePtrNodes();
        InstallNoPhysicsSleep();
        InstallDoNotPauseOnMinimize();
        InstallEpisodicWeaponSupport();
        InstallPoliceEpisodicWeaponSupport();
        InstallTBoGTHeliHeightLimit();
        InstallPedModelsInCutscenesFix();
        InstallRestoreDeathMusic();
        InstallMouseFix();
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RestoreDeathMusic
//
// The key has three sites: the game's death-music track select function entry and
// its two callers. The entry becomes a call to the install body (sub_10050AD0),
// which asks the gate body (sub_10050A50) whether death music should play and, if
// so, writes a random track number into the game's own `push imm8`. The gate body
// answers through three hash salts and four native-script calls, so the pieces it
// needs from the native layer are transcribed here as well: the two-argument
// wrapper (sub_1005B1E0, salt dword_10158054), the byte wrapper (sub_1005B490,
// salt dword_10158028) and the zero-argument bool wrapper (sub_1005B360, hash in
// ECX), plus the layer's own sub_1005B150/sub_100038C0/sub_100038E0/sub_1005DE40/
// sub_1005DE80 reached through forwarders.
// ---------------------------------------------------------------------------

// sub_10005ED0, recovered VA 0x10005ED0 (6 bytes, 2 instructions): the radio
// freeze value. The site is a call into the game's radio update; the body returns
// 19 in its place. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails (C2488).
static __declspec(naked) void CombinedRadioFreezeFixBody()
{
    __asm
    {
        mov eax, 13h
        ret
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

// ---------------------------------------------------------------------------
// MouseFix
//
// The game reads the mouse axes as 1/128 fixed point and converts them with
// integer-to-float sequences; the fix replaces those conversions with a multiply
// of the stored int, and it advances the per-vehicle camera angle by the mouse
// delta. Four axis helpers answer in st(0)/EAX on the game's behalf (0x10024060,
// 0x100240C0, 0x10024120, 0x10024170), the angle-step helper (0x10024440) advances
// yaw and pitch, and the two site bodies save and restore xmm0-7 around them
// (0x10024560 at the angle hook, 0x100243A0 at the advance hook). The advance
// helper (0x100241E0) patches three 8-byte windows of the resumed function so the
// game's own stores pull the values the helper computed.
//
// All eight are transcribed: none carries an EH frame, and their only
// image-internal references are the published words above.
// ---------------------------------------------------------------------------

// sub_10024060, recovered VA 0x10024060 (30 instructions): X or Y times 1/128,
// answered in st(0). Axis 0 is X, axis 1 is Y, anything else is +0.0.
static __declspec(naked) void MfAxisFloatBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        sub esp, 8
        test eax, eax
        jne mf_axis_float_y
        mov eax, DWORD PTR [zpatch_mf_x]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        movsd QWORD PTR [ebp - 8], xmm0
        fld QWORD PTR [ebp - 8]
        mov esp, ebp
        pop ebp
        ret
    mf_axis_float_y:
        cmp eax, 1
        jne mf_axis_float_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        movsd QWORD PTR [ebp - 8], xmm0
        fld QWORD PTR [ebp - 8]
        mov esp, ebp
        pop ebp
        ret
    mf_axis_float_zero:
        fldz
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_100240C0, recovered VA 0x100240C0 (23 instructions): the negated axis, as a
// float scaled by -127.5. The sign mask is the original's xorps pair.
static __declspec(naked) void MfAxisNegBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_neg_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_neg_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        jmp mf_axis_neg_scale
    mf_axis_neg_y:
        cmp eax, 1
        jne mf_axis_neg_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_neg_xy
    mf_axis_neg_zero:
        xorps xmm0, xmm0
    mf_axis_neg_scale:
        movss xmm1, DWORD PTR [zpatch_mf_sign]
        xorps xmm1, XMMWORD PTR [zpatch_mf_single_sign]
        cvtpd2ps xmm0, xmm0
        mulss xmm1, xmm0
        movss DWORD PTR [ebp + 0Ch], xmm1
        mov eax, DWORD PTR [ebp + 0Ch]
        pop ebp
        ret
    }
}

// sub_10024120, recovered VA 0x10024120 (22 instructions): the axis as an int,
// `(int)(value * 1/128 * 255)`.
static __declspec(naked) void MfAxisIntBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_int_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_int_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        mulsd xmm0, QWORD PTR [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop ebp
        ret
    mf_axis_int_y:
        cmp eax, 1
        jne mf_axis_int_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_int_xy
    mf_axis_int_zero:
        xorps xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop ebp
        ret
    }
}

// sub_10024170, recovered VA 0x10024170 (28 instructions): the negated axis, with
// the sign flipped back when the game's invert byte is set.
static __declspec(naked) void MfAxisFlagBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_flag_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_flag_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        jmp mf_axis_flag_scale
    mf_axis_flag_y:
        cmp eax, 1
        jne mf_axis_flag_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_flag_xy
    mf_axis_flag_zero:
        xorps xmm0, xmm0
    mf_axis_flag_scale:
        movss xmm1, DWORD PTR [zpatch_mf_sign]
        xorps xmm1, XMMWORD PTR [zpatch_mf_single_sign]
        mov eax, DWORD PTR [zpatch_mf_flag]
        cvtpd2ps xmm0, xmm0
        cmp BYTE PTR [eax], 0
        mulss xmm1, xmm0
        movss DWORD PTR [ebp + 0Ch], xmm1
        jz mf_axis_flag_store
        mulss xmm1, DWORD PTR [zpatch_mf_mul_neg1]
        movss DWORD PTR [ebp + 0Ch], xmm1
    mf_axis_flag_store:
        mov eax, DWORD PTR [ebp + 0Ch]
        pop ebp
        ret
    }
}

// sub_10024440, recovered VA 0x10024440 (62 instructions): advance the vehicle's
// pitch (+144h) and yaw (+148h) by the mouse delta scaled by the frame-time
// factor, wrapping yaw into [-pi, pi]. Reads `this` from ECX, calls the game's
// frame clock through the published slot, and keeps the original's two exits.
static __declspec(naked) void MfAngleStepBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0Ch
        push esi
        mov esi, ecx
        call DWORD PTR [zpatch_mf_time]
        movss xmm2, DWORD PTR [zpatch_mf_step_scale]
        mov eax, DWORD PTR [zpatch_mf_x]
        fstp QWORD PTR [esp + 8]
        movsd xmm0, QWORD PTR [esp + 8]
        mulsd xmm0, QWORD PTR [zpatch_mf_time_mul]
        movd xmm4, DWORD PTR [eax]
        mov eax, DWORD PTR [zpatch_mf_y]
        cvtdq2pd xmm4, xmm4
        addsd xmm0, QWORD PTR [zpatch_mf_time_add]
        movd xmm3, DWORD PTR [eax]
        mulsd xmm4, QWORD PTR [zpatch_mf_scale]
        mov eax, DWORD PTR [zpatch_mf_flag]
        cvtdq2pd xmm3, xmm3
        cmp BYTE PTR [eax], 0
        xorps xmm4, XMMWORD PTR [zpatch_mf_double_sign]
        mulsd xmm3, QWORD PTR [zpatch_mf_scale]
        cvtpd2ps xmm0, xmm0
        xorps xmm3, XMMWORD PTR [zpatch_mf_double_sign]
        mulss xmm2, xmm0
        mulss xmm2, DWORD PTR [esi + 60h]
        jz mf_angle_step_skip
        mulsd xmm3, QWORD PTR [zpatch_mf_mul_neg1_d]
    mf_angle_step_skip:
        movss xmm0, DWORD PTR [esi + 144h]
        cvtps2pd xmm2, xmm2
        cvtps2pd xmm0, xmm0
        movaps xmm1, xmm2
        mulsd xmm2, xmm4
        mulsd xmm1, xmm3
        addsd xmm1, xmm0
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 144h], xmm0
        movss xmm0, DWORD PTR [esi + 148h]
        cvtps2pd xmm0, xmm0
        addsd xmm2, xmm0
        cvtpd2ps xmm0, xmm2
        cvtps2pd xmm1, xmm0
        comisd xmm1, QWORD PTR [zpatch_mf_pi]
        jbe mf_angle_step_low
        subsd xmm1, QWORD PTR [zpatch_mf_two_pi]
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 148h], xmm0
        pop esi
        mov esp, ebp
        pop ebp
        ret
    mf_angle_step_low:
        movsd xmm0, QWORD PTR [zpatch_mf_neg_pi]
        comisd xmm0, xmm1
        jbe mf_angle_step_store
        addsd xmm1, QWORD PTR [zpatch_mf_two_pi]
    mf_angle_step_store:
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 148h], xmm0
        pop esi
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10024560, recovered VA 0x10024560 (25 instructions): the angle site's body.
// It saves xmm0-7, advances the angles through the helper, restores them, replays
// the fifteen bytes of the game's prologue the jump replaced, and tail-jumps to
// the published resume (the site's own match).
static __declspec(naked) void MfAngleBody()
{
    __asm
    {
        pushad
        movss DWORD PTR [zpatch_mf_xmm_save], xmm0
        movss DWORD PTR [zpatch_mf_xmm_save + 4], xmm1
        movss DWORD PTR [zpatch_mf_xmm_save + 8], xmm2
        movss DWORD PTR [zpatch_mf_xmm_save + 12], xmm3
        movss DWORD PTR [zpatch_mf_xmm_save + 16], xmm4
        movss DWORD PTR [zpatch_mf_xmm_save + 20], xmm5
        movss DWORD PTR [zpatch_mf_xmm_save + 24], xmm6
        movss DWORD PTR [zpatch_mf_xmm_save + 28], xmm7
        call MfAngleStepBody
        movss xmm0, DWORD PTR [zpatch_mf_xmm_save]
        movss xmm1, DWORD PTR [zpatch_mf_xmm_save + 4]
        movss xmm2, DWORD PTR [zpatch_mf_xmm_save + 8]
        movss xmm3, DWORD PTR [zpatch_mf_xmm_save + 12]
        movss xmm4, DWORD PTR [zpatch_mf_xmm_save + 16]
        movss xmm5, DWORD PTR [zpatch_mf_xmm_save + 20]
        movss xmm6, DWORD PTR [zpatch_mf_xmm_save + 24]
        movss xmm7, DWORD PTR [zpatch_mf_xmm_save + 28]
        popad
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF0h
        xorps xmm0, xmm0
        sub esp, 94h
        jmp DWORD PTR [zpatch_mf_resume_a]
    }
}

// sub_100241E0, recovered VA 0x100241E0 (138 instructions): the advance helper.
// It evaluates Y and then X (both unguarded, as in the original), picks the 8-byte
// window image the resumed function must carry, and writes three 8-byte windows at
// resume+34h, +5Eh and +6Bh through VirtualProtect. The window bytes are the
// original's immediates: `movss [esi+144h], xmm0` / `movss [esi+148h], xmm1` /
// `fstp [esi+148h]` for the pull form, and their disabled forms (`nop` fill, and
// `fstp st(0)` for the x87 window because the enclosing routine pops that value
// later). The /GS frame is the original's.
static __declspec(naked) void MfAdvanceWindowsBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 8], eax
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        push ebx
        push esi
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        push edi
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_0
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_0
        mov edi, 86110FF3h
        mov ebx, 144h
        jmp mf_advance_write_0
    mf_advance_disabled_0:
        mov edi, 90909090h
        mov ebx, edi
    mf_advance_write_0:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 0Ch]
        push eax
        push 40h
        push 8
        add esi, 34h
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        lea eax, [esp + 0Ch]
        mov DWORD PTR [esi + 4], ebx
        mov ebx, DWORD PTR [zpatch_mf_virtual_protect]
        push eax
        push DWORD PTR [esp + 10h]
        mov DWORD PTR [esi], edi
        push 8
        push esi
        call ebx
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_1
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_1
        mov edi, 8E110FF3h
        mov DWORD PTR [esp + 0Ch], 148h
        jmp mf_advance_write_1
    mf_advance_disabled_1:
        mov edi, 90909090h
        mov DWORD PTR [esp + 0Ch], edi
    mf_advance_write_1:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 10h]
        push eax
        push 40h
        push 8
        add esi, 5Eh
        push esi
        call ebx
        mov eax, DWORD PTR [esp + 0Ch]
        mov DWORD PTR [esi + 4], eax
        lea eax, [esp + 10h]
        push eax
        push DWORD PTR [esp + 14h]
        mov DWORD PTR [esi], edi
        push 8
        push esi
        call ebx
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_2
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_2
        mov edi, 1489ED9h
        mov ebx, 5C8B0000h
        jmp mf_advance_write_2
    mf_advance_disabled_2:
        mov edi, 9090D8DDh
        mov ebx, 5C8B9090h
    mf_advance_write_2:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 10h]
        push eax
        push 40h
        push 8
        add esi, 6Bh
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        lea eax, [esp + 10h]
        mov DWORD PTR [esi], edi
        push eax
        push DWORD PTR [esp + 14h]
        mov DWORD PTR [esi + 4], ebx
        push 8
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        mov ecx, DWORD PTR [esp + 14h]
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

// sub_100243A0, recovered VA 0x100243A0 (23 instructions): the advance site's
// body. The same xmm save/restore around the advance helper, then it replays the
// three instructions the jump replaced and tail-jumps to the published resume.
static __declspec(naked) void MfAdvanceBody()
{
    __asm
    {
        pushad
        movss DWORD PTR [zpatch_mf_xmm_save], xmm0
        movss DWORD PTR [zpatch_mf_xmm_save + 4], xmm1
        movss DWORD PTR [zpatch_mf_xmm_save + 8], xmm2
        movss DWORD PTR [zpatch_mf_xmm_save + 12], xmm3
        movss DWORD PTR [zpatch_mf_xmm_save + 16], xmm4
        movss DWORD PTR [zpatch_mf_xmm_save + 20], xmm5
        movss DWORD PTR [zpatch_mf_xmm_save + 24], xmm6
        movss DWORD PTR [zpatch_mf_xmm_save + 28], xmm7
        call MfAdvanceWindowsBody
        movss xmm0, DWORD PTR [zpatch_mf_xmm_save]
        movss xmm1, DWORD PTR [zpatch_mf_xmm_save + 4]
        movss xmm2, DWORD PTR [zpatch_mf_xmm_save + 8]
        movss xmm3, DWORD PTR [zpatch_mf_xmm_save + 12]
        movss xmm4, DWORD PTR [zpatch_mf_xmm_save + 16]
        movss xmm5, DWORD PTR [zpatch_mf_xmm_save + 20]
        movss xmm6, DWORD PTR [zpatch_mf_xmm_save + 24]
        movss xmm7, DWORD PTR [zpatch_mf_xmm_save + 28]
        popad
        mulss xmm0, xmm2
        addss xmm0, xmm3
        movss xmm3, DWORD PTR [esi + 150h]
        jmp DWORD PTR [zpatch_mf_resume_b]
    }
}
