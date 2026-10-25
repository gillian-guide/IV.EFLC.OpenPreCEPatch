// SPDX-License-Identifier: GPL-3.0-or-later
//
// multiplayer
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

export module multiplayer;

import common;
import settings;

namespace opp::features
{

    // BetterMPSync

    void InstallBetterMPSync(PatchContext& ctx)
    {

        TraceArm("BetterMPSync");

        // BetterMPSync: 1 siginstall
        if (ctx.Option("BetterMPSync", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 6, reinterpret_cast<void*>(Abi("abi_better_mp_sync")), true); //call
            }
        }

    }


    // publish FreezeCarFix

    void PublishFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeCarFix");

        // FreezeCarFix: 12 slots
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                zpatch_spawns_orig_a = MatchTarget(pattern, 8, 12);
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                zpatch_spawns_orig_b = MatchTarget(pattern, 12, 16);
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                zpatch_spawns_case9_resume = MatchTarget(pattern, 247, 251);
                zpatch_spawns_case6_resume = MatchTarget(pattern, 181, 185);
                zpatch_spawns_case1_resume = MatchTarget(pattern, 62, 66);
                zpatch_spawns_case2_resume = MatchTarget(pattern, 87, 91);
                zpatch_spawns_case4_resume = MatchTarget(pattern, 137, 141);
                zpatch_spawns_case7_resume = MatchTarget(pattern, 203, 207);
                zpatch_spawns_case3_resume = MatchTarget(pattern, 112, 116);
                zpatch_spawns_case5_resume = MatchTarget(pattern, 159, 163);
                zpatch_spawns_case8_resume = MatchTarget(pattern, 225, 229);
                zpatch_spawns_case0_resume = MatchTarget(pattern, 37, 41);
            }
        }

    }


    // FreezeCarFix

    void InstallFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("FreezeCarFix");

        // FreezeCarFix: 7 siginstalls
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 7, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 11, reinterpret_cast<void*>(Abi("abi_spawns_guard")), true); //call
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 36, reinterpret_cast<void*>(Abi("abi_spawns_case_0x24")), true); //call
                injector::MakeCALL(match + 61, reinterpret_cast<void*>(Abi("abi_spawns_case_0x3D")), true); //call
                injector::MakeCALL(match + 86, reinterpret_cast<void*>(Abi("abi_spawns_case_0x56")), true); //call
                injector::MakeCALL(match + 111, reinterpret_cast<void*>(Abi("abi_spawns_case_0x6F")), true); //call
                injector::MakeCALL(match + 136, reinterpret_cast<void*>(Abi("abi_spawns_case_0x88")), true); //call
                injector::MakeCALL(match + 158, reinterpret_cast<void*>(Abi("abi_spawns_case_0x9E")), true); //call
                injector::MakeCALL(match + 180, reinterpret_cast<void*>(Abi("abi_spawns_case_0xB4")), true); //call
                injector::MakeCALL(match + 202, reinterpret_cast<void*>(Abi("abi_spawns_case_0xCA")), true); //call
                injector::MakeCALL(match + 224, reinterpret_cast<void*>(Abi("abi_spawns_case_0xE0")), true); //call
                injector::MakeCALL(match + 246, reinterpret_cast<void*>(Abi("abi_spawns_case_0xF6")), true); //call
            }

            if (auto pattern = Pattern(image, "88 87 40 01 00 00 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 6, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
            }

            if (auto pattern = Pattern(image, "88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 9, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
                injector::MakeCALL(match + 254, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
            }

            if (auto pattern = Pattern(image, "38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 9, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
            }

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 7, reinterpret_cast<void*>(Abi("abi_spawns_probe")), true); //call
            }
        }

    }


    // publish FreezeGunFix

    void PublishFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeGunFix");

        // FreezeGunFix: 1 slot
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
            }
        }

    }


    // FreezeGunFix

    void InstallFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("FreezeGunFix");

        // FreezeGunFix: 1 siginstall
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match - 21, reinterpret_cast<void*>(Abi("abi_freeze_gun_shim")), true); //jmp
            }
        }

    }


    // RemoveAllWeaponsProtection

    void InstallRemoveAllWeaponsProtection(PatchContext& ctx)
    {

        TraceArm("RemoveAllWeaponsProtection");

        // RemoveAllWeaponsProtection: 1 literal
        if (ctx.Option("RemoveAllWeaponsProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match + 5, 12, true); //nop
            }
        }

    }


    // TeleportProtection

    void InstallTeleportProtection(PatchContext& ctx)
    {

        TraceArm("TeleportProtection");

        // TeleportProtection: 1 literal
        if (ctx.Option("TeleportProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint16_t>(match, 0x08C2u, true); //ret 8
                injector::WriteMemory<uint8_t>(match + 2, 0x00u, true); //ret 8
            }
        }

    }

namespace
{
    class MultiplayerFeatures
    {
    public:
        MultiplayerFeatures()
        {
            RegisterFeature(9, "PublishFreezeGunFix", &PublishFreezeGunFix);
            RegisterFeature(35, "PublishFreezeCarFix", &PublishFreezeCarFix);
            RegisterFeature(49, "InstallBetterMPSync", &InstallBetterMPSync);
            RegisterFeature(65, "InstallFreezeGunFix", &InstallFreezeGunFix);
            RegisterFeature(91, "InstallRemoveAllWeaponsProtection", &InstallRemoveAllWeaponsProtection);
            RegisterFeature(106, "InstallTeleportProtection", &InstallTeleportProtection);
            RegisterFeature(118, "InstallFreezeCarFix", &InstallFreezeCarFix);
        }
    } multiplayerFeatures;
} // namespace

} // namespace opp::features
