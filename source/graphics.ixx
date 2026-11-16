module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;

// The two published model hashes and the two resumes the shim reads. C linkage:
// the transcribed asm names them directly.
extern "C"
{
    uint32_t zpatch_bds_hash_a = 0;              // dword_10159C1C
    uint32_t zpatch_bds_hash_b = 0;              // dword_10159C24
    void* zpatch_bds_resume_a = nullptr;         // dword_10159C20
    void* zpatch_bds_resume_b = nullptr;         // dword_10159C28
}

// The EmissiveLerpFix pair's published state. The original keeps it in four .bss
// words beside its own code (dword_10159CC0/CCC/CC4/CC8); the transcribed asm
// names the slots directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_emissive_saved_ecx = 0;  // dword_10159CC0
    uint32_t zpatch_emissive_saved_edx = 0;  // dword_10159CCC
    uint32_t zpatch_emissive_scale = 0;      // dword_10159CC4 (match - 16)
    uint32_t zpatch_emissive_resume = 0;     // dword_10159CC8 (match - 12)
}

// HighQualityReflections: the published state its detour body reads. The original
// keeps them as dword_10158060 (flags = 0x11D, in .data), dword_10159CE0 (mode, a
// .bss word it never writes), the crash dump at dword_10159CE4..10159D0C,
// dword_10159CFC (resume) and dword_10159D04 (builder). The transcribed asm names
// the copies directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_hqr_dump[8] = {};    // the eight saved registers, never read
    uint32_t zpatch_hqr_flags = 0x11Du;  // dword_10158060
    uint32_t zpatch_hqr_mode = 0;        // dword_10159CE0
    void* zpatch_hqr_resume = nullptr;   // dword_10159CFC = match + 0x13
    void* zpatch_hqr_builder = nullptr;  // dword_10159D04 = match + [match+0x0F] + 0x13
}


