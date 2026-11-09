module;

#include <common.hxx>
// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h).
#include <intrin.h>
#include <cstdlib>
#include <cstring>

export module menu;

import bodies;
import common;
import game;
import loader;
import native;
import patch;

// The IVMenuAPI integration: the `AddOptionsInMenu` key.
//
// The key is not write-free. The original's config walker reads it into
// byte_10158038 (`strtol(...) != 0`, absent default "1"), and its consumer
// sub_10052380 is the companion-library probe:
//
//   10052380  cmp byte_10158038, 0
//   10052387  jz  short loc_100523BA
//   10052389  push offset "IVMenuAPI.asi"; call ds:GetModuleHandleA
//   10052394  test eax, eax
//   10052396  jz  short loc_1005239B
//   10052398  mov al, 1
//   1005239A  retn
//   1005239B  cmp byte_101A843C, 0
//   100523A2  jnz short loc_100523B3
//   100523A4  push offset "IVMenuAPI.asi"; call ds:LoadLibraryA
//   100523AF  test eax, eax
//   100523B1  jnz short loc_10052398
//   100523B3  mov byte_101A843C, 1
//   100523BA  xor al, al
//   100523BC  retn
//
// Only when that probe answers 1 does the walker run the three integration
// steps, and they are the whole arm:
//
//   * `if (sub_10052380()) sub_1001EDE0();` at 0x100557E9 -- sub_1001EDE0 is a
//     writer: it calls sub_1001ECB0 (the DoNotPauseOnMinimize site) and then
//     writes byte 6 over the `push 0Ah` in `6A 0A 56 50 FF D2 A1`.
//   * `if (RestoreDeathMusic == 1 || sub_10052380()) sub_10050BD0();` at
//     0x10055B8F/0x10055B98 -- sub_10050BD0 installs the three death-music
//     hooks. The RestoreDeathMusic arm in source/audio.ixx owns the first half
//     of that OR; this module owns the second.
//   * `if (sub_10052380()) { sub_100522B0(...Death Music...); sub_100522B0(...Pause
//     Game On Focus Loss...); }` at 0x10055BE6..0x10055C36 -- two
//     AddIVMenuOption registrations with four callbacks.
//
// The fault profile is the original's: a failed `LoadLibraryA` is a clean skip
// (no write, no callbacks, no error). Loading a DLL from DllMain runs under the
// loader lock, exactly as the original's config function did; that is preserved,
// not "improved" -- the call is made from the install tier, which the startup
// driver runs synchronously from DllMain.
//
// The gate is the original's own, and it is not `== 1`: the walker stores the
// parse's *answer*, `0x1005332E test ecx,ecx` / `0x10053337 setnz
// byte_10158038`, over the inlined `std::stoi` of the value it read at
// 0x1005327B with the default "1" (0x1005323F):
//
//   100532FD  push 0Ah ; Radix
//   100532FF  push eax ; EndPtr
//   10053300  push esi ; String
//   10053301  call ds:strtol
//   1005330A  mov ecx, eax
//   1005330C  cmp esi, [ebp-0C8h]
//   10053312  jnz short loc_1005331F      ; else _Xinvalid_argument
//   1005332E  test ecx, ecx
//   10053337  setnz byte_10158038
//
// So `AddOptionsInMenu=2` enables the key exactly as `1` does, and a non-numeric
// value still throws out of the loader. This module asks
// `Loader::EnabledNonZero` for that reason; every other key keeps the house
// `== 1` (`Loader::Enabled`).
//
// One entry point here is *not* part of this key: the ZMenuIV version guard
// sub_10052300, which the walker calls unconditionally at 0x1005527B. It has no
// key test in front of it -- the call is reached by falling through from
// SuperLODFix's handler (0x10055276) and returns to VSyncFix's read
// (0x10055280) -- so it is registered as an unconditional startup handler, in the
// publish tier (the tree's first pass; see the registration).
//
// The guard is a whole body, transcribed: 38 instructions, a plain /GS frame (no
// exception frame and no TLS static-init guard), one import call per step. In the
// original it reads:
//
//   10052310  push offset "ZMenuIV.asi"; call ds:GetModuleHandleA
//   1005231B  test eax, eax
//   1005231D  jz  short loc_10052368            ; no library -> clean skip
//   1005231F  push offset "GetZMenuVersion"
//   10052324  push eax; call ds:GetProcAddress
//   1005232B  test eax, eax
//   1005232D  jz  short loc_10052368            ; no export  -> clean skip
//   1005232F  lea ecx,[ebp+var_C]; push ecx
//   10052333  lea ecx,[ebp+var_8]; push ecx
//   10052337  call eax                          ; GetZMenuVersion(&version, &unused)
//   10052339  add esp, 8
//   1005233C  cmp [ebp+var_8], 35DD1h
//   10052343  jnb short loc_10052368            ; 220625 or newer -> nothing
//   10052345  push 10h                          ; MB_ICONERROR
//   10052347  push offset Caption               ; the .rdata empty string, 0x10063CD6
//   1005234C  push offset "Incompatible/outdated version of ZMenu detected! ..."
//   10052351  push 0; call ds:MessageBoxA
//   10052359  push 0; call ds:GetCurrentProcess
//   10052361  push eax; call ds:TerminateProcess
//   10052368  mov ecx,[ebp+var_4]; xor ecx,ebp; call __security_check_cookie
//   10052372  mov esp,ebp; pop ebp; retn
//
// Nothing else consumes the answer: no log, no store, no patch. The library's
// export is handed two stack out-parameters and the second (`var_C`) is never
// read, and the first (`var_8`) is read *uninitialised* if the library writes
// neither -- the original's own fault profile, kept: the slot is not zeroed here
// either. A missing library or a missing export is the original's clean path (the
// two `jz loc_10052368`s), which is also what keeps this arm inert in the
// harness: `zp_dump` has no `ZMenuIV.asi` beside it.
//
// The two writers (sub_1001EDE0, sub_10050BD0) are transcribed as bodies. The
// two helpers they call are C++ rebuilds because they are compiler output for
// functions with an EH frame and a TLS static-init guard:
//
//   * sub_1001ECB0: `1001ecd1 lea eax,[ebp-0Ch]` / `1001ecd4 mov large fs:0,eax`
//     (the EH4 frame) and `1001ece6 mov ecx,[eax+ecx*4]` / `1001ecfb call
//     __Init_thread_header` (the TLS guard around the one-time scan).
//   * sub_10050AD0: the same pair (`10050af1 lea eax,[ebp-0Ch]` /
//     `10050af4 mov large fs:0,eax`; `10050b13 mov ecx,[eax+ecx*4]` /
//     `10050b28 call __Init_thread_header`), plus the CRT's own thread-state
//     helpers, which a rebuilt module cannot name.
//
// The death-music gate and its three native-script wrappers are transcriptions
// too. They are the same recovered pieces the RestoreDeathMusic arm carries in
// source/audio.ixx; that module keeps them internal (a body has internal
// linkage), and the AddOptionsInMenu chain needs them under its own key, so this
// module carries its own copies and proves them. Only one copy is live in any
// configuration: audio's when RestoreDeathMusic is on (this arm then skips
// sub_10050BD0's installs, because the original calls it once on the OR), and
// this module's when RestoreDeathMusic is off and the companion library loaded.

