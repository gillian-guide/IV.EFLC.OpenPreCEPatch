module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import game;
import improvements;
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

// The SuperLODFix pair's published state: the two resumes the shim tails into
// (dword_101580AC and dword_101580B0) and the lookup the model helper calls
// (dword_101580B4). The original keeps them in .bss beside its code; the
// transcribed asm names the slots directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_super_lod_fail = 0;    // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;      // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;  // dword_101580B4
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

// ---------------------------------------------------------------------------
// CutsceneFixes
//
// One option, two halves of one chain. Chain B is the original's sub_100388C0: a
// thiscall `char(float)` that the worker sub_10038B40 installs as a `call` at the
// two places the game's camera function (1.0.8.0 `0xC9A640`) is entered, so both
// callers run it. Chain A is the single `je` -> `jmp` inside that same function
// that makes its "target found" test unconditional. The original runs them from
// two loader arms under one option, so one arm installs both here.
//
// The body's mutable state sits in the original's .bss beside it
// (dword_1015BDE4 / dword_1015BDF0 / dword_1015BDE8) and its constants in its
// .rdata (0x101522F0, 0x101522F8, 0x101522FC, 0x10152340, 0x101523D0); the
// transcribed asm names module slots instead, and the arm fills the two game
// pointers from the site's own call operand before it overwrites the call.
// ---------------------------------------------------------------------------
extern "C"
{
    // dword_1015BDE4: the camera function the site calls. The worker publishes
    // `match + [match - 4]` -- the destination of the `E8` it is about to
    // overwrite -- and this is that address.
    uint32_t zpatch_cutscene_target = 0;

    // dword_1015BDF0: the same address + 0x4F. The camera function's
    // `mov byte [esp + 1Ch], 1` sits at target + 0x4A and its immediate byte is the
    // last one (target + 0x4E); the body's `dec esi` lands the store there.
    uint32_t zpatch_cutscene_poke = 0;

    // dword_1015BDE8: the accumulated scene-frame time, a double that starts at
    // zero (the original's .bss).
    uint64_t zpatch_cutscene_accum = 0;

    // 0x10063010: the original's VirtualProtect import slot. The arm fills this
    // with the module's own import; the body still calls it indirectly (`call edi`
    // after `mov edi, [slot]`), exactly as the original does.
    uint32_t zpatch_cutscene_virtual_protect = 0;

    // The body's literal pool, from the original ASI's .rdata. Each is declared
    // with the type its instruction reads and initialised with the number it is;
    // every literal round-trips to the original's bytes at the VA in its comment.
    // The one mask stays a hex word because it is not a number. No explicit
    // alignment: a scalar load does not need one on x86.
    double zpatch_cutscene_zero = 0.0;        // 0x101522F0
    // 0x10152340: the double nearest 0.3333, not 1/3.
    double zpatch_cutscene_limit = 0.3333;
    // 0x101523D0: an `andps` sign mask, not a number (the body clears the sign bit
    // of a difference before comparing it), so this one stays a hex word.
    uint32_t zpatch_cutscene_abs_mask = 0x7FFFFFFFu;
    float zpatch_cutscene_eps_a = 0.05f;      // 0x101522F8
    float zpatch_cutscene_eps_b = 0.5f;       // 0x101522FC
}

