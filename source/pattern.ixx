// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pattern module: the signature layer as an importable unit. The scanner
// implementation stays in pattern.cpp (a plain TU, like every other behavioural
// unit); this interface only re-exports it, so a feature module can import the
// scanner without pulling in hooks.h/helpers.hxx.

module;

#include "common.hxx"
#include "pattern.hxx"

export module pattern;

export using ::opp::FindAllPatterns;
export using ::opp::FindPattern;
export using ::opp::ExecutableView;
export using ::opp::NormalizeSignature;