// The published state, in C linkage because the transcribed code names the slots
// directly (the original keeps them in its own .bss beside its bodies).
extern "C"
{
    // byte_10159B86, the death-music option word. source/audio.ixx defines it:
    // the RestoreDeathMusic arm's gate body reads the same byte, and the menu's
    // Death Music getter/setter is the original's sub_10050A30/sub_10050A40,
    // which read and write this one byte. Sharing the slot is what keeps the two
    // configurations on the original's single word.
    extern uint8_t zpatch_audio_death_music_flag;

    // byte_1015AD43, the focus-loss word. The original's config walker sets it
    // from DoNotPauseOnMinimize before any menu callback can read it; this
    // module's publish/register seeds it the same way and sub_1001EDC0's
    // transcription is the only writer afterwards.
    uint8_t zpatch_menu_focus_flag = 0;

    // The focus site and its pre-write word: sub_1001ECB0's one-time scan result
    // (`lpAddress`) and its word cache (`word_1015AD48`). The original fills both
    // on the first call, which happens before its own write; this tree's publish
    // tier resolves them before the install tier, because the DoNotPauseOnMinimize
    // arm overwrites the same two bytes first.
    uint32_t zpatch_menu_focus_site = 0;
    uint32_t zpatch_menu_focus_word = 0;

    // dword_101A84B8: sub_10050AD0's one-time `6A 06 68 ? ? ? ? 8D 8C` scan.
    uint32_t zpatch_menu_death_site = 0;

    // The five preamble salts the death-music gate and wrappers read, copied from
    // Native::Salt(ref) at registration (never literals: a probe miss substitutes
    // the fallback set -- source/native.ixx).
    uint32_t zpatch_menu_salt_10158054 = 0;  // sub_1005B1E0, two-argument wrapper
    uint32_t zpatch_menu_salt_10158050 = 0;  // sub_10050A50
    uint32_t zpatch_menu_salt_101580A8 = 0;  // sub_10050A50
    uint32_t zpatch_menu_salt_1015805C = 0;  // sub_10050A50
    uint32_t zpatch_menu_salt_10158028 = 0;  // sub_1005B490, byte wrapper

    // The native-layer pieces the wrappers call, reached through one-instruction
    // `jmp` forwarders (a module interface cannot export a naked function).
    uintptr_t zpatch_menu_context_init = 0;   // 0x100038C0
    uintptr_t zpatch_menu_invoke_entry = 0;   // 0x100038E0
    uintptr_t zpatch_menu_cleanup_dword = 0;  // 0x1005DE40
    uintptr_t zpatch_menu_cleanup_bool = 0;   // 0x1005DE80
    uintptr_t zpatch_menu_call_hash = 0;      // 0x1005B150

    // The two imported calls the transcribed writers make (`call ds:...` in the
    // original): GetModuleHandleA(0) and VirtualProtect.
    uintptr_t zpatch_menu_get_module = 0;
    uintptr_t zpatch_menu_virtual_protect = 0;

    // The four imports the ZMenuIV guard calls, one per `call ds:...` in the
    // original: GetModuleHandleA("ZMenuIV.asi"), GetProcAddress(h, ...),
    // MessageBoxA and the process pair the outdated-version path terminates.
    uintptr_t zpatch_menu_get_proc_address = 0;
    uintptr_t zpatch_menu_message_box = 0;
    uintptr_t zpatch_menu_get_current_process = 0;
    uintptr_t zpatch_menu_terminate_process = 0;
}