// The loader preamble's salt this body resolves its native with (dword_1015803C)
// and the native layer's guarded resolve entry, which is the original's
// sub_10003980 rebuilt as C (source/native.ixx). Both live in another module
// unit, so they are declared here with the C linkage they are defined with.
extern "C"
{
    extern uint32_t zpatch_native_salt_1015803C;
    extern uintptr_t zpatch_native_resolve_entry;
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
static void SuperLodModel();
static void SuperLodShim();
static void ShaderStreamingFilterHpe();
static void ShaderStreamingFilterV();
static void ShaderStreamingFindHpe();
static void ShaderStreamingFindV();
static void BorderlessWindowBodyW();
static void BorderlessWindowBodyA();
static void CutsceneResolveEntry();
static void CutsceneChainBody();

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
        if (!pattern.empty() && ExpectMatchCount(pattern, 2, "BuildingDynamicShadows hash pair"))
        {
            zpatch_bds_hash_a = *pattern.get(0).get<uint32_t>(20);
            zpatch_bds_hash_b = *pattern.get(1).get<uint32_t>(20);
        }
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

    // CutsceneFixes' published state. The worker publishes two game pointers from
    // the primary site's own `E8` operand: the call's destination (the camera
    // function) and that destination + 0x4F, one past the immediate the body
    // pokes. Both are read here, before the install tier overwrites the call; the
    // secondary site calls the same function and publishes nothing, as the
    // original's second scan does.
    void PublishCutsceneFixes()
    {
        if (!Loader::Enabled("CutsceneFixes"))
            return;

        auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *pattern.get_first<int32_t>(-4);
            zpatch_cutscene_target =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + displacement));
            zpatch_cutscene_poke =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + displacement + 0x4F));
            Log::Print("cutscene: match {:08X} disp {:08X} target {:08X} poke {:08X}",
                       reinterpret_cast<uintptr_t>(match), displacement,
                       zpatch_cutscene_target, zpatch_cutscene_poke);
        }

        // The body's VirtualProtect call goes through a slot because the original
        // loads its IAT entry by absolute address; fill it from the module's own
        // import, as audio.ixx does for its advance helper.
        zpatch_cutscene_virtual_protect = reinterpret_cast<uintptr_t>(&VirtualProtect);
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
        {
            if (ImprovementEnabled(Improvement::alpha_nop))
            {
                // Only the `jnz` (six bytes) is removed; the `test` keeps setting
                // the flags the following code reads.
                injector::MakeNOP(pattern.get_first<std::byte>(7), 6, true);
            }
            else
            {
                injector::MakeNOP(pattern.get_first(0), 13, true);
            }
        }
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
    // is preserved in the 1:1 configuration (the failsafe group off). With the
    // group on, the P13 count check refuses the partial install instead.
    void InstallNoImpostorHeightCutoff()
    {
        if (!Loader::Enabled("NoImpostorHeightCutoff"))
            return;

        auto pattern = find_pattern<2>("76 0B F3 0F 11 8C 24 88 00 00 00 EB 09");
        if (pattern.empty())
            return;
        if (!ExpectMatchCount(pattern, 2, "NoImpostorHeightCutoff literals"))
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

        // The shim compares against both published model hashes; a publish that
        // refused the pair leaves them zero, and installing it would make every
        // building shadow test compare against hash 0. Keep the game's path.
        if (Failsafe::Missing("BuildingDynamicShadows hash pair",
                zpatch_bds_hash_a == 0 || zpatch_bds_hash_b == 0))
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

    // ImprovedShaderStreaming: the name-by-name LookupShader walks become one call
    // each into a shader-array walk with a kind filter, and the rest of the old
    // chain is jumped over. The original's handler sub_10045AD0 writes the four
    // sites at match + 0x55 (E8 body A), + 0x62 (E9, displacement 0x8E), + 0xFD
    // (E8 body B) and + 0x10A (E9, displacement 0x377); the displacement it writes
    // is the constant but it encodes exactly the two targets below, so the jumps
    // are computed rather than hardcoded. The finders are the bodies the calls
    // name; the filters are only called by the finders, so they appear in the body
    // manifest rather than as branch targets.
    void InstallImprovedShaderStreaming()
    {
        if (!Loader::Enabled("ImprovedShaderStreaming"))
            return;

        auto pattern = find_pattern(
            "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindHpe), 0x10045A30u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindV), 0x10045A80u);

        // The OpenPreCEPatch compatibility change: only the two LookupShader calls
        // are replaced. The two jumps over the remaining name chain are a pure
        // speed-up, and the bytes they overwrite are inside the window
        // LibertyCityPlates scans for addCustomShaderEffect; dropping them keeps
        // that mod's address scan intact. The 1:1 arm keeps the original's four
        // writes, so its write set is unchanged.
        if (ImprovementEnabled(Improvement::shader))
        {
            // A build can match the surrounding bytes without the two call
            // opcodes; writing a branch over an arbitrary instruction is worse
            // than not applying the speed-up.
            if (match[85] != std::byte{ 0xE8 } || match[253] != std::byte{ 0xE8 })
                return;

            injector::MakeCALL(match + 85, reinterpret_cast<void*>(&ShaderStreamingFindHpe), true);
            injector::MakeCALL(match + 253, reinterpret_cast<void*>(&ShaderStreamingFindV), true);
            return;
        }

        injector::MakeCALL(match + 85, reinterpret_cast<void*>(&ShaderStreamingFindHpe), true);
        injector::MakeJMP(match + 98, match + 245, true);
        injector::MakeCALL(match + 253, reinterpret_cast<void*>(&ShaderStreamingFindV), true);
        injector::MakeJMP(match + 266, match + 1158, true);
    }

    // The cutscene_entry group: one hook at the shared camera callee instead of
    // the three sites. The callee is relocated into private memory, its 13
    // direct calls are re-pointed, and the branch fix is baked into the copy.
    // Any shape mismatch falls back to the three original sites, so the fix is
    // never lost.
    namespace
    {
        constexpr size_t kCutsceneCalleeSize = 0x2D2u;
        constexpr uint32_t kCutscenePokeOffset = 0x4Fu;
        constexpr uint32_t kCutsceneBranchOffset = 0x200u;
        constexpr uint32_t kCutsceneCalls[] = {
            0x00Eu, 0x01Du, 0x092u, 0x0E9u, 0x0F1u, 0x101u, 0x118u,
            0x127u, 0x13Cu, 0x233u, 0x24Cu, 0x286u, 0x29Au,
        };

        bool RelocateCalls(uint8_t* destination, const uint8_t* source,
            std::span<const uint32_t> offsets) noexcept
        {
            for (const uint32_t offset : offsets)
            {
                int32_t displacement{};
                std::memcpy(&displacement, source + offset + 1, sizeof(displacement));
                const auto target = reinterpret_cast<uintptr_t>(source + offset + 5) +
                                    static_cast<intptr_t>(displacement);
                const auto next = reinterpret_cast<uintptr_t>(destination + offset + 5);
                const auto relative = static_cast<intptr_t>(target) -
                                      static_cast<intptr_t>(next);
                if (relative < INT32_MIN || relative > INT32_MAX)
                    return false;
                const auto narrowed = static_cast<int32_t>(relative);
                std::memcpy(destination + offset + 1, &narrowed, sizeof(narrowed));
            }
            return true;
        }

        void* AllocateNearImage(size_t size) noexcept
        {
            const auto base = Game::Get().ImageBase();
            const auto imageSize = Game::Get().ImageSize();
            constexpr uintptr_t kStep = 0x1000000u;
            constexpr uintptr_t kReach = 0x40000000u;
            constexpr uintptr_t kPageMask = 0xFFFFu;
            const uintptr_t first = (base + imageSize + kPageMask) & ~kPageMask;
            for (uintptr_t candidate = first; candidate < first + kReach; candidate += kStep)
            {
                if (void* const block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            const uintptr_t lower = base > kReach ? base - kReach : kStep;
            for (uintptr_t candidate = first - kStep;
                 candidate > lower && candidate > kStep; candidate -= kStep)
            {
                if (void* const block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            return nullptr;
        }

        uint8_t* CutsceneCalleeCopy(uintptr_t callee) noexcept
        {
            static constexpr uint8_t kPrologue[5] = { 0x55u, 0x8Bu, 0xECu, 0x83u, 0xE4u };
            static constexpr uint8_t kPokeSite[5] = { 0xC6u, 0x44u, 0x24u, 0x1Cu, 0x01u };
            static constexpr uint8_t kBranchSite[8] = {
                0x74u, 0x24u, 0x8Bu, 0x44u, 0x24u, 0x2Cu, 0x83u, 0xF8u,
            };
            const auto& game = Game::Get();
            if (callee == 0 || !game.Contains(callee) ||
                !game.Contains(callee + kCutsceneCalleeSize - 1))
                return nullptr;

            const auto* source = reinterpret_cast<const uint8_t*>(callee);
            if (std::memcmp(source, kPrologue, sizeof(kPrologue)) != 0 ||
                std::memcmp(source + 0x4Au, kPokeSite, sizeof(kPokeSite)) != 0 ||
                std::memcmp(source + kCutsceneBranchOffset, kBranchSite,
                    sizeof(kBranchSite)) != 0)
                return nullptr;

            auto* copy = static_cast<uint8_t*>(AllocateNearImage(kCutsceneCalleeSize));
            if (copy == nullptr)
                return nullptr;
            if (!RelocateCalls(copy, source, kCutsceneCalls))
            {
                VirtualFree(copy, 0, MEM_RELEASE);
                return nullptr;
            }
            // The copy's `jz` at +0x200 becomes the unconditional `jmp`, with
            // the original displacement kept.
            copy[kCutsceneBranchOffset] = static_cast<uint8_t>(Bytes::ShortJmp);
            return copy;
        }
    } // namespace

    // CutsceneFixes: chain B's two camera call sites become calls into
    // sub_100388C0, and chain A's `je` in the camera function becomes the
    // unconditional `jmp` that skips its "target found" test. Each caller's
    // pattern starts at the epilogue/a byte the call precedes, so the site is the
    // five bytes before the match. Chain A's two texts are the original worker's
    // first and fallback patterns: the first is the one the supported 1.0.8.0-class
    // images carry, the second covers the older ones.
    void InstallCutsceneFixes()
    {
        if (!Loader::Enabled("CutsceneFixes"))
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&CutsceneChainBody), 0x100388C0u);
        // The body's call names the resolve forwarder, which stands in for the
        // original's sub_10003980; recording that VA keeps the registered branch in
        // the original's coordinates.
        Bodies::Register(reinterpret_cast<uintptr_t>(&CutsceneResolveEntry), 0x10003980u);

        if (ImprovementEnabled(Improvement::cutscene_entry) && zpatch_cutscene_target != 0)
        {
            const auto callee = static_cast<uintptr_t>(zpatch_cutscene_target);
            if (uint8_t* copy = CutsceneCalleeCopy(callee))
            {
                zpatch_cutscene_target = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(copy));
                zpatch_cutscene_poke = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(copy) + kCutscenePokeOffset);
                injector::MakeJMP(reinterpret_cast<void*>(callee),
                    reinterpret_cast<void*>(&CutsceneChainBody), true);
                Log::Print("cutscene: hooked callee {:08X}, copy {:08X}", callee,
                    reinterpret_cast<uintptr_t>(copy));
                return;
            }
        }

        auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8");
        if (!pattern.empty())
        {
            injector::MakeCALL(pattern.get_first(-5),
                reinterpret_cast<void*>(&CutsceneChainBody), true);
        }

        pattern = find_pattern("8B CF 88 44 24 0F E8 ? ? ? ? A1");
        if (!pattern.empty())
        {
            injector::MakeCALL(pattern.get_first(-5),
                reinterpret_cast<void*>(&CutsceneChainBody), true);
        }

        pattern = find_pattern("74 24 8B 44 24 2C 83 F8 03");
        if (pattern.empty())
            pattern = find_pattern("74 0B 83 7C 24 2C 03");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);

        Log::Print("cutscene: resolved={} target={:08X} poke={:08X} resolveEntry={:08X}",
                   zpatch_cutscene_target != 0 ? 1 : 0, zpatch_cutscene_target,
                   zpatch_cutscene_poke, zpatch_native_resolve_entry);
    }
}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterGraphics()
{
    Loader::PublishEvent() += []
    {
        PublishBuildingDynamicShadows();
        PublishCutsceneFixes();
        PublishEmissiveLerpFix();
        PublishHighQualityReflections();
        PublishSuperLODFix();
    };
    Loader::InstallEvent() += [] { InstallBorderlessWindowed(); };
    Loader::InstallEvent() += [] { InstallBuildingAlphaFix(); };
    Loader::InstallEvent() += [] { InstallBuildingDynamicShadows(); };
    Loader::InstallEvent() += [] { InstallCutsceneFixes(); };
    Loader::InstallEvent() += [] { InstallEmissiveLerpFix(); };
    Loader::InstallEvent() += [] { InstallForceDynamicShadowsEverywhere(); };
    Loader::InstallEvent() += [] { InstallForceShadowsOnObjects(); };
    Loader::InstallEvent() += [] { InstallHighQualityReflections(); };
    Loader::InstallEvent() += [] { InstallImprovedShaderStreaming(); };
    Loader::InstallEvent() += [] { InstallNoImpostorHeightCutoff(); };
    Loader::InstallEvent() += [] { InstallNoLODLightHeightCutoff(); };
    Loader::InstallEvent() += [] { InstallRemoveBoundingBoxCulling(); };
    Loader::InstallEvent() += [] { InstallRemoveStaticCarShadows(); };
    Loader::InstallEvent() += [] { InstallRemoveStaticCutsceneCarShadows(); };
    Loader::InstallEvent() += [] { InstallSuperLODFix(); };
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

// sub_100459E0, recovered VA 0x100459E0: the HPE filter the shader finder calls
// with the shader-name object. The finder takes the entry whose filter result is 0,
// so 0 means "this is the name kind we want": the kind byte 'h', or 'p' followed by
// 'e'. Every other name returns 2, which the finder reads as "keep looking".
static __declspec(naked) void ShaderStreamingFilterHpe()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        mov cl, BYTE PTR [eax + 4]
        cmp cl, 68h
        jz hpe_match
        cmp cl, 70h
        jnz hpe_skip
        cmp BYTE PTR [eax + 5], 65h
        jz hpe_match
    hpe_skip:
        mov eax, 2
        pop ebp
        ret
    hpe_match:
        xor eax, eax
        pop ebp
        ret
    }
}

