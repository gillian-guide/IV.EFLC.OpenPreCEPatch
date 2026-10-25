// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loader's two passes: the unconditional pointer preamble and the
// INI-gated option pass, both in the original loader's order.

#pragma once

#include <cstddef>

#include "common.hxx"

namespace opp
{
    // The loader's AddOptionsInMenu && IVMenuAPI decision. RestoreDeathMusic
    // installs through the menu even with its INI key off, so its arm asks this.
    void SetMenuIntegration(bool available) noexcept;

    // Applies every INI-gated install for `build`.
    void PatchGame(GameBuild build, GameImage image, const Config& config);

    // The loader's unconditional preamble: seven scans whose results the option
    // bodies read, plus the salt fallback when the build probe misses.
    struct PreambleReport
    {
        size_t resolved{};
        size_t missing{};
    };

    PreambleReport FindGamePointers(GameImage image) noexcept;
} // namespace opp
