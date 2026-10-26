// SPDX-License-Identifier: GPL-3.0-or-later
//
// input
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

export module input;

import common;
import settings;

namespace opp::features
{

    // BorderlessWindowed / DoNotPauseOnMinimize live in the separate
    // XLivelessAddon successor module (xlivelessaddon/).


    // publish DrivebyFOVFix

    void PublishDrivebyFOVFix(PatchContext& ctx)
    {

        TraceArm("publish DrivebyFOVFix");

        // DrivebyFOVFix: 2 slots
        if (ctx.Option("DrivebyFOVFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
            {
                zpatch_driveby_resume = MatchAddress(pattern, 5);
            }

            if (auto pattern = Pattern(image, "08 0B C2 8B F0 E8"); pattern.size() > 0)
            {
                zpatch_driveby_resolver = MatchTarget(pattern, 6, 10);
            }
        }

    }


    // DrivebyFOVFix

    void InstallDrivebyFOVFix(PatchContext& ctx)
    {

        TraceArm("DrivebyFOVFix");

        // DrivebyFOVFix: 1 sigjump
        if (ctx.Option("DrivebyFOVFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_driveby_fov_shim")), true); //jmp
            }
        }

    }


    // publish MouseFix

    void PublishMouseFix(PatchContext& ctx)
    {

        TraceArm("publish MouseFix");

        // MouseFix: 6 slots
        if (ctx.Option("MouseFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
            {
                zpatch_mf_x = *pattern.get(0).get<uint32_t>(13);
                zpatch_mf_y = *pattern.get(0).get<uint32_t>(20);
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
            {
                zpatch_mf_flag = *pattern.get(0).get<uint32_t>(20);
            }

            if (auto pattern = Pattern(image, "83 EC 08 DB 05 ? ? ? ? 68"); pattern.size() > 0)
            {
                zpatch_mf_time = MatchAddress(pattern);
            }

            if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
            {
                zpatch_mf_resume_a = MatchAddress(pattern);
                zpatch_mf_resume_b = MatchAddress(pattern, 1878);
            }
        }

    }


    // MouseFix

    void InstallMouseFix(PatchContext& ctx)
    {

        TraceArm("MouseFix");

        // MouseFix: 8 siginstalls
        if (ctx.Option("MouseFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_mouse_axis_float")), true); //jmp
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_mouse_axis_neg")), true); //jmp
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint32_t>(match + 5, 0xC06E0F66u, true); //patch
                injector::WriteMemory<uint32_t>(match + 33, 0xC06E0F66u, true); //patch
                injector::MakeNOP(match + 3, 2, true); //nop
                injector::MakeNOP(match + 31, 2, true); //nop
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint32_t>(match + 5, 0xC06E0F66u, true); //patch
                injector::WriteMemory<uint32_t>(match + 33, 0xC06E0F66u, true); //patch
                injector::MakeNOP(match + 3, 2, true); //nop
                injector::MakeNOP(match + 31, 2, true); //nop
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint32_t>(match + 5, 0xC86E0F66u, true); //patch
                injector::MakeNOP(match + 3, 2, true); //nop
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_mouse_axis_flag")), true); //jmp
            }

            if (auto pattern = Pattern(image, "56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 51, reinterpret_cast<void*>(Abi("abi_mouse_axis_int")), true); //call
                injector::MakeCALL(match + 90, reinterpret_cast<void*>(Abi("abi_mouse_axis_int")), true); //call
            }

            if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 15, reinterpret_cast<void*>(Abi("abi_mouse_angle")), true); //jmp
                injector::MakeJMP(match + 1862, reinterpret_cast<void*>(Abi("abi_mouse_advance")), true); //jmp
            }
        }

    }


    // VSyncFix

    void InstallVSyncFix(PatchContext& ctx)
    {

        TraceArm("VSyncFix");

        // VSyncFix: 2 literalstores
        if (ctx.Option("VSyncFix", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
                         GameBuild::v1100, GameBuild::v1110, GameBuild::v1120, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::WriteMemory<uint32_t>(
                    match + 30, static_cast<uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
                }

                if (auto pattern = Pattern(image, "8B 10 50 8B 82 A8 00 00 00 FF D0 83"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::WriteMemory<uint32_t>(
                    match + 13, static_cast<uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
                }
            }
        }
    }

namespace
{
    class InputFeatures
    {
    public:
        InputFeatures()
        {
            RegisterFeature(29, "PublishDrivebyFOVFix", &PublishDrivebyFOVFix);
            RegisterFeature(41, "PublishMouseFix", &PublishMouseFix);
            RegisterFeature(112, "InstallDrivebyFOVFix", &InstallDrivebyFOVFix);
            RegisterFeature(124, "InstallMouseFix", &InstallMouseFix);
            RegisterFeature(147, "InstallVSyncFix", &InstallVSyncFix);
        }
    } inputFeatures;
} // namespace

} // namespace opp::features
