// SPDX-License-Identifier: GPL-3.0-or-later
//
// The XLivelessAddon feature set. The original shipped all eleven signature
// writes under one `MiscFixes` key; each logical fix now has its own key below.
//
// Compatibility:
//   * an old INI that still has `MiscFixes` keeps the original bundle behaviour
//     (present => that value gates all five arms);
//   * an external XLivelessAddon.asi still suppresses the whole set, exactly as
//     the original did when byte_10159B84 was set;
//   * `IV.EFLC.OpenPreCEPatch.asi` running under the XLivelessAddon.asi name is
//     recognised as self and does not suppress its own arms.

module;

#include "common.hxx"
#include "patches.hxx"
#include "rebased.h"
#include "feature_registry.hxx"
#include "helpers.hxx"

export module xliveless_features;

import common;
import settings;

namespace opp::features
{
    namespace
    {
        bool ExternalXlivelessPresent() noexcept
        {
            // CaptureModules() leaves this at 0 when the module with that name is
            // this DLL itself (the compatibility-name build or the shim).
            return zpatch_xliveless_present != 0;
        }

        bool XlivelessOption(const PatchContext& ctx, const char* key, const char* defaultValue)
        {
            // Legacy master switch: when the old key is present it gates the bundle.
            if (ctx.config.Has("Options", "MiscFixes"))
                return ctx.Option("MiscFixes", "1");
            return ctx.Option(key, defaultValue);
        }

        bool Enabled(const PatchContext& ctx, const char* key, const char* defaultValue)
        {
            return !ExternalXlivelessPresent() && XlivelessOption(ctx, key, defaultValue);
        }
    } // namespace

    // data integrity checks VDS100/VDS102
    void InstallRemoveDataIntegrityChecks(PatchContext& ctx)
    {
        TraceArm("RemoveDataIntegrityChecks");
        if (!Enabled(ctx, "RemoveDataIntegrityChecks", "1"))
            return;

        const GameImage& image = ctx.image;

        // [v1006 - v1008] VDS102
        if (auto pattern = Pattern(image, "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9");
            pattern.size() > 0)
        {
            injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(), 0xEBu, true);
        }

        // [v1002 - v1004] VDS102
        if (auto pattern = Pattern(image, "C7 06 ? ? ? ? 74 19"); pattern.size() > 0)
        {
            injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(6), 0xEBu, true);
        }

        // [v1005 - v1008] VDS102
        if (auto pattern = Pattern(image, "83 C4 ? 3B 46 20"); pattern.size() > 0)
        {
            injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(12), 0xEBu, true);
        }

        // [v1006 - v1008] VDS100
        if (auto pattern = Pattern(image, "75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C");
            pattern.size() > 0)
        {
            injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(), 0xEBu, true);
        }
    }

    // token activation
    void InstallSkipTokenActivation(PatchContext& ctx)
    {
        TraceArm("SkipTokenActivation");
        if (!Enabled(ctx, "SkipTokenActivation", "1"))
            return;

        const GameImage& image = ctx.image;

        // [v1006 - v1007] make the activation function return success
        if (auto pattern = Pattern(image, "8B 54 24 04 52 A3"); pattern.size() > 0)
        {
            injector::WriteMemory<uint16_t>(pattern.get(0).get<uint8_t>(), 0xC9EBu, true);
        }

        // [v1006 - v1007] force the "already activated" result
        if (auto pattern = Pattern(image, "83 F8 01 75 30 A3"); pattern.size() > 0)
        {
            auto* match = pattern.get(0).get<uint8_t>();
            injector::WriteMemory<uint8_t>(match, 0xB8u, true);
            injector::WriteMemory<uint32_t>(match + 1, 0x00000001u, true);
        }
    }

    // first-run "play" prompt
    void InstallSkipFirstTimePlayAsk(PatchContext& ctx)
    {
        TraceArm("SkipFirstTimePlayAsk");
        if (!Enabled(ctx, "SkipFirstTimePlayAsk", "1"))
            return;

        const GameImage& image = ctx.image;

        if (auto pattern = Pattern(image, "C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7");
            pattern.size() > 0)
        {
            injector::WriteMemory<uint16_t>(pattern.get(0).get<uint8_t>(), 0x0DEBu, true);
        }
    }

    // InternetGetConnectedState gate
    void InstallSkipInternetConnectionCheck(PatchContext& ctx)
    {
        TraceArm("SkipInternetConnectionCheck");
        if (!Enabled(ctx, "SkipInternetConnectionCheck", "1"))
            return;

        const GameImage& image = ctx.image;

        // [v1000 - v1001]
        if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 18"); pattern.size() > 0)
        {
            injector::MakeNOP(pattern.get(0).get<uint8_t>(8), 2, true);
        }

        // [v1002 - v1005]
        if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 04 32"); pattern.size() > 0)
        {
            injector::MakeNOP(pattern.get(0).get<uint8_t>(8), 2, true);
        }

        // [v1006 - v1008]
        if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 07 A0"); pattern.size() > 0)
        {
            injector::MakeNOP(pattern.get(0).get<uint8_t>(8), 2, true);
        }
    }

    // GFWL health check
    void InstallSkipHealthCheck(PatchContext& ctx)
    {
        TraceArm("SkipHealthCheck");
        if (!Enabled(ctx, "SkipHealthCheck", "1"))
            return;

        const GameImage& image = ctx.image;

        if (auto pattern = Pattern(
                image,
                "81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8");
            pattern.size() > 0)
        {
            injector::WriteMemory<uint8_t>(pattern.get(0).get<uint8_t>(), 0xC3u, true);
        }
    }
namespace
{
    class XlivelessFeaturesFeatures
    {
    public:
        XlivelessFeaturesFeatures()
        {
            RegisterFeature(74, "InstallRemoveDataIntegrityChecks", &InstallRemoveDataIntegrityChecks);
            RegisterFeature(75, "InstallSkipTokenActivation", &InstallSkipTokenActivation);
            RegisterFeature(76, "InstallSkipFirstTimePlayAsk", &InstallSkipFirstTimePlayAsk);
            RegisterFeature(77, "InstallSkipInternetConnectionCheck", &InstallSkipInternetConnectionCheck);
            RegisterFeature(78, "InstallSkipHealthCheck", &InstallSkipHealthCheck);
        }
    } xliveless_featuresFeatures;
} // namespace

} // namespace opp::features
