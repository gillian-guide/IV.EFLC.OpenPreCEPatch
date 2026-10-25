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
        all = (1u << 7) - 1u,
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
