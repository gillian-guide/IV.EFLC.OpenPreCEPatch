// SPDX-License-Identifier: GPL-3.0-or-later
//
// The option pass: one function per INI arm, called in the original loader's
// order. The order matters because some arms publish resume addresses before
// another arm overwrites the site that owns them.

#include "patches.hxx"

#include "feature_registry.hxx"
import opp.game;
#include "helpers.hxx"
import opp.log;
#include "pattern.hxx"
#include "rebased.h"

#include <cstring>
#include <optional>
#include <vector>

import opp.common;
import opp.settings;

import opp.stability;
import opp.performance;
import opp.graphics;
import opp.vehicles;
import opp.input;
import opp.multiplayer;
import opp.loading;
import opp.compatibility;
import opp.interface;

namespace opp
{
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer)
    {
        PatchContext ctx{image, reinterpret_cast<uintptr_t>(image.base), build, config};

        const std::vector<Feature>& features = RegisteredFeatures();
        size_t publishes = 0;
        size_t installs = 0;
        for (const Feature& feature : features)
        {
            if (feature.phase == Phase::publish)
                ++publishes;
            else if (feature.phase == Phase::install)
                ++installs;
        }
        Log("order: %zu features (%zu publish, %zu install)", features.size(), publishes,
            installs);
        if (features.empty())
        {
            // ComputeOrder refuses an invalid graph; installing nothing is the
            // fail-closed outcome and the reason is already in the log.
            Log("install: no arms ran");
            return;
        }

        std::optional<Phase> currentPhase;
        size_t phaseApplied = 0;
        size_t phaseSkipped = 0;

        for (const Feature& feature : features)
        {
            // One line per phase boundary plus one per phase end: a crash in the
            // install pass leaves the phase visible, not just the last arm.
            if (!currentPhase || feature.phase != *currentPhase)
            {
                if (currentPhase)
                {
                    Log("phase %s: %zu applied, %zu skipped", PhaseName(*currentPhase),
                        ArmWritesApplied() - phaseApplied,
                        ArmWritesSkipped() - phaseSkipped);
                }
                currentPhase = feature.phase;
                phaseApplied = ArmWritesApplied();
                phaseSkipped = ArmWritesSkipped();
                Log("phase %s", PhaseName(feature.phase));
            }

            if (observer.begin != nullptr)
                observer.begin(observer.user, feature.name);
            feature.install(ctx);
            if (observer.end != nullptr)
                observer.end(observer.user, feature.name);
        }
        if (currentPhase)
        {
            Log("phase %s: %zu applied, %zu skipped", PhaseName(*currentPhase),
                ArmWritesApplied() - phaseApplied,
                ArmWritesSkipped() - phaseSkipped);
        }

        // The post step the original runs after its arm sequence. It is not a
        // registered arm (the launcher write is gated by a process probe, not by
        // the arm schedule), but it is bracketed for the dump so its writes are
        // attributed and covered by the matrix.
        if (observer.begin != nullptr)
            observer.begin(observer.user, "ApplyLaunchWithPlayGTAIV");
        ApplyLaunchWithPlayGTAIV(ctx);
        if (observer.end != nullptr)
            observer.end(observer.user, "ApplyLaunchWithPlayGTAIV");

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }

    namespace
    {
        // -1 = ask the process (or the dump's env override), 0/1 = forced.
        int g_launcherProbeOverride = -1;
    }

    void SetLauncherProbeOverride(bool forced) noexcept
    {
        g_launcherProbeOverride = forced ? 1 : 0;
    }

    bool LauncherProbe() noexcept
    {
        if (g_launcherProbeOverride >= 0)
            return g_launcherProbeOverride != 0;
#if defined(OPCEP_DUMP_TOOL)
        // The offline harness has no PlayGTAIV.exe process; the parity driver
        // pins the probe so the launcher writes stay observable.
        char value[8]{};
        const DWORD length =
            GetEnvironmentVariableA("OPCEP_LAUNCHER_PROBE", value, sizeof(value));
        return length > 0 && value[0] == '1';
#else
        return FindGameProcess() != 0;
#endif
    }

    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx)
    {
        // The original handler installs this on five builds only; the string
        // exists twice on each of them (and once on the EFLC builds, which the
        // original never patched, so the gate stays).
        if (!ctx.Option("LaunchWithPlayGTAIV", "0"))
            return;
        if (!BuildIs(ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1060,
                                 GameBuild::v1070, GameBuild::v1080}))
            return;
        if (!LauncherProbe())
            return;

        // Exactly the game's launcher-name string ("LaunchGTAIV.exe\0"); rows
        // LaunchName in tests/parity/address_sites.tsv prove two hits per gated
        // build. The replacement is the 16 bytes the original strcpy_s leaves in
        // the slot: "PlayGTAIV.exe\0" followed by the surviving "e\0" of the old
        // name.
        constexpr char kLaunchNameSignature[] =
            "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";
        constexpr char kLauncherNamePatch[16] = {
            'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};
        // The original's handler writes exactly its two table slots. Requiring
        // the exact count keeps a modded image with a third "LaunchGTAIV.exe\0"
        // untouched instead of writing a slot the original never wrote.
        constexpr size_t kLauncherSlotCount = 2;

        std::vector<const uint8_t*> matches;
        FindAllPatterns({ctx.image.base, ctx.image.size}, kLaunchNameSignature, matches);
        if (matches.size() != kLauncherSlotCount)
        {
            Log("launcher: %zu LaunchGTAIV name slot(s), expected %zu: skipped",
                matches.size(), kLauncherSlotCount);
            return;
        }
        for (const uint8_t* site : matches)
            WriteRaw(ctx, site, kLauncherNamePatch, sizeof(kLauncherNamePatch));
        Log("launcher: %zu PlayGTAIV name slot(s) patched", matches.size());
    }
} // namespace opp
