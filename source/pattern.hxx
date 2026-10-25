// SPDX-License-Identifier: GPL-3.0-or-later
//
// Signature scanning. The original feeds whitespace-separated tokens to
// strtoul(token, nullptr, 16) and compares the low byte; "?" and "??" are the
// wildcards. Hooking.Patterns does the scan, after the signature is normalised
// to its strict "AA ?? BB" form.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "common.hxx"

namespace opp
{
    std::string NormalizeSignature(std::string_view signature);

    bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept;

    size_t FindAllPatterns(ModuleView module, std::string_view signature,
                                std::vector<const uint8_t*>& output) noexcept;
} // namespace opp
