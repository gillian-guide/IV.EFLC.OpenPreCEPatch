// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pipeline module: the install driver, as an importable unit. The body stays
// in patches.cpp; the order model it consumes is re-exported from `order`.

module;

#include "common.hxx"
#include "feature_registry.hxx"
#include "patches.hxx"

export module pipeline;

export import order;

export using ::opp::FindGamePointers;
export using ::opp::PatchGame;
export using ::opp::PatchObserver;
export using ::opp::PreambleReport;
export using ::opp::RegisteredFeatures;
