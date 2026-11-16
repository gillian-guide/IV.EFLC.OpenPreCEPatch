module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
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

// The six GRAPHICS-category vehicle light and shadow arms, moved here from
// vehicles.ixx: their published state, bodies and registrations live in this
// module because the [GRAPHICS] section owns them.

extern "C"
{
    uint32_t zpatch_carparts_resume_if = 0;    // dword_1015BDC8
    uint32_t zpatch_carparts_resume_else = 0;  // dword_1015BDC0
    uint32_t zpatch_carparts_resume_2 = 0;     // dword_1015BDCC

    // dword_1015B5B8, defined in source/config.ixx: the original keeps it in `.data`
    // immediately after the [HeadlightConeOffsets] hash array, and the cone check's
    // walk reads it as the 513th hash, so this tree defines it as the next object after
    // that array (docs/rebuilt.md Â§2g). This arm publishes the same value into it.
    extern uint32_t zpatch_carparts_shadow_fn;


    // The game model-index lookup (source/shared.ixx, dword_10159C0C), which the
    // moved ForceCarHeadlightShadows check body calls.
    extern uintptr_t zpatch_model_lookup;

    // ---- ForceCarHeadlightShadows --------------------------------------------
    // dword_1015BDD0: the object the hooked function receives in ECX (the shim
    // stores it, the checker reads +0x2E of it). dword_101580C0: the offset float
    // the site's `movss xmm2, [0xD95B68]` is retargeted at; the original seeds its
    // copy with 2.0f on disk, so this one starts there too. dword_1015B5BC: the
    // resume, published from the site's own prologue before the E9 replaces it, and
    // defined in source/config.ixx as the second word of the [HeadlightConeOffsets]
    // block (the original keeps it between the cone's hashes and its offsets, and the
    // cone walk can read it -- docs/rebuilt.md Â§2g).
    uint32_t zpatch_fchs_param = 0;
    float zpatch_fchs_offset = 2.0f;
    extern uint32_t zpatch_fchs_resume;


    // DualVehicleHeadlights' published state, one word per operand its two bodies
    // read: the light-state global the site pushes (the `push 0x1252D90` at
    // match+207), the three call targets both bodies share, the resume, the two
    // light indices the init helper sets, and the eight register slots a body saves
    // before its first draw and restores before its second.
    uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
    uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
    uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
    uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
    uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
    uint32_t zpatch_dual_light_a = 1;  // dword_1015802C (the original's on-disk 1)
    uint32_t zpatch_dual_light_b = 2;  // dword_10158030 (the original's on-disk 2)
    uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
    uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
    uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
    uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
    uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
    uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
    uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
    uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
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
static void CarPartsShadowShim();
static void CarDynamicShadowShim();
static void CarDynamicShadowShimB();
static void FchsCheckBody();
static void FchsShimBody();
static void DualHeadlightsNewBody();
static void DualHeadlightsOldBody();

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


// ---------------------------------------------------------------------------
// The vehicle light and shadow arms: force-headlight shadows, car-part shadows,
// the dual headlights, the bottom-distance clamp and the reversing light. Each
// publishes through Loader::AddPublish/AddInstall with the driver position it had
// when the arms lived in vehicles.ixx, so the tier order does not change.
// ---------------------------------------------------------------------------
namespace
{
    // One recovered sub-gate: is the current build's id a bit of `mask`? The
    // catalogue's own mask is the arm's gate; a few recovered rows carry a narrower
    // one of their own, which is what this reads. (The same helper as vehicles.ixx's.)
    bool CurrentBuildInMask(uint32_t mask)
    {
        return (mask & (1u << static_cast<uint32_t>(Loader::Current()))) != 0;
    }

    // A rel32 target inside the match: the displacement dword at `displacement`,
    // measured from the instruction's last byte at `next`. (The same helper as
    // vehicles.ixx's.)
    uint32_t Rel32Target(std::byte* match, std::ptrdiff_t displacement, std::ptrdiff_t next)
    {
        const auto displacementValue = *reinterpret_cast<const int32_t*>(match + displacement);
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(match + next) + static_cast<uint32_t>(displacementValue));
    }

    // CarDynamicShadowFix@noE's own gate: the recovered table gives the row the
    // mask 0x22 (build ids 1000 and 1040), the builds where the installer's pattern
    // E is absent. 1.0.0.4 has pattern E, so it keeps the E path and is not in the
    // mask even though pattern C matches there too.
    constexpr uint32_t kCarDynamicShadowNoEBuilds = 0x22u;

    // DualVehicleHeadlights' two recovered arm gates: the original's own table gives
    // the old body 0x62 (build ids 1, 5 and 6 -- 1.0.0.0, 1.0.4.0, 1.0.0.4) and the
    // new body 0x4BF80 (id 7 and every later build). The original's handler selects
    // between the two by the build id it classified (`cmp [build], 1` .. `cmp
    // [build], 6`, 0x100067F3), which is the same split. The union is every
    // supported build.
    constexpr uint32_t kDualHeadlightsOldBuilds = 0x62u;
    constexpr uint32_t kDualHeadlightsNewBuilds = 0x4BF80u;

// sub_100064D0, recovered VA 0x100064D0: the two light indices the dual-headlight
// bodies push. The original's bodies call it inside pushad/popad, so the clobbering
// is harmless there; the words hold the same values from load time on.
static void DualHeadlightConstants() noexcept
{
    zpatch_dual_light_a = 1;
    zpatch_dual_light_b = 2;
}

    // DualVehicleHeadlights' published operands, all read from the one match: the
    // light-state global the site pushes at match+207, the ped getter's call target
    // (the disp32 at match+140, next byte +144), the driver test's (+151/+155), the
    // draw call's (+331/+335) and the resume (`pop edi`, match+338). The site's own
    // instructions carry those operands, so the publish is a read, not a decode.
    void PublishDualVehicleHeadlights()
    {
        // The catalogue's key mask cannot be the gate here: see the install arm.
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_dual_bcc = *pattern.get_first<uint32_t>(207);
        zpatch_dual_bd0 = Rel32Target(match, 140, 144);
        zpatch_dual_be4 = Rel32Target(match, 151, 155);
        zpatch_dual_bd4 = Rel32Target(match, 331, 335);
        zpatch_dual_be0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 338));
    }


    // CarPartsShadowFix's published resumes: the site's own bytes -- the `jle` it
    // would have run (match+2) and the loop exit that `jle` jumps to (match+0x45).
    // The shared detour is installed under either key, so the publish uses the same
    // combined gate.
    void PublishCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_carparts_resume_if = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 2));
        zpatch_carparts_resume_else =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 0x45));
    }

    // CarDynamicShadowFix's published call target and resume, from whichever body
    // variant the build carries: pattern A's `call` sits at match+6, so its resume is
    // match+0xB and the displacement is at match+7; pattern B's `call` sits at
    // match+8, so its resume is match+0xD and the displacement is at match+9.
    void PublishCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 11));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 7);
            zpatch_carparts_resume_2 = resume;
            return;
        }

        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 13));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 9);
            zpatch_carparts_resume_2 = resume;
        }
    }

    // ForceCarHeadlightShadows' resume: the E9 replaces the shadow function's entry
    // (game 0x8F7590), and the shim replays those twelve prologue bytes itself, so
    // the continuation is the instruction after them (match+12, the `mov eax,
    // [ebp+20h]`). Read before the site is written, as the original's handler does.
    void PublishForceCarHeadlightShadows()
    {
        if (!Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B");
        if (!pattern.empty())
            zpatch_fchs_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(12)));
    }

    // DualVehicleHeadlights: both recovered arms jump to their own body at the same
    // site, and the original's handler picks the body from the build id it classified
    // (`cmp [build], 1` .. `cmp [build], 6` at 0x100067F3): ids 1..6 get the old body,
    // every later build the new one. That is the pair of recovered masks, so each
    // build runs exactly one arm; the reference tree ran both unconditionally and the
    // later write won, which lands on the same bytes everywhere the masks are
    // disjoint (they are).
    //
    // The gate is the catalogue's INI read alone. This key's catalogue mask (0x20) is
    // the feature document's build list, not the original's: the original's own two
    // arm masks cover every supported build, so `Loader::Enabled` would never install
    // this arm anywhere but 1.0.4.0. See docs/parity.md.
    void InstallDualVehicleHeadlights()
    {
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        if (CurrentBuildInMask(kDualHeadlightsOldBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsOldBody), 0x10006630u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsOldBody), true);
        }

        if (CurrentBuildInMask(kDualHeadlightsNewBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsNewBody), 0x100064F0u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsNewBody), true);
        }
    }


    // CarPartsShadowFix shares the detour with CarDynamicShadowFix: the original
    // gates the rewrite on either key and only the standalone forcing writes below
    // are CarPartsShadowFix's own.
    void InstallCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarPartsShadowShim), 0x10006880u);
        injector::MakeJMP(match - 6, reinterpret_cast<void*>(&CarPartsShadowShim), true);

        if (!Loader::Enabled("CarPartsShadowFix"))
            return;

        // The standalone arm forces the shadow path on its own: the `jnz` that would
        // skip it becomes a short jump. The recovered standalone handler scans two
        // variants: the `83 F8 04` site (1.0.6.0 and later) and the `83 FA 04 75 0C`
        // site (the older builds). Each scan is a clean skip on the builds that do
        // not carry it.
        pattern = find_pattern("83 F8 04 C6 44 24 0E 00 75 0F");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::ShortJmp, true);

        pattern = find_pattern("83 FA 04 75 0C");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(3), Bytes::ShortJmp, true);
    }

    // CarDynamicShadowFix: the shadow call site is detached so the shadow is drawn
    // twice with the remaining arguments. Two recovered body variants exist: pattern
    // A (1.0.6.0 and later) and pattern B (the older builds); the key's own build
    // mask covers both, and the signatures are the gate. The same installer's C and
    // E branches are the old builds' two extra writes: a range check rewritten as an
    // unconditional jump, and the six-byte NOP fill where pattern E is absent.
    void InstallCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShim), 0x100068A0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShim), true);
        }

        // Pattern B is the old-build variant of the same call site.
        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShimB), 0x100068D0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShimB), true);
        }

        // Pattern C: the double range check (`>= 0x14` / `>= 6`) is skipped by
        // turning the second test's `jnz` into a jump over it. The row exists only on
        // the older builds, which is also where the @noE fill below belongs.
        pattern = find_pattern("83 F8 14 7D 05 83 F8 06 7D 14");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match + 3, match + 0x1E, true);

            // CarDynamicShadowFix@noE: with pattern E absent (the recovered gate is
            // build ids 1000 and 1040) the installer NOP-fills six bytes after the C
            // anchor. 1.0.0.4 has pattern E, so it is not in the mask.
            if (CurrentBuildInMask(kCarDynamicShadowNoEBuilds))
                injector::MakeNOP(match + 62, 6, true);
        }

        // The shadow's record size is forced to 0x100 from 0x104, and the branch that
        // would skip the shadow becomes unconditional.
        pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(2), Bytes::Zero, true);

        pattern = find_pattern("83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);

        // With ForceCarHeadlightShadows off the original also re-encodes the `jz`
        // that skips the shadow as a five-byte `jmp` to the same target, and forces
        // the pushed shadow size from 0x504 to 0x100.
        if (Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        pattern = find_pattern("0F 84 9E 00 00 00 80 7D 28");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 2);
            injector::MakeJMP(match, match + 6 + displacement, true);
        }

        pattern = find_pattern("51 68 04 05 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(2), 0x100u, true);
    }

    // ForceCarHeadlightShadows: the shadow function hooks to the shim, the three
    // branches that decide whether the headlight cone draws its shadow are forced so
    // the shadow always runs, and the offset float is retargeted at this module's
    // word instead of the game's 0.0f at 0xD95B68. The writes, in the original
    // handler's order:
    //   * `EB` at the anchor's +6 (the `jne +79h` of `cmp al,1` / `test bl,bl`),
    //     `E9` at +95 to match+476 (the second `jne +177h`, one byte shorter than the
    //     six-byte `0F 85` it replaces, which is why the write starts on the opcode
    //     and its sixth byte becomes the unreachable remainder), `EB` at +379 (the
    //     `jne +5Fh` on the same target) and the `movss` operand at +211;
    //   * the E9 at the shadow function's entry;
    //   * the handler tail's restore of the two shadow-buffer pushes (0x504 / 0x104)
    //     and of the `je +9Eh` at 0x8F719F. On a pristine image every one of those is
    //     a no-op; they are the original's `sub_1000E870` writes, which undo exactly
    //     the CarDynamicShadowFix rewrites that arm skips while this key is on, so
    //     the two keys interact the way they do in the original.
    void InstallForceCarHeadlightShadows()
    {
        if (!Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        auto pattern = find_pattern("3C 01 75 50 84 DB 75 79");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::WriteMemory<std::byte>(match + 6, Bytes::ShortJmp, true);
            injector::MakeJMP(match + 95, match + 476, true);
            injector::WriteMemory<std::byte>(match + 379, Bytes::ShortJmp, true);

            // The `movss xmm2, [0xD95B68]` operand becomes this module's float. The
            // original writes its own image's dword_101580C0 here (0x101580C0); the
            // stored baseline records the reference dump's 0xD0000000 placeholder for
            // the word, so the slot is registered for the dump's canonicalisation.
            injector::WriteMemory<uint32_t>(match + 211,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_fchs_offset)), true);
            Bodies::RegisterPublishedImage(reinterpret_cast<uintptr_t>(match + 211));
        }

        pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&FchsShimBody), true);

        // `push 0x504` (the first shadow buffer) and `push 0x104` (the second) are
        // written back as dwords; both immediates are unchanged on a pristine image.
        pattern = find_pattern("8D 4C 24 50 51 68 04 05 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(6), 0x00000504u, true);

        pattern = find_pattern("8D 44 24 50 50 68 04 01 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(6), 0x00000104u, true);

        // `0F 84 9E 00 00 00` (the `je` that skips the shadow) is rewritten with its
        // own six bytes, the last one left as it was.
        pattern = find_pattern("F3 0F 11 44 24 1C 0F 84 9E 00 00 00");
        if (!pattern.empty())
        {
            auto* site = pattern.get_first<std::byte>(6);
            injector::WriteMemory<std::byte>(site, std::byte{ 0x0F }, true);
            injector::WriteMemory<std::byte>(site + 1, std::byte{ 0x84 }, true);
            injector::WriteMemory<uint16_t>(site + 2, 0x009Eu, true);
            injector::WriteMemory<std::byte>(site + 4, Bytes::Zero, true);
        }
    }

    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = find_pattern("0F 2F D0 76 25 0F 28 C2");
        if (pattern.empty())
            return;

        auto* site = pattern.get_first<std::byte>(3);
        injector::WriteMemory<std::byte>(site, Bytes::ShortJmp, true);
        injector::WriteMemory<std::byte>(site + 1, Bytes::JumpOverClamp, true);
    }

    // ReversingLightFix: the slot displacement of `mov eax,[edx+64h]` becomes 0x60,
    // so the reversing-light path reads the neighbouring member.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::Slot60, true);
    }

