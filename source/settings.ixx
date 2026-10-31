// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings module: INI creation/import, the parsed option document, the
// typed value readers and the FusionFix compatibility gate. The section order is
// exported as the table it is, so an importer sees the lookup policy without
// reading config.cpp.

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

// The typed, non-throwing value readers.
export using ::opp::FlagExactlyOne;
export using ::opp::IntResult;
export using ::opp::IntStatus;
export using ::opp::ParseIntPrefix;

// The section table the option lookup walks, in order.
export using ::opp::OptionSections;