// sub_10045A10, recovered VA 0x10045A10: the V filter, the same 0-for-match /
// 2-for-skip contract for the single kind byte 'v'.
static __declspec(naked) void ShaderStreamingFilterV()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov ecx, DWORD PTR [ebp + 0Ch]
        xor edx, edx
        mov eax, 2
        cmp BYTE PTR [ecx + 4], 76h
        cmovz eax, edx
        pop ebp
        ret
    }
}

// sub_10045A30, recovered VA 0x10045A30: the replacement for the
// `LookupShader("gta_ped")` call (the arm's second write then jumps over the
// following ped-name lookups). It walks the shader group's array (count at +0Ch,
// array at +8), asks each shader's virtual +0x10 for its name object and takes
// the first one the HPE filter reports as the wanted kind (a 0 result; 2 means
// keep looking), or returns null. `ret 4` because the caller pushed two arguments.
static __declspec(naked) void ShaderStreamingFindHpe()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe hpe_none
        mov ebx, DWORD PTR [esp + 10h]
    hpe_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz hpe_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterHpe
        add esp, 8
        test eax, eax
        jz hpe_found
    hpe_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl hpe_loop
    hpe_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    hpe_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
    }
}

// sub_10045A80, recovered VA 0x10045A80: the same walk for the V filter, the
// replacement for the `LookupShader("gta_vehicle_default")` call (the arm's fourth
// write then jumps to the end of the shader chain).
static __declspec(naked) void ShaderStreamingFindV()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe v_none
        mov ebx, DWORD PTR [esp + 10h]
    v_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz v_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterV
        add esp, 8
        test eax, eax
        jz v_found
    v_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl v_loop
    v_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    v_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
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

