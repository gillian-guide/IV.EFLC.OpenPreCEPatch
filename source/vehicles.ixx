// SPDX-License-Identifier: GPL-3.0-or-later
//
// vehicles
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

export module opp.vehicles;

import opp.common;
import opp.settings;

namespace opp::features
{

    // BikeFeetFix

    void InstallBikeFeetFix(PatchContext& ctx)
    {

        TraceArm("BikeFeetFix");

        // BikeFeetFix: 1 literal
        if (ctx.Option("BikeFeetFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "39 82 54 13 00 00 0F 85 94 04 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 12, 12);
            }
        }

    }


    // BikePhoneAnimsFix

    void InstallBikePhoneAnimsFix(PatchContext& ctx)
    {

        TraceArm("BikePhoneAnimsFix");

        // BikePhoneAnimsFix: 1 literal
        if (ctx.Option("BikePhoneAnimsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 33, 6);
            }
        }

    }


    // publish CopSirensFlag

    void PublishCopSirensFlag(PatchContext& ctx)
    {

        TraceArm("publish CopSirensFlag");

        // CopSirensFlag: 3 slots
        if (ctx.Option("CopSirensFlag", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
            {
                zpatch_copsirens_resume = MatchAddress(pattern, 5);
            }

            if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
            {
                zpatch_copsirens_resume_b = MatchAddress(pattern, 28);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }
        }

    }


    // CopSirensFlag

    void InstallCopSirensFlag(PatchContext& ctx)
    {

        TraceArm("CopSirensFlag");

        // CopSirensFlag: 3 siginstalls, 2 sigjumps
        if (ctx.Option("CopSirensFlag", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_copsirens_a");
            }

            if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 22, "abi_copsirens_b");
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 10, "abi_copsirens_ide");
            }
        }

    }


    // EpisodicVehicleSupport

    void InstallEpisodicVehicleSupport(PatchContext& ctx)
    {

        TraceArm("EpisodicVehicleSupport");

        // EpisodicVehicleSupport: 23 literals, 4 sigwrites. The four pointer
        // stores point at the episode flag; each site was a per-build RVA
        // (0x021500/0x7D91AE on 1.0.7.0, 0x095040/0x684ABE on 1.0.8.0) and is
        // now the pre-store instruction sequence at the site. The build gates
        // stay: each signature also resolves on other builds, where the original
        // did not install this option.
        if (ctx.Option("EpisodicVehicleSupport", "1"))
        {
            const GameImage& image = ctx.image;

            const auto storeEpisodeFlag = [&ctx](hook::pattern& pattern) {
                if (pattern.size() > 0)
                {
                    WriteValue<uint32_t>(ctx, pattern.get(0).get<uint8_t>(),
                                         AddressOf(&zpatch_episode_flag));
                }
            };

            if (ctx.build == GameBuild::v1070)
            {
                if (auto pattern = Pattern(image, "44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
                if (auto pattern = Pattern(image, "44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
            }

            if (ctx.build == GameBuild::v1080)
            {
                if (auto pattern = Pattern(image, "54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
                if (auto pattern = Pattern(image, "54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
                    storeEpisodeFlag(pattern);
            }

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                {
                    if (auto pattern = PatternAll(image, "7C 18 0F BF 4E 2E"); pattern.size() > 0)
                    {
                        NopAt(ctx, pattern.get(0).get<uint8_t>() - 6, 8);
                        if (pattern.size() > 1)
                            NopAt(ctx, pattern.get(1).get<uint8_t>() - 6, 8);
                    }
                }

                {
                    if (auto pattern = PatternAll(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08"); pattern.size() > 0)
                    {
                        NopAt(ctx, pattern.get(0).get<uint8_t>(), 9);
                        if (pattern.size() > 1)
                            NopAt(ctx, pattern.get(1).get<uint8_t>(), 9);
                    }
                }

                if (auto pattern = Pattern(image, "39 35 ? ? ? ? 75 0E 80 3D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 6, 2);
                }

                if (auto pattern = Pattern(image, "74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 2, 9);
                }

                if (auto pattern = Pattern(image, "7C 22 83 F8 28 75 1D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C 0E 05 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 08 3B 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 1F 8B 44 24 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 35 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 38 8B 16"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 11 8B 4E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 1F 8B"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 35 0F BF 43 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 9);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 43"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 89 44 24 38"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 11, 6);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 8A 0D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 7);
                    NopAt(ctx, match + 17, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 21, 2);
                }

                if (auto pattern = Pattern(image, "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match - 15, 7);
                    NopAt(ctx, match + 10, 2);
                }
            }
        }

    }


    // EpisodicWeaponSupport

    void InstallEpisodicWeaponSupport(PatchContext& ctx)
    {

        TraceArm("EpisodicWeaponSupport");

        // EpisodicWeaponSupport: 8 literals
        if (ctx.Option("EpisodicWeaponSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "75 5B 83 3D ? ? ? ? 00 75"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 85 8C 01 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 6);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C D5 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match, 13);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 20 8B CD E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 50"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 32 8B 41"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }
            }
        }

    }


    // publish HighFPSBikePhysicsFix

    void PublishHighFPSBikePhysicsFix(PatchContext& ctx)
    {

        TraceArm("publish HighFPSBikePhysicsFix");

        // HighFPSBikePhysicsFix: 1 slot
        if (ctx.Option("HighFPSBikePhysicsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
            {
                zpatch_bike_resume = MatchAddress(pattern, 9);
            }
        }

    }


    // HighFPSBikePhysicsFix

    void InstallHighFPSBikePhysicsFix(PatchContext& ctx)
    {

        TraceArm("HighFPSBikePhysicsFix");

        // HighFPSBikePhysicsFix: 1 sigjump
        if (ctx.Option("HighFPSBikePhysicsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_bike_physics_detour");
            }
        }

    }


    // publish MovingPlaneParts

    void PublishMovingPlaneParts(PatchContext& ctx)
    {

        TraceArm("publish MovingPlaneParts");

        // MovingPlaneParts: 5 slots
        if (ctx.Option("MovingPlaneParts", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }
        }

    }


    // MovingPlaneParts

    void InstallMovingPlaneParts(PatchContext& ctx)
    {

        TraceArm("MovingPlaneParts");

        // MovingPlaneParts: 3 siginstalls
        if (ctx.Option("MovingPlaneParts", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_eax");
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_edx");
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 10, "abi_copsirens_ide");
            }
        }

    }


    // publish MovingSteeringWheelSupport

    void PublishMovingSteeringWheelSupport(PatchContext& ctx)
    {

        TraceArm("publish MovingSteeringWheelSupport");

        // MovingSteeringWheelSupport: 5 slots
        if (ctx.Option("MovingSteeringWheelSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }
        }

    }


    // MovingSteeringWheelSupport

    void InstallMovingSteeringWheelSupport(PatchContext& ctx)
    {

        TraceArm("MovingSteeringWheelSupport");

        // MovingSteeringWheelSupport: 3 siginstalls
        if (ctx.Option("MovingSteeringWheelSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_eax");
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_edx");
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 10, "abi_copsirens_ide");
            }
        }

    }


    // publish NoLiveryLimit

    void PublishNoLiveryLimit(PatchContext& ctx)
    {

        TraceArm("publish NoLiveryLimit");

        // NoLiveryLimit: 5 slots
        if (ctx.Option("NoLiveryLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
            {
                zpatch_nolivery_fn = MatchTarget(pattern, -6, -2);
                zpatch_nolivery_resume_d = MatchAddress(pattern, 90);
            }

            if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
            {
                zpatch_nolivery_resume = MatchAddress(pattern, 13);
            }

            if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
            {
                zpatch_nolivery_fn = MatchTarget(pattern, -8, -4);
                zpatch_nolivery_resume_d = MatchAddress(pattern, 84);
            }
        }

    }


    // NoLiveryLimit

    void InstallNoLiveryLimit(PatchContext& ctx)
    {

        TraceArm("NoLiveryLimit");

        // NoLiveryLimit: 1 siginstall, 3 sigjumps
        if (ctx.Option("NoLiveryLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 237, "abi_nolivery_b");
                JumpToBody(ctx, match - 199, "abi_nolivery_c");
                JumpToBody(ctx, match, "abi_nolivery_a");
            }

            if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_nolivery_d");
            }

            if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_nolivery_e");
            }
        }

    }


    // PoliceEpisodicWeaponSupport

    void InstallPoliceEpisodicWeaponSupport(PatchContext& ctx)
    {

        TraceArm("PoliceEpisodicWeaponSupport");

        // PoliceEpisodicWeaponSupport: 1 literal
        if (ctx.Option("PoliceEpisodicWeaponSupport", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "39 2D ? ? ? ? 75 3E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 6, 2);
                }
            }
        }

    }


    // publish PopupHeadlightsFlag

    void PublishPopupHeadlightsFlag(PatchContext& ctx)
    {

        TraceArm("publish PopupHeadlightsFlag");

        // PopupHeadlightsFlag: 5 slots
        if (ctx.Option("PopupHeadlightsFlag", "1"))
        {
            const GameImage& image = ctx.image;

            // The original handler (0x10044590) is `if (pattern A) { ... }
            // else { pattern B ... }`: exactly ONE of the two EAX/EDX sites is
            // ever patched, and the two resume slots belong to that site. Two
            // independent `if`s would install both bodies against whichever
            // pattern matched last, so the first body would tail-jump to the
            // second site's resume.
            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                zpatch_popup_resume_b = MatchAddress(pattern, 10);
                zpatch_popup_resume_a = MatchAddress(pattern, 12);
            }
            else if (auto patternB = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); patternB.size() > 0)
            {
                zpatch_popup_resume_b = MatchAddress(patternB, 10);
                zpatch_popup_resume_a = MatchAddress(patternB, 12);
            }

            // The IDE parser hook is independent of the two model tests and is
            // shared with the other arms that install it.
            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }
        }

    }


    // PopupHeadlightsFlag

    void InstallPopupHeadlightsFlag(PatchContext& ctx)
    {

        TraceArm("PopupHeadlightsFlag");

        // PopupHeadlightsFlag: 3 siginstalls, 1 sigjump
        if (ctx.Option("PopupHeadlightsFlag", "1"))
        {
            const GameImage& image = ctx.image;

            // if / else, like the original: see the publish arm above.
            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_popup_headlights");
                WriteValue<uint8_t>(ctx, match + 10, opp::ops::kJmpRel8);
                WriteValue<uint32_t>(ctx, match + 82, AddressOf(&zpatch_popup_angle));
            }
            else if (auto patternB = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); patternB.size() > 0)
            {
                auto* match = patternB.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_popup_headlights_edx");
                WriteValue<uint8_t>(ctx, match + 10, opp::ops::kJmpRel8);
                WriteValue<uint32_t>(ctx, match + 82, AddressOf(&zpatch_popup_angle));
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 10, "abi_copsirens_ide");
            }
        }

    }


    // publish ShakingEngineBlock

    void PublishShakingEngineBlock(PatchContext& ctx)
    {

        TraceArm("publish ShakingEngineBlock");

        // ShakingEngineBlock: 5 slots
        if (ctx.Option("ShakingEngineBlock", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                zpatch_steer_resume = MatchAddress(pattern, 16);
                zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }
        }

    }


    // ShakingEngineBlock

    void InstallShakingEngineBlock(PatchContext& ctx)
    {

        TraceArm("ShakingEngineBlock");

        // ShakingEngineBlock: 3 siginstalls
        if (ctx.Option("ShakingEngineBlock", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_eax");
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 2, "abi_steer_detour_edx");
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 10, "abi_copsirens_ide");
            }
        }

    }


    // TBoGTHeliHeightLimit

    void InstallTBoGTHeliHeightLimit(PatchContext& ctx)
    {

        TraceArm("TBoGTHeliHeightLimit");

        // TBoGTHeliHeightLimit: 1 literal
        if (ctx.Option("TBoGTHeliHeightLimit", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 0A F3 0F"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match + 7, 2);
                }
            }
        }

    }