// Each stub's inline `call` is invisible to C++; naming the forwarders here keeps
// their out-of-line definitions in the image.
[[maybe_unused]] const uintptr_t graphicsBodies[] = {
    reinterpret_cast<uintptr_t>(&FchsCheckBody),
};
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

    // The vehicle light and shadow arms, at their original driver positions.
    Loader::AddPublish(Loader::Entry::Vehicles, 1, PublishCarPartsShadowFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 2, PublishCarDynamicShadowFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 3, PublishForceCarHeadlightShadows);
    Loader::AddPublish(Loader::Entry::Vehicles, 5, PublishDualVehicleHeadlights);
    Loader::AddInstall(Loader::Entry::Vehicles, 2, InstallCarPartsShadowFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 3, InstallCarDynamicShadowFix);
    // After the shadow arm: the original's ForceCarHeadlightShadows handler runs
    // after the CarDynamicShadowFix block, and its tail restores what that block
    // rewrites -- so with both keys on, the restore is what the game sees.
    Loader::AddInstall(Loader::Entry::Vehicles, 4, InstallForceCarHeadlightShadows);
    Loader::AddInstall(Loader::Entry::Vehicles, 5, InstallInfiniteCarBottomDistance);
    Loader::AddInstall(Loader::Entry::Vehicles, 6, InstallReversingLightFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 13, InstallDualVehicleHeadlights);

    // ForceCarHeadlightShadows' two [HeadlightConeOffsets] arrays: dword_1015ADB8 (the
    // hashes) and dword_1015B5C0 (the offsets). The hash registration is the original's
    // **extended** 2,052-byte range -- 512 words plus the one word the walk reads past
    // slot 511 (dword_1015B5B8) -- so the proof compares that read too. The word is not
    // a copy: it is CarDynamicShadowFix's published shadow-function slot, defined
    // immediately after the array in source/config.ixx so the layout matches the
    // original's .data (docs/rebuilt.md Â§2g). The offsets array is registered at its
    // own base, which the original places 2,056 bytes into the block.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_hashes),
        0x1015ADB8u, sizeof(zpatch_headlight_cone_hashes) + sizeof(uint32_t));  // 2052
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_values),
        0x1015B5C0u, sizeof(zpatch_headlight_cone_values));   // 512 * 4 = 2048

    Bodies::Register(reinterpret_cast<uintptr_t>(&FchsShimBody), 0x1001ADB0u);
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

