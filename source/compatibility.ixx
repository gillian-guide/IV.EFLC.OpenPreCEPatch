// SPDX-License-Identifier: GPL-3.0-or-later
//
// compatibility
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"
#include "steam_achievements.hxx"

export module opp.compatibility;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;

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

        // The recovered handler wrote per-build RVAs. Each site is now the
        // pre-write instruction window at that RVA (research 08 §3.2), so a build
        // whose bytes do not match installs nothing instead of writing a stale
        // address. The build gate stays because several windows also resolve on
        // builds the original did not patch here.
        //
        // Every site is matched before the first write: the seven adjacent
        // one-byte NOPs live inside each other's windows, so a scan after a write
        // would silently lose the neighbours (the double-scan defect).
        // Destination deltas from a scan to the trampoline the recovered jumps
        // target: +0x87 skips the whole check (1.0.6.0 0x003982->0x003A09,
        // 1.0.7.0 0x003962->0x0039E9) and +0x12 is 1.0.7.0's shorter skip
        // (0x00389C->0x0038AE). Both are the same on every gated build.
        constexpr uintptr_t kModCheckJumpDelta = 0x87;
        constexpr uintptr_t kModCheckShortJumpDelta = 0x12;
        // A cleared flag byte (the recovery's "the check is off" value).
        constexpr uint8_t kFlagOff = 0x00;
        // The modded-file check instruction run carries seven one-byte stores.
        constexpr size_t kModCheckNopCount = 7;
        const auto findSite = [&image](const char* signature) -> uint8_t* {
            auto pattern = Pattern(image, signature);
            return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
        };

        // Patch 6 (1060) modded-file checks: one jump over the check, two flag
        // bytes cleared, seven operand bytes NOPed, two short jumps forced.
        if (ctx.build == GameBuild::v1060)
        {
            uint8_t* const jumpOverCheck =
                findSite("75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8");
            uint8_t* const clearA =
                findSite("01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74");
            uint8_t* const clearB =
                findSite("01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8");
            // The seven NOPs are consecutive bytes of one instruction run; the
            // first byte is the anchor and the run is match+0..match+6.
            uint8_t* const nopRun =
                findSite("C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8");
            uint8_t* const shortJumpA =
                findSite("74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68");
            uint8_t* const shortJumpB =
                findSite("74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F");

            if (jumpOverCheck != nullptr)
                JumpTo(ctx, jumpOverCheck, jumpOverCheck + kModCheckJumpDelta);
            if (clearA != nullptr)
                WriteValue<uint8_t>(ctx, clearA, kFlagOff);
            if (clearB != nullptr)
                WriteValue<uint8_t>(ctx, clearB, kFlagOff);
            if (nopRun != nullptr)
            {
                for (size_t i = 0; i < kModCheckNopCount; ++i)
                    NopAt(ctx, nopRun + i, 1);
            }
            if (shortJumpA != nullptr)
                WriteValue<uint8_t>(ctx, shortJumpA, opp::ops::kJmpRel8);
            if (shortJumpB != nullptr)
                WriteValue<uint8_t>(ctx, shortJumpB, opp::ops::kJmpRel8);
        }

        // Patch 7 (1070) modded-file checks: the same shape with two jumps, and
        // its own RVAs/signatures.
        if (ctx.build == GameBuild::v1070)
        {
            uint8_t* const jumpOverCheckA =
                findSite("75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D");
            uint8_t* const jumpOverCheckB =
                findSite("75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8");
            uint8_t* const clearA =
                findSite("01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74");
            uint8_t* const clearB =
                findSite("01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8");
            // Same run shape as 1.0.6.0, one build-earlier RVA.
            uint8_t* const nopRun =
                findSite("C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8");
            uint8_t* const shortJumpA =
                findSite("74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68");
            uint8_t* const shortJumpB =
                findSite("74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F");

            if (jumpOverCheckA != nullptr)
                JumpTo(ctx, jumpOverCheckA, jumpOverCheckA + kModCheckShortJumpDelta);
            if (jumpOverCheckB != nullptr)
                JumpTo(ctx, jumpOverCheckB, jumpOverCheckB + kModCheckJumpDelta);
            if (clearA != nullptr)
                WriteValue<uint8_t>(ctx, clearA, kFlagOff);
            if (clearB != nullptr)
                WriteValue<uint8_t>(ctx, clearB, kFlagOff);
            if (nopRun != nullptr)
            {
                for (size_t i = 0; i < kModCheckNopCount; ++i)
                    NopAt(ctx, nopRun + i, 1);
            }
            if (shortJumpA != nullptr)
                WriteValue<uint8_t>(ctx, shortJumpA, opp::ops::kJmpRel8);
            if (shortJumpB != nullptr)
                WriteValue<uint8_t>(ctx, shortJumpB, opp::ops::kJmpRel8);
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
                // The 1.0.8.0 site (RVA 0x498460) has its own instruction
                // sequence; scanning it keeps the same fail-safe (a build that
                // does not match installs nothing) instead of writing a bare RVA.
                if (auto fallback = Pattern(image, "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 ?? ?? 8B 0D"); fallback.size() > 0)
                {
                    JumpToBody(ctx, fallback.get(0).get<uint8_t>(), "abi_rgsc_login_removed");
                }
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
                // The standalone ASI flips xlive.dll+0xF1605 (988677) from its
                // jz or jmp to a jmp. Keep the same pre-image check so a
                // different xlive build is left untouched. The site is inside
                // xlive.dll, so the write goes through the module-scoped guard.
                constexpr uintptr_t kXliveProtectionBranchRva = 0xF1605;
                auto* site = reinterpret_cast<uint8_t*>(xlive) + kXliveProtectionBranchRva;
                const uint8_t current = *site;
                if (current == opp::ops::kJzRel8 || current == opp::ops::kJmpRel8)
                    WriteModuleValue<uint8_t>(ctx, xlive, site, opp::ops::kJmpRel8);
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
            RegisterFeature({Phase::publish, "PublishRemoveModChecks", &PublishRemoveModChecks});
            RegisterFeature({Phase::install, "InstallAllowHashesForIDE", &InstallAllowHashesForIDE});
            RegisterFeature({Phase::install, "InstallRemoveModChecks", &InstallRemoveModChecks, {"PublishRemoveModChecks"}});
            RegisterFeature({Phase::install, "InstallRemoveRGSCLoginRequirement", &InstallRemoveRGSCLoginRequirement});
            RegisterFeature({Phase::install, "InstallSavegameFix", &InstallSavegameFix});
            RegisterFeature({Phase::install, "InstallGFWLProtectionDisabler", &InstallGFWLProtectionDisabler});
            RegisterFeature({Phase::install, "InstallSteamAchievements", &InstallSteamAchievements});
        }
    } compatibilityFeatures;
} // namespace

} // namespace opp::features
