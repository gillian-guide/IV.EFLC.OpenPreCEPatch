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

                JumpToBody(ctx, match, "abi_game_hash");
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
            // "Patch 4" in GTA IV's numbering is 1.0.4.0 (v1040), not 1.0.0.4
            // (v1004): the port read the INI's "patch 4" as 1.0.0.4 and gated the
            // modded-file check to the wrong build. The original installs it from
            // a pattern with no build compare (recovered rows carry no build id),
            // so the narrowed gate stays, but on the build the check exists on.
            if (ctx.build == GameBuild::v1040 && ModChecksEnabled(ctx))
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

                JumpToBody(ctx, match, "abi_gfwl_stub");
            }
        }

        if (!ModChecksEnabled(ctx))
            return;

        const GameImage& image = ctx.image;

        // Patch 6 (1060) modded-file checks.
        if (ctx.build == GameBuild::v1060)
        {
            JumpTo(ctx, image.base + 0x003982, reinterpret_cast<void*>(ctx.gameBase + 0x003A09));
            WriteValue<uint8_t>(ctx, image.base + 0x0038AC, 0x00u);
            WriteValue<uint8_t>(ctx, image.base + 0x00397E, 0x00u);
            NopAt(ctx, image.base + 0x0042E7, 1);
            NopAt(ctx, image.base + 0x0042E8, 1);
            NopAt(ctx, image.base + 0x0042E9, 1);
            NopAt(ctx, image.base + 0x0042EA, 1);
            NopAt(ctx, image.base + 0x0042EB, 1);
            NopAt(ctx, image.base + 0x0042EC, 1);
            NopAt(ctx, image.base + 0x0042ED, 1);
            WriteValue<uint8_t>(ctx, image.base + 0x00392E, 0xEBu);
            WriteValue<uint8_t>(ctx, image.base + 0x003A14, 0xEBu);
        }

        // Patch 7 (1070) modded-file checks.
        if (ctx.build == GameBuild::v1070)
        {
            JumpTo(ctx, image.base + 0x00389C, reinterpret_cast<void*>(ctx.gameBase + 0x0038AE));
            JumpTo(ctx, image.base + 0x003962, reinterpret_cast<void*>(ctx.gameBase + 0x0039E9));
            WriteValue<uint8_t>(ctx, image.base + 0x00388C, 0x00u);
            WriteValue<uint8_t>(ctx, image.base + 0x00395E, 0x00u);
            NopAt(ctx, image.base + 0x0042C7, 1);
            NopAt(ctx, image.base + 0x0042C8, 1);
            NopAt(ctx, image.base + 0x0042C9, 1);
            NopAt(ctx, image.base + 0x0042CA, 1);
            NopAt(ctx, image.base + 0x0042CB, 1);
            NopAt(ctx, image.base + 0x0042CC, 1);
            NopAt(ctx, image.base + 0x0042CD, 1);
            WriteValue<uint8_t>(ctx, image.base + 0x00390E, 0xEBu);
            WriteValue<uint8_t>(ctx, image.base + 0x0039F4, 0xEBu);
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

                JumpToBody(ctx, match, "abi_rgsc_login_removed");
            }
            else if (ctx.build == GameBuild::v1080)
            {
                JumpToBody(ctx, image.base + 0x498460, "abi_rgsc_login_removed");
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
                    CallToBody(ctx, match - 73, "abi_savegame_fix_close");
                    CallToBody(ctx, match - 143, "abi_savegame_fix_copy");
                    CallToBody(ctx, match - 176, "abi_savegame_fix_copy");
                    CallToBody(ctx, match - 280, "abi_savegame_fix_mutex");
                    CallToBody(ctx, match - 240, "abi_savegame_fix_null");
                    NopAt(ctx, pattern.get(0).get<uint8_t>(), 2);
                }
            }

            if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 86, "abi_savegame_fix_mutex");
                CallToBody(ctx, match - 47, "abi_savegame_fix_null");
                CallToBody(ctx, match + 10, "abi_savegame_fix_copy");
                CallToBody(ctx, match + 90, "abi_savegame_fix_copy");
                CallToBody(ctx, match + 128, "abi_savegame_fix_close");
            }
        }

    }


    // SkipLauncher
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
                // is left untouched. The site is inside xlive.dll, not the game
                // image, so the write goes through the module-scoped guard.
                auto* site = reinterpret_cast<uint8_t*>(xlive) + 988677;
                const uint8_t current = *site;
                if (current == 0x74 || current == 0xEB)
                    WriteModuleValue<uint8_t>(ctx, xlive, site, 0xEBu);
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