// abi_carparts_shadow, recovered VA 0x10006880: the shared car-parts shadow body.
// The site's `movzx ebx,[edi+1Ch]` / `xor esi,esi` are re-run here (the jump
// overwrites them), with a null guard on the parts pointer in front, and the
// re-run `test ebx,ebx` is what the resumed `jle` reads. The nops are the
// original's own padding between those instructions, not filler added here.
static __declspec(naked) void CarPartsShadowShim()
{
    __asm
    {
        cmp edi, 0
        nop
        jz carparts_shadow_skip
        nop
        movzx ebx, WORD PTR [edi + 0x1C]
        nop
        xor esi, esi
        nop
        test ebx, ebx
        nop
        jmp DWORD PTR [zpatch_carparts_resume_if]
    carparts_shadow_skip:
        nop
        jmp DWORD PTR [zpatch_carparts_resume_else]
    }
}

// abi_carparts_shadow_2, recovered VA 0x100068A0: CarDynamicShadowFix's detour. It
// re-runs the shadow call the site overwrote and then repeats it with the
// remaining arguments, calling through the published target rather than a direct
// call, which is what the original's body does.
static __declspec(naked) void CarDynamicShadowShim()
{
    __asm
    {
        push esi
        nop
        push 2
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        push esi
        nop
        push 0
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        add esp, 0x14
        nop
        jmp DWORD PTR [zpatch_carparts_resume_2]
    }
}

