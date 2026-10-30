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
        // The INI's `Improvements` key overrides the environment switch when it
        // is present ("1" = every group including the entry-point rewrites,
        // "0" = none, absent = the environment / the folded default).
        const std::string ini_improvements = config.String("", "Improvements", "");
        const uint32_t improvements =
            ResolveImprovements(EnabledImprovements(), ini_improvements.c_str());
        // One boot-log line naming the active groups, so a live log shows which
        // path ran (the opt-in entry-point rewrites are listed by name).
        {
            char groups[256];
            FormatImprovements(improvements, groups, sizeof(groups));
            Log("improvements: %s", groups);
        }
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
        // Runtime-pointer failsafes are not tied to one option: a body whose
        // preamble pointer or published lookup is missing continues with the
        // value the game would have had without the fix instead of faulting.
        zpatch_opt_failsafe = has(Improvement::failsafe) ? 1u : 0u;
        // Opt-in entry-point rewrite: GarageModelFix's site A moves from the
        // mid-body anchor to the function prologue (the write set changes, so
        // it is never part of `all`).
        zpatch_opt_garage_entry = has(Improvement::garage_entry) ? 1u : 0u;
        // CutsceneFixes' single-callee hook; also opt-in (it relocates the
        // callee into private memory and changes the write set).
        zpatch_opt_cutscene_entry = has(Improvement::cutscene_entry) ? 1u : 0u;
        // NewMemorySystem's init-thunk form; opt-in because it restores the
        // game's own allocator construction (memory behaviour changes).
        zpatch_opt_nms_thunk = has(Improvement::nms_thunk) ? 1u : 0u;
        zpatch_opt_alpha_nop = has(Improvement::alpha_nop) ? 1u : 0u;

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
