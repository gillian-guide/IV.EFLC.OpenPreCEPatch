// SPDX-License-Identifier: GPL-3.0-or-later
//
// graphics
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

export module graphics;

import common;
import settings;

namespace opp::features
{

    // BuildingAlphaFix

    void InstallBuildingAlphaFix(PatchContext& ctx)
    {

        TraceArm("BuildingAlphaFix");

        // BuildingAlphaFix: 1 literal, 1 siginstall. Patch 6+ only (the LOD
        // fade-in path it fixes does not exist before 1060).
        if (ctx.Option("BuildingAlphaFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                GameBuild::v1120, GameBuild::v1130, GameBuild::v12043}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 84"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                if (InImage(image, reinterpret_cast<uintptr_t>(match + 7)))
                    JumpTo(ctx, match + 7, reinterpret_cast<void*>(BranchTarget(match, 7)));
            }

            if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 85"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match, 13);
            }
        }

    }


    // publish BuildingDynamicShadows

    void PublishBuildingDynamicShadows(PatchContext& ctx)
    {

        TraceArm("publish BuildingDynamicShadows");

        // BuildingDynamicShadows: 4 slots
        if (ctx.Option("BuildingDynamicShadows", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
            {
                zpatch_bds_resume_a = MatchAddress(pattern, 15);
                zpatch_bds_resume_b = MatchAddress(pattern, 112);
            }

            // The two hash slots take the same field of the FIRST and the SECOND
            // match (recovered rows: dword_10159C1C index 0, dword_10159C24 index 1).
            // The two matches hold different pointers (0xD8FA2C / 0xDCF48C on 1080),
            // so publishing both from the first is not the original's state.
            auto hashes = PatternAll(
                image, "8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
            if (hashes.size() > 0)
                zpatch_bds_hash_a = *hashes.get(0).get<uint32_t>(20);
            if (hashes.size() > 1)
                zpatch_bds_hash_b = *hashes.get(1).get<uint32_t>(20);
        }

    }


    // BuildingDynamicShadows

    void InstallBuildingDynamicShadows(PatchContext& ctx)
    {

        TraceArm("BuildingDynamicShadows");

        // BuildingDynamicShadows: 1 sigjump, 1 sigwrite
        if (ctx.Option("BuildingDynamicShadows", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(2), 0x00u);
                }
            }

            if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_building_dynamic_shadows");
            }
        }

    }


    // CarDecalsFix

    void InstallCarDecalsFix(PatchContext& ctx)
    {

        TraceArm("CarDecalsFix");

        // CarDecalsFix: 1 siginstall
        if (ctx.Option("CarDecalsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_car_decals_fix");
            }
        }

    }


    // publish CarDynamicShadowFix

    void PublishCarDynamicShadowFix(PatchContext& ctx)
    {

        TraceArm("publish CarDynamicShadowFix");

        // CarDynamicShadowFix: 5 slots
        if (ctx.Option("CarDynamicShadowFix", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
            {
                zpatch_carparts_resume_2 = MatchAddress(pattern, 11);
                zpatch_carparts_shadow_fn = MatchTarget(pattern, 7, 11);
            }

            if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
            {
                zpatch_carparts_resume_2 = MatchAddress(pattern, 13);
                zpatch_carparts_shadow_fn = MatchTarget(pattern, 9, 13);
            }

            if (auto pattern = Pattern(image, "8B 45 10 8B 16 8B 92 8C 00 00 00"); pattern.size() > 0)
            {
                zpatch_car_dynamic_d = MatchAddress(pattern);
            }
        }

    }


    // CarDynamicShadowFix

    void InstallCarDynamicShadowFix(PatchContext& ctx)
    {

        TraceArm("CarDynamicShadowFix");

        // CarDynamicShadowFix: 3 siginstalls, 2 sigwrites
        if (ctx.Option("CarDynamicShadowFix", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_carparts_shadow_2");
            }

            if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_carparts_shadow_2b");
            }

            if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 3, reinterpret_cast<void*>(match + 30));
            }

            {
                if (auto pattern = Pattern(image, "83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(6), 0xEBu);
                }
            }

            {
                if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(2), 0x00u);
                }
            }

            // Two further writes the original makes under this arm; they are in its
            // write log for a CarDynamicShadowFix-only cell but not in the recovered
            // row tables, so the reconstruction does not make them either. They are
            // the arm's @noFCHS variant: with ForceCarHeadlightShadows on, the
            // original takes the other path and makes neither write (probed:
            // CarDynamic+FCHS writes only 0x8F7422; CarDynamic alone also writes
            // 0x8F73EC and 0x8F719F).
            //
            //  * `0F 84 <rel32>` (jz) becomes `E9 <rel32+1>`: a jmp to the same
            //    target. The displacement grows by one because the jz encoding is one
            //    byte longer than the jmp (match+6+rel == match+5+newRel).
            //  * `push 0x504` becomes `push 0x100` (the shadow size the arm forces),
            //    written as a dword over the immediate like the original does.
            if (!ctx.Option("ForceCarHeadlightShadows", "1"))
            {
                if (auto pattern = Pattern(image, "0F 84 9E 00 00 00 80 7D 28"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    const int32_t rel = static_cast<int32_t>(*pattern.get(0).get<uint32_t>(2)) + 1;
                    uint8_t bytes[5] = {0xE9u};
                    std::memcpy(bytes + 1, &rel, sizeof(rel));
                    WriteRaw(ctx, match, bytes, sizeof(bytes));
                }

                if (auto pattern = Pattern(image, "51 68 04 05 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    WriteValue<uint32_t>(ctx, match + 2, static_cast<uint32_t>(0x100u));
                }
            }
        }

    }


    // CarDynamicShadowFixNoE

    void InstallCarDynamicShadowFixNoE(PatchContext& ctx)
    {

        TraceArm("CarDynamicShadowFixNoE");

        // CarDynamicShadowFix@noE: 1 siginstall
        if (ctx.Option("CarDynamicShadowFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 62, 6);
            }
        }

    }


    // publish CarPartsShadowFix

    void PublishCarPartsShadowFix(PatchContext& ctx)
    {

        TraceArm("publish CarPartsShadowFix");

        // CarPartsShadowFix: 2 slots
        if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
            {
                zpatch_carparts_resume_if = MatchAddress(pattern, 2);
                zpatch_carparts_resume_else = MatchAddress(pattern, 69);
            }
        }

    }


    // CarPartsShadowFix

    void InstallCarPartsShadowFix(PatchContext& ctx)
    {

        TraceArm("CarPartsShadowFix");

        // CarPartsShadowFix: 1 siginstall, 1 sigjump
        if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 6, "abi_carparts_shadow");
            }
        }

    }


    // CarPartsShadowFixStandalone

    void InstallCarPartsShadowFixStandalone(PatchContext& ctx)
    {

        TraceArm("CarPartsShadowFixStandalone");

        // CarPartsShadowFix@standalone: 2 sigwrites
        if (ctx.Option("CarPartsShadowFix", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "83 F8 04 C6 44 24 0E 00 75 0F"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(8), 0xEBu);
                }
            }

            {
                if (auto pattern = Pattern(image, "83 FA 04 75 0C"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(3), 0xEBu);
                }
            }
        }

    }


    // publish CutsceneFixes

    void PublishCutsceneFixes(PatchContext& ctx)
    {

        TraceArm("publish CutsceneFixes");

        // CutsceneFixes: 2 slots
        if (ctx.Option("CutsceneFixes", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
            {
                zpatch_cutscene_target = MatchAddress(pattern) + *pattern.get(0).get<uint32_t>(-4);
                zpatch_cutscene_poke = MatchTarget(pattern, -4, 79);
            }
        }

    }


    // CutsceneFixes

    void InstallCutsceneFixes(PatchContext& ctx)
    {

        TraceArm("CutsceneFixes");

        // CutsceneFixes: 2 siginstalls, 1 sigwrite
        if (ctx.Option("CutsceneFixes", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 5, "abi_cutscene_chain_b");
            }

            if (auto pattern = Pattern(image, "8B CF 88 44 24 0F E8 ? ? ? ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 5, "abi_cutscene_chain_b");
            }

            {
                // 1080 has exactly one match of the primary pattern (0xC9A840); the
                // alternate is only for builds where it is absent. Falling back on
                // "<= 1 match" threw that single match away and applied nothing.
                auto pattern = Pattern(image, "74 24 8B 44 24 2C 83 F8 03");
                if (pattern.size() == 0)
                    pattern = Pattern(image, "74 0B 83 7C 24 2C 03");
                if (pattern.size() > 0)
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(), 0xEBu);
            }
        }

    }


    // publish DualVehicleHeadlights

    void PublishDualVehicleHeadlights(PatchContext& ctx)
    {

        TraceArm("publish DualVehicleHeadlights");

        // DualVehicleHeadlights: 5 slots
        if (ctx.Option("DualVehicleHeadlights", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
            {
                zpatch_dual_bcc = *pattern.get(0).get<uint32_t>(207);
                zpatch_dual_bd4 = MatchTarget(pattern, 331, 335);
                zpatch_dual_bd0 = MatchTarget(pattern, 140, 144);
                zpatch_dual_be4 = MatchTarget(pattern, 151, 155);
                zpatch_dual_be0 = MatchAddress(pattern, 338);
            }
        }

    }


    // DualVehicleHeadlights

    void InstallDualVehicleHeadlights(PatchContext& ctx)
    {

        TraceArm("DualVehicleHeadlights");

        // DualVehicleHeadlights (handler 0x10006770): one E9 at the game match.
        // The original picks the old body for build ids 1..6 and the new body for
        // 7+ (0x10006630 / 0x100064F0); the bodies are not interchangeable. The
        // pre-fix layout registered both bodies ungated: the old arm ran first and
        // overwrote the five bytes the new arm's scan needs, so 1.0.8.0 (build 9)
        // silently got the old body. Keep the build choice explicit and install
        // the site once.
        if (ctx.Option("DualVehicleHeadlights", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();
                const bool old_body = BuildIs(
                    ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004});
                const char* body =
                    old_body ? "abi_dual_headlights_old" : "abi_dual_headlights_new";

                JumpToBody(ctx, match, body);
            }
        }

    }


    // publish EmissiveLerpFix

    void PublishEmissiveLerpFix(PatchContext& ctx)
    {

        TraceArm("publish EmissiveLerpFix");

        // EmissiveLerpFix: 2 slots
        if (ctx.Option("EmissiveLerpFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
            {
                zpatch_emissive_const = MatchAddress(pattern, -16);
                zpatch_emissive_resume = MatchAddress(pattern, -12);
            }
        }

    }


    // EmissiveLerpFix

    void InstallEmissiveLerpFix(PatchContext& ctx)
    {

        TraceArm("EmissiveLerpFix");

        // EmissiveLerpFix: 1 siginstall
        if (ctx.Option("EmissiveLerpFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 24, "abi_emissive_lerp_shim");
            }
        }

    }


    // publish ForceCarHeadlightShadows

    void PublishForceCarHeadlightShadows(PatchContext& ctx)
    {

        TraceArm("publish ForceCarHeadlightShadows");

        // ForceCarHeadlightShadows: 1 slot
        if (ctx.Option("ForceCarHeadlightShadows", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
            {
                zpatch_fchs_resume = MatchAddress(pattern, 12);
            }
        }

    }


    // ForceCarHeadlightShadows

    void InstallForceCarHeadlightShadows(PatchContext& ctx)
    {

        TraceArm("ForceCarHeadlightShadows");

        // ForceCarHeadlightShadows: 2 siginstalls, 5 sigwrites
        if (ctx.Option("ForceCarHeadlightShadows", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "3C 01 75 50 84 DB 75 79"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 6, static_cast<uint8_t>(0xEBu));
                JumpTo(ctx, match + 95, reinterpret_cast<void*>(match + 476));
                WriteValue<uint8_t>(ctx, match + 379, static_cast<uint8_t>(0xEBu));
                WriteValue<uint32_t>(ctx, match + 211, AddressOf(&zpatch_fchs_offset));
            }

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_fchs_shim");
            }

            {
                if (auto pattern = Pattern(image, "8D 4C 24 50 51 68 04 05 00 00"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(6), 0x04u);
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(7), 0x05u);
                }
            }

            {
                if (auto pattern = Pattern(image, "8D 44 24 50 50 68 04 01 00 00"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(6), 0x04u);
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(7), 0x01u);
                }
            }

            {
                if (auto pattern = Pattern(image, "F3 0F 11 44 24 1C 0F 84 9E 00 00 00"); pattern.size() > 0)
                {
                    auto* site = pattern.get(0).get<uint8_t>(6);
                    WriteValue<uint32_t>(ctx, site, 0x009E840Fu); //jz rel32
                    WriteValue<uint8_t>(ctx, site + 4, 0x00u); //jz rel32
                }
            }
        }

    }


    // ForceShadowsOnObjects

    void InstallForceShadowsOnObjects(PatchContext& ctx)
    {

        TraceArm("ForceShadowsOnObjects");

        // ForceShadowsOnObjects: 1 literal
        if (ctx.Option("ForceShadowsOnObjects", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F7 47 40 00 10 00 00 74 07 81 4E 24 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 7, 0xEBu);
            }
        }

    }


    // InfiniteCarBottomDistance

    void InstallInfiniteCarBottomDistance(PatchContext& ctx)
    {

        TraceArm("InfiniteCarBottomDistance");

        // InfiniteCarBottomDistance: 1 sigwrite
        if (ctx.Option("InfiniteCarBottomDistance", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "0F 2F D0 76 25 0F 28 C2"); pattern.size() > 0)
                {
                    WriteValue<uint16_t>(ctx, pattern.get(0).get<uint8_t>(3), 0x2AEBu);
                }
            }
        }

    }


    // NoImpostorHeightCutoff

    void InstallNoImpostorHeightCutoff(PatchContext& ctx)
    {

        TraceArm("NoImpostorHeightCutoff");

        // NoImpostorHeightCutoff: 2 matchliterals
        if (ctx.Option("NoImpostorHeightCutoff", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = PatternAll(image, "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09"); pattern.size() > 0)
                {
                    NopAt(ctx, pattern.get(0).get<uint8_t>(), kWordLength);
                    if (pattern.size() > 1)
                        NopAt(ctx, pattern.get(1).get<uint8_t>(), kWordLength);
                }
            }
        }

    }


    // NoLODLightHeightCutoff

    void InstallNoLODLightHeightCutoff(PatchContext& ctx)
    {

        TraceArm("NoLODLightHeightCutoff");

        // NoLODLightHeightCutoff: 1 literalstore
        if (ctx.Option("NoLODLightHeightCutoff", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match, kWordLength);
            }
        }

    }


    // RemoveStaticCarShadows

    void InstallRemoveStaticCarShadows(PatchContext& ctx)
    {

        TraceArm("RemoveStaticCarShadows");

        // RemoveStaticCarShadows: 1 literal
        if (ctx.Option("RemoveStaticCarShadows", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 11 4C 24 0C 76 5B D9 45 14"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 6, 0xEBu);
            }
        }

    }


    // RemoveStaticCutsceneCarShadows

    void InstallRemoveStaticCutsceneCarShadows(PatchContext& ctx)
    {

        TraceArm("RemoveStaticCutsceneCarShadows");

        // RemoveStaticCutsceneCarShadows: 1 literal
        if (ctx.Option("RemoveStaticCutsceneCarShadows", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 11 44 24 0C 76 5B D9 45 14"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 6, 0xEBu);
            }
        }

    }


    // ReversingLightFix

    void InstallReversingLightFix(PatchContext& ctx)
    {

        TraceArm("ReversingLightFix");

        // ReversingLightFix: 1 literal
        if (ctx.Option("ReversingLightFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 8, 0x60u);
            }
        }

    }