// abi_carparts_shadow_2b, recovered VA 0x100068D0: CarDynamicShadowFix's second
// shadow-submission body, the old builds' variant of CarDynamicShadowShim. It makes
// the same double call through the published target and pops the same 20 argument
// bytes, but the first submission pushes 2/0/0 instead of 2/ebx/ebx.
static __declspec(naked) void CarDynamicShadowShimB()
{
    __asm
    {
        push esi
        nop
        push 2
        nop
        push 0
        nop
        push 0
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        push esi
        nop
        push 0
        nop
        push 0
        nop
        push 0
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        add esp, 0x14
        nop
        jmp DWORD PTR [zpatch_carparts_resume_2]
    }
}

// abi_fchs_check, recovered VA 0x1001AD20 (46 instructions, 139 bytes,
// 0x1001AD20..0x1001ADAA): ForceCarHeadlightShadows' per-model offset check,
// transcribed instruction for instruction.
//
// It seeds the offset float (dword_101580C0) with 2.0f, then walks the
// [HeadlightConeOffsets] hash table from slot 0: `mov eax,ds:[0x1015ADB8]` loads
// slot 0, and each iteration asks the game's model store for that hash's model index
// and compares it with the object's signed word at +0x2E (the object is read
// unguarded, as the original's `movsx eax,word [edi+2Eh]` is). On a match it
// publishes that slot's float; otherwise it loads the next slot with
// `mov eax,ds:[esi*4+0x1015ADBC]` -- the table's base **plus four**, because ESI
// counts the slot it just consumed -- and stops at the first zero word.
//
// The one-word-past read. With ESI at 511 that load is `ds:[0x1015B5B8]`, the word
// immediately after the 512-slot table in the original's `.data`. It is *not* dead
// space: it is CarDynamicShadowFix's published shadow-function pointer, written
// before this check can run by the ASI's own car-shadow code (see the block comment
// in source/config.ixx). Our layout reproduces it as the same slot, with the check's
// own resume word (0x1015B5BC) after it, so the walk reads the value the original's
// does rather than a placeholder -- which is what makes this transcription possible
// instead of a bounded walk (docs/rebuilt.md Â§2g). The two arrays are registered with
// their original VAs; the hash table's registration is the extended 2,052 bytes
// (512 words + the neighbour).
//
// The /GS frame is the original's (cookie slot, check call slot) and the
// `nop dword ptr [eax]` at 0x1001AD4D is emitted as bytes. No exception frame, no TLS
// static-init guard.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void FchsCheckBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov eax, DWORD PTR [zpatch_headlight_cone_hashes]
        push esi
        xor esi, esi
        mov DWORD PTR [zpatch_fchs_offset], 40000000h   // 2.0f, the .data seed
        push edi
        mov edi, DWORD PTR [zpatch_fchs_param]
        test eax, eax
        je fchs_done
        __emit 0x0F
        __emit 0x1F
        __emit 0x00                  // nop dword ptr [eax]
    fchs_loop:
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        movsx eax, WORD PTR [edi + 2Eh]
        add esp, 8
        cmp eax, DWORD PTR [ebp - 8]
        je fchs_found
        // The table's base plus four: ESI is the slot just consumed, so slot ESI+1 is
        // at [esi*4+base+4], and at ESI=511 that is the word past the table.
        mov eax, DWORD PTR [esi*4 + zpatch_headlight_cone_hashes + 4]
        inc esi
        test eax, eax
        jne fchs_loop
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    fchs_found:
        movss xmm0, DWORD PTR [esi*4 + zpatch_headlight_cone_values]
        movss DWORD PTR [zpatch_fchs_offset], xmm0
    fchs_done:
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

