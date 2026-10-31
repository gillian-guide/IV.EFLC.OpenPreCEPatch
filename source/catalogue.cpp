// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalogue.hxx"
#include "common.hxx"

#include <algorithm>

import opp.game;

extern const char* const kModelNames[];
extern const size_t kModelNameCount;


namespace
{
    // ModelNameIndex: sorted (hash, first table index) pairs. The original scans
    // all 38,455 names per diagnostic lookup; the table is static, so build the
    // index once. A fixed array keeps this free of CRT/TLS-backed static
    // initialisation, which is unsafe before the ASI's CRT is ready.
    constexpr size_t kModelIndexCapacity = 40000;

    struct ModelHashEntry
    {
        uint32_t hash;
        uint32_t index;
    };

    ModelHashEntry g_modelIndex[kModelIndexCapacity];
    size_t g_modelIndexCount = 0;
    volatile LONG g_modelIndexState = 0;  // 0 uninitialised, 1 building, 2 ready

    bool BuildModelHashIndex() noexcept
    {
        for (;;)
        {
            const LONG observed =
                InterlockedCompareExchange(&g_modelIndexState, 1, 0);
            if (observed == 2)
                return g_modelIndexCount != 0;
            if (observed == 1)
            {
                Sleep(0);
                continue;
            }
            if (kModelNameCount > kModelIndexCapacity)
            {
                InterlockedExchange(&g_modelIndexState, 2);
                return false;
            }
            for (size_t i = 0; i < kModelNameCount; ++i)
                g_modelIndex[i] = {opp::GameHash(kModelNames[i]),
                                   static_cast<uint32_t>(i)};
            std::sort(g_modelIndex, g_modelIndex + kModelNameCount,
                      [](const ModelHashEntry& left, const ModelHashEntry& right) {
                          if (left.hash != right.hash) return left.hash < right.hash;
                          return left.index < right.index;
                      });
            g_modelIndexCount = kModelNameCount;
            InterlockedExchange(&g_modelIndexState, 2);
            return true;
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
        if (BuildModelHashIndex())
        {
            size_t low = 0;
            size_t high = g_modelIndexCount;
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                if (g_modelIndex[mid].hash < hash)
                    low = mid + 1;
                else
                    high = mid;
            }
            if (low < g_modelIndexCount && g_modelIndex[low].hash == hash)
                return kModelNames[g_modelIndex[low].index];
            return "**UNKNOWN**";
        }

        // Index unavailable (catalogue grew past the fixed capacity): keep the
        // original lookup as the fallback.
        for (size_t i = 0; i < kModelNameCount; ++i)
        {
            if (GameHash(kModelNames[i]) == hash)
                return kModelNames[i];
        }
        return "**UNKNOWN**";
    }
} // namespace opp
