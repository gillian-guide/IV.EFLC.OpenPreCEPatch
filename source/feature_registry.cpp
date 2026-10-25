// SPDX-License-Identifier: GPL-3.0-or-later

#include "feature_registry.hxx"

#include <algorithm>

namespace opp
{
    namespace
    {
        std::vector<Feature>& Registry()
        {
            static std::vector<Feature> registry;
            return registry;
        }
    } // namespace

    void RegisterFeature(int order, const char* name, void (*install)(PatchContext&))
    {
        Registry().push_back({order, name, install});
    }

    const std::vector<Feature>& RegisteredFeatures()
    {
        static const bool sorted = []() {
            std::vector<Feature>& registry = Registry();
            std::sort(registry.begin(), registry.end(),
                      [](const Feature& left, const Feature& right) {
                          return left.order < right.order;
                      });
            return true;
        }();
        (void)sorted;
        return Registry();
    }
} // namespace opp
