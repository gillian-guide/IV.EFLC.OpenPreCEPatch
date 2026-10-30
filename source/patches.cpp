// SPDX-License-Identifier: GPL-3.0-or-later
//
// The option pass: one function per INI arm, called in the original loader's
// order. The order matters because some arms publish resume addresses before
// another arm overwrites the site that owns them.

#include "patches.hxx"

#include "feature_registry.hxx"
#include "helpers.hxx"
#include "improvements.hxx"
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

        // The improvements are folded into the original options. This block
        // publishes the resolved runtime switches once, before any arm runs:
        // an original option selects its improvement, and the test-only
        // OPCEP_IMPROVEMENTS environment variable / OPCEP_PARITY_BASELINE build
        // can turn groups back off without touching the INI.
        const uint32_t improvements = EnabledImprovements();
        const auto has = [improvements](Improvement group) {
            return (improvements & Improve(group)) != 0u;
        };
        const bool moving_parts =
            config.Option("MovingSteeringWheelSupport", "1") ||
            config.Option("ShakingEngineBlock", "1") ||
            config.Option("MovingPlaneParts", "1");
        const bool model_lookup =
            config.Option("PopupHeadlightsFlag", "1") ||
            config.Option("ForceCarHeadlightShadows", "0");

        zpatch_opt_new_saves_guards =
            has(Improvement::new_saves) &&
                    config.Option("NewSavesCompatibility", "1")
                ? 1u
                : 0u;
        zpatch_opt_new_saves_seek = zpatch_opt_new_saves_guards;
        zpatch_opt_ooc_safe =
            has(Improvement::ooc) && config.Option("OutOfCommissionFix", "1") ? 1u : 0u;
        zpatch_opt_ooc_cache = zpatch_opt_ooc_safe;
        zpatch_opt_component_guards =
            has(Improvement::vehicle) && moving_parts ? 1u : 0u;
        zpatch_opt_steer_cache = zpatch_opt_component_guards;
        zpatch_opt_model_lookup_guards =
            has(Improvement::popup) && model_lookup ? 1u : 0u;
        zpatch_opt_popup_cache = zpatch_opt_model_lookup_guards;
        zpatch_opt_model_index =
            has(Improvement::model_index) &&
                    (config.Option("BadPlayerModelCrashFix", "1") ||
                     config.Option("DebugPrintSpawns", "0"))
                ? 1u
                : 0u;
        zpatch_opt_skip_intro_screens =
            has(Improvement::skip_intro) && config.Option("SkipIntro", "1") ? 1u : 0u;
        zpatch_opt_trace_arms = has(Improvement::trace) ? 1u : 0u;
        zpatch_opt_windowed = has(Improvement::windowed) ? 1u : 0u;

        for (const Feature& feature : RegisteredFeatures())
        {
            if (observer.begin != nullptr)
                observer.begin(observer.user, feature.name);
            feature.install(ctx);
            if (observer.end != nullptr)
                observer.end(observer.user, feature.name);
        }

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }
} // namespace opp