// The transcribed bodies are defined in the tail. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above names each
// body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void MenuWriterBody();
static void MenuInstallerBody();
static bool MenuDeathGateBody();
static unsigned int __fastcall MenuCallTwoBody(unsigned int, unsigned int);
static unsigned int __fastcall MenuCallZeroBoolBody(unsigned int);
static unsigned int __fastcall MenuCallByteBody(unsigned int, unsigned int);
static void MenuDeathGetBody();
static void MenuDeathSetBody(unsigned int, unsigned int);
static void MenuFocusGetBody();
static void MenuFocusSetBody(unsigned int, unsigned int);
static void ZMenuVersionGuardBody();
static void MenuContextInitEntry();
static void MenuInvokeEntry();
static void MenuCleanupDwordEntry();
static void MenuCleanupBoolEntry();
static void MenuCallHashEntry();

// The one-shot load-attempt word, byte_101A843C: a failed LoadLibraryA is not
// retried.
static bool zpatch_menu_load_attempted = false;

// The three pattern strings the two writers hand to the scanner. They are the
// original's own .rdata strings, byte for byte.
static const char kMenuPausePattern[] = "6A 0A 56 50 FF D2 A1";
static const char kMenuDeathPatternA[] = "0F 57 C0 F3 0F 11 86 20 02 00 00";
static const char kMenuDeathPatternB[] = "C7 86 04 02 00 00 04 00 00 00 E9";
static const char kMenuDeathPatternC[] = "E8 ? ? ? ? 84 C0 74 C6 8B BE";

// The ZMenuIV guard's own strings, the original's `.rdata` at 0x1012FD34,
// 0x1012FD88, 0x1012FDE0 and 0x10063CD6. The caption is the image's shared empty
// literal (a single 0x00 byte, also used as a `std::string` source elsewhere), so
// the original's message box has an empty title -- reproduced, not tidied up.
static const char kZmenuLibraryName[] = "ZMenuIV.asi";
static const char kZmenuVersionExport[] = "GetZMenuVersion";
static const char kZmenuOutdatedText[] = "Incompatible/outdated version of ZMenu detected! The game will now exit.";
static const char kZmenuCaption[] = "";

// The CRT `memset` the three wrappers open with. The original calls the CRT's
// own thunk (0x10060D6E) with the same argument order; the inline assembler
// rejects a direct call to the intrinsic's name (C2420), so the call names this
// cdecl forwarder, which is still one `E8`. Same shape as native.ixx's
// NativeZeroBlock.
static void __cdecl MenuZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

// The original's `sub_10003830(pattern, moduleHandle)`: the ASI's own scanner,
// returning the first match or 0. The transcribed writers call it with the
// pattern string in ECX and the module handle in EDX; this rebuild takes the
// string in ECX (__fastcall) and ignores the handle, because the tree's scanner
// reads the same game image the handle names.
static void* __fastcall ScanFirst(const char* signature)
{
    auto pattern = find_pattern(signature);
    return pattern.empty() ? nullptr : pattern.get_first<std::byte>(0);
}