// The body's `call 0x10003980` must stay a direct `call`: the original's is an
// `E8`, and an indirect `call [slot]` is a different instruction shape. A module
// interface cannot export a naked function either (C2488), so the call names this
// six-byte forwarder, which enters the layer's C entry with ECX (the salt) and
// the stack untouched -- the same shape as misc.ixx's drive-by forwarder.
static __declspec(naked) void CutsceneResolveEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_resolve_entry]
    }
}

// sub_100388C0, recovered VA 0x100388C0: chain B's body, 146 instructions and 592
// bytes to its `ret 4`, plus the restore block at 0x10038B10..0x10038B3F that the
// six `ja` exits reach (the proof sweeps the 146; the restore block follows the
// `ret` and is not decoded by it). Its frame is the original's 0xA18: two
// 0x140-dword object copies (ebp-504h and ebp-0A04h), the 8-byte block
// VirtualProtect writes its old protection to, and the three scratch words the
// camera call uses.
//
// It resolves a game native through the loader salt dword_1015803C, copies the
// scene object it answers with into the first block and accumulates that object's
// frame delta (a float at +8) into the double accumulator. It then makes the
// camera function's `mov byte [esp + 1Ch], 1` immediate readable, writes 1 over
// it, calls the function with the body's own float argument and keeps the call's
// double result as a flag. Once the recovered limit (0.3333) has accumulated it
// calls again with that immediate forced to 0, and if the two runs' position
// floats differ by more than (0.05, 0.05, 0.05, 0.5, 0.5, 0.5) it restores the
// object as it was before the second call and resets the accumulator; otherwise
// it answers the second call's flag.
//
// The `ucomisd`/`lahf`/`test ah,44h`/`jnp` sequences are the original's own
// codegen for its `!= 0` tests: `mov al,1` when the comparison is non-zero or
// unordered. `rep movsd` is one instruction (F3 A5) on both sides.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CutsceneChainBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0A18h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, ecx
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        push edi
        mov DWORD PTR [ebp - 0A10h], esi
        call CutsceneResolveEntry
        lea edi, [ebp - 504h]
        mov ecx, DWORD PTR [eax + 0Eh]
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, 140h
        rep movsd
        movss xmm0, DWORD PTR [eax + 8]
        lea eax, [ebp - 0A08h]
        mov esi, DWORD PTR [zpatch_cutscene_poke]
        mov edi, DWORD PTR [zpatch_cutscene_virtual_protect]
        dec esi
        cvtps2pd xmm0, xmm0
        push eax
        push 40h
        push 1
        push esi
        addsd xmm0, QWORD PTR [zpatch_cutscene_accum]
        movsd QWORD PTR [zpatch_cutscene_accum], xmm0
        call edi
        lea eax, [ebp - 0A08h]
        mov BYTE PTR [esi], 1
        push eax
        push DWORD PTR [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, DWORD PTR [zpatch_cutscene_target]
        movss xmm0, DWORD PTR [ebp + 8]
        push ecx
        mov ecx, DWORD PTR [ebp - 0A10h]
        mov DWORD PTR [ebp - 0A14h], eax
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [ebp - 0A14h]
        fstp QWORD PTR [ebp - 0A18h]
        movsd xmm0, QWORD PTR [ebp - 0A18h]
        ucomisd xmm0, QWORD PTR [zpatch_cutscene_zero]
        lahf
        test ah, 44h
        jnp ccb_al_zero
        mov al, 1
        jmp ccb_al_done
    ccb_al_zero:
        xor al, al
    ccb_al_done:
        mov edx, DWORD PTR [ebp - 0A10h]
        lea edi, [ebp - 0A04h]
        movsd xmm0, QWORD PTR [zpatch_cutscene_limit]
        mov ecx, 140h
        comisd xmm0, QWORD PTR [zpatch_cutscene_accum]
        mov esi, edx
        mov BYTE PTR [ebp - 0A09h], al
        rep movsd
        ja ccb_return_al
        mov edi, edx
        lea eax, [ebp - 0A08h]
        push eax
        mov ecx, 140h
        lea esi, [ebp - 504h]
        rep movsd
        mov esi, DWORD PTR [zpatch_cutscene_poke]
        mov edi, DWORD PTR [zpatch_cutscene_virtual_protect]
        dec esi
        push 40h
        push 1
        push esi
        call edi
        lea eax, [ebp - 0A08h]
        mov BYTE PTR [esi], 0
        push eax
        push DWORD PTR [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, DWORD PTR [zpatch_cutscene_target]
        movss xmm0, DWORD PTR [ebp + 8]
        push ecx
        mov ecx, DWORD PTR [ebp - 0A10h]
        mov DWORD PTR [ebp - 0A14h], eax
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [ebp - 0A14h]
        fstp QWORD PTR [ebp - 0A18h]
        movsd xmm0, QWORD PTR [ebp - 0A18h]
        xorps xmm3, xmm3
        ucomisd xmm0, xmm3
        lahf
        test ah, 44h
        jnp ccb_dl_zero
        mov dl, 1
        jmp ccb_dl_done
    ccb_dl_zero:
        xor dl, dl
    ccb_dl_done:
        mov eax, DWORD PTR [ebp - 0A10h]
        lea edi, [ebp - 504h]
        movss xmm1, DWORD PTR [zpatch_cutscene_abs_mask]
        mov ecx, 140h
        movss xmm2, DWORD PTR [zpatch_cutscene_eps_a]
        mov esi, eax
        rep movsd
        movss xmm0, DWORD PTR [ebp - 4E4h]
        subss xmm0, DWORD PTR [ebp - 9E4h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4E0h]
        subss xmm0, DWORD PTR [ebp - 9E0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4DCh]
        subss xmm0, DWORD PTR [ebp - 9DCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4C4h]
        subss xmm0, DWORD PTR [ebp - 9C4h]
        movss xmm2, DWORD PTR [zpatch_cutscene_eps_b]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4C0h]
        subss xmm0, DWORD PTR [ebp - 9C0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4BCh]
        subss xmm0, DWORD PTR [ebp - 9BCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        pop edi
        mov al, dl
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret 4
    ccb_restore:
        mov edi, eax
        movsd QWORD PTR [zpatch_cutscene_accum], xmm3
        mov al, BYTE PTR [ebp - 0A09h]
        lea esi, [ebp - 0A04h]
        mov ecx, 140h
        rep movsd
    ccb_return_al:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret 4
        __emit 0CCh
    }
}