// BorderlessWindowed's two window-proc shims: 0x10005890 for the Unicode window
// proc and 0x10005900 for the ANSI one. The original's handler (0x10005970) scans
// one pattern for the two `call [DefWindowProc]` sites, VirtualProtects six bytes
// of each site and fills them with 0x90, then writes `E8 <body>` over the first
// five, so the sixth stays a NOP; the shim's `ret 0x10` consumes the discarded
// call's four argument slots exactly like the call it replaced, and the game's own
// `pop edi/esi/ebp/ebx; add esp,10h; ret 10h` still unwinds.
//
// Each shim applies its style fixup once, guarded by the original's own done flag
// (0x101A8445 for the Unicode shim, 0x101A8444 for the ANSI one): SetWindowPos to
// HWND_NOTOPMOST, GWL_STYLE &= 0xFF30FFFF, GWL_EXSTYLE &= ~WS_EX_TOPMOST. The five
// USER32 exports the original calls through its own import slots (0x10063264
// SetWindowPos, 0x10063268 SetWindowLongA, 0x1006326C GetWindowLongA, 0x10063258
// DefWindowProcW, 0x10063274 DefWindowProcA) are resolved into the module words
// below by the arm, before the site is written: this module links no USER32, so
// the transcribed `call dword ptr [slot]` operands stay absolute.
extern "C"
{
    uint32_t zpatch_borderless_set_window_pos = 0;     // 0x10063264
    uint32_t zpatch_borderless_set_window_long = 0;    // 0x10063268
    uint32_t zpatch_borderless_get_window_long = 0;    // 0x1006326C
    uint32_t zpatch_borderless_def_window_proc_w = 0;  // 0x10063258
    uint32_t zpatch_borderless_def_window_proc_a = 0;  // 0x10063274
    uint8_t zpatch_borderless_w_done = 0;              // 0x101A8445
    uint8_t zpatch_borderless_a_done = 0;              // 0x101A8444
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void BuildingsDynamicShadowsShim();
static void EmissiveLerpHelper();
static void EmissiveLerpShim();
static void HqrBody();
static void BorderlessWindowBodyW();
static void BorderlessWindowBodyA();

// The emissive shim's address, registered at its recovered VA on first use.
export void* EmissiveLerpShimAddress()
{
    Bodies::Register(reinterpret_cast<uintptr_t>(&EmissiveLerpShim), 0x1001AF60u);
    return reinterpret_cast<void*>(&EmissiveLerpShim);
}

namespace
{
    // The original's FusionFix probe (sub_10003E20), the gate loading.ixx applies
    // to SkipIntro/SkipMenu: a loaded GTAIV.EFLC.FusionFix.asi whose two signature
    // dwords are not the known build owns these fixes, and the original then
    // leaves BorderlessWindowed's sites alone. A loaded module probes the dwords
    // unconditionally, as the original does. This is the same probe, kept local
    // because loading.ixx's copy is not exported; one shared home for it is a
    // pending cleanup, not a behaviour question.
    bool FusionFixGateClosed()
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

    // BorderlessWindowed: the two `call [DefWindowProc]` sites take a call to the
    // matching shim and the sixth byte of the six-byte indirect call becomes a NOP.
    //
    // The call is written as its two pieces rather than through injector::MakeCALL:
    // MakeCALL first reads the branch's current destination, and for this site's
    // `FF 15 <disp32>` form that read dereferences the slot's absolute address
    // (0x00D6B460 on 1.0.8.0), which the offline dump's heap copy does not map. The
    // original reads nothing here either - its handler writes the E8 and then the
    // displacement (0x100059FA .. 0x10005A2D) - so the write set is the same, and
    // NOPping byte 5 afterwards cannot corrupt an earlier read.
    void WriteCallNoRead(std::byte* at, void* target)
    {
        const auto displacement = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(at) + 5));
        injector::WriteMemory<std::byte>(at, Bytes::Call, true);
        injector::WriteMemory<uint32_t>(at + 1, displacement, true);
    }


    // BuildingDynamicShadows' published state: both resumes come from the site's
    // original bytes (match+15 and match+112) and the two hash slots take the same
    // field of the first and of the second scan hit.
    void PublishBuildingDynamicShadows()
    {
        if (!Loader::Enabled("BuildingDynamicShadows"))
            return;

        auto pattern = find_pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_bds_resume_a = match + 15;
        zpatch_bds_resume_b = match + 112;

        // Two hits, one per model hash the shim compares against, so the scan has to
        // ask for both: the scanner's count is a hint that stops the scan, and the
        // default one hit would leave the second slot at zero.
        pattern = find_pattern<2>("8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
        if (!pattern.empty())
            zpatch_bds_hash_a = *pattern.get(0).get<uint32_t>(20);
        if (pattern.size() > 1)
            zpatch_bds_hash_b = *pattern.get(1).get<uint32_t>(20);
    }

    // HighQualityReflections' published state: the resume is the instruction after
    // the site's own five-byte call, and the builder is that call's target. The
    // scanned block is the six-byte `mov eax,[edi+938h]` then `push 8`,
    // `push 131Dh`, `push eax` and `call`, so the E8 opcode is at match+0x0E and
    // match+0x0F is its rel32: resume = match+0x13, builder = match+0x13 + the
    // dword at match+0x0F.
    void PublishHighQualityReflections()
    {
        if (!Loader::Enabled("HighQualityReflections"))
            return;

        auto pattern = find_pattern("8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_hqr_resume = match + 0x13;
        zpatch_hqr_builder = match + 0x13 + *pattern.get_first<int32_t>(0x0F);
    }

    // EmissiveLerpFix's published state: match - 12 as the resume and match - 16 as
    // the float slot. Both published values are addresses, not values read out of
    // the game (see EmissiveLerpShim).
    void PublishEmissiveLerpFix()
    {
        if (!Loader::Enabled("EmissiveLerpFix"))
            return;

        auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_emissive_scale = static_cast<uint32_t>(match - 16);
        zpatch_emissive_resume = static_cast<uint32_t>(match - 12);
    }


    // BuildingAlphaFix: two copies of the `test [esi+24h], 40000000h` alpha gate.
    // The first one's `jz` (0F 84 rel32, six bytes) becomes a five-byte `jmp` to
    // the same target, so the alpha path is entered unconditionally; the second
    // one's `jnz` and the five bytes of the test it follows are filled with NOPs,
    // so that copy never skips the path either. The targets are the sites' own
    // operands, read before the opcode is overwritten.
    void InstallBuildingAlphaFix()
    {
        if (!Loader::Enabled("BuildingAlphaFix"))
            return;

        auto pattern = find_pattern("F7 46 24 00 00 00 40 0F 84");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 9);
            injector::MakeJMP(match + 7, match + 13 + displacement, true);
        }

        pattern = find_pattern("F7 46 24 00 00 00 40 0F 85");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);
    }

    void InstallBorderlessWindowed()
    {
        if (!Loader::Enabled("BorderlessWindowed") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern<2>("FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00");
        if (pattern.empty())
            return;

        const HMODULE user32 = LoadLibraryA("user32.dll");
        const auto resolve = [user32](const char* name) -> uint32_t
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetProcAddress(user32, name)));
        };
        zpatch_borderless_set_window_pos = resolve("SetWindowPos");
        zpatch_borderless_set_window_long = resolve("SetWindowLongA");
        zpatch_borderless_get_window_long = resolve("GetWindowLongA");
        zpatch_borderless_def_window_proc_w = resolve("DefWindowProcW");
        zpatch_borderless_def_window_proc_a = resolve("DefWindowProcA");

        Bodies::Register(reinterpret_cast<uintptr_t>(&BorderlessWindowBodyW), 0x10005890u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&BorderlessWindowBodyA), 0x10005900u);

        auto* first = pattern.get(0).get<std::byte>(0);
        WriteCallNoRead(first, reinterpret_cast<void*>(&BorderlessWindowBodyW));
        injector::MakeNOP(first + 5, 1, true);

        auto* second = pattern.get(1).get<std::byte>(0);
        WriteCallNoRead(second, reinterpret_cast<void*>(&BorderlessWindowBodyA));
        injector::MakeNOP(second + 5, 1, true);
    }

    // RemoveBoundingBoxCulling: the `jnz` that skips the bounding-box path when the
    // object is not a building becomes an unconditional `jmp` to that same target,
    // i.e. the path is always skipped.
    void InstallRemoveBoundingBoxCulling()
    {
        if (!Loader::Enabled("RemoveBoundingBoxCulling"))
            return;

        auto pattern = find_pattern("3C 01 57 0F 85");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 5);
        injector::MakeJMP(match + 3, match + 9 + displacement, true);
    }

    // NoImpostorHeightCutoff: both copies of the two-byte `76 0B` branch that skips
    // the impostor's height cutoff become 0x9090. The original's block (0x10053689
    // in the loader) scans the pattern once for two hits and then writes hits[0]
    // and hits[1] with no bounds check - on 1.0.0.0 the pattern matches once, so it
    // VirtualProtects and writes through an uninitialised vector entry. That fault
    // is preserved: there is deliberately no `pattern.size() > 1` here.
    void InstallNoImpostorHeightCutoff()
    {
        if (!Loader::Enabled("NoImpostorHeightCutoff"))
            return;

        auto pattern = find_pattern<2>("76 0B F3 0F 11 8C 24 88 00 00 00 EB 09");
        if (pattern.empty())
            return;

        injector::MakeNOP(pattern.get(0).get<std::byte>(0), 2, true);
        injector::MakeNOP(pattern.get(1).get<std::byte>(0), 2, true);
    }

    // NoLODLightHeightCutoff: the two-byte `76 08` branch that skips the distant
    // lights' height cutoff becomes 0x9090. The original's block (0x100538CA) has
    // its finder return a single match and skips cleanly when there is none, so
    // the empty test is the original's own guard, not a fault-avoidance one.
    void InstallNoLODLightHeightCutoff()
    {
        if (!Loader::Enabled("NoLODLightHeightCutoff"))
            return;

        auto pattern = find_pattern("76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 2, true);
    }

    // RemoveStaticCutsceneCarShadows: the cutscene copy of RemoveStaticCarShadows.
    // Its `jbe` (+0x5B) that guards the static car-shadow draw becomes a short
    // `jmp` over it, so the shadow is never drawn; only the store's source register
    // differs (44 here, 4C in game). The original's handler is sub_10038860.
    void InstallRemoveStaticCutsceneCarShadows()
    {
        if (!Loader::Enabled("RemoveStaticCutsceneCarShadows"))
            return;

        auto pattern = find_pattern("F3 0F 11 44 24 0C 76 5B D9 45 14");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);
    }

    // RemoveStaticCarShadows: the `jbe` (+0x5B) that guards the static car-shadow
    // draw becomes a short `jmp` over it, so the shadow is never drawn.
    void InstallRemoveStaticCarShadows()
    {
        if (!Loader::Enabled("RemoveStaticCarShadows"))
            return;

        auto pattern = find_pattern("F3 0F 11 4C 24 0C 76 5B D9 45 14");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);
    }

    // ForceShadowsOnObjects: the `jz` (+7) that would skip the shadow flag on an
    // object with the 0x100000 bit set becomes a short `jmp` over it, so the flag
    // is always set.
    void InstallForceShadowsOnObjects()
    {
        if (!Loader::Enabled("ForceShadowsOnObjects"))
            return;

        auto pattern = find_pattern("F7 47 40 00 10 00 00 74 07 81 4E 24 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(7), Bytes::ShortJmp, true);
    }

    // ForceDynamicShadowsEverywhere: the short `jz` that skips the dynamic-shadow
    // path becomes two NOPs, so the path always runs.
    void InstallForceDynamicShadowsEverywhere()
    {
        if (!Loader::Enabled("ForceDynamicShadowsEverywhere"))
            return;

        auto pattern = find_pattern("F7 46 4C 00 80 00 00 74");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // BuildingDynamicShadows: the shadow-size operand at the call site becomes zero
    // (0x104 -> 0x100) and the second site jumps into the shim.
    void InstallBuildingDynamicShadows()
    {
        if (!Loader::Enabled("BuildingDynamicShadows"))
            return;

        auto pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(2), Bytes::Zero, true);

        pattern = find_pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&BuildingsDynamicShadowsShim), 0x10005AF0u);
        injector::MakeJMP(pattern.get_first(0),
            reinterpret_cast<void*>(&BuildingsDynamicShadowsShim), true);
    }

    // HighQualityReflections: the original's handler E9s the match (the site's
    // `mov eax,[edi+938h]`) to the detour; the body replays the twelve builder
    // calls the block makes and tail-jumps to the resume published above.
    void InstallHighQualityReflections()
    {
        if (!Loader::Enabled("HighQualityReflections"))
            return;

        auto pattern = find_pattern("8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&HqrBody), 0x1001F030u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&HqrBody), true);
    }

    // EmissiveLerpFix: the original handler sub_1001AFA0 scans the signature and
    // writes the E9 at match - 24.
    void InstallEmissiveLerpFix()
    {
        if (!Loader::Enabled("EmissiveLerpFix"))
            return;

        auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1");
        if (pattern.empty())
            return;

        injector::MakeJMP(pattern.get_first(-24), EmissiveLerpShimAddress(), true);
    }



}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
// ---------------------------------------------------------------------------
// VRAMFix, moved from the misc module.
// ---------------------------------------------------------------------------

