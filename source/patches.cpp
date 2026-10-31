// SPDX-License-Identifier: GPL-3.0-or-later
//
// The option pass: one function per INI arm, called in the original loader's
// order. The order matters because some arms publish resume addresses before
// another arm overwrites the site that owns them.

#include "patches.hxx"

#include "feature_registry.hxx"
#include "helpers.hxx"
#include "log.hxx"
#include "rebased.h"

import common;
import settings;

import stability;
import performance;
import graphics;
import vehicles;
import input;
import multiplayer;
import loading;
import compatibility;
import interface;

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

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }
} // namespace opp
