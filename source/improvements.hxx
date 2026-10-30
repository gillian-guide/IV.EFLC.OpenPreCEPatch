// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opt-in improvement groups. The improvements are folded into the original INI
// options (there are no per-fix keys), so this module provides the test-only
// switches: the OPCEP_IMPROVEMENTS environment variable and the
// OPCEP_PARITY_BASELINE build definition.

#pragma once

#include <cstdint>

namespace opp
{
    enum class Improvement : uint32_t
    {
        none = 0,
        new_saves = 1u << 0,
        ooc = 1u << 1,
        vehicle = 1u << 2,
        popup = 1u << 3,
        model_index = 1u << 4,
        skip_intro = 1u << 5,
        trace = 1u << 6,
        windowed = 1u << 7,
        // Runtime-pointer failsafes in the hook bodies: a preamble pointer or a
        // game-published lookup that is missing makes the body continue with
        // the value the game would have had without the fix instead of
        // dereferencing null. On by default; `except:failsafe` restores the
        // original fault for a fault-parity run.
        failsafe = 1u << 8,
        all = (1u << 9) - 1u,
        // Opt-in entry-point rewrites. They move a patch from a mid-body site
        // to a function prologue, so the write set changes; kept outside `all`
        // so a default run is byte-identical to the original install.
        garage_entry = 1u << 9,
        // CutsceneFixes: one hook at the shared camera callee instead of the
        // three sites (two call rewrites + the branch byte). Needs a relocated
        // copy of the callee, so it is opt-in as well.
        cutscene_entry = 1u << 10,
    };

    constexpr uint32_t Improve(Improvement value) noexcept
    {
        return static_cast<uint32_t>(value);
    }

    // Pure parser used by the loader and the native tests.
    //   unset/empty/all/on/1          -> all
    //   none/off/0                    -> none
    //   only:a,b / a,b                -> only the named groups
    //   except:a,b                    -> all except the named groups
    uint32_t ParseImprovements(const char* value) noexcept;

    // Resolves OPCEP_IMPROVEMENTS once per call. OPCEP_PARITY_BASELINE forces
    // every group off regardless of the environment.
    uint32_t EnabledImprovements() noexcept;

    const char* ImprovementName(Improvement value) noexcept;
} // namespace opp