// sub_1001ECB0, rebuilt: the DoNotPauseOnMinimize site writer. The original
// scans `75 ? 8B 0D ...` once (TLS-guarded), caches the pre-write word and then
// writes `9090` when byte_1015AD43 is set, or the cached word back when it is
// clear. Both the scan and the word are resolved in the publish tier, because
// audio's DoNotPauseOnMinimize arm overwrites this site before this arm runs.
// Returns the original's answer: 0 when the scan missed, nonzero after a write.
static uint32_t __cdecl MenuRebuildFocusSite()
{
    if (zpatch_menu_focus_site == 0)
        return 0;

    if (zpatch_menu_focus_word == 0)
    {
        uint16_t word = 0;
        std::memcpy(&word, reinterpret_cast<const void*>(
            static_cast<uintptr_t>(zpatch_menu_focus_site)), sizeof(word));
        zpatch_menu_focus_word = word;
    }

    const auto value = zpatch_menu_focus_flag != 0
        ? static_cast<uint16_t>(0x9090u)
        : static_cast<uint16_t>(zpatch_menu_focus_word);
    injector::WriteMemory<uint16_t>(
        static_cast<uintptr_t>(zpatch_menu_focus_site), value, true);
    return 1;
}

// sub_10050AD0, rebuilt: the death-music track install. It asks the gate first,
// resolves `6A 06 68 ? ? ? ? 8D 8C` once, and writes a random track number 1..5
// into the push immediate at site+1. Rebuilt because the original is compiler
// output with an EH4 frame (handler 0x100619C1, scope table 0x10154384) and a
// clock/seed block that calls the static CRT's own TLS helpers; the write itself
// is the original's `rand() % 5 + 1`.
static char MenuDeathInstallBody()
{
    if (!MenuDeathGateBody())
        return 0;

    if (zpatch_menu_death_site == 0)
    {
        auto pattern = find_pattern("6A 06 68 ? ? ? ? 8D 8C");
        if (!pattern.empty())
        {
            zpatch_menu_death_site = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
        }
    }

    if (zpatch_menu_death_site != 0)
    {
        injector::WriteMemory<std::byte>(
            static_cast<uintptr_t>(zpatch_menu_death_site) + 1,
            static_cast<std::byte>(std::rand() % 5 + 1), true);
    }
    return 1;
}

// sub_10052380, rebuilt: the companion-library probe. byte_10158038 is the arm's
// gate (Loader::Enabled), so this is the `GetModuleHandleA` / `LoadLibraryA`
// half. The one-shot `byte_101A843C` word makes a failed load a clean skip that
// is not retried; a successful load leaves it 0 and every later call finds the
// module.
static bool MenuIvMenuApiLoad()
{
    if (GetModuleHandleA("IVMenuAPI.asi") != nullptr)
        return true;

    if (!zpatch_menu_load_attempted && LoadLibraryA("IVMenuAPI.asi") != nullptr)
        return true;

    zpatch_menu_load_attempted = true;
    return false;
}

// sub_100522B0, rebuilt: one AddIVMenuOption registration. The original takes
// the family in ECX, the title in EDX and the remaining arguments on the stack,
// probes the library again and calls the export with
// (family, title, "MENU_DISPLAY_ON_OFF", pref, 1, 0, getter, setter). The
// original's third stack argument (the unused one its caller pushes from a
// leftover ECX) is never read, so the rebuild does not carry it.
static void MenuAddOption(const char* family, const wchar_t* title, const char* pref,
    uintptr_t getter, uintptr_t setter)
{
    const HMODULE menu = GetModuleHandleA("IVMenuAPI.asi");
    if (menu == nullptr)
        return;

    using AddOption = int(__cdecl*)(const char*, const wchar_t*, const char*,
        const char*, int, int, int, int);
    const auto add = reinterpret_cast<AddOption>(
        reinterpret_cast<void*>(GetProcAddress(menu, "AddIVMenuOption")));
    if (add == nullptr)
        return;

    add(family, title, "MENU_DISPLAY_ON_OFF", pref, 1, 0,
        static_cast<int>(getter), static_cast<int>(setter));
}

namespace
{
    // A body another module transcribed, by its recovered VA (the same helper
    // source/audio.ixx and source/stability.ixx use).
    uint32_t RegisteredBody(uint32_t reference)
    {
        for (const auto& entry : Bodies::All())
        {
            if (entry.reference == reference)
                return static_cast<uint32_t>(entry.address);
        }
        return 0;
    }

