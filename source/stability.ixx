// SPDX-License-Identifier: GPL-3.0-or-later
//
// stability
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

export module stability;

import common;
import settings;

namespace opp::features
{

    // AimProjectileCrashFix

    void InstallAimProjectileCrashFix(PatchContext& ctx)
    {

        TraceArm("AimProjectileCrashFix");

        // AimProjectileCrashFix: 1 sigjump
        if (ctx.Option("AimProjectileCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_aim_projectile_crash_fix");
            }
        }

    }


    // publish BadPlayerModelCrashFix

    void PublishBadPlayerModelCrashFix(PatchContext& ctx)
    {

        TraceArm("publish BadPlayerModelCrashFix");

        // BadPlayerModelCrashFix: 1 slot
        if (ctx.Option("BadPlayerModelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
            {
                zpatch_bpm_resume = MatchAddress(pattern, 10);
            }
        }

    }


    // BadPlayerModelCrashFix

    void InstallBadPlayerModelCrashFix(PatchContext& ctx)
    {

        TraceArm("BadPlayerModelCrashFix");

        // BadPlayerModelCrashFix: 1 siginstall, 1 sigjump
        if (ctx.Option("BadPlayerModelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_bad_player_model");
            }
        }

    }


    // publish BenchmarkFix

    void PublishBenchmarkFix(PatchContext& ctx)
    {

        TraceArm("publish BenchmarkFix");

        // BenchmarkFix: 2 slots
        if (ctx.Option("BenchmarkFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
            {
                zpatch_benchmark_resume_a = MatchAddress(pattern, 13);
                zpatch_benchmark_resume_b = MatchAddress(pattern, 13);
            }
        }

    }


    // BenchmarkFix

    void InstallBenchmarkFix(PatchContext& ctx)
    {

        TraceArm("BenchmarkFix");

        // BenchmarkFix: 2 matchjumps
        if (ctx.Option("BenchmarkFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004, GameBuild::v1060,
                         GameBuild::v1070, GameBuild::v1061, GameBuild::v1100, GameBuild::v1110,
                         GameBuild::v1120, GameBuild::v12043}))
            {

                {
                    if (auto pattern = PatternAll(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
                    {
                        JumpToBody(ctx, pattern.get(0).get<uint8_t>(8), "abi_benchmark_a");
                        if (pattern.size() > 1)
                            JumpToBody(ctx, pattern.get(1).get<uint8_t>(8), "abi_benchmark_b");
                    }
                }
            }
        }

    }


    // publish BlackscreenFix

    void PublishBlackscreenFix(PatchContext& ctx)
    {

        TraceArm("publish BlackscreenFix");

        // BlackscreenFix: 2 slots
        if (ctx.Option("BlackscreenFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
            {
                zpatch_blackscreen_resume = MatchTarget(pattern, 82, 86);
                const auto address = static_cast<uintptr_t>(MatchTarget(pattern, 82, 87));
                if (InImage(image, address))
                    zpatch_blackscreen_value_ptr = *reinterpret_cast<const uint32_t*>(address);
            }
        }

    }


    // BlackscreenFix

    void InstallBlackscreenFix(PatchContext& ctx)
    {

        TraceArm("BlackscreenFix");

        // BlackscreenFix: 1 sigjump
        if (ctx.Option("BlackscreenFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 81, "abi_blackscreen_shim");
            }
        }

    }


    // publish CollisionDeadlockFix

    void PublishCollisionDeadlockFix(PatchContext& ctx)
    {

        TraceArm("publish CollisionDeadlockFix");

        // CollisionDeadlockFix: 1 slot
        if (ctx.Option("CollisionDeadlockFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
            {
                zpatch_collision_deadlock_should_walk = MatchTarget(pattern, 91, 95);
            }
        }

    }


    // CollisionDeadlockFix

    void InstallCollisionDeadlockFix(PatchContext& ctx)
    {

        TraceArm("CollisionDeadlockFix");

        // CollisionDeadlockFix: 1 sigjump
        if (ctx.Option("CollisionDeadlockFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_collision_deadlock_walk");
            }
        }

    }


    // CombinedRadioFreezeFix

    void InstallCombinedRadioFreezeFix(PatchContext& ctx)
    {

        TraceArm("CombinedRadioFreezeFix");

        // CombinedRadioFreezeFix: 1 siginstall
        if (ctx.Option("CombinedRadioFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match, "abi_combined_radio_freeze_fix");
            }
        }

    }


    // publish CrashMsgFix

    void PublishCrashMsgFix(PatchContext& ctx)
    {

        TraceArm("publish CrashMsgFix");

        // CrashMsgFix: 1 slot
        if (ctx.Option("CrashMsgFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
            {
                const auto site = static_cast<uintptr_t>(*pattern.get(0).get<uint32_t>(-4));
                if (InImage(image, site))
                    zpatch_crash_msg_getter = static_cast<uint32_t>(
                                site + ReadDword(reinterpret_cast<const uint8_t*>(site) + 1) + 5);
            }
        }

    }


    // CrashMsgFix

    void InstallCrashMsgFix(PatchContext& ctx)
    {

        TraceArm("CrashMsgFix");

        // CrashMsgFix: 1 siginstall
        if (ctx.Option("CrashMsgFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
            {
                uint8_t* site = MatchPointer(pattern, -4);
                CallToBody(ctx, site, "abi_crash_msg_shim");
            }
        }

    }


    // publish EpisodeSwitchFreezeFix

    void PublishEpisodeSwitchFreezeFix(PatchContext& ctx)
    {

        TraceArm("publish EpisodeSwitchFreezeFix");

        // EpisodeSwitchFreezeFix: 3 slots
        if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_a = MatchAddress(pattern, 5);
            }

            if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_b = MatchAddress(pattern, 6);
            }

            if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_c = MatchAddress(pattern, 7);
            }
        }

    }


    // EpisodeSwitchFreezeFix

    void InstallEpisodeSwitchFreezeFix(PatchContext& ctx)
    {

        TraceArm("EpisodeSwitchFreezeFix");

        // EpisodeSwitchFreezeFix: 5 sigjumps
        if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_episode_switch_shim_c");
            }

            if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_episode_switch_store_shim");
            }

            if (auto pattern = Pattern(image, "8B 41 10 03 41 0C C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_episode_switch_sum");
            }

            if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_episode_switch_shim_b");
            }

            if (auto pattern = Pattern(image, "8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_episode_switch_vcall");
            }
        }

    }


    // publish GarageModelFix

    void PublishGarageModelFix(PatchContext& ctx)
    {

        TraceArm("publish GarageModelFix");

        // GarageModelFix: 2 slots
        if (ctx.Option("GarageModelFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
            {
                zpatch_garage_resume_a = MatchAddress(pattern, 6);

                // Site A is at the function's mid-body anchor; the hook goes to
                // the prologue, which starts with an 8-byte `movss xmm0, ds:X`,
                // so the resume is just past it and the float to reload is the
                // dword that instruction's operand points at.
                constexpr ptrdiff_t kGarageEntryDelta = -0x20;
                constexpr ptrdiff_t kGarageResumeDelta = kGarageEntryDelta + 8;
                constexpr ptrdiff_t kGarageScaleOperandDelta = kGarageResumeDelta - 4;
                static_assert(kGarageResumeDelta == -0x18);
                static_assert(kGarageScaleOperandDelta == -0x1C);
                zpatch_garage_entry_resume_a = MatchAddress(pattern, kGarageResumeDelta);
                zpatch_garage_scale = *reinterpret_cast<const float*>(
                    static_cast<uintptr_t>(
                        Field(pattern.get(0).get<uint8_t>(), kGarageScaleOperandDelta)));
            }

            if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
            {
                zpatch_garage_resume_b = MatchAddress(pattern, 5);
            }
        }

    }


    // GarageModelFix

    void InstallGarageModelFix(PatchContext& ctx)
    {

        TraceArm("GarageModelFix");

        // GarageModelFix: 2 siginstalls
        if (ctx.Option("GarageModelFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // Site A is hooked at the function prologue, 0x20 bytes before
                // the anchor; the body reloads the `xmm0` the skipped
                // instruction sets.
                constexpr ptrdiff_t kGarageEntryDelta = -0x20;
                JumpToBody(ctx, match + kGarageEntryDelta, "abi_garage_entry_a");
            }

            if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_garage_body_b");
            }
        }

    }


    // publish GroupHackFix

    void PublishGroupHackFix(PatchContext& ctx)
    {

        TraceArm("publish GroupHackFix");

        // GroupHackFix: 1 slot
        if (ctx.Option("GroupHackFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
            {
                zpatch_group_hack_resume = MatchAddress(pattern, 11);
            }
        }

    }


    // GroupHackFix

    void InstallGroupHackFix(PatchContext& ctx)
    {

        TraceArm("GroupHackFix");

        // GroupHackFix: 1 sigjump
        if (ctx.Option("GroupHackFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_group_hack_shim");
            }
        }

    }


    // MPNikoCrashFix

    void InstallMPNikoCrashFix(PatchContext& ctx)
    {

        TraceArm("MPNikoCrashFix");

        // MPNikoCrashFix: 1 literal
        if (ctx.Option("MPNikoCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match, opp::ops::kJmpRel8);
            }
        }

    }


    // publish OutOfCommissionFix

    void PublishOutOfCommissionFix(PatchContext& ctx)
    {

        TraceArm("publish OutOfCommissionFix");

        // OutOfCommissionFix: 3 slots. The finale archive path exists on
        // 1060-1130; gate it explicitly (12043 has no verified payload).
        if (ctx.Option("OutOfCommissionFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                GameBuild::v1120, GameBuild::v1130}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
            {
                zpatch_ooc_resume = MatchAddress(pattern, 17);
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
            {
                zpatch_ooc_logger = MatchTarget(pattern, -50, -46);
                zpatch_ooc_orig = MatchTarget(pattern, 1, 5);
            }
        }

    }


    // OutOfCommissionFix

    void InstallOutOfCommissionFix(PatchContext& ctx)
    {

        TraceArm("OutOfCommissionFix");

        // OutOfCommissionFix: 2 siginstalls
        if (ctx.Option("OutOfCommissionFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                GameBuild::v1120, GameBuild::v1130}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 7, "abi_ooc_branch");
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match, "abi_ooc_wrap");
            }
        }

    }


    // publish PathNodeCrashFix

    void PublishPathNodeCrashFix(PatchContext& ctx)
    {

        TraceArm("publish PathNodeCrashFix");

        // PathNodeCrashFix: 2 slots
        if (ctx.Option("PathNodeCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
            {
                zpatch_path_node_resume_ok = MatchAddress(pattern);
                zpatch_path_node_resume_fail = MatchAddress(pattern, 59);
            }
        }

    }


    // PathNodeCrashFix

    void InstallPathNodeCrashFix(PatchContext& ctx)
    {

        TraceArm("PathNodeCrashFix");

        // PathNodeCrashFix: 1 sigjump
        if (ctx.Option("PathNodeCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 53, "abi_path_node_shim");
            }
        }

    }


    // publish PedModelsInCutscenesFix

    void PublishPedModelsInCutscenesFix(PatchContext& ctx)
    {

        TraceArm("publish PedModelsInCutscenesFix");

        // PedModelsInCutscenesFix: 1 slot
        if (ctx.Option("PedModelsInCutscenesFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
            {
                zpatch_model_lookup = MatchAddress(pattern);
            }
        }

    }


    // PedModelsInCutscenesFix

    void InstallPedModelsInCutscenesFix(PatchContext& ctx)
    {

        TraceArm("PedModelsInCutscenesFix");

        // PedModelsInCutscenesFix: 1 sigjump
        if (ctx.Option("PedModelsInCutscenesFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_ped_models_in_cutscenes_check");
            }
        }

    }


    // publish PlaneCrashFix

    void PublishPlaneCrashFix(PatchContext& ctx)
    {

        TraceArm("publish PlaneCrashFix");

        // PlaneCrashFix: 2 slots
        if (ctx.Option("PlaneCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
            {
                zpatch_plane_crash_resume_b = MatchAddress(pattern, 14);
                zpatch_plane_crash_resume_a = MatchAddress(pattern, -705);
            }
        }

    }


    // PlaneCrashFix

    void InstallPlaneCrashFix(PatchContext& ctx)
    {

        TraceArm("PlaneCrashFix");

        // PlaneCrashFix: 1 sigjump
        if (ctx.Option("PlaneCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 3, "abi_plane_crash_shim");
            }
        }

    }


    // publish PlayerAsPedComponentsFix

    void PublishPlayerAsPedComponentsFix(PatchContext& ctx)
    {

        TraceArm("publish PlayerAsPedComponentsFix");

        // PlayerAsPedComponentsFix: 2 slots
        if (ctx.Option("PlayerAsPedComponentsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
            {
                zpatch_player_ped_resume = MatchAddress(pattern, 14);
            }

            if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
            {
                zpatch_model_lookup = MatchAddress(pattern);
            }
        }

    }


    // PlayerAsPedComponentsFix

    void InstallPlayerAsPedComponentsFix(PatchContext& ctx)
    {

        TraceArm("PlayerAsPedComponentsFix");

        // PlayerAsPedComponentsFix: 1 sigjump
        if (ctx.Option("PlayerAsPedComponentsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match + 5, "abi_player_ped_components_shim");
            }
        }

    }


    // publish SMPA60Fix

    void PublishSMPA60Fix(PatchContext& ctx)
    {

        TraceArm("publish SMPA60Fix");

        // SMPA60Fix: 3 slots
        if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
            {
                zpatch_smpa60_resume_a = MatchAddress(pattern, 28);
                zpatch_smpa60_resume_b = MatchAddress(pattern, 14);
            }

            if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
            {
                zpatch_smpa60_resume_c = MatchAddress(pattern, 136);
            }
        }

    }


    // SMPA60Fix

    void InstallSMPA60Fix(PatchContext& ctx)
    {

        TraceArm("SMPA60Fix");

        // SMPA60Fix: 1 literal, 1 siginstall, 1 sigjump
        if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The A60 branch's conditional jump sits 38 bytes before the
                // anchor and 123 bytes after it is the tail the shim resumes at.
                constexpr ptrdiff_t kSmpa60BranchDelta = -38;
                constexpr ptrdiff_t kSmpa60TailDelta = 123;
                JumpToBody(ctx, match + kSmpa60TailDelta, "abi_smpa60_tail_shim");
                WriteValue<uint8_t>(ctx, match + kSmpa60BranchDelta, opp::ops::kJmpRel8);
            }

            if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_smpa60_shim");
            }
        }

    }


    // publish SpeedupBugFix

    void PublishSpeedupBugFix(PatchContext& ctx)
    {

        TraceArm("publish SpeedupBugFix");

        // SpeedupBugFix: 8 slots
        if (ctx.Option("SpeedupBugFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
            {
                zpatch_speedup_t50 = *pattern.get(0).get<uint32_t>(26);
                zpatch_speedup_t64 = *pattern.get(0).get<uint32_t>(165);
                zpatch_speedup_t60 = *pattern.get(0).get<uint32_t>(188);
                zpatch_speedup_t5c = *pattern.get(0).get<uint32_t>(226);
                zpatch_speedup_t68 = *pattern.get(0).get<uint32_t>(234);
                zpatch_speedup_t4c = *pattern.get(0).get<uint32_t>(249);
                zpatch_speedup_t54 = *pattern.get(0).get<uint32_t>(264);
                zpatch_speedup_t58 = *pattern.get(0).get<uint32_t>(276);
            }
        }

    }


    // SpeedupBugFix

    void InstallSpeedupBugFix(PatchContext& ctx)
    {

        TraceArm("SpeedupBugFix");

        // SpeedupBugFix: 1 siginstall
        if (ctx.Option("SpeedupBugFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 21, "abi_speedup_body");
            }
        }

    }


    // publish SuperLODFix

    void PublishSuperLODFix(PatchContext& ctx)
    {

        TraceArm("publish SuperLODFix");

        // SuperLODFix: 6 slots
        if (ctx.Option("SuperLODFix", "1"))
        {
            const GameImage& image = ctx.image;

            // The shim's three targets are derived from the arm's own site scan
            // and the lookup function's entry scan: the fail/ok blocks sit 0x4E
            // and 6 bytes past the site on both gated builds (verified on
            // 1.0.7.0 and 1.0.8.0), and the lookup window is unique (proved with
            // tools/opcep_sigcheck.py). The build gate keeps both scans on the
            // build the original patched.
            constexpr uintptr_t kSuperLodFailDelta = 0x4E;
            constexpr uintptr_t kSuperLodOkDelta = 0x6;
            const auto findSite = [&image](const char* signature) -> uint8_t* {
                auto pattern = Pattern(image, signature);
                return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
            };

            if (ctx.build == GameBuild::v1070)
            {
                if (auto* site = findSite("F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"))
                {
                    zpatch_super_lod_fail = AddressOf(site + kSuperLodFailDelta);
                    zpatch_super_lod_ok = AddressOf(site + kSuperLodOkDelta);
                }
                if (auto* lookup = findSite("8B 44 24 04 89 44 24 04 66 A1 44 C2 F2 00 66 85 C0 76 3E 8B"))
                    zpatch_super_lod_lookup = AddressOf(lookup);
            }

            if (ctx.build == GameBuild::v1080)
            {
                if (auto* site = findSite("F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"))
                {
                    zpatch_super_lod_fail = AddressOf(site + kSuperLodFailDelta);
                    zpatch_super_lod_ok = AddressOf(site + kSuperLodOkDelta);
                }
                if (auto* lookup = findSite("8B 44 24 04 89 44 24 04 66 A1 E0 E9 F3 00 66 85 C0 76 3E 8B"))
                    zpatch_super_lod_lookup = AddressOf(lookup);
            }
        }

    }


    // SuperLODFix

    void InstallSuperLODFix(PatchContext& ctx)
    {

        TraceArm("SuperLODFix");

        // SuperLODFix: 1 sigjump. The recovered handler writes the same site on
        // 1.0.7.0 (RVA 0x4A3CE9) and 1.0.8.0 (RVA 0x540E19); the scan is the
        // instruction pair at the site and resolves on both, so the RVA table is
        // gone. The build gate stays: the same bytes exist on other builds and
        // the original only installed it on these two.
        if (ctx.Option("SuperLODFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1070, GameBuild::v1080}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
            {
                JumpToBody(ctx, pattern.get(0).get<uint8_t>(), "abi_super_lod_fix_shim");
            }
        }

    }


    // TryToSkipAllErrors

    void InstallTryToSkipAllErrors(PatchContext& ctx)
    {

        TraceArm("TryToSkipAllErrors");

        // TryToSkipAllErrors: 1 literal
        if (ctx.Option("TryToSkipAllErrors", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match, opp::ops::kRet);
            }
        }

    }


    // publish WantedLevelCrashFix

    void PublishWantedLevelCrashFix(PatchContext& ctx)
    {

        TraceArm("publish WantedLevelCrashFix");

        // WantedLevelCrashFix: 1 slot
        if (ctx.Option("WantedLevelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
            {
                zpatch_wanted_level_resume = MatchAddress(pattern, 12);
            }
        }

    }


    // WantedLevelCrashFix

    void InstallWantedLevelCrashFix(PatchContext& ctx)
    {

        TraceArm("WantedLevelCrashFix");

        // WantedLevelCrashFix: 1 sigjump
        if (ctx.Option("WantedLevelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_wanted_level_shim");
            }
        }

    }

namespace
{
    class StabilityFeatures
    {
    public:
        StabilityFeatures()
        {
            RegisterFeature({Phase::publish, "PublishBadPlayerModelCrashFix", &PublishBadPlayerModelCrashFix});
            RegisterFeature({Phase::publish, "PublishBlackscreenFix", &PublishBlackscreenFix});
            RegisterFeature({Phase::publish, "PublishCollisionDeadlockFix", &PublishCollisionDeadlockFix});
            RegisterFeature({Phase::publish, "PublishEpisodeSwitchFreezeFix", &PublishEpisodeSwitchFreezeFix});
            RegisterFeature({Phase::publish, "PublishGroupHackFix", &PublishGroupHackFix});
            RegisterFeature({Phase::publish, "PublishPathNodeCrashFix", &PublishPathNodeCrashFix});
            RegisterFeature({Phase::publish, "PublishPedModelsInCutscenesFix", &PublishPedModelsInCutscenesFix});
            RegisterFeature({Phase::publish, "PublishPlaneCrashFix", &PublishPlaneCrashFix});
            RegisterFeature({Phase::publish, "PublishPlayerAsPedComponentsFix", &PublishPlayerAsPedComponentsFix});
            RegisterFeature({Phase::publish, "PublishSMPA60Fix", &PublishSMPA60Fix});
            RegisterFeature({Phase::publish, "PublishSpeedupBugFix", &PublishSpeedupBugFix});
            RegisterFeature({Phase::publish, "PublishWantedLevelCrashFix", &PublishWantedLevelCrashFix});
            RegisterFeature({Phase::publish, "PublishSuperLODFix", &PublishSuperLODFix});
            RegisterFeature({Phase::publish, "PublishCrashMsgFix", &PublishCrashMsgFix});
            RegisterFeature({Phase::publish, "PublishBenchmarkFix", &PublishBenchmarkFix});
            RegisterFeature({Phase::publish, "PublishGarageModelFix", &PublishGarageModelFix});
            RegisterFeature({Phase::publish, "PublishOutOfCommissionFix", &PublishOutOfCommissionFix});
            RegisterFeature({Phase::install, "InstallAimProjectileCrashFix", &InstallAimProjectileCrashFix});
            RegisterFeature({Phase::install, "InstallBadPlayerModelCrashFix", &InstallBadPlayerModelCrashFix, {"PublishBadPlayerModelCrashFix"}});
            RegisterFeature({Phase::install, "InstallBlackscreenFix", &InstallBlackscreenFix, {"PublishBlackscreenFix"}});
            RegisterFeature({Phase::install, "InstallCollisionDeadlockFix", &InstallCollisionDeadlockFix, {"PublishCollisionDeadlockFix"}});
            RegisterFeature({Phase::install, "InstallCombinedRadioFreezeFix", &InstallCombinedRadioFreezeFix});
            RegisterFeature({Phase::install, "InstallEpisodeSwitchFreezeFix", &InstallEpisodeSwitchFreezeFix, {"PublishEpisodeSwitchFreezeFix"}});
            RegisterFeature({Phase::install, "InstallGroupHackFix", &InstallGroupHackFix, {"PublishGroupHackFix"}});
            RegisterFeature({Phase::install, "InstallMPNikoCrashFix", &InstallMPNikoCrashFix});
            RegisterFeature({Phase::install, "InstallPathNodeCrashFix", &InstallPathNodeCrashFix, {"PublishPathNodeCrashFix"}});
            RegisterFeature({Phase::install, "InstallPedModelsInCutscenesFix", &InstallPedModelsInCutscenesFix, {"PublishPedModelsInCutscenesFix"}});
            RegisterFeature({Phase::install, "InstallPlaneCrashFix", &InstallPlaneCrashFix, {"PublishPlaneCrashFix"}});
            RegisterFeature({Phase::install, "InstallPlayerAsPedComponentsFix", &InstallPlayerAsPedComponentsFix, {"PublishPlayerAsPedComponentsFix"}});
            RegisterFeature({Phase::install, "InstallSMPA60Fix", &InstallSMPA60Fix, {"PublishSMPA60Fix"}});
            RegisterFeature({Phase::install, "InstallSpeedupBugFix", &InstallSpeedupBugFix, {"PublishSpeedupBugFix"}});
            RegisterFeature({Phase::install, "InstallTryToSkipAllErrors", &InstallTryToSkipAllErrors});
            RegisterFeature({Phase::install, "InstallWantedLevelCrashFix", &InstallWantedLevelCrashFix, {"PublishWantedLevelCrashFix"}});
            RegisterFeature({Phase::install, "InstallSuperLODFix", &InstallSuperLODFix, {"PublishSuperLODFix"}});
            RegisterFeature({Phase::install, "InstallCrashMsgFix", &InstallCrashMsgFix, {"PublishCrashMsgFix"}});
            RegisterFeature({Phase::install, "InstallBenchmarkFix", &InstallBenchmarkFix, {"PublishBenchmarkFix"}});
            RegisterFeature({Phase::install, "InstallGarageModelFix", &InstallGarageModelFix, {"PublishGarageModelFix"}});
            RegisterFeature({Phase::install, "InstallOutOfCommissionFix", &InstallOutOfCommissionFix, {"PublishOutOfCommissionFix"}});
        }
    } stabilityFeatures;
} // namespace

} // namespace opp::features
