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

export module loading;

import common;
import settings;

namespace opp::features
{

    // publish LoadDLCs

    void PublishLoadDLCs(PatchContext& ctx)
    {

        TraceArm("publish LoadDLCs");

        // LoadDLCs: 9 rvas
        if (ctx.Option("LoadDLCs", "1"))
        {
            if (ctx.build == GameBuild::v1060)
            {
                zpatch_dlc_load_callback = static_cast<uint32_t>(ctx.gameBase + 0x414560);
                DlcRegisterRva() = 0x004136E0u;
                DlcApplyRva() = 0x00414080u;
            }

            if (ctx.build == GameBuild::v1070)
            {
                zpatch_dlc_load_callback = static_cast<uint32_t>(ctx.gameBase + 0x414300);
                DlcRegisterRva() = 0x00413480u;
                DlcApplyRva() = 0x00413E20u;
            }

            if (ctx.build == GameBuild::v1080)
            {
                zpatch_dlc_load_callback = static_cast<uint32_t>(ctx.gameBase + 0x490460);
                DlcApplyRva() = 0x0048FE00u;
                DlcRegisterRva() = 0x0048F440u;
            }
        }

    }


    // LoadDLCs

    void InstallLoadDLCs(PatchContext& ctx)
    {

        TraceArm("LoadDLCs");

        // LoadDLCs: 18 rvas, 6 rvajumps
        if (ctx.Option("LoadDLCs", "1"))
        {
            const GameImage& image = ctx.image;

            if (ctx.build == GameBuild::v1060)
            {
                CallToBody(ctx, image.base + 0x4137CD, "abi_load_dlcs_dummy");
                JumpTo(ctx, image.base + 0x41445C, reinterpret_cast<void*>(ctx.gameBase + 0x4144B8));
                JumpToBody(ctx, image.base + 0x4144D4, "abi_load_dlcs_struct_fixup");
                JumpTo(ctx, image.base + 0x41450E, reinterpret_cast<void*>(ctx.gameBase + 0x41452C));
                CallTo(ctx, image.base + 0x41453A, reinterpret_cast<void*>(ctx.gameBase + 0x413E30));
                NopAt(ctx, image.base + 0x41453F, 12);
            }

            if (ctx.build == GameBuild::v1070)
            {
                CallToBody(ctx, image.base + 0x41356D, "abi_load_dlcs_dummy");
                JumpTo(ctx, image.base + 0x4141FC, reinterpret_cast<void*>(ctx.gameBase + 0x414258));
                JumpToBody(ctx, image.base + 0x414274, "abi_load_dlcs_struct_fixup");
                JumpTo(ctx, image.base + 0x4142AE, reinterpret_cast<void*>(ctx.gameBase + 0x4142CC));
                CallTo(ctx, image.base + 0x4142DA, reinterpret_cast<void*>(ctx.gameBase + 0x413BD0));
                NopAt(ctx, image.base + 0x4142DF, 12);
            }

            if (ctx.build == GameBuild::v1080)
            {
                CallToBody(ctx, image.base + 0x48F558, "abi_load_dlcs_dummy");
                JumpTo(ctx, image.base + 0x49035C, reinterpret_cast<void*>(ctx.gameBase + 0x4903B8));
                JumpToBody(ctx, image.base + 0x4903D4, "abi_load_dlcs_struct_fixup");
                JumpTo(ctx, image.base + 0x49040E, reinterpret_cast<void*>(ctx.gameBase + 0x49042C));
                CallTo(ctx, image.base + 0x49043A, reinterpret_cast<void*>(ctx.gameBase + 0x48FBB0));
                NopAt(ctx, image.base + 0x49043F, 12);
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

                WriteValue<uint8_t>(ctx, match + 8, 0xEBu);
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
            RegisterFeature(12, "PublishLoadDLCs", &PublishLoadDLCs);
            RegisterFeature(14, "PublishNewFilesCompatibility", &PublishNewFilesCompatibility);
            RegisterFeature(16, "PublishNewSavesCompatibility", &PublishNewSavesCompatibility);
            RegisterFeature(71, "InstallLoadDLCs", &InstallLoadDLCs);
            RegisterFeature(79, "InstallNewFilesCompatibility", &InstallNewFilesCompatibility);
            RegisterFeature(81, "InstallNewSavesCompatibility", &InstallNewSavesCompatibility);
            RegisterFeature(98, "InstallSCOSignatureFix", &InstallSCOSignatureFix);
            RegisterFeature(146, "InstallOverrideGTADatPath", &InstallOverrideGTADatPath);
        }
    } loadingFeatures;
} // namespace

} // namespace opp::features
