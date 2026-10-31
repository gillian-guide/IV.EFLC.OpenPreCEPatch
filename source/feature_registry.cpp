// SPDX-License-Identifier: GPL-3.0-or-later

#include "feature_registry.hxx"

#include "log.hxx"

namespace opp
{
    namespace
    {
        std::vector<FeatureRegistration>& Registry()
        {
            static std::vector<FeatureRegistration> registry;
            return registry;
        }

        std::vector<Feature> ComputeOrder()
        {
            const OrderResult result = BuildOrder(Registry());
            for (const std::string& diagnostic : result.diagnostics)
                Log("%s", diagnostic.c_str());
            if (!result.valid)
            {
                // A programming error (a cycle or a name the schedule does not
                // know) must not install a partial, order-dependent patch set.
                Log("install: refused, order invalid");
                return {};
            }
            return result.order;
        }
    } // namespace

    void RegisterFeature(const FeatureRegistration& feature)
    {
        Registry().push_back(feature);
    }

    const std::vector<Feature>& RegisteredFeatures()
    {
        static const std::vector<Feature> order = ComputeOrder();
        return order;
    }
} // namespace opp
