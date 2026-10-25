// SPDX-License-Identifier: GPL-3.0-or-later
//
// The install registry. Feature groups register their arms from a class
// constructor, the same shape FusionFix uses for its subsystems; the explicit
// order number keeps the original loader sequence regardless of static-init
// order across translation units.

#pragma once

#include <vector>

#include "common.hxx"

namespace opp
{
    struct Feature
    {
        int order;
        const char* name;
        void (*install)(PatchContext&);
    };

    void RegisterFeature(int order, const char* name, void (*install)(PatchContext&));

    // Registered features sorted by `order`, built on first use.
    const std::vector<Feature>& RegisteredFeatures();
} // namespace opp