    // The two registrations, in the original's order: the death-music option
    // first (0x10055C0C) and the focus-loss option second (0x10055C31). The
    // callback VAs are the original's sub_10050A30/sub_10050A40 and
    // sub_1001EDB0/sub_1001EDC0.
    void RegisterMenuOptions()
    {
        MenuAddOption("PREF_DEATHMUSIC", L"Death Music", "PREF_MUSIC_VOLUME",
            reinterpret_cast<uintptr_t>(&MenuDeathGetBody),
            reinterpret_cast<uintptr_t>(&MenuDeathSetBody));
        MenuAddOption("PREF_FOCUSLOSS", L"Pause Game On Focus Loss", "PREF_VSYNC",
            reinterpret_cast<uintptr_t>(&MenuFocusGetBody),
            reinterpret_cast<uintptr_t>(&MenuFocusSetBody));
    }

    // sub_1001ECB0's scan, moved into the publish tier: the site and its
    // pre-write word have to be read before the DoNotPauseOnMinimize arm writes
    // the same two bytes. A miss leaves both zero, and the writer's rebuild is
    // then a no-op exactly like the original's null `lpAddress`.
    void PublishAddOptionsInMenu()
    {
        if (!Loader::EnabledNonZero("AddOptionsInMenu"))
            return;

        auto pattern = find_pattern("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
        if (!pattern.empty())
        {
            const auto site = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
            uint16_t word = 0;
            std::memcpy(&word, reinterpret_cast<const void*>(site), sizeof(word));
            zpatch_menu_focus_site = static_cast<uint32_t>(site);
            zpatch_menu_focus_word = word;
        }
    }

    // The AddOptionsInMenu install tier. The probe is the original's clean skip:
    // a failed LoadLibraryA runs nothing at all.
    void InstallAddOptionsInMenu()
    {
        if (!Loader::EnabledNonZero("AddOptionsInMenu"))
            return;

        if (!MenuIvMenuApiLoad())
            return;

        // `if (sub_10052380()) sub_1001EDE0();` (0x100557E9): the writer.
        MenuWriterBody();

        // `if (RestoreDeathMusic == 1 || sub_10052380()) sub_10050BD0();`
        // (0x10055B98). The audio arm owns the first half; this arm owns the
        // second, so the original's single call is split across the two arms and
        // never runs twice.
        if (!Loader::Enabled("RestoreDeathMusic"))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MenuDeathInstallBody),
                0x10050AD0u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&MenuDeathGateBody),
                0x10050A50u);
            MenuInstallerBody();
        }

        // `if (sub_10052380()) { ...two registrations... }` (0x10055BE6).
        RegisterMenuOptions();
    }
}

