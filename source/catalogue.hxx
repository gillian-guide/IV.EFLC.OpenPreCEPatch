// SPDX-License-Identifier: GPL-3.0-or-later
//
// The multiplayer model-name catalogue: 38,455 names in the original ASI's
// table order, plus the reverse hash lookup used by the spawn diagnostics.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opp
{
    size_t ModelCatalogSize() noexcept;
    std::string_view ModelCatalogName(size_t index) noexcept;

    // 0x10003F00: first match in the original table order; "**UNKNOWN**" on a
    // miss.
    std::string_view ModelNameFromHash(uint32_t hash) noexcept;
} // namespace opp
