module;

#include <common.hxx>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <shlwapi.h>

export module loading;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import patch;
import shared;

// The load-time arms: the fixes the original applies while the game is loading,
// one arm per INI key, each anchored to its own recovered signature.

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


// The published state of the two build-gated arms below, in C linkage because the
// transcribed code names the slots directly. Each comment names the original's own
// resume word, the dword its body's `jmp` reads (0x10005659 and 0x10005679).

extern "C"
{
    uint32_t zpatch_benchmark_resume_a = 0;  // dword_1015AD50
    uint32_t zpatch_benchmark_resume_b = 0;  // dword_1015AD54
}

namespace
{
    // VSyncFix: both call shapes load the vsync flag from an operand the original
    // retargets to its own dword. The key's build mask excludes 1.0.8.0, where
    // neither anchor exists, so the arm is inert on that image.
    uint32_t zpatch_vsync_flag = 0;

    // OverrideGTADatPath's sibling key: the path the game's gta.dat push is
    // retargeted to. The original's buffer is 64 bytes and its static initializer
    // holds the stock path; the loader overwrites it from OverrideGTADatPathTo
    // with strcpy_s, so a value of 64 bytes or more faults through the CRT's
    // invalid-parameter path (preserved). Absent or "0" leaves the initializer.
    char zpatch_dat_path[64] = "common:/data/gta.dat";
}

extern "C"
{
    uint32_t zpatch_skip_intro_match = 0;    // dword_1015AD74
    uint32_t zpatch_skip_menu_resume = 0;    // dword_10159348
    uint8_t zpatch_skip_menu_flag = 0;       // byte_101A8450
}



// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void GfwlStubBody();
static void RgscLoginRemovedBody();
static void BenchmarkBodyA();
static void BenchmarkBodyB();
static void SkipIntroBody();
static void SkipMenuBody();

// The original's FusionFix probe (sub_10003E20): a loaded
// GTAIV.EFLC.FusionFix.asi whose two signature dwords do not match the known
// build closes SkipIntro/SkipMenu (FusionFix owns those fixes there). A loaded
// module probes the dwords unconditionally, as the original does.
static bool FusionFixGateClosed()
{
    const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
    if (module == nullptr)
        return false;

    const auto* image = reinterpret_cast<const std::byte*>(module);
    uint32_t first{};
    uint32_t second{};
    std::memcpy(&first, image + 0x20D3, sizeof(first));
    std::memcpy(&second, image + 0x20D7, sizeof(second));
    return first != 0xC7C47589u || second != 0x00002446u;
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


namespace
{

    // RemoveGFWLUpdatePopup: the anchor's first twelve bytes are the subtraction
    // and its two stores; the stub replays them and resumes at match + 12.
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


    void InstallVSyncFix()
    {
        if (!Loader::Enabled("VSyncFix"))
            return;

        auto pattern = find_pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(30),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(30)));
        }

        pattern = find_pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(13),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
        }
    }

    // BenchmarkFix: two match sites eight bytes into the matched call sequence.
    // The resume is the anchor's +13, read before the sites are overwritten; the
    // key's build mask excludes 1.0.8.0, so this arm is inert on that image.
    void InstallBenchmarkFix()
    {
        if (!Loader::Enabled("BenchmarkFix"))
            return;

        auto pattern = find_pattern<2>("E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20");
        if (!pattern.empty())
        {
            // The two resumes are each match's own +13, not one shared value:
            // `mov eax,[edx]; add eax,0Dh; mov dword_1015AD50,eax` publishes the
            // first, `mov eax,[ecx+4]; add eax,0Dh; mov dword_1015AD54,eax` the
            // second, and the bodies read their own word
            // (`jmp [dword_1015AD50]` / `jmp [dword_1015AD54]`, 0x10005667 /
            // 0x10005687). The matches sit in different functions, so one shared
            // resume sends body B to body A's continuation.
            zpatch_benchmark_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get(0).get<std::byte>(0)) + 13);
            if (pattern.size() > 1)
                zpatch_benchmark_resume_b = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(pattern.get(1).get<std::byte>(0)) + 13);

            injector::MakeJMP(pattern.get(0).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyA), true);
            if (pattern.size() > 1)
                injector::MakeJMP(pattern.get(1).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyB), true);
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

    // SkipIntro: the anchor is the intro sequence's store; the arm jumps from the
    // anchor's -6, and the body resumes at the anchor itself.
    void InstallSkipIntro()
    {
        if (!Loader::Enabled("SkipIntro") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_skip_intro_match = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match));
            injector::MakeJMP(match - 6, reinterpret_cast<void*>(&SkipIntroBody), true);
        }
    }

    // SkipMenu: each anchor's call is retargeted to the body, and the resume is
    // the call's original target (the call's displacement is read before the
    // site is overwritten).
    void InstallSkipMenu()
    {
        if (!Loader::Enabled("SkipMenu") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 24, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 28 + displacement);
            injector::MakeCALL(match + 23, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }
    }






}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLoading()
{
    Loader::AddPublish(Loader::Entry::Loading, 1, CopyOverrideDatPath);

    Loader::AddInstall(Loader::Entry::Loading, 3, InstallRemoveGFWLUpdatePopup);
    Loader::AddInstall(Loader::Entry::Loading, 4, InstallRemoveRGSCLoginRequirement);
    Loader::AddInstall(Loader::Entry::Loading, 6, InstallVSyncFix);
    Loader::AddInstall(Loader::Entry::Loading, 7, InstallBenchmarkFix);
    Loader::AddInstall(Loader::Entry::Loading, 8, InstallOverrideGTADatPath);
    Loader::AddInstall(Loader::Entry::Loading, 9, InstallSkipIntro);
    Loader::AddInstall(Loader::Entry::Loading, 10, InstallSkipMenu);

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyA), 0x10005650u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyB), 0x10005670u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipIntroBody), 0x10045C30u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipMenuBody), 0x10046610u);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

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

