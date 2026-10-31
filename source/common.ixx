// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shared module. Feature modules import this after their global module
// fragment, matching the FusionFix layout.

module;

#include "common.hxx"

export module opp.common;

export using ::opp::GameBuild;
export using ::opp::GameImage;
export using ::opp::PatchContext;
export using ::opp::Match;
export using ::opp::ModuleView;
export using ::opp::Config;
export using ::opp::PatchResult;
export using ::opp::PatchResultCode;