extern "C"
{
    uint32_t zpatch_vram_arg_a = 0;  // dword_1015D630
    uint32_t zpatch_vram_arg_b = 0;  // dword_1015D634
    uint8_t zpatch_vram_flag = 0;    // byte_1015D638
}

static void VramFixShimBody();


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
}


export void RegisterGraphics()
{
    Loader::AddPublish(Loader::Entry::Graphics, 0, PublishBuildingDynamicShadows);
    Loader::AddPublish(Loader::Entry::Graphics, 2, PublishEmissiveLerpFix);
    Loader::AddPublish(Loader::Entry::Graphics, 3, PublishHighQualityReflections);

    Loader::AddInstall(Loader::Entry::Misc, 7, InstallVRAMFix);
    Loader::AddInstall(Loader::Entry::Graphics, 0, InstallBorderlessWindowed);
    Loader::AddInstall(Loader::Entry::Graphics, 1, InstallBuildingAlphaFix);
    Loader::AddInstall(Loader::Entry::Graphics, 2, InstallBuildingDynamicShadows);
    Loader::AddInstall(Loader::Entry::Graphics, 4, InstallEmissiveLerpFix);
    Loader::AddInstall(Loader::Entry::Graphics, 5, InstallForceDynamicShadowsEverywhere);
    Loader::AddInstall(Loader::Entry::Graphics, 6, InstallForceShadowsOnObjects);
    Loader::AddInstall(Loader::Entry::Graphics, 7, InstallHighQualityReflections);
    Loader::AddInstall(Loader::Entry::Graphics, 9, InstallNoImpostorHeightCutoff);
    Loader::AddInstall(Loader::Entry::Graphics, 10, InstallNoLODLightHeightCutoff);
    Loader::AddInstall(Loader::Entry::Graphics, 11, InstallRemoveBoundingBoxCulling);
    Loader::AddInstall(Loader::Entry::Graphics, 12, InstallRemoveStaticCarShadows);
    Loader::AddInstall(Loader::Entry::Graphics, 13, InstallRemoveStaticCutsceneCarShadows);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// abi_building_dynamic_shadows, recovered VA 0x10005AF0 (sub_10005af0). It reads
// the model pointer the caller holds in EDI, matches it against the two published
// hashes, and either takes the special path or rewinds the packed field by one.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void BuildingsDynamicShadowsShim()
{
    __asm
    {
        mov eax, DWORD PTR [edi]
        cmp eax, DWORD PTR [zpatch_bds_hash_a]
        je bds_special
        cmp eax, DWORD PTR [zpatch_bds_hash_b]
        je bds_special
        mov eax, DWORD PTR [edi + 0x28]
        shr eax, 6
        and eax, 0x0f
        add eax, -1
        jmp DWORD PTR [zpatch_bds_resume_a]
    bds_special:
        jmp DWORD PTR [zpatch_bds_resume_b]
    }
}

// sub_1001AF20, recovered VA 0x1001AF20: the body sub_1001AF60 calls. At the
// 1.0.8.0 site (0x7F8BA1) the shim saves ECX = the clock's hours and EDX = its
// minutes. `shl ecx, 4; sub ecx, [saved_ecx]` leaves 15 * hours, and
// `lea eax, [eax + ecx * 4]` adds 4 * 15 * hours to the minutes, i.e.
// 60 * hours + minutes = the minute of the day. 0xFFFFFE6B (-405) is 06:45 and
// 0x2ED (753) is twelve hours and thirty-three minutes, so the unsigned compare
// selects 12 inside [06:45, 19:18] and 0 outside it. The selector is written over
// the saved ECX, and the saved EDX is zeroed; the shim reloads both, so the
// resumed code takes ECX as hours and computes the day/night factor from noon or
// midnight with no minutes at all.
static __declspec(naked) void EmissiveLerpHelper()
{
    __asm
    {
        mov ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        xor edx, edx
        mov eax, DWORD PTR [zpatch_emissive_saved_edx]
        shl ecx, 4
        sub ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        mov DWORD PTR [zpatch_emissive_saved_edx], 0
        lea eax, [eax + ecx * 4]
        mov ecx, 0Ch
        add eax, -405
        cmp eax, 2EDh
        cmovbe edx, ecx
        mov DWORD PTR [zpatch_emissive_saved_ecx], edx
        ret
    }
}

// sub_1001AF60, recovered VA 0x1001AF60: the trampoline the EmissiveLerpFix arm
// installs at match - 24. The original handler sub_1001AFA0 publishes two
// addresses before it writes the site: match - 16 into dword_10159CC4 and
// match - 12 into dword_10159CC8. Both slots hold addresses, and the shim consumes
// them differently:
//   * dword_10159CC8 is the resume (0x7F8BAD on 1.0.8.0); the final indirect
//     `jmp dword ptr [slot]` takes the slot's value as the jump target;
//   * dword_10159CC4 is match - 16 (0x7F8BA9), the address of the disp32 of the
//     `mulss xmm0, 60.0f` being replaced (the four bytes there are 0x00DF2C30,
//     the address of the game's 60.0f). The mulss is IDA's ABSOLUTE operand:
//     `F3 0F 59 05 <disp32>` reads the slot's own dword as the float, with no
//     register load and no dereference. So the arm publishes match - 16, not a
//     float copied out of the game.
// The multiply has no effect on the result: the helper zeroed the saved EDX, so
// `cvtsi2ss xmm0, edx` produces 0.0f before it runs. The original keeps the
// multiply and the slot write anyway, so this body does too.
//
// The 0x3E before the mulss is the original's DS segment prefix (the byte at
// 0x1001AF83, not an instruction of its own). `pusha`/`popa` are emitted as
// bytes because the inline assembler renders those mnemonics with a 0x66
// operand-size prefix, which the original does not have.
static __declspec(naked) void EmissiveLerpShim()
{
    __asm
    {
        mov DWORD PTR [zpatch_emissive_saved_ecx], ecx
        mov DWORD PTR [zpatch_emissive_saved_edx], edx
        __emit 0x60                                  // pusha
        call EmissiveLerpHelper
        __emit 0x61                                  // popa
        mov ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        mov edx, DWORD PTR [zpatch_emissive_saved_edx]
        cvtsi2ss xmm0, edx
        __emit 0x3E                                  // DS prefix, as in the original
        mulss xmm0, DWORD PTR [zpatch_emissive_scale]
        jmp DWORD PTR [zpatch_emissive_resume]
    }
}

// sub_1001F030, recovered VA 0x1001F030: the HighQualityReflections detour. It
// dumps the eight registers the original saves, calls the game's draw-command
// builder twelve times with (index 0..11, flags 0x11D, mode 0), restoring the
// saved registers after every call but the last, and tail-jumps to the published
// resume. The site is the six-byte `mov eax,[edi+938h]`; its own five-byte call
// starts at match+0x0E, so the resume is match+0x13 (the instruction after that
// call, `push 0` on 1.0.8.0) and the builder is the call's target, i.e.
// match + 0x13 + the rel32 at match+0x0F.
static __declspec(naked) void HqrBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_hqr_dump], eax
        mov DWORD PTR [zpatch_hqr_dump + 4], ebx
        mov DWORD PTR [zpatch_hqr_dump + 8], ecx
        mov DWORD PTR [zpatch_hqr_dump + 12], edx
        mov DWORD PTR [zpatch_hqr_dump + 16], esi
        mov DWORD PTR [zpatch_hqr_dump + 20], edi
        mov DWORD PTR [zpatch_hqr_dump + 24], ebp
        mov DWORD PTR [zpatch_hqr_dump + 28], esp
        mov eax, 0
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 1
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 2
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 3
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 4
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 5
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 6
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 7
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 8
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 9
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 0Ah
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        mov eax, DWORD PTR [zpatch_hqr_dump]
        mov ebx, DWORD PTR [zpatch_hqr_dump + 4]
        mov ecx, DWORD PTR [zpatch_hqr_dump + 8]
        mov edx, DWORD PTR [zpatch_hqr_dump + 12]
        mov esi, DWORD PTR [zpatch_hqr_dump + 16]
        mov edi, DWORD PTR [zpatch_hqr_dump + 20]
        mov ebp, DWORD PTR [zpatch_hqr_dump + 24]
        mov esp, DWORD PTR [zpatch_hqr_dump + 28]
        mov eax, 0Bh
        push DWORD PTR [zpatch_hqr_mode]
        push DWORD PTR [zpatch_hqr_flags]
        push eax
        call DWORD PTR [zpatch_hqr_builder]
        jmp DWORD PTR [zpatch_hqr_resume]
    }
}