// sub_10005650, recovered VA 0x10005650: BenchmarkFix's first match-site shim. It
// checks the benchmark object, runs the interposed callback (ESI with EDI), then
// resumes, or unwinds through the enclosing function's tail when the object is
// null.
static __declspec(naked) void BenchmarkBodyA()
{
    __asm
    {
        test eax, eax
        jz benchmark_a_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_a]
    benchmark_a_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// sub_10005670, recovered VA 0x10005670: BenchmarkFix's second match-site shim,
// the same shape as the first with its own resume.
static __declspec(naked) void BenchmarkBodyB()
{
    __asm
    {
        test eax, eax
        jz benchmark_b_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_b]
    benchmark_b_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// sub_10045C30, recovered VA 0x10045C30 (6 instructions): the intro-screen
// detour. Entered through a jump from the anchor's -6 with the site's ECX and
// EDX; it scales ECX by 400 and zeroes EDX below the 8000 threshold, which is
// what skips the intro outright, then resumes at the anchor.
static __declspec(naked) void SkipIntroBody()
{
    __asm
    {
        imul ecx, ecx, 190h
        cmp edx, 1F40h
        jge skip_intro_ok
        xor edx, edx
    skip_intro_ok:
        jmp DWORD PTR [zpatch_skip_intro_match]
    }
}

// sub_10046610, recovered VA 0x10046610 (12 instructions): the menu detour. The
// site's call is retargeted to this body, whose first call with a zero argument
// switches the menu id to 6 (one-shot through the flag), then tail-jumps to the
// original call's target; the call's own return address stays on the stack, so
// the target returns to the game's next instruction.
static __declspec(naked) void SkipMenuBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp BYTE PTR [zpatch_skip_menu_flag], 0
        mov eax, DWORD PTR [ebp + 8]
        jne skip_menu_go
        test eax, eax
        jne skip_menu_go
        mov eax, 6
        mov BYTE PTR [zpatch_skip_menu_flag], 1
    skip_menu_go:
        mov DWORD PTR [ebp + 8], eax
        pop ebp
        jmp DWORD PTR [zpatch_skip_menu_resume]
    }
}





