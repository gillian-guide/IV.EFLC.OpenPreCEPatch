// SPDX-License-Identifier: GPL-3.0-or-later
//
// compatibility
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
#include "steam_achievements.hxx"

export module compatibility;

import common;
import settings;

namespace opp::features
{

    // AllowHashesForIDE

    void InstallAllowHashesForIDE(PatchContext& ctx)
    {

        TraceArm("AllowHashesForIDE");

        // AllowHashesForIDE: 1 sigjump
        if (ctx.Option("AllowHashesForIDE", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 4C 24 08 56 8B 74 24 08 80 3E 22"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_game_hash")), true); //jmp
            }
        }

    }


    // RemoveModChecks / RemoveGFWLUpdatePopup (patch 4 and patch 6/7 modded-file
    // checks plus the pre-patch-6 GFWL update popup).

    namespace
    {
        constexpr const char* kGfwlStubPattern =
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08";

        bool ModChecksEnabled(const PatchContext& ctx)
        {
            // RemoveModChecks is the unified key. The old per-patch keys stay
            // readable as aliases, so an existing INI keeps working without the
            // user having to rewrite it.
            return ctx.Option("RemoveModChecks", "0") ||
                   ctx.Option("Patch4RemoveModChecks", "0") ||
                   ctx.Option("Patch6and7RemoveModChecks", "0") ||
                   ctx.Option("Patch7RemoveModChecks", "0");
        }

        bool PrePatch6(GameBuild build) noexcept
        {
            return BuildIs(build, {GameBuild::v1000, GameBuild::v1004, GameBuild::v1040});
        }

        bool WantsGfwlStub(const PatchContext& ctx)
        {
            // Patch 4: the modded-file check is the RemoveModChecks half.
            if (ctx.build == GameBuild::v1004 && ModChecksEnabled(ctx))
                return true;
            // Pre-patch-6: the update/sign-out popup removal half.
            if (PrePatch6(ctx.build) && ctx.Option("RemoveGFWLUpdatePopup", "0"))
                return true;
            return false;
        }
    } // namespace

    void PublishRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("publish RemoveModChecks");
        if (!WantsGfwlStub(ctx))
            return;
        if (auto pattern = Pattern(ctx.image, kGfwlStubPattern); pattern.size() > 0)
            zpatch_gfwl_resume = MatchAddress(pattern, 12);
    }

    void InstallRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("RemoveModChecks");

        // Patch 4 / pre-6 GFWL update popup share one stub.
        if (WantsGfwlStub(ctx))
        {
            if (auto pattern = Pattern(ctx.image, kGfwlStubPattern); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_gfwl_stub")), true); //jmp
            }
        }

        if (!ModChecksEnabled(ctx))
            return;

        const GameImage& image = ctx.image;

        // Patch 6 (1060) modded-file checks.
        if (ctx.build == GameBuild::v1060)
        {
            injector::MakeJMP(image.base + 0x003982,
                reinterpret_cast<void*>(ctx.gameBase + 0x003A09), true); //jmp
            injector::WriteMemory<uint8_t>(image.base + 0x0038AC, 0x00u, true); //patch
            injector::WriteMemory<uint8_t>(image.base + 0x00397E, 0x00u, true); //patch
            injector::MakeNOP(image.base + 0x0042E7, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042E8, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042E9, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042EA, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042EB, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042EC, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042ED, 1, true); //nop
            injector::WriteMemory<uint8_t>(image.base + 0x00392E, 0xEBu, true); //jmp
            injector::WriteMemory<uint8_t>(image.base + 0x003A14, 0xEBu, true); //jmp
        }

        // Patch 7 (1070) modded-file checks.
        if (ctx.build == GameBuild::v1070)
        {
            injector::MakeJMP(image.base + 0x00389C,
                reinterpret_cast<void*>(ctx.gameBase + 0x0038AE), true); //jmp
            injector::MakeJMP(image.base + 0x003962,
                reinterpret_cast<void*>(ctx.gameBase + 0x0039E9), true); //jmp
            injector::WriteMemory<uint8_t>(image.base + 0x00388C, 0x00u, true); //patch
            injector::WriteMemory<uint8_t>(image.base + 0x00395E, 0x00u, true); //patch
            injector::MakeNOP(image.base + 0x0042C7, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042C8, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042C9, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042CA, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042CB, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042CC, 1, true); //nop
            injector::MakeNOP(image.base + 0x0042CD, 1, true); //nop
            injector::WriteMemory<uint8_t>(image.base + 0x00390E, 0xEBu, true); //jmp
            injector::WriteMemory<uint8_t>(image.base + 0x0039F4, 0xEBu, true); //jmp
        }
    }


    // RemoveRGSCLoginRequirement

    void InstallRemoveRGSCLoginRequirement(PatchContext& ctx)
    {

        TraceArm("RemoveRGSCLoginRequirement");

        // RemoveRGSCLoginRequirement: no option gate in the original. The
        // pattern handles every build; 1.0.8.0 has an RVA fallback because the
        // pattern is absent there (original handler 0x10045490).
        if (ctx.Option("RemoveRGSCLoginRequirement", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_rgsc_login_removed")), true); //jmp
            }
            else if (ctx.build == GameBuild::v1080)
            {
                injector::MakeJMP(image.base + 0x498460,
                    reinterpret_cast<void*>(Abi("abi_rgsc_login_removed")), true); //jmp
            }
        }

    }


    // SavegameFix

    void InstallSavegameFix(PatchContext& ctx)
    {

        TraceArm("SavegameFix");

        // SavegameFix: 2 siginstalls, 1 sigwrite
        if (ctx.Option("SavegameFix", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "75 ? 83 C1 01 83 C0 01 83 F9 04"); pattern.size() > 0)
                {
                    uint8_t* match = pattern.get(0).get<uint8_t>();
                    injector::MakeCALL(match - 73, reinterpret_cast<void*>(Abi("abi_savegame_fix_close")), true); //call
                    injector::MakeCALL(match - 143, reinterpret_cast<void*>(Abi("abi_savegame_fix_copy")), true); //call
                    injector::MakeCALL(match - 176, reinterpret_cast<void*>(Abi("abi_savegame_fix_copy")), true); //call
                    injector::MakeCALL(match - 280, reinterpret_cast<void*>(Abi("abi_savegame_fix_mutex")), true); //call
                    injector::MakeCALL(match - 240, reinterpret_cast<void*>(Abi("abi_savegame_fix_null")), true); //call
                    injector::MakeNOP(pattern.get(0).get<uint8_t>(), 2, true); //nop
                }
            }

            if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match - 86, reinterpret_cast<void*>(Abi("abi_savegame_fix_mutex")), true); //call
                injector::MakeCALL(match - 47, reinterpret_cast<void*>(Abi("abi_savegame_fix_null")), true); //call
                injector::MakeCALL(match + 10, reinterpret_cast<void*>(Abi("abi_savegame_fix_copy")), true); //call
                injector::MakeCALL(match + 90, reinterpret_cast<void*>(Abi("abi_savegame_fix_copy")), true); //call
                injector::MakeCALL(match + 128, reinterpret_cast<void*>(Abi("abi_savegame_fix_close")), true); //call
            }
        }

    }


    // SkipLauncher

    void InstallSkipLauncher(PatchContext& ctx)
    {

        TraceArm("SkipLauncher");

        // SkipLauncher: 7 literals
        if (ctx.Option("SkipLauncher", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match, 6, true); //nop
            }

            if (auto pattern = Pattern(image, "3B 4E 18 0F 85"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match + 3, 6, true); //nop
            }

            if (auto pattern = Pattern(image, "3B 56 18 0F 85"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match + 3, 6, true); //nop
            }

            if (auto pattern = Pattern(image, "74 C4 8B 07 3B 46 18 75 BD 68"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match, 2, true); //nop
            }

            if (auto pattern = Pattern(image, "74 EC 38 5E 06 74 E7 68"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeNOP(match, 2, true); //nop
            }

            if (auto pattern = Pattern(image, "84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint8_t>(match + 223, 0xEBu, true); //jmp
            }

            if (auto pattern = Pattern(image, "8B 56 1C 3B 56 20 ? ? 6A 00 6A 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::WriteMemory<uint16_t>(match, 0xC033u, true); //patch
            }
        }

    }


    // publish SkipIntro

    void PublishSkipIntro(PatchContext& ctx)
    {

        TraceArm("publish SkipIntro");

        // SkipIntro: 1 slot
        if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
            {
                zpatch_skip_intro_match = MatchAddress(pattern);
            }
        }

    }


    // SkipIntro

    void InstallSkipIntro(PatchContext& ctx)
    {

        TraceArm("SkipIntro");

        // SkipIntro: 1 siginstall
        if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The 300 ms FusionFix-style body is part of SkipIntro now; the
                // OPCEP_IMPROVEMENTS test switch can still select the original
                // skip-outright body without an INI key.
                const char* body = zpatch_opt_skip_intro_screens != 0u
                                       ? "abi_skip_intro_screens"
                                       : "abi_skip_intro_body";
                injector::MakeJMP(match - 6, reinterpret_cast<void*>(Abi(body)), true); //jmp
            }
        }

    }


    // publish SkipMenu

    void PublishSkipMenu(PatchContext& ctx)
    {

        TraceArm("publish SkipMenu");

        // SkipMenu: 3 slots
        if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
            {
                zpatch_skip_menu_resume = MatchTarget(pattern, 24, 28);
            }

            if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
            {
                zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
            }

            if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
            {
                zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
            }
        }

    }


    // SkipMenu

    void InstallSkipMenu(PatchContext& ctx)
    {

        TraceArm("SkipMenu");

        // SkipMenu: 3 siginstalls
        if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 23, reinterpret_cast<void*>(Abi("abi_skip_menu_body")), true); //call
            }

            if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 2, reinterpret_cast<void*>(Abi("abi_skip_menu_body")), true); //call
            }

            if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeCALL(match + 2, reinterpret_cast<void*>(Abi("abi_skip_menu_body")), true); //call
            }
        }

    }


    // GFWLProtectionDisabler (standalone GFWLProtectionDisabler2019.asi)

    void InstallGFWLProtectionDisabler(PatchContext& ctx)
    {

        TraceArm("GFWLProtectionDisabler");

        if (ctx.Option("GFWLProtectionDisabler", "1"))
        {
            HMODULE xlive = GetModuleHandleA("xlive.dll");
            if (xlive != nullptr)
            {
                // The standalone ASI flips xlive.dll+0xF1605 from 0x74/0xEB to
                // 0xEB. Keep the same pre-image check so a different xlive build
                // is left untouched.
                auto* site = reinterpret_cast<uint8_t*>(xlive) + 988677;
                const uint8_t current = *site;
                if (current == 0x74 || current == 0xEB)
                    injector::WriteMemory<uint8_t>(site, 0xEBu, true);
            }
        }

    }


    // SteamAchievements (XUserWriteAchievements -> ISteamUserStats mirror)

    void InstallSteamAchievements(PatchContext& ctx)
    {

        TraceArm("SteamAchievements");

        if (ctx.Option("SteamAchievements", "1"))
            (void)InstallSteamAchievementsBridge(ctx.image);

    }


    // VRAMFix

    void InstallVRAMFix(PatchContext& ctx)
    {

        TraceArm("VRAMFix");

        // VRAMFix: 1 sigjump
        if (ctx.Option("VRAMFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 10 D9 2C 24 83 C4 1C C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                injector::MakeJMP(match, reinterpret_cast<void*>(Abi("abi_vram_fix_shim")), true); //jmp
            }
        }

    }

namespace
{
    class CompatibilityFeatures
    {
    public:
        CompatibilityFeatures()
        {
            RegisterFeature(34, "PublishRemoveModChecks", &PublishRemoveModChecks);
            RegisterFeature(47, "InstallAllowHashesForIDE", &InstallAllowHashesForIDE);
            RegisterFeature(83, "InstallRemoveModChecks", &InstallRemoveModChecks);
            RegisterFeature(93, "InstallRemoveRGSCLoginRequirement", &InstallRemoveRGSCLoginRequirement);
            RegisterFeature(100, "InstallSavegameFix", &InstallSavegameFix);
            RegisterFeature(148, "InstallGFWLProtectionDisabler", &InstallGFWLProtectionDisabler);
            RegisterFeature(149, "InstallSteamAchievements", &InstallSteamAchievements);
        }
    } compatibilityFeatures;
} // namespace

} // namespace opp::features