// 0x10005890: the Unicode window proc's shim. EBP+8 is the window handle, EBP+0C
// the message, and EBP+10/EBP+14 its two arguments; EBX carries the handle into
// every call because the style fixup and the tail call both need it.
static __declspec(naked) void BorderlessWindowBodyW()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp BYTE PTR [zpatch_borderless_w_done], 0
        push ebx
        mov ebx, DWORD PTR [ebp + 8]
        jne bw_defer
        push 3
        push 0
        push 0
        push 0
        push 0
        push -2
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_pos]
        push -16
        push ebx
        call DWORD PTR [zpatch_borderless_get_window_long]
        and eax, 0FF30FFFFh
        push eax
        push -16
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_long]
        push -20
        push ebx
        call DWORD PTR [zpatch_borderless_get_window_long]
        and eax, -9
        push eax
        push -20
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_long]
        mov BYTE PTR [zpatch_borderless_w_done], 1
    bw_defer:
        push DWORD PTR [ebp + 20]
        push DWORD PTR [ebp + 16]
        push DWORD PTR [ebp + 12]
        push ebx
        call DWORD PTR [zpatch_borderless_def_window_proc_w]
        pop ebx
        pop ebp
        ret 16
    }
}

// 0x10005900: the same shim for the ANSI window proc - its own done flag and its
// own DefWindowProc. The original has two bodies rather than one parameterised
// body, and the two sites are what select them.
static __declspec(naked) void BorderlessWindowBodyA()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp BYTE PTR [zpatch_borderless_a_done], 0
        push ebx
        mov ebx, DWORD PTR [ebp + 8]
        jne ba_defer
        push 3
        push 0
        push 0
        push 0
        push 0
        push -2
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_pos]
        push -16
        push ebx
        call DWORD PTR [zpatch_borderless_get_window_long]
        and eax, 0FF30FFFFh
        push eax
        push -16
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_long]
        push -20
        push ebx
        call DWORD PTR [zpatch_borderless_get_window_long]
        and eax, -9
        push eax
        push -20
        push ebx
        call DWORD PTR [zpatch_borderless_set_window_long]
        mov BYTE PTR [zpatch_borderless_a_done], 1
    ba_defer:
        push DWORD PTR [ebp + 20]
        push DWORD PTR [ebp + 16]
        push DWORD PTR [ebp + 12]
        push ebx
        call DWORD PTR [zpatch_borderless_def_window_proc_a]
        pop ebx
        pop ebp
        ret 16
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
