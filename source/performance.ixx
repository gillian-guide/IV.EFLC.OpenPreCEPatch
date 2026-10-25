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
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(1), 0x00u, true); //patch
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

                injector::MakeNOP(match - 7, 2, true); //nop
                injector::MakeNOP(match + 3, 2, true); //nop
                injector::WriteMemory<uint8_t>(match + 224, 0xEBu, true); //jmp
                injector::MakeNOP(match + 231, 2, true); //nop
                injector::MakeNOP(match + 367, 2, true); //nop
                injector::MakeNOP(match + 377, 2, true); //nop
                injector::MakeNOP(match + 607, 2, true); //nop
                injector::MakeNOP(match + 614, 2, true); //nop
                injector::MakeNOP(match + 764, 2, true); //nop
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

                injector::MakeCALL(match + 85,
                                   reinterpret_cast<void*>(Abi("abi_improved_shader_streaming_find_hpe")), true); //call
                injector::MakeJMP(match + 98, reinterpret_cast<void*>(match + 245), true); //jmp
                injector::MakeCALL(match + 253,
                                   reinterpret_cast<void*>(Abi("abi_improved_shader_streaming_find_v")), true); //call
                injector::MakeJMP(match + 266, reinterpret_cast<void*>(match + 1158), true); //jmp
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

                injector::WriteMemory<uint32_t>(match + 8, static_cast<uint32_t>(0x4E200u), true); //patch
            }

            if (auto pattern = Pattern(image, "6A 10 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint32_t>(match + 8, static_cast<uint32_t>(0xFA00u), true); //patch
            }

            if (auto pattern = Pattern(image, "6A 14 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint32_t>(match + 8, static_cast<uint32_t>(0xFA00u), true); //patch
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
                    injector::WriteMemory<uint32_t>(target, static_cast<uint32_t>(0x400u), true); //patch
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

                injector::WriteMemory<uint8_t>(match + 6, static_cast<uint8_t>(0x64u), true); //patch
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

                injector::MakeCALL(match - 34, reinterpret_cast<void*>(Abi("abi_nms_apply")), true); //call
                injector::MakeJMP(match - 29, reinterpret_cast<void*>(match - 16), true); //jmp
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

                injector::WriteMemory<uint32_t>(match + 1, static_cast<uint32_t>(0x0u), true); //patch
            }
        }

    }


    // NoPhysicsSleep

    void InstallNoPhysicsSleep(PatchContext& ctx)
    {

        TraceArm("NoPhysicsSleep");

        // NoPhysicsSleep: 1 indirect
        if (ctx.Option("NoPhysicsSleep", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00"); pattern.size() > 0)
            {
                auto* target = MatchPointer(pattern, -38);
                if (target != nullptr)
                    injector::WriteMemory<uint32_t>(target, static_cast<uint32_t>(0x0u), true); //patch
            }
        }

    }


    // RemoveUselessChecks

    void ApplyUselessChecksArm(PatchContext& ctx)
    {

        TraceArm("RemoveUselessChecks");

        ApplyUselessChecks(ctx);

    }


    // RemoveUselessChecks

    void InstallRemoveUselessChecks(PatchContext& ctx)
    {

        TraceArm("RemoveUselessChecks");

        // RemoveUselessChecks: 14 literals, 9 rvas
        if (ctx.Option("RemoveUselessChecks", "0"))
        {
            const GameImage& image = ctx.image;

            if (ctx.build == GameBuild::v1040)
            {
                injector::MakeNOP(image.base + 0x46CCE7, 38, true); //nop
                injector::MakeNOP(image.base + 0x46CE67, 38, true); //nop
            }

            if (ctx.build == GameBuild::v1070)
            {
                injector::MakeNOP(image.base + 0x3DA1B7, 38, true); //nop
                injector::MakeNOP(image.base + 0x3DA337, 38, true); //nop
            }

            if (ctx.build == GameBuild::v1080)
            {
                injector::MakeNOP(image.base + 0x559127, 38, true); //nop
                injector::MakeNOP(image.base + 0x5592A7, 38, true); //nop
                injector::MakeNOP(image.base + 0x559CC0, 9, true); //nop
                injector::MakeNOP(image.base + 0x55EB83, 13, true); //nop
                injector::MakeNOP(image.base + 0x77027A, 9, true); //nop
                injector::WriteMemory<uint8_t>(image.base + 0x559598, 0xEBu, true); //jmp
                injector::WriteMemory<uint8_t>(image.base + 0x62555D, 0xEBu, true); //jmp
                injector::MakeNOP(image.base + 0x5594B7, 2, true); //nop
                injector::MakeNOP(image.base + 0x559013, 6, true); //nop
                injector::MakeNOP(image.base + 0x55EADF, 6, true); //nop
                injector::MakeNOP(image.base + 0x55F5BD, 6, true); //nop
                injector::MakeNOP(image.base + 0x55FC2E, 6, true); //nop
                injector::MakeNOP(image.base + 0x5594A2, 7, true); //nop
                injector::MakeNOP(image.base + 0x5594CA, 7, true); //nop
                injector::MakeNOP(image.base + 0x559591, 7, true); //nop
                injector::MakeNOP(image.base + 0x55EACA, 7, true); //nop
                injector::MakeNOP(image.base + 0x55F5B5, 7, true); //nop
                injector::MakeNOP(image.base + 0x55FC21, 7, true); //nop
                injector::MakeNOP(image.base + 0x625556, 7, true); //nop
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
                if (auto pattern = Pattern(image, "68 00 C2 EC 80 68"); pattern.size() > 0)
                {
                    for (size_t i = 0; i < pattern.size(); ++i)
                        injector::WriteMemory<uint32_t>(
                            pattern.get(i).get<uint8_t>() + 6,
                            static_cast<uint32_t>(FindGlobalValue("StartupTimeFix")), true); //patch
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
            RegisterFeature(0, "ApplyUselessChecksArm", &ApplyUselessChecksArm);
            RegisterFeature(15, "PublishNewMemorySystem", &PublishNewMemorySystem);
            RegisterFeature(68, "InstallHighFPSSpeedupFix", &InstallHighFPSSpeedupFix);
            RegisterFeature(69, "InstallImprovedShaderStreaming", &InstallImprovedShaderStreaming);
            RegisterFeature(80, "InstallNewMemorySystem", &InstallNewMemorySystem);
            RegisterFeature(96, "InstallRemoveUselessChecks", &InstallRemoveUselessChecks);
            RegisterFeature(135, "InstallFastLoading", &InstallFastLoading);
            RegisterFeature(138, "InstallIncreaseVehicleModelLimit", &InstallIncreaseVehicleModelLimit);
            RegisterFeature(139, "InstallNoPhysicsSleep", &InstallNoPhysicsSleep);
            RegisterFeature(141, "InstallStartupTimeFix", &InstallStartupTimeFix);
            RegisterFeature(142, "InstallIncreasePtrNodes", &InstallIncreasePtrNodes);
            RegisterFeature(143, "InstallIncreaseVehicleStructLimit", &InstallIncreaseVehicleStructLimit);
            RegisterFeature(145, "InstallNoLoadingSleep", &InstallNoLoadingSleep);
        }
    } performanceFeatures;
} // namespace

} // namespace opp::features
