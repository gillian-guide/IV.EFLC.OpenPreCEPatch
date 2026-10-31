// SPDX-License-Identifier: GPL-3.0-or-later
//
// loading
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "game.hxx"
#include "hooks.h"
#include "patches.hxx"
#include "rebased.h"
#include "feature_registry.hxx"
#include "helpers.hxx"
#include "opcodes.hxx"

export module opp.loading;

import opp.common;
import opp.settings;

namespace opp::features
{

    // publish LoadDLCs

    void PublishLoadDLCs(PatchContext& ctx)
    {

        TraceArm("publish LoadDLCs");

        // LoadDLCs. The loader callback the struct-fixup body calls sits 0x8C
        // bytes past the fixup site on every gated build (0x4144D4+0x8C on
        // 1.0.6.0, 0x414274+0x8C on 1.0.7.0, 0x4903D4+0x8C on 1.0.8.0), so the
        // publish scans the same instruction window the install does instead of
        // carrying three RVAs.
        //
        // The two helpers the discovery loop calls (DlcRegisterRva/DlcApplyRva)
        // are separate functions: each per-build RVA below comes from that
        // function's own unique 24/16-byte window, proved with
        // tools/opcep_sigcheck.py (tests/parity/address_sites.tsv).
        if (ctx.Option("LoadDLCs", "1"))
        {
            // Distance from the struct-fixup site to the loader callback entry
            // (1.0.6.0 0x4144D4->0x414560, 1.0.7.0 0x414274->0x414300,
            // 1.0.8.0 0x4903D4->0x490460: the same 0x8C on every gated build).
            constexpr uintptr_t kDlcCallbackDelta = 0x8C;
            const GameImage& image = ctx.image;

            // The two helpers the discovery loop calls are entries too, so the
            // publish scans them (unique 24/16-byte windows, proved with
            // tools/opcep_sigcheck.py) and stores the RVA the body rebases.
            const auto AsRva = [&ctx](const void* address) {
                return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address) -
                                             ctx.gameBase);
            };

