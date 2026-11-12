module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call, and the CRT calls the death-music body makes.
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
import options;
import patch;
import shared;

// MISC: RestoreDeathMusic, OverrideGTADatPath/OverrideGTADatPathTo,
// AllowHashesForIDE, PlayerAsPedComponentsFix and PedModelsInCutscenesFix. The
// keys came from the old `audio` module (the death-music payload, the model-hash
// hook, the cutscene player-model check), `loading` (the gta.dat path) and
// `stability` (the ped-components shim's arm).
export namespace Misc
{
    void Prepare();

    void PublishRestoreDeathMusic();
    void CopyOverrideDatPath();
    void PublishPlayerAsPedComponentsFix();

    void InstallRestoreDeathMusic();
    void InstallOverrideGTADatPath();
    void InstallAllowHashesForIDE();
    void InstallPedModelsInCutscenesFix();
    void InstallPlayerAsPedComponentsFix();
}

// The shared resolver's address, for the thunk below. The original's body calls
// sub_10003E50 with one direct `E8`; ours cannot name it: the resolver is
// `static` in shared.ixx (internal linkage), and C2488 forbids giving a naked
// function external linkage in a module interface unit, so no importer can use
// it as a link-time call target. The body calls this module's one-instruction
// thunk instead, which jumps through this slot -- the single deviation from the
// original's instruction stream in this body.
extern "C"
{
    // dword_10159C0C, the game's model-index lookup shared.ixx publishes: the
    // PedModels body reads it through the same slot.
    extern uintptr_t zpatch_model_lookup;
    // dword_10159D2C: PlayerAsPedComponentsFix's published resume (source/shared.ixx).
    extern uint32_t zpatch_player_ped_resume;

    uintptr_t zpatch_misc_value_resolver = 0;

    // The "player" model name the PedModels body pushes (original 0x1013A038). A
    // copy rather than shared.ixx's literal: a module-attached literal is not
    // visible to an importer, and the proof compares the push as a slot anyway.
    const char zpatch_misc_player_name[] = "player";
}

