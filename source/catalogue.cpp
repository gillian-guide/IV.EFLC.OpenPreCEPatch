// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalogue.hxx"

#include "game.hxx"

extern const char* const kModelNames[];
extern const size_t kModelNameCount;

namespace opp
{
    size_t ModelCatalogSize() noexcept
    {
        return kModelNameCount;
    }

    std::string_view ModelCatalogName(size_t index) noexcept
    {
        return index < kModelNameCount ? kModelNames[index] : "**UNKNOWN**";
    }

    std::string_view ModelNameFromHash(uint32_t hash) noexcept
    {
        for (size_t i = 0; i < kModelNameCount; ++i)
        {
            if (GameHash(kModelNames[i]) == hash)
                return kModelNames[i];
        }
        return "**UNKNOWN**";
    }
} // namespace opp