// abi_fchs_shim, recovered VA 0x1001ADB0 (9 instructions, 0x1001ADB0..0x1001ADCE):
// the hook body the arm installs at the shadow function's entry (game 0x8F7590).
// It stores the object the function was called with, runs the check between
// pushad/popad, then replays the prologue the E9 replaced -- the resume must run
// with the frame those twelve bytes establish -- and tail-jumps to the published
// resume, never calls it.
static __declspec(naked) void FchsShimBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_fchs_param], ecx
        __emit 0x60  // pushad
        call FchsCheckBody
        __emit 0x61  // popad
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF0h
        sub esp, 0D4h
        jmp DWORD PTR [zpatch_fchs_resume]
    }
}

// sub_100064F0, recovered VA 0x100064F0: DualVehicleHeadlights' new-build body. It
// saves every GPR (and ESP) into the module words, draws the two headlights through
// the site's own calls -- the game's light-state global, the ped getter, the driver
// test and the light-draw call the site's instructions name -- restoring the caller's
// registers between the draws, then tail-jumps to the site's `pop edi` (match+338).
// The stack offsets are the recovered ones: this variant is the 1.0.6.0-and-later
// light call's frame, and the old-build body below differs only in them.
static __declspec(naked) void DualHeadlightsNewBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 1Ch]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 18h]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
    }
}

// sub_10006630, recovered VA 0x10006630: DualVehicleHeadlights' old-build body, the
// 1.0.0.0/1.0.4.0/1.0.0.4 variant. Instruction for instruction the same as the new
// body except for the four float offsets each draw reads: this build's light call
// pushes its arguments one dword (then two) further up the frame.
static __declspec(naked) void DualHeadlightsOldBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 20h]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 1Ch]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
    }
}