// Registered like every other arm: the publish tier reads the site's original
// bytes, the install tier owns the gate and the writes. RegisterMenu() runs
// after RegisterAudio(), because the AddOptionsInMenu install checks whether the
// RestoreDeathMusic arm already owns the death-music hooks.
export void RegisterMenu()
{
    // The imported calls the transcribed writers make.
    zpatch_menu_get_module = reinterpret_cast<uintptr_t>(&GetModuleHandleA);
    zpatch_menu_virtual_protect = reinterpret_cast<uintptr_t>(&VirtualProtect);

    // ... and the four the ZMenuIV guard adds (its GetModuleHandleA call reuses
    // the slot above).
    zpatch_menu_get_proc_address = reinterpret_cast<uintptr_t>(&GetProcAddress);
    zpatch_menu_message_box = reinterpret_cast<uintptr_t>(&MessageBoxA);
    zpatch_menu_get_current_process = reinterpret_cast<uintptr_t>(&GetCurrentProcess);
    zpatch_menu_terminate_process = reinterpret_cast<uintptr_t>(&TerminateProcess);

    // The native-layer pieces the wrappers call, by their recovered VAs.
    zpatch_menu_context_init = RegisteredBody(0x100038C0u);
    zpatch_menu_invoke_entry = RegisteredBody(0x100038E0u);
    zpatch_menu_cleanup_dword = RegisteredBody(0x1005DE40u);
    zpatch_menu_cleanup_bool = RegisteredBody(0x1005DE80u);
    zpatch_menu_call_hash = RegisteredBody(0x1005B150u);

    // The five preamble salts, taken from the layer's resolved words.
    zpatch_menu_salt_10158054 = Native::Salt(0x10158054u);
    zpatch_menu_salt_10158050 = Native::Salt(0x10158050u);
    zpatch_menu_salt_101580A8 = Native::Salt(0x101580A8u);
    zpatch_menu_salt_1015805C = Native::Salt(0x1015805Cu);
    zpatch_menu_salt_10158028 = Native::Salt(0x10158028u);

    // byte_1015AD43's initial value: the original's config walker sets it from
    // DoNotPauseOnMinimize before any menu callback can read it.
    zpatch_menu_focus_flag = Loader::Enabled("DoNotPauseOnMinimize") ? 1 : 0;

    // The ZMenuIV version guard, unconditionally. In the original it is not an
    // arm: the walker calls sub_10052300 at 0x1005527B with no key test and no
    // build test in front of it, so the tree registers it as a handler that no
    // gate can suppress. It goes in the publish tier because that is the first
    // pass of the load: the guard reads no site (it probes a module and calls its
    // export), writes no site, and its verdict -- carry on, or terminate after a
    // message box -- is the mod's pre-flight decision, so it has to have run
    // before any arm's install. The original's own position, after SuperLODFix's
    // handler and before VSyncFix's read, is the interleave of a walk this tree
    // splits into tiers; the move is unobservable for this call (nothing here
    // depends on an arm's write, and the failure path ends the process).
    Loader::PublishEvent() += []
    {
        ZMenuVersionGuardBody();
    };

    Loader::PublishEvent() += []
    {
        PublishAddOptionsInMenu();
    };

    Loader::InstallEvent() += []
    {
        InstallAddOptionsInMenu();
    };

    // The bodies' recovered VAs, so the dump expresses every branch in the
    // original's coordinates. The death-music pair is registered by the install
    // itself (only when this arm owns those installs): audio's copies carry the
    // same two VAs in the other configuration, and one live body per VA is the
    // rule.
    Bodies::Register(reinterpret_cast<uintptr_t>(&ZMenuVersionGuardBody), 0x10052300u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuWriterBody), 0x1001EDE0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuInstallerBody), 0x10050BD0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuDeathGetBody), 0x10050A30u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuDeathSetBody), 0x10050A40u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuFocusGetBody), 0x1001EDB0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuFocusSetBody), 0x1001EDC0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuCallTwoBody), 0x1005B1E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuCallZeroBoolBody), 0x1005B360u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&MenuCallByteBody), 0x1005B490u);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// sub_10052300, recovered VA 0x10052300 (38 instructions): the ZMenuIV version
// guard. Probe the library, probe its export, call it with the original's two
// stack out-parameters, and terminate the process with a message box when the
// answer is below 0x35DD1. A missing library or export is the original's clean
// skip (both `jz loc_10052368`); the second out-parameter is never read, and the
// first is read uninitialised if the library leaves it alone -- the two locals are
// deliberately not zeroed. The /GS frame and every call shape are the original's;
// the five `call ds:...` imports are slot calls here, which the proof compares as
// `call abs` on both sides.
static __declspec(naked) void ZMenuVersionGuardBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push OFFSET kZmenuLibraryName
        call DWORD PTR [zpatch_menu_get_module]
        test eax, eax
        jz zmenu_guard_done
        push OFFSET kZmenuVersionExport
        push eax
        call DWORD PTR [zpatch_menu_get_proc_address]
        test eax, eax
        jz zmenu_guard_done
        lea ecx, [ebp - 0Ch]
        push ecx
        lea ecx, [ebp - 8]
        push ecx
        call eax
        add esp, 8
        cmp DWORD PTR [ebp - 8], 35DD1h
        jnb zmenu_guard_done
        push 10h
        push OFFSET kZmenuCaption
        push OFFSET kZmenuOutdatedText
        push 0
        call DWORD PTR [zpatch_menu_message_box]
        push 0
        call DWORD PTR [zpatch_menu_get_current_process]
        push eax
        call DWORD PTR [zpatch_menu_terminate_process]
    zmenu_guard_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1001EDE0, recovered VA 0x1001EDE0 (36 instructions): the writer. It calls
// sub_1001ECB0 first, then scans `6A 0A 56 50 FF D2 A1` and writes byte 6 at
// match+1 through the original's VirtualProtect pair. The /GS frame is the
// original's; its two calls into the rebuilt helper and the scanner are `call
// imm` slots the proof compares as such.
static __declspec(naked) void MenuWriterBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push edi
        call MenuRebuildFocusSite
        push 0
        call DWORD PTR [zpatch_menu_get_module]
        mov edx, eax
        mov ecx, OFFSET kMenuPausePattern
        call ScanFirst
        test eax, eax
        jz menu_writer_done
        lea edi, [eax + 1]
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 1
        push edi
        call DWORD PTR [zpatch_menu_virtual_protect]
        lea eax, [ebp - 8]
        mov BYTE PTR [edi], 6
        push eax
        push DWORD PTR [ebp - 8]
        push 1
        push edi
        call DWORD PTR [zpatch_menu_virtual_protect]
    menu_writer_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        pop edi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10050BD0, recovered VA 0x10050BD0 (133 instructions): the death-music
