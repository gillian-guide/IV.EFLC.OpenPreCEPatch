// SPDX-License-Identifier: GPL-3.0-or-later
//
// interface
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

export module interface;

import common;
import settings;

namespace opp::features
{

    // PipeBombDropIconSupport

    void InstallPipeBombDropIconSupport(PatchContext& ctx)
    {

        TraceArm("PipeBombDropIconSupport");

        // PipeBombDropIconSupport: 1 literal
        if (ctx.Option("PipeBombDropIconSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "83 7D 18 1C 75 14"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::MakeNOP(match - 9, 9, true); //nop
                }
            }
        }

    }


    // RestoreDeathMusic

    void InstallRestoreDeathMusic(PatchContext& ctx)
    {

        TraceArm("RestoreDeathMusic");

        // RestoreDeathMusic: 3 siginstalls
        if (bMenuIntegration || ctx.Option("RestoreDeathMusic", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match - 13, reinterpret_cast<void*>(Abi("abi_death_music_install")), true); //call
            }

            if (auto pattern = Pattern(image, "C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match - 13, reinterpret_cast<void*>(Abi("abi_death_music_enabled")), true); //call
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match, reinterpret_cast<void*>(Abi("abi_death_music_enabled")), true); //call
            }
        }

    }

namespace
{
    class InterfaceFeatures
    {
    public:
        InterfaceFeatures()
        {
            RegisterFeature(86, "InstallPipeBombDropIconSupport", &InstallPipeBombDropIconSupport);
            RegisterFeature(130, "InstallRestoreDeathMusic", &InstallRestoreDeathMusic);
        }
    } interfaceFeatures;
} // namespace

} // namespace opp::features
