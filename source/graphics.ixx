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

        // BuildingAlphaFix: 1 literal, 1 siginstall
        if (ctx.Option("BuildingAlphaFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 84"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                if (InImage(image, reinterpret_cast<uintptr_t>(match + 7)))
                    injector::MakeJMP(match + 7, reinterpret_cast<void*>(BranchTarget(match, 7)), true); //jmp
            }

            if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 85"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match, 13, true); //nop
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
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(2), 0x00u, true); //patch
                }
            }

            if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_building_dynamic_shadows")), true); //jmp
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

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_car_decals_fix")), true); //jmp
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

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_carparts_shadow_2")), true); //jmp
            }

            if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_carparts_shadow_2b")), true); //jmp
            }

            if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match + 3, reinterpret_cast<void*>(match + 30), true); //jmp
            }

            {
                if (auto pattern = Pattern(image, "83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04"); pattern.size() > 0)
                {
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(6), 0xEBu, true); //jmp
                }
            }

            {
                if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                {
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(2), 0x00u, true); //patch
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
                    injector::WriteMemoryRaw(match, bytes, sizeof(bytes), true); //write
                }

                if (auto pattern = Pattern(image, "51 68 04 05 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    injector::WriteMemory<uint32_t>(match + 2, static_cast<uint32_t>(0x100u), true); //patch
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

                injector::MakeNOP(match + 62, 6, true); //nop
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

                injector::MakeJMP(match - 6, reinterpret_cast<void*>(Abi("abi_carparts_shadow")), true); //jmp
            }

            if (auto pattern = Pattern(image, "485 DB 7E 41 55 EB 0A"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 6, reinterpret_cast<void*>(Abi("abi_carparts_shadow")), true); //jmp
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
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(8), 0xEBu, true); //jmp
                }
            }

            {
                if (auto pattern = Pattern(image, "83 FA 04 75 0C"); pattern.size() > 0)
                {
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(3), 0xEBu, true); //jmp
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

                injector::MakeCALL(match - 5, reinterpret_cast<void*>(Abi("abi_cutscene_chain_b")), true); //call
            }

            if (auto pattern = Pattern(image, "8B CF 88 44 24 0F E8 ? ? ? ? A1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match - 5, reinterpret_cast<void*>(Abi("abi_cutscene_chain_b")), true); //call
            }

            {
                // 1080 has exactly one match of the primary pattern (0xC9A840); the
                // alternate is only for builds where it is absent. Falling back on
                // "<= 1 match" threw that single match away and applied nothing.
                auto pattern = Pattern(image, "74 24 8B 44 24 2C 83 F8 03");
                if (pattern.size() == 0)
                    pattern = Pattern(image, "74 0B 83 7C 24 2C 03");
                if (pattern.size() > 0)
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(), 0xEBu, true); //jmp
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

        // DualVehicleHeadlights
        if (ctx.Option("DualVehicleHeadlights", "1"))
        {
        }

    }


    // DualVehicleHeadlightsOld

    void InstallDualVehicleHeadlightsOld(PatchContext& ctx)
    {

        TraceArm("DualVehicleHeadlightsOld");

        // DualVehicleHeadlights@old: 1 siginstall
        if (ctx.Option("DualVehicleHeadlights", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_dual_headlights_old")), true); //jmp
            }
        }

    }


    // DualVehicleHeadlightsNew

    void InstallDualVehicleHeadlightsNew(PatchContext& ctx)
    {

        TraceArm("DualVehicleHeadlightsNew");

        // DualVehicleHeadlights@new: 1 siginstall
        if (ctx.Option("DualVehicleHeadlights", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_dual_headlights_new")), true); //jmp
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

                injector::MakeJMP(match - 24, reinterpret_cast<void*>(Abi("abi_emissive_lerp_shim")), true); //jmp
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

                injector::WriteMemory<uint8_t>(match + 6, static_cast<uint8_t>(0xEBu), true); //jmp
                injector::MakeJMP(match + 95, reinterpret_cast<void*>(match + 476), true); //jmp
                injector::WriteMemory<uint8_t>(match + 379, static_cast<uint8_t>(0xEBu), true); //jmp
                injector::WriteMemory<uint32_t>(match + 211, AddressOf(&zpatch_fchs_offset), true); //patch
            }

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_fchs_shim")), true); //jmp
            }

            {
                if (auto pattern = Pattern(image, "8D 4C 24 50 51 68 04 05 00 00"); pattern.size() > 0)
                {
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(6), 0x04u, true); //patch
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(7), 0x05u, true); //patch
                }
            }

            {
                if (auto pattern = Pattern(image, "8D 44 24 50 50 68 04 01 00 00"); pattern.size() > 0)
                {
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(6), 0x04u, true); //patch
                    injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(7), 0x01u, true); //patch
                }
            }

            {
                if (auto pattern = Pattern(image, "F3 0F 11 44 24 1C 0F 84 9E 00 00 00"); pattern.size() > 0)
                {
                    auto* site = pattern.get(0).get<uint8_t>(6);
                    injector::WriteMemory<uint32_t>(site, 0x009E840Fu, true); //jz rel32
                    injector::WriteMemory<uint8_t>(site + 4, 0x00u, true); //jz rel32
                }
            }
        }

    }


    // ForceDynamicShadowsEverywhere

    void InstallForceDynamicShadowsEverywhere(PatchContext& ctx)
    {

        TraceArm("ForceDynamicShadowsEverywhere");

        // ForceDynamicShadowsEverywhere: 1 literal
        if (ctx.Option("ForceDynamicShadowsEverywhere", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F7 46 4C 00 80 00 00 74"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match + 7, 2, true); //nop
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

                injector::WriteMemory<uint8_t>(match + 7, 0xEBu, true); //jmp
            }
        }

    }


    // publish HighQualityReflections

    void PublishHighQualityReflections(PatchContext& ctx)
    {

        TraceArm("publish HighQualityReflections");

        // HighQualityReflections: 2 slots
        if (ctx.Option("HighQualityReflections", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
            {
                zpatch_hqr_resume = MatchAddress(pattern, 19);
                zpatch_hqr_builder = MatchTarget(pattern, 15, 19);
            }
        }

    }


    // HighQualityReflections

    void InstallHighQualityReflections(PatchContext& ctx)
    {

        TraceArm("HighQualityReflections");

        // HighQualityReflections: 1 siginstall
        if (ctx.Option("HighQualityReflections", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_hqr_body")), true); //jmp
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
                    injector::WriteMemory<uint16_t>(pattern.get(0).get<uint8_t>(3), 0x2AEBu, true); //patch
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
                    injector::MakeNOP(pattern.get(0).get<uint8_t>(), kWordLength, true); //nop
                    if (pattern.size() > 1)
                        injector::MakeNOP(pattern.get(1).get<uint8_t>(), kWordLength, true); //nop
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

                injector::MakeNOP(match, kWordLength, true); //nop
            }
        }

    }


    // RemoveBoundingBoxCulling

    void InstallRemoveBoundingBoxCulling(PatchContext& ctx)
    {

        TraceArm("RemoveBoundingBoxCulling");

        // RemoveBoundingBoxCulling: 1 siginstall
        if (ctx.Option("RemoveBoundingBoxCulling", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "3C 01 57 0F 85"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                if (InImage(image, reinterpret_cast<uintptr_t>(match + 3)))
                    injector::MakeJMP(match + 3, reinterpret_cast<void*>(BranchTarget(match, 3)), true); //jmp
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

                injector::WriteMemory<uint8_t>(match + 6, 0xEBu, true); //jmp
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

                injector::WriteMemory<uint8_t>(match + 6, 0xEBu, true); //jmp
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

                injector::WriteMemory<uint8_t>(match + 8, 0x60u, true); //patch
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
            RegisterFeature(43, "PublishHighQualityReflections", &PublishHighQualityReflections);
            RegisterFeature(44, "PublishCutsceneFixes", &PublishCutsceneFixes);
            RegisterFeature(53, "InstallBuildingAlphaFix", &InstallBuildingAlphaFix);
            RegisterFeature(54, "InstallBuildingDynamicShadows", &InstallBuildingDynamicShadows);
            RegisterFeature(58, "InstallDualVehicleHeadlights", &InstallDualVehicleHeadlights);
            RegisterFeature(59, "InstallEmissiveLerpFix", &InstallEmissiveLerpFix);
            RegisterFeature(63, "InstallForceDynamicShadowsEverywhere", &InstallForceDynamicShadowsEverywhere);
            RegisterFeature(64, "InstallForceShadowsOnObjects", &InstallForceShadowsOnObjects);
            RegisterFeature(70, "InstallInfiniteCarBottomDistance", &InstallInfiniteCarBottomDistance);
            RegisterFeature(92, "InstallRemoveBoundingBoxCulling", &InstallRemoveBoundingBoxCulling);
            RegisterFeature(94, "InstallRemoveStaticCarShadows", &InstallRemoveStaticCarShadows);
            RegisterFeature(95, "InstallRemoveStaticCutsceneCarShadows", &InstallRemoveStaticCutsceneCarShadows);
            RegisterFeature(97, "InstallReversingLightFix", &InstallReversingLightFix);
            RegisterFeature(115, "InstallCarPartsShadowFix", &InstallCarPartsShadowFix);
            RegisterFeature(116, "InstallForceCarHeadlightShadows", &InstallForceCarHeadlightShadows);
            RegisterFeature(119, "InstallCarDynamicShadowFix", &InstallCarDynamicShadowFix);
            RegisterFeature(126, "InstallHighQualityReflections", &InstallHighQualityReflections);
            RegisterFeature(127, "InstallCutsceneFixes", &InstallCutsceneFixes);
            RegisterFeature(129, "InstallCarDecalsFix", &InstallCarDecalsFix);
            RegisterFeature(131, "InstallCarDynamicShadowFixNoE", &InstallCarDynamicShadowFixNoE);
            RegisterFeature(132, "InstallDualVehicleHeadlightsOld", &InstallDualVehicleHeadlightsOld);
            RegisterFeature(133, "InstallDualVehicleHeadlightsNew", &InstallDualVehicleHeadlightsNew);
            RegisterFeature(137, "InstallCarPartsShadowFixStandalone", &InstallCarPartsShadowFixStandalone);
            RegisterFeature(140, "InstallNoImpostorHeightCutoff", &InstallNoImpostorHeightCutoff);
            RegisterFeature(144, "InstallNoLODLightHeightCutoff", &InstallNoLODLightHeightCutoff);
        }
    } graphicsFeatures;
} // namespace

} // namespace opp::features