            const auto publishCallback = [&ctx](hook::pattern& pattern) {
                if (pattern.size() == 0)
                    return;
                zpatch_dlc_load_callback = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(pattern.get(0).get<uint8_t>()) +
                    kDlcCallbackDelta);
            };

            if (ctx.build == GameBuild::v1060)
            {
                if (auto pattern = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8"); pattern.size() > 0)
                    publishCallback(pattern);
                if (auto pattern = Pattern(image, "81 EC 0C 02 00 00 A1 40 2F F4 00 33 C4 89 84 24 08 02 00 00 53 55 56 57"); pattern.size() > 0)
                    DlcRegisterRva() = AsRva(pattern.get(0).get<uint8_t>());
                if (auto pattern = Pattern(image, "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"); pattern.size() > 0)
                    DlcApplyRva() = AsRva(pattern.get(0).get<uint8_t>());
            }

            if (ctx.build == GameBuild::v1070)
            {
                if (auto pattern = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E 5B"); pattern.size() > 0)
                    publishCallback(pattern);
                if (auto pattern = Pattern(image, "81 EC 0C 02 00 00 A1 40 3F F4 00 33 C4 89 84 24 08 02 00 00 53 55 56 57"); pattern.size() > 0)
                    DlcRegisterRva() = AsRva(pattern.get(0).get<uint8_t>());
                if (auto pattern = Pattern(image, "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"); pattern.size() > 0)
                    DlcApplyRva() = AsRva(pattern.get(0).get<uint8_t>());
            }

            if (ctx.build == GameBuild::v1080)
            {
                if (auto pattern = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6"); pattern.size() > 0)
                    publishCallback(pattern);
                if (auto pattern = Pattern(image, "81 EC 0C 02 00 00 A1 40 2C F6 00 33 C4 89 84 24 08 02 00 00 53 55 56 8B"); pattern.size() > 0)
                    DlcRegisterRva() = AsRva(pattern.get(0).get<uint8_t>());
                if (auto pattern = Pattern(image, "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"); pattern.size() > 0)
                    DlcApplyRva() = AsRva(pattern.get(0).get<uint8_t>());
            }
        }

    }


    // LoadDLCs

    void InstallLoadDLCs(PatchContext& ctx)
    {

        TraceArm("LoadDLCs");

        // LoadDLCs: 6 sites per build, each the pre-write instruction window at
        // the recovered RVA. The three destination deltas are recovered facts and
        // are the same on both 1.0.6.0/1.0.7.0 (jump +0x5C, jump +0x1E, call
        // -0x70A); 1.0.8.0's helper call is at -0x88A. Keeping them relative to
        // the match is what lets the sites be scans.
        //
        // Order: the call site is written before the twelve-byte NOP after it,
        // because the call's window contains the NOP'd bytes. Later blocks keep
        // the original statement order for the same reason.
        if (ctx.Option("LoadDLCs", "1"))
        {
            const GameImage& image = ctx.image;

            // Destinations as deltas from the site: +0x5C jumps into the same
            // block, +0x1E skips the fixup (identical on 1.0.6.0/1.0.7.0), and
            // the helper call is -0x70A on those builds / -0x88A on 1.0.8.0.
            constexpr uintptr_t kJumpIntoSameBlock = 0x5C;
            constexpr uintptr_t kJumpOverFixup = 0x1E;
            constexpr uintptr_t kHelperCallBeforeSite = 0x70A;
            constexpr uintptr_t kHelperCallBeforeSite1080 = 0x88A;

            const auto findSite = [&image](const char* signature) -> uint8_t* {
                auto pattern = Pattern(image, signature);
                return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
            };

            // The two jumps and the trailing NOP share their shape on 1.0.6.0
            // and 1.0.7.0, so the block is written once per build with the
            // build's own windows.
            if (ctx.build == GameBuild::v1060 || ctx.build == GameBuild::v1070)
            {
                const bool first = ctx.build == GameBuild::v1060;

                uint8_t* const dummyCall = findSite(
                    "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52");
                uint8_t* const earlyJump = findSite(
                    "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24");
                uint8_t* const structFixup = findSite(
                    first ? "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8"
                          : "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E 5B");
                uint8_t* const skipFixupJump = findSite(
                    "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E");
                uint8_t* const helperCall = findSite(
                    "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E");
                uint8_t* const tail = findSite(
                    "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC");

                if (dummyCall != nullptr)
                    CallToBody(ctx, dummyCall, "abi_load_dlcs_dummy");
                if (earlyJump != nullptr)
                    JumpTo(ctx, earlyJump, earlyJump + kJumpIntoSameBlock);
                if (structFixup != nullptr)
                    JumpToBody(ctx, structFixup, "abi_load_dlcs_struct_fixup");
                if (skipFixupJump != nullptr)
                    JumpTo(ctx, skipFixupJump, skipFixupJump + kJumpOverFixup);
                if (helperCall != nullptr)
                    CallTo(ctx, helperCall, helperCall - kHelperCallBeforeSite);
                if (tail != nullptr)
                    NopAt(ctx, tail, 12);
            }

            if (ctx.build == GameBuild::v1080)
            {
                uint8_t* const dummyCall = findSite(
                    "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 4C 24 ?? 51 8D 94 24 ?? ?? ?? ?? 52 E8");
                uint8_t* const earlyJump = findSite(
                    "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24");
                uint8_t* const structFixup = findSite(
                    "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6");
                uint8_t* const skipFixupJump = findSite(
                    "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E");
                uint8_t* const helperCall = findSite(
                    "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E");
                uint8_t* const tail = findSite(
                    "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC");

                if (dummyCall != nullptr)
                    CallToBody(ctx, dummyCall, "abi_load_dlcs_dummy");
                if (earlyJump != nullptr)
                    JumpTo(ctx, earlyJump, earlyJump + kJumpIntoSameBlock);
                if (structFixup != nullptr)
                    JumpToBody(ctx, structFixup, "abi_load_dlcs_struct_fixup");
                if (skipFixupJump != nullptr)
                    JumpTo(ctx, skipFixupJump, skipFixupJump + kJumpOverFixup);
                if (helperCall != nullptr)
                    CallTo(ctx, helperCall, helperCall - kHelperCallBeforeSite1080);
                if (tail != nullptr)
                    NopAt(ctx, tail, 12);
            }
        }

    }


    // publish NewFilesCompatibility

    void PublishNewFilesCompatibility(PatchContext& ctx)
    {

        TraceArm("publish NewFilesCompatibility");

        // NewFilesCompatibility: 1 slot
        if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
            {
                zpatch_newfiles_register_slot = MatchTarget(pattern, 6, 10);
            }
        }

    }


    // NewFilesCompatibility

    void InstallNewFilesCompatibility(PatchContext& ctx)
    {

        TraceArm("NewFilesCompatibility");

        // NewFilesCompatibility: 2 siginstalls, 1 sigjump
        if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match, "abi_newfiles_update_a");
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match, "abi_newfiles_update_b");
            }

            if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 28, "abi_newfiles_register_body");
            }
        }

    }


    // publish NewSavesCompatibility

    void PublishNewSavesCompatibility(PatchContext& ctx)
    {

        TraceArm("publish NewSavesCompatibility");

        // NewSavesCompatibility: 3 slots. The newer save format is 1060-1080
        // only; gate the arm instead of relying on the pattern coincidentally
        // missing on 1000/1004/1040 and EFLC.
        if (ctx.Option("NewSavesCompatibility", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080}) &&
            Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08"); pattern.size() > 0)
            {
                zpatch_new_saves_src = *pattern.get(0).get<uint32_t>(1);
            }

            if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
            {
                zpatch_new_saves_resume = MatchAddress(pattern, 13);
            }
        }

    }


    // NewSavesCompatibility

    void InstallNewSavesCompatibility(PatchContext& ctx)
    {

        TraceArm("NewSavesCompatibility");

        // NewSavesCompatibility: 1 siginstall, 1 sigjump
        if (ctx.Option("NewSavesCompatibility", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080}) &&
            Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_new_saves_compat");
            }
        }

    }


    // OverrideGTADatPath

    void InstallOverrideGTADatPath(PatchContext& ctx)
    {

        TraceArm("OverrideGTADatPath");

        // OverrideGTADatPath: 1 literalstore
        if (ctx.Option("OverrideGTADatPath", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint32_t>(ctx, match + 1, static_cast<uint32_t>(FindGlobalValue("OverrideGTADatPath")));
            }
        }

    }


    // SCOSignatureFix

    void InstallSCOSignatureFix(PatchContext& ctx)
    {

        TraceArm("SCOSignatureFix");

        // SCOSignatureFix: 1 literal
        if (ctx.Option("SCOSignatureFix", "1"))
        {
            const GameImage& image = ctx.image;

            // The recovered op row anchors the write at offset 0 of its own
            // signature. The old arm scanned a signature 32 bytes downstream
            // and wrote at match-32, which is only correct while that distance
            // holds; a build where it does not would flip an unrelated `jz`.
            // This signature ends on the `jz` itself (`mov ds:X,edi; test
            // ebp,ebp; jz`), unique on 1.0.8.0, so the site is match+8.
            if (auto pattern = Pattern(image, "89 3D ? ? ? ? 85 ED 74 1E"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 8, opp::ops::kJmpRel8);
            }
        }

    }

namespace
{
    class LoadingFeatures
    {
    public:
        LoadingFeatures()
        {
            RegisterFeature({Phase::publish, "PublishLoadDLCs", &PublishLoadDLCs});
            RegisterFeature({Phase::publish, "PublishNewFilesCompatibility", &PublishNewFilesCompatibility});
            RegisterFeature({Phase::publish, "PublishNewSavesCompatibility", &PublishNewSavesCompatibility});
            RegisterFeature({Phase::install, "InstallLoadDLCs", &InstallLoadDLCs, {"PublishLoadDLCs"}});
            RegisterFeature({Phase::install, "InstallNewFilesCompatibility", &InstallNewFilesCompatibility, {"PublishNewFilesCompatibility"}});
            RegisterFeature({Phase::install, "InstallNewSavesCompatibility", &InstallNewSavesCompatibility, {"PublishNewSavesCompatibility"}});
            RegisterFeature({Phase::install, "InstallSCOSignatureFix", &InstallSCOSignatureFix});
            RegisterFeature({Phase::install, "InstallOverrideGTADatPath", &InstallOverrideGTADatPath});
        }
    } loadingFeatures;
} // namespace

} // namespace opp::features
