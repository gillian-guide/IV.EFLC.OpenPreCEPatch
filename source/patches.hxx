// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loader's two passes: the unconditional pointer preamble and the
// INI-gated option pass, both in the original loader's order.

#pragma once

#include <cstddef>

#include "common.hxx"

namespace opp
{
    // Optional per-arm instrumentation for the offline dump tool. The ASI never
    // installs an observer, so the release path stays allocation- and call-free.
    struct PatchObserver
    {
        void* user{};
        void (*begin)(void* user, const char* name){};
        void (*end)(void* user, const char* name){};
    };

    // Applies every INI-gated install for `build`. When `observer` is set, its
    // hooks bracket every registered arm, in install order.
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer = {});

    // The loader's unconditional preamble: seven scans whose results the option
    // bodies read, plus the salt fallback when the build probe misses.
    struct PreambleReport
    {
        size_t resolved{};
        size_t missing{};
    };

    PreambleReport FindGamePointers(GameImage image) noexcept;

    // True when the game was started through PlayGTAIV.exe (the original's
    // process probe). The offline parity dump cannot have that process, so the
    // dump pins the answer from OPCEP_LAUNCHER_PROBE=1.
    bool LauncherProbe() noexcept;

    // Test seam: force the launcher probe (-1 = use the real process/env probe).
    // The native tests use it; the shipped ASI never sets it.
    void SetLauncherProbeOverride(bool forced) noexcept;

    // LaunchWithPlayGTAIV: rewrites the game's two "LaunchGTAIV.exe" name slots
    // to the PlayGTAIV launcher name. Runs as the pipeline's post step (after
    // every arm), through the same guarded writes as the arms.
    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx);
} // namespace opp
