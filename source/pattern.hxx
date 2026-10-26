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

    // The range the original's hook::pattern(str) scans: module base up to the end
    // of the last executable section. The installers used to hand Hooking.Patterns
    // the whole SizeOfImage, which scans .rdata/.data/.rsrc too (2.6x more bytes on
    // 1.0.8.0) and can match outside code. Falls back to the whole image when the
    // view is not a readable PE32 image, so synthetic test buffers keep working.
    ModuleView ExecutableView(ModuleView image) noexcept;

    bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept;

    size_t FindAllPatterns(ModuleView module, std::string_view signature,
                                std::vector<const uint8_t*>& output) noexcept;
} // namespace opp
