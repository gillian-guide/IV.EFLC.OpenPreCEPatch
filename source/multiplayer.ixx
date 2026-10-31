// SPDX-License-Identifier: GPL-3.0-or-later
//
// multiplayer
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

#include <algorithm>

export module opp.multiplayer;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.catalogue_data;
import opp.settings;

namespace opp::features
{

    // BetterMPSync

    void InstallBetterMPSync(PatchContext& ctx)
    {

        TraceArm("BetterMPSync");

        // BetterMPSync: 1 siginstall
        if (ctx.Option("BetterMPSync", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 6, "abi_better_mp_sync");
            }
        }

    }


    // publish FreezeCarFix

    void PublishFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeCarFix");

        // FreezeCarFix: 12 slots
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                zpatch_spawns_orig_a = MatchTarget(pattern, 8, 12);
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                zpatch_spawns_orig_b = MatchTarget(pattern, 12, 16);
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                zpatch_spawns_case9_resume = MatchTarget(pattern, 247, 251);
                zpatch_spawns_case6_resume = MatchTarget(pattern, 181, 185);
                zpatch_spawns_case1_resume = MatchTarget(pattern, 62, 66);
                zpatch_spawns_case2_resume = MatchTarget(pattern, 87, 91);
                zpatch_spawns_case4_resume = MatchTarget(pattern, 137, 141);
                zpatch_spawns_case7_resume = MatchTarget(pattern, 203, 207);
                zpatch_spawns_case3_resume = MatchTarget(pattern, 112, 116);
                zpatch_spawns_case5_resume = MatchTarget(pattern, 159, 163);
                zpatch_spawns_case8_resume = MatchTarget(pattern, 225, 229);
                zpatch_spawns_case0_resume = MatchTarget(pattern, 37, 41);
            }
        }

    }


    // FreezeCarFix

    void InstallFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("FreezeCarFix");

        // FreezeCarFix: 7 siginstalls
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 7, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 11, "abi_spawns_guard");
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // Each offset is the call instruction of one jump-table case;
                // the body name carries that case's value (0x24..0xF6 are the
                // case ids the original's dispatch compares).
                struct SpawnCase final
                {
                    uintptr_t call_offset;
                    const char* body;
                };
                constexpr SpawnCase kSpawnCases[] = {
                    {36, "abi_spawns_case_0x24"},
                    {61, "abi_spawns_case_0x3D"},
                    {86, "abi_spawns_case_0x56"},
                    {111, "abi_spawns_case_0x6F"},
                    {136, "abi_spawns_case_0x88"},
                    {158, "abi_spawns_case_0x9E"},
                    {180, "abi_spawns_case_0xB4"},
                    {202, "abi_spawns_case_0xCA"},
                    {224, "abi_spawns_case_0xE0"},
                    {246, "abi_spawns_case_0xF6"},
                };
                for (const SpawnCase& spawn : kSpawnCases)
                    CallToBody(ctx, match + spawn.call_offset, spawn.body);
            }

            if (auto pattern = Pattern(image, "88 87 40 01 00 00 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 6, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 9, "abi_spawns_probe");
                CallToBody(ctx, match + 254, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 9, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 7, "abi_spawns_probe");
            }
        }

    }


    // publish FreezeGunFix

    void PublishFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeGunFix");

        // FreezeGunFix: 1 slot
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
            }
        }

    }


    // FreezeGunFix

    void InstallFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("FreezeGunFix");

        // FreezeGunFix: 1 siginstall
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 21, "abi_freeze_gun_shim");
            }
        }

    }


    // RemoveAllWeaponsProtection

    void InstallRemoveAllWeaponsProtection(PatchContext& ctx)
    {

        TraceArm("RemoveAllWeaponsProtection");

        // RemoveAllWeaponsProtection: 1 literal
        if (ctx.Option("RemoveAllWeaponsProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 5, 12);
            }
        }

    }


    // TeleportProtection

    void InstallTeleportProtection(PatchContext& ctx)
    {

        TraceArm("TeleportProtection");

        // TeleportProtection: 1 literal
        if (ctx.Option("TeleportProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // `ret 8`: C2 08 00, through the guarded helper.
                MakeReturn(ctx, match, 8);
            }
        }

    }

namespace
{
    class MultiplayerFeatures
    {
    public:
        MultiplayerFeatures()
        {
            RegisterFeature({Phase::publish, "PublishFreezeGunFix", &PublishFreezeGunFix});
            RegisterFeature({Phase::publish, "PublishFreezeCarFix", &PublishFreezeCarFix});
            RegisterFeature({Phase::install, "InstallBetterMPSync", &InstallBetterMPSync});
            RegisterFeature({Phase::install, "InstallFreezeGunFix", &InstallFreezeGunFix, {"PublishFreezeGunFix"}});
            RegisterFeature({Phase::install, "InstallRemoveAllWeaponsProtection", &InstallRemoveAllWeaponsProtection});
            RegisterFeature({Phase::install, "InstallTeleportProtection", &InstallTeleportProtection});
            RegisterFeature({Phase::install, "InstallFreezeCarFix", &InstallFreezeCarFix, {"PublishFreezeCarFix"}});
        }
    } multiplayerFeatures;
} // namespace

} // namespace opp::features

// ---------------------------------------------------------------------------
// The multiplayer model-name catalogue (folded in from catalogue.*): the hash
// index over the generated table in opp.catalogue_data. The spawn diagnostics
// print names from here; the index is built once.
// ---------------------------------------------------------------------------

export namespace opp
{
    size_t ModelCatalogSize() noexcept;
    std::string_view ModelCatalogName(size_t index) noexcept;

    // 0x10003F00: first match in the original table order; "**UNKNOWN**" on a
    // miss.
    std::string_view ModelNameFromHash(uint32_t hash) noexcept;
}

namespace opp
{
    namespace
    {
        // ModelNameIndex: sorted (hash, first table index) pairs. The original
        // scans all 38,455 names per diagnostic lookup; the table is static, so
        // build the index once. A fixed array keeps this free of CRT/TLS-backed
        // static initialisation, which is unsafe before the ASI's CRT is ready.
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
                const size_t count = ModelNameCount();
                if (count > kModelIndexCapacity)
                {
                    InterlockedExchange(&g_modelIndexState, 2);
                    return false;
                }
                for (size_t i = 0; i < count; ++i)
                    g_modelIndex[i] = {GameHash(ModelNameAt(i)),
                                       static_cast<uint32_t>(i)};
                std::sort(g_modelIndex, g_modelIndex + count,
                          [](const ModelHashEntry& left, const ModelHashEntry& right) {
                              if (left.hash != right.hash) return left.hash < right.hash;
                              return left.index < right.index;
                          });
                g_modelIndexCount = count;
                InterlockedExchange(&g_modelIndexState, 2);
                return true;
            }
        }
    } // namespace

    size_t ModelCatalogSize() noexcept
    {
        return ModelNameCount();
    }

    std::string_view ModelCatalogName(size_t index) noexcept
    {
        const char* name = ModelNameAt(index);
        return name != nullptr ? std::string_view(name) : "**UNKNOWN**";
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
            {
                const char* name = ModelNameAt(g_modelIndex[low].index);
                return name != nullptr ? std::string_view(name) : "**UNKNOWN**";
            }
            return "**UNKNOWN**";
        }

        // Index unavailable (catalogue grew past the fixed capacity): keep the
        // original lookup as the fallback.
        const size_t count = ModelNameCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (GameHash(ModelNameAt(i)) == hash)
                return ModelNameAt(i);
        }
        return "**UNKNOWN**";
    }
} // namespace opp