namespace
{
    class GraphicsFeatures
    {
    public:
        GraphicsFeatures()
        {
            RegisterFeature(3, "PublishBuildingDynamicShadows", &PublishBuildingDynamicShadows);
            RegisterFeature(6, "PublishDualVehicleHeadlights", &PublishDualVehicleHeadlights);
            RegisterFeature(7, "PublishEmissiveLerpFix", &PublishEmissiveLerpFix);
            RegisterFeature(32, "PublishCarPartsShadowFix", &PublishCarPartsShadowFix);
            RegisterFeature(33, "PublishForceCarHeadlightShadows", &PublishForceCarHeadlightShadows);
            RegisterFeature(36, "PublishCarDynamicShadowFix", &PublishCarDynamicShadowFix);
            RegisterFeature(44, "PublishCutsceneFixes", &PublishCutsceneFixes);
            RegisterFeature(53, "InstallBuildingAlphaFix", &InstallBuildingAlphaFix);
            RegisterFeature(54, "InstallBuildingDynamicShadows", &InstallBuildingDynamicShadows);
            RegisterFeature(58, "InstallDualVehicleHeadlights", &InstallDualVehicleHeadlights);
            RegisterFeature(59, "InstallEmissiveLerpFix", &InstallEmissiveLerpFix);
            RegisterFeature(64, "InstallForceShadowsOnObjects", &InstallForceShadowsOnObjects);
            RegisterFeature(70, "InstallInfiniteCarBottomDistance", &InstallInfiniteCarBottomDistance);
            RegisterFeature(94, "InstallRemoveStaticCarShadows", &InstallRemoveStaticCarShadows);
            RegisterFeature(95, "InstallRemoveStaticCutsceneCarShadows", &InstallRemoveStaticCutsceneCarShadows);
            RegisterFeature(97, "InstallReversingLightFix", &InstallReversingLightFix);
            RegisterFeature(115, "InstallCarPartsShadowFix", &InstallCarPartsShadowFix);
            RegisterFeature(116, "InstallForceCarHeadlightShadows", &InstallForceCarHeadlightShadows);
            RegisterFeature(119, "InstallCarDynamicShadowFix", &InstallCarDynamicShadowFix);
            RegisterFeature(127, "InstallCutsceneFixes", &InstallCutsceneFixes);
            RegisterFeature(129, "InstallCarDecalsFix", &InstallCarDecalsFix);
            RegisterFeature(131, "InstallCarDynamicShadowFixNoE", &InstallCarDynamicShadowFixNoE);
            RegisterFeature(137, "InstallCarPartsShadowFixStandalone", &InstallCarPartsShadowFixStandalone);
            RegisterFeature(140, "InstallNoImpostorHeightCutoff", &InstallNoImpostorHeightCutoff);
            RegisterFeature(144, "InstallNoLODLightHeightCutoff", &InstallNoLODLightHeightCutoff);
        }
    } graphicsFeatures;
} // namespace

} // namespace opp::features