namespace
{
    class VehiclesFeatures
    {
    public:
        VehiclesFeatures()
        {
            RegisterFeature({Phase::publish, "PublishCopSirensFlag", &PublishCopSirensFlag});
            RegisterFeature({Phase::publish, "PublishHighFPSBikePhysicsFix", &PublishHighFPSBikePhysicsFix});
            RegisterFeature({Phase::publish, "PublishNoLiveryLimit", &PublishNoLiveryLimit});
            RegisterFeature({Phase::publish, "PublishPopupHeadlightsFlag", &PublishPopupHeadlightsFlag});
            RegisterFeature({Phase::publish, "PublishMovingSteeringWheelSupport", &PublishMovingSteeringWheelSupport});
            RegisterFeature({Phase::publish, "PublishShakingEngineBlock", &PublishShakingEngineBlock});
            RegisterFeature({Phase::publish, "PublishMovingPlaneParts", &PublishMovingPlaneParts});
            RegisterFeature({Phase::install, "InstallBikeFeetFix", &InstallBikeFeetFix});
            RegisterFeature({Phase::install, "InstallBikePhoneAnimsFix", &InstallBikePhoneAnimsFix});
            RegisterFeature({Phase::install, "InstallCopSirensFlag", &InstallCopSirensFlag, {"PublishCopSirensFlag"}});
            RegisterFeature({Phase::install, "InstallEpisodicVehicleSupport", &InstallEpisodicVehicleSupport});
            RegisterFeature({Phase::install, "InstallEpisodicWeaponSupport", &InstallEpisodicWeaponSupport});
            RegisterFeature({Phase::install, "InstallHighFPSBikePhysicsFix", &InstallHighFPSBikePhysicsFix, {"PublishHighFPSBikePhysicsFix"}});
            RegisterFeature({Phase::install, "InstallNoLiveryLimit", &InstallNoLiveryLimit, {"PublishNoLiveryLimit"}});
            RegisterFeature({Phase::install, "InstallPoliceEpisodicWeaponSupport", &InstallPoliceEpisodicWeaponSupport});
            RegisterFeature({Phase::install, "InstallPopupHeadlightsFlag", &InstallPopupHeadlightsFlag, {"PublishPopupHeadlightsFlag"}});
            RegisterFeature({Phase::install, "InstallTBoGTHeliHeightLimit", &InstallTBoGTHeliHeightLimit});
            RegisterFeature({Phase::install, "InstallMovingSteeringWheelSupport", &InstallMovingSteeringWheelSupport, {"PublishMovingSteeringWheelSupport"}});
            RegisterFeature({Phase::install, "InstallShakingEngineBlock", &InstallShakingEngineBlock, {"PublishShakingEngineBlock"}});
            RegisterFeature({Phase::install, "InstallMovingPlaneParts", &InstallMovingPlaneParts, {"PublishMovingPlaneParts"}});
        }
    } vehiclesFeatures;
} // namespace

} // namespace opp::features
