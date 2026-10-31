// SPDX-License-Identifier: GPL-3.0-or-later
//
// The install registry. Feature modules register their arms from a class
// constructor (the shape FusionFix uses for its subsystems); the run order is
// derived from each arm's phase, the shipping schedule and any declared
// dependencies, so it never depends on static-init or link order.

#pragma once

#include <vector>

#include "common.hxx"
#include "order.hxx"

namespace opp
{
    void RegisterFeature(const FeatureRegistration& feature);

    // The registered arms in effective order, computed once. Empty when the
    // order is invalid (a cycle, an unknown or duplicate name); the reason is in
    // the log.
    const std::vector<Feature>& RegisteredFeatures();
} // namespace opp
