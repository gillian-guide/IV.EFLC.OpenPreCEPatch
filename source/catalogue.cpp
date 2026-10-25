// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalogue.hxx"

#include "game.hxx"

#include <algorithm>
#include <vector>

extern const char* const kModelNames[];
extern const size_t kModelNameCount;
extern "C" uint8_t zpatch_opt_model_index;

namespace
{
    struct ModelHashEntry
    {
        uint32_t hash;
        uint32_t index;
    };

    // ModelNameIndex: sorted (hash, first table index) pairs. The original scans
    // all 38,455 names per diagnostic lookup; the table is static, so build the
    // index once. The state machine keeps a second thread from reading a partial
    // table without relying on CRT thread-safe statics.
    std::vector<ModelHashEntry>& ModelHashIndex()
    {
        static std::vector<ModelHashEntry> index;
        static volatile LONG state = 0;  // 0 uninitialised, 1 building, 2 ready
        for (;;)
        {
            const LONG observed = InterlockedCompareExchange(&state, 1, 0);
            if (observed == 2)
                return index;
            if (observed == 1)
            {
                Sleep(0);
                continue;
            }
            index.reserve(kModelNameCount);
            for (size_t i = 0; i < kModelNameCount; ++i)
                index.push_back({opp::GameHash(kModelNames[i]),
                                 static_cast<uint32_t>(i)});
            std::stable_sort(index.begin(), index.end(),
                             [](const ModelHashEntry& left, const ModelHashEntry& right) {
                                 return left.hash < right.hash;
                             });
            InterlockedExchange(&state, 2);
            return index;
        }
    }
} // namespace

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
        if (zpatch_opt_model_index == 0u)
        {
            for (size_t i = 0; i < kModelNameCount; ++i)
            {
                if (GameHash(kModelNames[i]) == hash)
                    return kModelNames[i];
            }
            return "**UNKNOWN**";
        }

        const std::vector<ModelHashEntry>& index = ModelHashIndex();
        const auto found = std::lower_bound(
            index.begin(), index.end(), hash,
            [](const ModelHashEntry& entry, uint32_t value) { return entry.hash < value; });
        if (found != index.end() && found->hash == hash)
            return kModelNames[found->index];
        return "**UNKNOWN**";
    }
} // namespace opp
