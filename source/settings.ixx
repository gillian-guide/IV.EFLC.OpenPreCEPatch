// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings module: INI creation/import, option access and the FusionFix
// compatibility gate.

module;

#include "config.hxx"
#include "fusionfix_compat.hxx"
#include "ini_import.hxx"

export module settings;

export using ::opp::Config;
export using ::opp::EnsureConfigFile;
export using ::opp::DetectFusionFix;
export using ::opp::SetFusionFixPresent;
export using ::opp::FusionFixPresent;
export using ::opp::IsFusionFixGated;
export using ::opp::FusionFixGateReason;
