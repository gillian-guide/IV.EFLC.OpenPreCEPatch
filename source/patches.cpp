// SPDX-License-Identifier: GPL-3.0-or-later
//
// The option pass: one function per INI arm, called in the original loader's
// order. The order matters because some arms publish resume addresses before
// another arm overwrites the site that owns them.

#include "patches.hxx"

#include "feature_registry.hxx"
#include "helpers.hxx"
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
import xliveless_features;

namespace opp
{
    void SetMenuIntegration(bool available) noexcept
    {
        bMenuIntegration = available;
    }

    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer)
    {
        PatchContext ctx{image, reinterpret_cast<uintptr_t>(image.base), build, config};

        // Opt-in improvements: publish the INI switches once, before any body can
        // run. The defaults keep every original branch selected.
        const auto flag = [&config](const char* key) -> uint8_t {
            return config.Option(key, "0") ? 1u : 0u;
        };
        zpatch_opt_new_saves_guards = flag("NewSavesGuards");
        zpatch_opt_new_saves_seek = flag("NewSavesSeekPayloads");
        zpatch_opt_ooc_safe = flag("OutOfCommissionSafeInit");
        zpatch_opt_ooc_cache = flag("OutOfCommissionCache");
        zpatch_opt_component_guards = flag("VehicleComponentGuards");
        zpatch_opt_model_lookup_guards = flag("ModelLookupGuards");
        zpatch_opt_steer_cache = flag("SteerLookupCache");
        zpatch_opt_popup_cache = flag("PopupLookupCache");
        zpatch_opt_model_index = flag("ModelNameIndex");
        zpatch_opt_skip_intro_screens = flag("SkipIntroShowScreens");
        zpatch_opt_trace_arms = flag("TraceInstallArms");

        for (const Feature& feature : RegisteredFeatures())
        {
            if (observer.begin != nullptr)
                observer.begin(observer.user, feature.name);
            feature.install(ctx);
            if (observer.end != nullptr)
                observer.end(observer.user, feature.name);
        }
    }
} // namespace opp
