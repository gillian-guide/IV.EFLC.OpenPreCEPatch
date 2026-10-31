// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order module: the phase model, the feature declaration and the
// deterministic order builder. The implementation is order.cpp; this interface
// lets a module state its arms' dependencies without including feature_registry.

module;

#include "common.hxx"
#include "order.hxx"

export module order;

export using ::opp::BuildOrder;
export using ::opp::Feature;
export using ::opp::FeatureRegistration;
export using ::opp::OrderResult;
export using ::opp::Phase;
export using ::opp::PhaseName;
export using ::opp::ShippingOrder;
