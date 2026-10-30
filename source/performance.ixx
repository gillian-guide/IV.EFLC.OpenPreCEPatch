// SPDX-License-Identifier: GPL-3.0-or-later
//
// performance
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

export module performance;

import common;
import settings;

namespace opp::features
{

    // FastLoading

    void InstallFastLoading(PatchContext& ctx)
    {

        TraceArm("FastLoading");

        // FastLoading: 1 sigwrite
        if (ctx.Option("FastLoading", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF"); pattern.size() > 0)
                {
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(1), 0x00u);
                }
            }
        }

    }


    // HighFPSSpeedupFix

    void InstallHighFPSSpeedupFix(PatchContext& ctx)
    {

        TraceArm("HighFPSSpeedupFix");

        // HighFPSSpeedupFix: 9 literals
        if (ctx.Option("HighFPSSpeedupFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 2F C1 76 03 0F 28 C8 F3 0F 10 15"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match - 7, 2);
                NopAt(ctx, match + 3, 2);
                WriteValue<uint8_t>(ctx, match + 224, 0xEBu);
                NopAt(ctx, match + 231, 2);
                NopAt(ctx, match + 367, 2);
                NopAt(ctx, match + 377, 2);
                NopAt(ctx, match + 607, 2);
                NopAt(ctx, match + 614, 2);
                NopAt(ctx, match + 764, 2);
            }
        }

    }


    // ImprovedShaderStreaming

    void InstallImprovedShaderStreaming(PatchContext& ctx)
    {

        TraceArm("ImprovedShaderStreaming");

        // ImprovedShaderStreaming: 1 siginstall
        if (ctx.Option("ImprovedShaderStreaming", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 85, "abi_improved_shader_streaming_find_hpe");
                JumpTo(ctx, match + 98, reinterpret_cast<void*>(match + 245));
                CallToBody(ctx, match + 253, "abi_improved_shader_streaming_find_v");
                JumpTo(ctx, match + 266, reinterpret_cast<void*>(match + 1158));
            }
        }

    }


    // IncreasePtrNodes

    void InstallIncreasePtrNodes(PatchContext& ctx)
    {

        TraceArm("IncreasePtrNodes");

        // IncreasePtrNodes: 3 literalstores
        if (ctx.Option("IncreasePtrNodes", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "6A 08 ? ? ? ? ? 68 80 38 01 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0x4E200u));
            }

            if (auto pattern = Pattern(image, "6A 10 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0xFA00u));
            }

            if (auto pattern = Pattern(image, "6A 14 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0xFA00u));
            }
        }

    }


    // IncreaseVehicleModelLimit

    void InstallIncreaseVehicleModelLimit(PatchContext& ctx)
    {

        TraceArm("IncreaseVehicleModelLimit");

        // IncreaseVehicleModelLimit: 1 indirect
        if (ctx.Option("IncreaseVehicleModelLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C7 01 81 FF 18 79 00 00 7C D2"); pattern.size() > 0)
            {
                auto* target = MatchPointer(pattern, 77);
                if (target != nullptr)
                    WriteValue<uint32_t>(ctx, target, static_cast<uint32_t>(0x400u));
            }
        }

    }


    // IncreaseVehicleStructLimit

    void InstallIncreaseVehicleStructLimit(PatchContext& ctx)
    {

        TraceArm("IncreaseVehicleStructLimit");

        // IncreaseVehicleStructLimit: 1 literalstore
        if (ctx.Option("IncreaseVehicleStructLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 ? ? ? ? 6A 32 8B C8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match + 6, static_cast<uint8_t>(0x64u));
            }
        }

    }


    // publish NewMemorySystem

    void PublishNewMemorySystem(PatchContext& ctx)
    {

        TraceArm("publish NewMemorySystem");

        // NewMemorySystem: 1 slot
        if (ctx.Option("NewMemorySystem", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "6A 01 B9 00 00 40 06"); pattern.size() > 0)
            {
                zpatch_nms_object = *pattern.get(0).get<uint32_t>(8);
            }
        }

    }


    // NewMemorySystem

    void InstallNewMemorySystem(PatchContext& ctx)
    {

        TraceArm("NewMemorySystem");

        // NewMemorySystem: 1 siginstall
        if (ctx.Option("NewMemorySystem", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "3D 80 99 99 59 73 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 34, "abi_nms_apply");
                JumpTo(ctx, match - 29, reinterpret_cast<void*>(match - 16));
            }
        }

    }

    // NoLoadingSleep

    void InstallNoLoadingSleep(PatchContext& ctx)
    {

        TraceArm("NoLoadingSleep");

        // NoLoadingSleep: 1 literalstore
        if (ctx.Option("NoLoadingSleep", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 88 13 00 00 FF 15"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint32_t>(ctx, match + 1, static_cast<uint32_t>(0x0u));
            }
        }

    }


    // StartupTimeFix

    void InstallStartupTimeFix(PatchContext& ctx)
    {

        TraceArm("StartupTimeFix");

        // StartupTimeFix: 1 matchliteral
        if (ctx.Option("StartupTimeFix", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = PatternAll(image, "68 00 C2 EC 80 68"); pattern.size() > 0)
                {
                    for (size_t i = 0; i < pattern.size(); ++i)
                        WriteValue<uint32_t>(ctx, pattern.get(i).get<uint8_t>() + 6, static_cast<uint32_t>(FindGlobalValue("StartupTimeFix")));
                }
            }
        }

    }

namespace
{
    class PerformanceFeatures
    {
    public:
        PerformanceFeatures()
        {
            RegisterFeature(15, "PublishNewMemorySystem", &PublishNewMemorySystem);
            RegisterFeature(68, "InstallHighFPSSpeedupFix", &InstallHighFPSSpeedupFix);
            RegisterFeature(69, "InstallImprovedShaderStreaming", &InstallImprovedShaderStreaming);
            RegisterFeature(80, "InstallNewMemorySystem", &InstallNewMemorySystem);
            RegisterFeature(135, "InstallFastLoading", &InstallFastLoading);
            RegisterFeature(138, "InstallIncreaseVehicleModelLimit", &InstallIncreaseVehicleModelLimit);
            RegisterFeature(141, "InstallStartupTimeFix", &InstallStartupTimeFix);
            RegisterFeature(142, "InstallIncreasePtrNodes", &InstallIncreasePtrNodes);
            RegisterFeature(143, "InstallIncreaseVehicleStructLimit", &InstallIncreaseVehicleStructLimit);
            RegisterFeature(145, "InstallNoLoadingSleep", &InstallNoLoadingSleep);
        }
    } performanceFeatures;
} // namespace

} // namespace opp::features
