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

export module vehicles;

import common;
import settings;

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

                injector::MakeNOP(match + 12, 12, true); //nop
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

                injector::MakeNOP(match + 33, 6, true); //nop
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

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_copsirens_a")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 22, reinterpret_cast<void*>(Abi("abi_copsirens_b")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 10, reinterpret_cast<void*>(Abi("abi_copsirens_ide")), true); //jmp
            }
        }

    }


    // EpisodicVehicleSupport

    void InstallEpisodicVehicleSupport(PatchContext& ctx)
    {

        TraceArm("EpisodicVehicleSupport");

        // EpisodicVehicleSupport: 23 literals, 4 rvas, 4 sigwrites
        if (ctx.Option("EpisodicVehicleSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (ctx.build == GameBuild::v1070)
            {
                injector::WriteMemory<uint32_t>(image.base + 0x021500, AddressOf(&zpatch_episode_flag), true); //patch
                injector::WriteMemory<uint32_t>(image.base + 0x7D91AE, AddressOf(&zpatch_episode_flag), true); //patch
            }

            if (ctx.build == GameBuild::v1080)
            {
                injector::WriteMemory<uint32_t>(image.base + 0x095040, AddressOf(&zpatch_episode_flag), true); //patch
                injector::WriteMemory<uint32_t>(image.base + 0x684ABE, AddressOf(&zpatch_episode_flag), true); //patch
            }

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                {
                    if (auto pattern = PatternAll(image, "7C 18 0F BF 4E 2E"); pattern.size() > 0)
                    {
                        injector::MakeNOP(pattern.get(0).get<uint8_t>() - 6, 8, true); //nop
                        if (pattern.size() > 1)
                            injector::MakeNOP(pattern.get(1).get<uint8_t>() - 6, 8, true); //nop
                    }
                }

                {
                    if (auto pattern = PatternAll(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08"); pattern.size() > 0)
                    {
                        injector::MakeNOP(pattern.get(0).get<uint8_t>(), 9, true); //nop
                        if (pattern.size() > 1)
                            injector::MakeNOP(pattern.get(1).get<uint8_t>(), 9, true); //nop
                    }
                }

                if (auto pattern = Pattern(image, "39 35 ? ? ? ? 75 0E 80 3D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 6, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 2, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "7C 22 83 F8 28 75 1D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C 0E 05 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 13, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 08 3B 05"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 1F 8B 44 24 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 35 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 38 8B 16"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 11 8B 4E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 1F 8B"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 35 0F BF 43 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 9, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 43"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 13, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 89 44 24 38"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 11, 6, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 8A 0D"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 7, true); //nop
                    injector::MakeNOP(match + 17, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 21, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match - 15, 7, true); //nop
                    injector::MakeNOP(match + 10, 2, true); //nop
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

                    injector::MakeNOP(match, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 85 8C 01 00 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 6, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C D5 00"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match, 13, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 20 8B CD E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 50"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 32 8B 41"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
                }

                if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match + 7, 2, true); //nop
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

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_bike_physics_detour")), true); //jmp
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

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_eax")), true); //jmp
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_edx")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 10, reinterpret_cast<void*>(Abi("abi_copsirens_ide")), true); //jmp
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

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_eax")), true); //jmp
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_edx")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 10, reinterpret_cast<void*>(Abi("abi_copsirens_ide")), true); //jmp
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

                injector::MakeJMP(match - 237, reinterpret_cast<void*>(Abi("abi_nolivery_b")), true); //jmp
                injector::MakeJMP(match - 199, reinterpret_cast<void*>(Abi("abi_nolivery_c")), true); //jmp
                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_nolivery_a")), true); //jmp
            }

            if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_nolivery_d")), true); //jmp
            }

            if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_nolivery_e")), true); //jmp
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

                    injector::MakeNOP(match + 6, 2, true); //nop
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

            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                zpatch_popup_resume_b = MatchAddress(pattern, 10);
                zpatch_popup_resume_a = MatchAddress(pattern, 12);
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                zpatch_ide_hook_slot = MatchAddress(pattern, -1);
            }

            if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                zpatch_popup_resume_b = MatchAddress(pattern, 10);
                zpatch_popup_resume_a = MatchAddress(pattern, 12);
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

            if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_popup_headlights")), true); //jmp
                injector::WriteMemory<uint8_t>(match + 10, static_cast<uint8_t>(0xEBu), true); //jmp
                injector::WriteMemory<uint32_t>(match + 82, AddressOf(&zpatch_popup_angle), true); //patch
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 10, reinterpret_cast<void*>(Abi("abi_copsirens_ide")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_popup_headlights_edx")), true); //jmp
                injector::WriteMemory<uint8_t>(match + 10, static_cast<uint8_t>(0xEBu), true); //jmp
                injector::WriteMemory<uint32_t>(match + 82, AddressOf(&zpatch_popup_angle), true); //patch
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

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_eax")), true); //jmp
            }

            if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 2, reinterpret_cast<void*>(Abi("abi_steer_detour_edx")), true); //jmp
            }

            if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 10, reinterpret_cast<void*>(Abi("abi_copsirens_ide")), true); //jmp
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

                    injector::MakeNOP(match + 7, 2, true); //nop
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
            RegisterFeature(5, "PublishCopSirensFlag", &PublishCopSirensFlag);
            RegisterFeature(11, "PublishHighFPSBikePhysicsFix", &PublishHighFPSBikePhysicsFix);
            RegisterFeature(17, "PublishNoLiveryLimit", &PublishNoLiveryLimit);
            RegisterFeature(22, "PublishPopupHeadlightsFlag", &PublishPopupHeadlightsFlag);
            RegisterFeature(38, "PublishMovingSteeringWheelSupport", &PublishMovingSteeringWheelSupport);
            RegisterFeature(39, "PublishShakingEngineBlock", &PublishShakingEngineBlock);
            RegisterFeature(40, "PublishMovingPlaneParts", &PublishMovingPlaneParts);
            RegisterFeature(50, "InstallBikeFeetFix", &InstallBikeFeetFix);
            RegisterFeature(51, "InstallBikePhoneAnimsFix", &InstallBikePhoneAnimsFix);
            RegisterFeature(57, "InstallCopSirensFlag", &InstallCopSirensFlag);
            RegisterFeature(61, "InstallEpisodicVehicleSupport", &InstallEpisodicVehicleSupport);
            RegisterFeature(62, "InstallEpisodicWeaponSupport", &InstallEpisodicWeaponSupport);
            RegisterFeature(67, "InstallHighFPSBikePhysicsFix", &InstallHighFPSBikePhysicsFix);
            RegisterFeature(82, "InstallNoLiveryLimit", &InstallNoLiveryLimit);
            RegisterFeature(89, "InstallPoliceEpisodicWeaponSupport", &InstallPoliceEpisodicWeaponSupport);
            RegisterFeature(90, "InstallPopupHeadlightsFlag", &InstallPopupHeadlightsFlag);
            RegisterFeature(105, "InstallTBoGTHeliHeightLimit", &InstallTBoGTHeliHeightLimit);
            RegisterFeature(121, "InstallMovingSteeringWheelSupport", &InstallMovingSteeringWheelSupport);
            RegisterFeature(122, "InstallShakingEngineBlock", &InstallShakingEngineBlock);
            RegisterFeature(123, "InstallMovingPlaneParts", &InstallMovingPlaneParts);
        }
    } vehiclesFeatures;
} // namespace

} // namespace opp::features
