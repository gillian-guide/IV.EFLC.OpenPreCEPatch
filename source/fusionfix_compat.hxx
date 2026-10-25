// SPDX-License-Identifier: GPL-3.0-or-later
//
// FusionFix overlap detection. FusionFix replaces several of the same
// subsystems OpenPreCEPatch patches; running both on one site is how a working
// install turns into a crash. When a FusionFix ASI is present next to the game,
// the overlapping options are forced off.

#pragma once

#include <cstddef>
#include <filesystem>

namespace opp
{
    struct FusionFixConflict
    {
        const char* option;
        const char* reason;
    };

    const FusionFixConflict* FusionFixConflicts(size_t& count) noexcept;

    // Looks for a FusionFix ASI in the module directory (and for a loaded
    // module). Records the result for Config::Option.
    bool DetectFusionFix(const std::filesystem::path& moduleDirectory);

    void SetFusionFixPresent(bool present) noexcept;
    bool FusionFixPresent() noexcept;

    bool IsFusionFixGated(const char* option) noexcept;
    const char* FusionFixGateReason(const char* option) noexcept;
} // namespace opp