// installer. Three scans, each followed by the same VirtualProtect/store pair:
// the first two write the `E8` at match-13 (to sub_10050AD0 and sub_10050A50),
// the third writes it at the match (to sub_10050A50). The /GS frame is the
// original's; `mov eax, offset sub_...` becomes `OFFSET` of this module's
// transcriptions, which the dump canonicalises back to the recovered VAs.
static __declspec(naked) void MenuInstallerBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push ebx
        push esi
        push edi

        push 0
        call DWORD PTR [zpatch_menu_get_module]
        mov edx, eax
        mov ecx, OFFSET kMenuDeathPatternA
        call ScanFirst
        mov ebx, DWORD PTR [zpatch_menu_virtual_protect]
        test eax, eax
        jz menu_installer_b
        lea edi, [eax - 0Dh]
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        mov BYTE PTR [edi], 0E8h
        push eax
        push DWORD PTR [ebp - 8]
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 4
        lea esi, [edi + 1]
        push esi
        call ebx
        mov eax, OFFSET MenuDeathInstallBody
        sub eax, edi
        sub eax, 5
        mov DWORD PTR [esi], eax
        lea eax, [ebp - 8]
        push eax
        push DWORD PTR [ebp - 8]
        push 4
        push esi
        call ebx

    menu_installer_b:
        push 0
        call DWORD PTR [zpatch_menu_get_module]
        mov edx, eax
        mov ecx, OFFSET kMenuDeathPatternB
        call ScanFirst
        test eax, eax
        jz menu_installer_c
        lea edi, [eax - 0Dh]
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        mov BYTE PTR [edi], 0E8h
        push eax
        push DWORD PTR [ebp - 8]
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 4
        lea esi, [edi + 1]
        push esi
        call ebx
        mov eax, OFFSET MenuDeathGateBody
        sub eax, edi
        sub eax, 5
        mov DWORD PTR [esi], eax
        lea eax, [ebp - 8]
        push eax
        push DWORD PTR [ebp - 8]
        push 4
        push esi
        call ebx

    menu_installer_c:
        push 0
        call DWORD PTR [zpatch_menu_get_module]
        mov edx, eax
        mov ecx, OFFSET kMenuDeathPatternC
        call ScanFirst
        mov edi, eax
        test edi, edi
        jz menu_installer_done
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        mov BYTE PTR [edi], 0E8h
        push eax
        push DWORD PTR [ebp - 8]
        push 1
        push edi
        call ebx
        lea eax, [ebp - 8]
        push eax
        push 40h
        push 4
        lea esi, [edi + 1]
        push esi
        call ebx
        mov eax, OFFSET MenuDeathGateBody
        sub eax, edi
        sub eax, 5
        mov DWORD PTR [esi], eax
        lea eax, [ebp - 8]
        push eax
        push DWORD PTR [ebp - 8]
        push 4
        push esi
        call ebx

    menu_installer_done:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        pop esi
        xor ecx, ebp
        pop ebx
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10050A50, recovered VA 0x10050A50 (41 instructions): the death-music gate.
// It answers 0 when the shared option word is clear, then asks the game three
// questions through the hash salts: the current "death music enabled" state (a
// zero-argument call, an out-parameter read by the two-argument call, then a byte
// answer), and the two flags dword_101580A8 / dword_10158050. The /GS frame is
// the original's.
static __declspec(naked) bool MenuDeathGateBody()
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
        jne menu_death_enabled
    menu_death_answer_zero:
        xor al, al
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    menu_death_enabled:
        mov ecx, DWORD PTR [zpatch_menu_salt_1015805C]
        call MenuCallHashEntry
        lea ecx, [ebp - 8]
        mov edx, eax
        push ecx
        call MenuCallTwoBody
        mov edx, DWORD PTR [ebp - 8]
        add esp, 4
        call MenuCallByteBody
        test al, al
        jne menu_death_answer_one
        mov ecx, DWORD PTR [zpatch_menu_salt_101580A8]
        call MenuCallZeroBoolBody
        test al, al
        je menu_death_answer_zero
        mov ecx, DWORD PTR [zpatch_menu_salt_10158050]
        call MenuCallZeroBoolBody
        test al, al
        je menu_death_answer_zero
    menu_death_answer_one:
        mov ecx, DWORD PTR [ebp - 4]
        mov al, 1
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1005B1E0, recovered VA 0x1005B1E0 (48 instructions): the two-argument
// native wrapper. The hash is the salt dword_10158054, the two arguments travel
// in EDX and on the stack, and the answer is args[0]. __fastcall-shaped; the
// first register argument (ECX) is unused exactly as in the original. The /GS
// frame is the original's.
static __declspec(naked) unsigned int __fastcall MenuCallTwoBody(unsigned int, unsigned int)
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
        mov eax, DWORD PTR [zpatch_menu_salt_10158054]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call MenuZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
    menu_two_records:
        mov ecx, edi
        call MenuContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz menu_two_records
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
        call MenuInvokeEntry
        lea ecx, [esp + 10h]
        call MenuCleanupDwordEntry
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
static __declspec(naked) unsigned int __fastcall MenuCallZeroBoolBody(unsigned int)
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
        call MenuZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
        // The original's loop alignment (0F 1F 40 00), emitted as bytes so the
        // assembler cannot choose a shorter nop.
        __emit 0x0F
        __emit 0x1F
        __emit 0x40
        __emit 0x00
    menu_zero_records:
        mov ecx, edi
        call MenuContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz menu_zero_records
        lea eax, [esp + 70h]
        mov DWORD PTR [esp + 14h], esi
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov ecx, ebx
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        call MenuInvokeEntry
        lea ecx, [esp + 10h]
        call MenuCleanupBoolEntry
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
static __declspec(naked) unsigned int __fastcall MenuCallByteBody(unsigned int, unsigned int)
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
        mov eax, DWORD PTR [zpatch_menu_salt_10158028]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call MenuZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
    menu_byte_records:
        mov ecx, edi
        call MenuContextInitEntry
        add edi, 10h
        sub esi, 1
        jnz menu_byte_records
        mov ecx, DWORD PTR [esp + 0Ch]
        lea eax, [esp + 70h]
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 14h], 1
        call MenuInvokeEntry
        lea ecx, [esp + 10h]
        call MenuCleanupBoolEntry
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