// RestoreDeathMusic's published state. The gate body reads the original's option
// word and three hash salts; the install body keeps the site it resolves once and
// the clock word its seed guard compares against. Every salt below is a copy of
// one of the native layer's ten resolved words, taken with `Native::Salt(ref)` at
// registration time and never written as a literal: on 1.0.8.0 the probe hits and
// the shipped `.data` values stand, while 1.0.0.0 and 1.0.4.0 miss and the
// preamble's fallback set applies (source/native.ixx).
extern "C"
{
    // byte_10159B86: the option word the gate body tests. The original's loader
    // preamble stores it from the INI before any body runs.
    uint8_t zpatch_misc_death_music_flag = 0;

    // dword_101A84B8: the `6A 06 68 ? ? ? ? 8D 8C` site the install body resolves
    // once. The original's other word, dword_101A84C0, is the guard for its CRT
    // clock/seed block; the rebuild does not carry that block, so
    // the slot is not published.
    uint32_t zpatch_misc_death_music_site = 0;

    // The five preamble salts this key's bodies and wrappers read:
    //   dword_1015805C / dword_101580A8 / dword_10158050 -- sub_10050A50
    //   dword_10158054 -- the two-argument wrapper sub_1005B1E0
    //   dword_10158028 -- the byte wrapper sub_1005B490
    uint32_t zpatch_misc_salt_10158054 = 0;
    uint32_t zpatch_misc_salt_10158050 = 0;
    uint32_t zpatch_misc_salt_101580A8 = 0;
    uint32_t zpatch_misc_salt_1015805C = 0;
    uint32_t zpatch_misc_salt_10158028 = 0;

    // The native-layer pieces the three wrappers call, reached through
    // one-instruction `jmp` forwarders for the reason the multiplayer module's
    // BadPlayerModelCrashFix forwarders exist: a module interface cannot export a
    // naked function, and a C entry's decorated name is not nameable from inline
    // asm. Filled from the body registry by each piece's recovered VA.
    uintptr_t zpatch_misc_context_init = 0;
    uintptr_t zpatch_misc_invoke_entry = 0;
    uintptr_t zpatch_misc_cleanup_dword = 0;
    uintptr_t zpatch_misc_cleanup_bool = 0;
    uintptr_t zpatch_misc_call_hash = 0;

    // OverrideGTADatPath's sibling key: the path the game's gta.dat push is
    // retargeted to. The original's buffer is 64 bytes and its static initializer
    // holds the stock path; the loader overwrites it from OverrideGTADatPathTo
    // with strcpy_s, so a value of 64 bytes or more faults through the CRT's
    // invalid-parameter path (preserved). Absent or "0" leaves the initializer.
    char zpatch_dat_path[64] = "common:/data/gta.dat";
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void __cdecl AudioZeroBlock(void* destination, int value, unsigned int count);
static char DeathMusicInstallBody();
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

// The CRT `memset` the three wrappers open with. The original calls the CRT's own
// thunk (0x10060D6E) with the same argument order; the inline assembler rejects a
// direct call to the intrinsic's name (C2420), so the call names this cdecl
// forwarder, which is still one `E8`.
static void __cdecl AudioZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

static char DeathMusicInstallBody()
{
    if (!DeathMusicGateBody())
        return 0;

    if (zpatch_misc_death_music_site == 0)
    {
        auto pattern = find_pattern("6A 06 68 ? ? ? ? 8D 8C");
        if (!pattern.empty())
        {
            zpatch_misc_death_music_site = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
        }
    }

    if (zpatch_misc_death_music_site != 0)
    {
        // The site is `push 6`; the second byte is the immediate the track number
        // replaces. The original writes it unguarded, and so does this.
        auto* site = reinterpret_cast<std::byte*>(
            static_cast<uintptr_t>(zpatch_misc_death_music_site) + 1);
        injector::WriteMemory<std::byte>(site,
            static_cast<std::byte>(std::rand() % 5 + 1), true);
    }
    return 1;
}

namespace Misc
{
    void Prepare()
    {
        // The thunk's slot: published once, before any arm can install the body.
        zpatch_misc_value_resolver = Shared::ValueResolver();

        // The five preamble salts the death-music bodies read, taken from the layer's
        // resolved words rather than written as literals: a hit build keeps the
        // shipped values and 1.0.0.0/1.0.4.0 get the fallback set (source/native.ixx).
        zpatch_misc_salt_10158054 = Native::Salt(0x10158054u);
        zpatch_misc_salt_10158050 = Native::Salt(0x10158050u);
        zpatch_misc_salt_101580A8 = Native::Salt(0x101580A8u);
        zpatch_misc_salt_1015805C = Native::Salt(0x1015805Cu);
        zpatch_misc_salt_10158028 = Native::Salt(0x10158028u);

        // The native-layer pieces the wrappers call, by their recovered VAs. Only one
        // body can carry a recovered VA, so these are the layer's transcriptions
        // reached through this module's forwarders.
        zpatch_misc_context_init = static_cast<uint32_t>(Bodies::AddressFor(0x100038C0u));
        zpatch_misc_invoke_entry = static_cast<uint32_t>(Bodies::AddressFor(0x100038E0u));
        zpatch_misc_cleanup_dword = static_cast<uint32_t>(Bodies::AddressFor(0x1005DE40u));
        zpatch_misc_cleanup_bool = static_cast<uint32_t>(Bodies::AddressFor(0x1005DE80u));
        zpatch_misc_call_hash = static_cast<uint32_t>(Bodies::AddressFor(0x1005B150u));
    }
}

namespace Misc
{

    // RestoreDeathMusic's option word. The original's loader preamble stores it
    // from the INI before any body runs and the gate body tests it at run time;
    // ours is set in the publish tier, once the key is known to be on.
    void PublishRestoreDeathMusic()
    {
        if (!Loader::Enabled("RestoreDeathMusic"))
            return;

        zpatch_misc_death_music_flag = 1;
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

    void CopyOverrideDatPath()
    {
        const std::string fallback("0");
        const auto& value = Feather::Value(Config::Ini(), "Options", "OverrideGTADatPathTo", fallback);
        if (value == "0")
            return;

        strcpy_s(zpatch_dat_path, sizeof(zpatch_dat_path), value.c_str());
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
        jmp DWORD PTR [zpatch_misc_value_resolver]
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
        push OFFSET zpatch_misc_player_name
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
    __asm { jmp DWORD PTR [zpatch_misc_context_init] }
}

static __declspec(naked) void AudioInvokeEntry()
{
    __asm { jmp DWORD PTR [zpatch_misc_invoke_entry] }
}

static __declspec(naked) void AudioCleanupDwordEntry()
{
    __asm { jmp DWORD PTR [zpatch_misc_cleanup_dword] }
}

static __declspec(naked) void AudioCleanupBoolEntry()
{
    __asm { jmp DWORD PTR [zpatch_misc_cleanup_bool] }
}

static __declspec(naked) void AudioCallHashEntry()
{
    __asm { jmp DWORD PTR [zpatch_misc_call_hash] }
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
        mov eax, DWORD PTR [zpatch_misc_salt_10158054]
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
        mov eax, DWORD PTR [zpatch_misc_salt_10158028]
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
        cmp BYTE PTR [zpatch_misc_death_music_flag], 0
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
        mov ecx, DWORD PTR [zpatch_misc_salt_1015805C]
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
        mov ecx, DWORD PTR [zpatch_misc_salt_101580A8]
        call AudioCallZeroBoolBody
        test al, al
        je death_music_answer_zero
        mov ecx, DWORD PTR [zpatch_misc_salt_10158050]
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