// sub_10050A30, recovered VA 0x10050A30 (2 instructions): the Death Music
// getter. It answers the shared option word.
static __declspec(naked) void MenuDeathGetBody()
{
    __asm
    {
        movzx eax, BYTE PTR [zpatch_audio_death_music_flag]
        ret
    }
}

// sub_10050A40, recovered VA 0x10050A40 (6 instructions): the Death Music
// setter. The value is the second argument; the first is unused.
static __declspec(naked) void MenuDeathSetBody(unsigned int, unsigned int)
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp DWORD PTR [ebp + 0Ch], 0
        setnz BYTE PTR [zpatch_audio_death_music_flag]
        pop ebp
        ret
    }
}

// sub_1001EDB0, recovered VA 0x1001EDB0 (4 instructions): the focus-loss getter.
// It answers `byte_1015AD43 == 0`, i.e. "pause on focus loss".
static __declspec(naked) void MenuFocusGetBody()
{
    __asm
    {
        xor eax, eax
        cmp BYTE PTR [zpatch_menu_focus_flag], al
        setz al
        ret
    }
}

// sub_1001EDC0, recovered VA 0x1001EDC0 (6 instructions): the focus-loss setter.
// The value is the second argument: 0 means "do not pause" (byte_1015AD43 = 1,
// the site is NOPed), nonzero means "pause" (byte_1015AD43 = 0, the site is
// restored). It tail-jumps to the site writer, exactly as the original does.
static __declspec(naked) void MenuFocusSetBody(unsigned int, unsigned int)
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp DWORD PTR [ebp + 0Ch], 0
        setz BYTE PTR [zpatch_menu_focus_flag]
        pop ebp
        jmp MenuRebuildFocusSite
    }
}

// The one-instruction forwarders to the native layer's bodies (a module interface
// cannot export a naked function, and a C entry's decorated name is not nameable
// from inline asm). Each is filled from the body registry by its recovered VA.
static __declspec(naked) void MenuContextInitEntry()
{
    __asm { jmp DWORD PTR [zpatch_menu_context_init] }
}

static __declspec(naked) void MenuInvokeEntry()
{
    __asm { jmp DWORD PTR [zpatch_menu_invoke_entry] }
}

static __declspec(naked) void MenuCleanupDwordEntry()
{
    __asm { jmp DWORD PTR [zpatch_menu_cleanup_dword] }
}

static __declspec(naked) void MenuCleanupBoolEntry()
{
    __asm { jmp DWORD PTR [zpatch_menu_cleanup_bool] }
}

static __declspec(naked) void MenuCallHashEntry()
{
    __asm { jmp DWORD PTR [zpatch_menu_call_hash] }
}
