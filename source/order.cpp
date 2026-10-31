// SPDX-License-Identifier: GPL-3.0-or-later
//
// Declarative load order: see order.hxx. The schedule table below is the
// shipping sequence; BuildOrder is the deterministic topological sort that
// turns registrations plus their named dependencies into the run order.

#include "order.hxx"

import opp.log;

#include <algorithm>
#include <iterator>
#include <limits>
#include <queue>
#include <utility>
#include <unordered_map>

namespace opp
{
    namespace
    {
        // The shipping schedule, grouped by phase. Frozen by
        // tests/order_golden.txt; a position change is a behaviour change.
        constexpr std::string_view kShippingOrder[] = {
        "PublishBadPlayerModelCrashFix",
        "PublishBlackscreenFix",
        "PublishBuildingDynamicShadows",
        "PublishCollisionDeadlockFix",
        "PublishCopSirensFlag",
        "PublishDualVehicleHeadlights",
        "PublishEmissiveLerpFix",
        "PublishEpisodeSwitchFreezeFix",
        "PublishFreezeGunFix",
        "PublishGroupHackFix",
        "PublishHighFPSBikePhysicsFix",
        "PublishLoadDLCs",
        "PublishNewFilesCompatibility",
        "PublishNewMemorySystem",
        "PublishNewSavesCompatibility",
        "PublishNoLiveryLimit",
        "PublishPathNodeCrashFix",
        "PublishPedModelsInCutscenesFix",
        "PublishPlaneCrashFix",
        "PublishPlayerAsPedComponentsFix",
        "PublishPopupHeadlightsFlag",
        "PublishSMPA60Fix",
        "PublishSpeedupBugFix",
        "PublishWantedLevelCrashFix",
        "PublishSuperLODFix",
        "PublishDrivebyFOVFix",
        "PublishCrashMsgFix",
        "PublishBenchmarkFix",
        "PublishCarPartsShadowFix",
        "PublishForceCarHeadlightShadows",
        "PublishRemoveModChecks",
        "PublishFreezeCarFix",
        "PublishCarDynamicShadowFix",
        "PublishGarageModelFix",
        "PublishMovingSteeringWheelSupport",
        "PublishShakingEngineBlock",
        "PublishMovingPlaneParts",
        "PublishMouseFix",
        "PublishOutOfCommissionFix",
        "PublishCutsceneFixes",
        "InstallAimProjectileCrashFix",
        "InstallAllowHashesForIDE",
        "InstallBadPlayerModelCrashFix",
        "InstallBetterMPSync",
        "InstallBikeFeetFix",
        "InstallBikePhoneAnimsFix",
        "InstallBlackscreenFix",
        "InstallBuildingAlphaFix",
        "InstallBuildingDynamicShadows",
        "InstallCollisionDeadlockFix",
        "InstallCombinedRadioFreezeFix",
        "InstallCopSirensFlag",
        "InstallDualVehicleHeadlights",
        "InstallEmissiveLerpFix",
        "InstallEpisodeSwitchFreezeFix",
        "InstallEpisodicVehicleSupport",
        "InstallEpisodicWeaponSupport",
        "InstallForceShadowsOnObjects",
        "InstallFreezeGunFix",
        "InstallGroupHackFix",
        "InstallHighFPSBikePhysicsFix",
        "InstallHighFPSSpeedupFix",
        "InstallImprovedShaderStreaming",
        "InstallInfiniteCarBottomDistance",
        "InstallLoadDLCs",
        "InstallMPNikoCrashFix",
        "InstallNewFilesCompatibility",
        "InstallNewMemorySystem",
        "InstallNewSavesCompatibility",
        "InstallNoLiveryLimit",
        "InstallRemoveModChecks",
        "InstallPathNodeCrashFix",
        "InstallPedModelsInCutscenesFix",
        "InstallPipeBombDropIconSupport",
        "InstallPlaneCrashFix",
        "InstallPlayerAsPedComponentsFix",
        "InstallPoliceEpisodicWeaponSupport",
        "InstallPopupHeadlightsFlag",
        "InstallRemoveAllWeaponsProtection",
        "InstallRemoveRGSCLoginRequirement",
        "InstallRemoveStaticCarShadows",
        "InstallRemoveStaticCutsceneCarShadows",
        "InstallReversingLightFix",
        "InstallSCOSignatureFix",
        "InstallSMPA60Fix",
        "InstallSavegameFix",
        "InstallSpeedupBugFix",
        "InstallTBoGTHeliHeightLimit",
        "InstallTeleportProtection",
        "InstallTryToSkipAllErrors",
        "InstallWantedLevelCrashFix",
        "InstallSuperLODFix",
        "InstallDrivebyFOVFix",
        "InstallCrashMsgFix",
        "InstallBenchmarkFix",
        "InstallCarPartsShadowFix",
        "InstallForceCarHeadlightShadows",
        "InstallFreezeCarFix",
        "InstallCarDynamicShadowFix",
        "InstallGarageModelFix",
        "InstallMovingSteeringWheelSupport",
        "InstallShakingEngineBlock",
        "InstallMovingPlaneParts",
        "InstallMouseFix",
        "InstallOutOfCommissionFix",
        "InstallCutsceneFixes",
        "InstallCarDecalsFix",
        "InstallRestoreDeathMusic",
        "InstallCarDynamicShadowFixNoE",
        "InstallFastLoading",
        "InstallCarPartsShadowFixStandalone",
        "InstallIncreaseVehicleModelLimit",
        "InstallNoImpostorHeightCutoff",
        "InstallStartupTimeFix",
        "InstallIncreasePtrNodes",
        "InstallIncreaseVehicleStructLimit",
        "InstallNoLODLightHeightCutoff",
        "InstallNoLoadingSleep",
        "InstallOverrideGTADatPath",
        "InstallVSyncFix",
        "InstallGFWLProtectionDisabler",
        "InstallSteamAchievements",
        };

        constexpr size_t kScheduleSize = std::size(kShippingOrder);
        constexpr size_t kNotInSchedule = std::numeric_limits<size_t>::max();

        int PhaseRank(Phase phase) noexcept
        {
            return static_cast<int>(phase);
        }

        size_t ScheduleIndex(std::string_view name) noexcept
        {
            for (size_t i = 0; i < kScheduleSize; ++i)
            {
                if (kShippingOrder[i] == name)
                    return i;
            }
            return kNotInSchedule;
        }

        void AddDiagnostic(OrderResult& result, std::string text)
        {
            result.diagnostics.push_back(std::move(text));
        }

        std::string Quote(std::string_view name)
        {
            return '"' + std::string(name) + '"';
        }

        // One cycle path in `residual`, for the diagnostic. Iterative DFS so a
        // long chain cannot overflow the stack.
        std::vector<std::string> FindCycle(
            const std::vector<std::vector<size_t>>& next, const std::vector<bool>& residual,
            std::span<const FeatureRegistration> features)
        {
            const size_t count = features.size();
            std::vector<uint8_t> colour(count, 0);  // 0 new, 1 on stack, 2 done
            std::vector<size_t> stack;
            std::vector<size_t> path;

            for (size_t start = 0; start < count; ++start)
            {
                if (!residual[start] || colour[start] != 0)
                    continue;
                stack.push_back(start);
                while (!stack.empty())
                {
                    const size_t node = stack.back();
                    if (colour[node] == 0)
                    {
                        colour[node] = 1;
                        path.push_back(node);
                        for (const size_t successor : next[node])
                        {
                            if (!residual[successor])
                                continue;
                            if (colour[successor] == 1)
                            {
                                std::vector<std::string> cycle;
                                bool inCycle = false;
                                for (const size_t item : path)
                                {
                                    inCycle = inCycle || item == successor;
                                    if (inCycle)
                                        cycle.push_back(std::string(features[item].name));
                                }
                                cycle.push_back(std::string(features[successor].name));
                                return cycle;
                            }
                            if (colour[successor] == 0)
                                stack.push_back(successor);
                        }
                    }
                    else
                    {
                        colour[node] = 2;
                        stack.pop_back();
                        if (!path.empty() && path.back() == node)
                            path.pop_back();
                    }
                }
            }
            return {};
        }
    } // namespace

    const char* PhaseName(Phase phase) noexcept
    {
        switch (phase)
        {
        case Phase::probe:
            return "probe";
        case Phase::config:
            return "config";
        case Phase::publish:
            return "publish";
        case Phase::install:
            return "install";
        case Phase::post:
            return "post";
        }
        return "?";
    }

    OrderResult BuildOrder(std::span<const FeatureRegistration> features)
    {
        OrderResult result;
        const size_t count = features.size();
        std::vector<size_t> position(count, kNotInSchedule);
        std::unordered_map<std::string_view, size_t> byName;
        byName.reserve(count * 2);

        for (size_t i = 0; i < count; ++i)
        {
            const FeatureRegistration& feature = features[i];
            if (feature.name.empty() || feature.install == nullptr)
            {
                AddDiagnostic(result, "order: feature #" + std::to_string(i) +
                                          " has no name or no body");
                continue;
            }
            const auto [iterator, inserted] = byName.emplace(feature.name, i);
            if (!inserted)
            {
                AddDiagnostic(result, "order: duplicate feature " + Quote(feature.name));
                continue;
            }
            position[i] = ScheduleIndex(feature.name);
            if (position[i] == kNotInSchedule)
            {
                AddDiagnostic(result, "order: " + Quote(feature.name) +
                                          " is not in the shipping schedule");
            }
        }

        // A `Requires` target that is not registered skips the dependent arm; an
        // `After` target that is not registered is a soft edge and is ignored.
        std::vector<bool> active(count, true);
        for (size_t i = 0; i < count; ++i)
        {
            for (const std::string_view required : features[i].needs)
            {
                if (byName.find(required) != byName.end())
                    continue;
                active[i] = false;
                AddDiagnostic(result, "order: " + std::string(features[i].name) +
                                          " requires " + std::string(required) +
                                          " (absent): arm skipped");
            }
        }

        std::vector<std::vector<size_t>> next(count);
        std::vector<int> indegree(count, 0);
        for (size_t i = 0; i < count; ++i)
        {
            if (!active[i])
                continue;
            for (const std::string_view target : features[i].after)
            {
                const auto found = byName.find(target);
                if (found == byName.end())
                    continue;  // soft edge
                const size_t dependency = found->second;
                if (PhaseRank(features[dependency].phase) > PhaseRank(features[i].phase))
                {
                    AddDiagnostic(result,
                                  "order: phase inversion: " + std::string(features[i].name) +
                                      " (" + PhaseName(features[i].phase) + ") after " +
                                      std::string(target) + " (" +
                                      PhaseName(features[dependency].phase) + ")");
                    continue;
                }
                next[dependency].push_back(i);
                ++indegree[i];
            }
        }

        // Total-order ready set: phase, then the shipping schedule position, then
        // the name. Independent of registration order and of sort stability.
        const auto later = [&](size_t left, size_t right) {
            const int leftPhase = PhaseRank(features[left].phase);
            const int rightPhase = PhaseRank(features[right].phase);
            if (leftPhase != rightPhase)
                return leftPhase > rightPhase;
            if (position[left] != position[right])
                return position[left] > position[right];
            return features[left].name > features[right].name;
        };
        std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);

        size_t activeCount = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!active[i])
                continue;
            ++activeCount;
            if (indegree[i] == 0)
                ready.push(i);
        }

        while (!ready.empty())
        {
            const size_t node = ready.top();
            ready.pop();
            result.order.push_back(
                {features[node].phase, features[node].name.data(), features[node].install});
            for (const size_t successor : next[node])
            {
                if (--indegree[successor] == 0)
                    ready.push(successor);
            }
        }

        if (result.order.size() != activeCount)
        {
            std::vector<bool> residual(count, false);
            for (size_t i = 0; i < count; ++i)
                residual[i] = active[i] && indegree[i] > 0;
            std::vector<std::string> cycle = FindCycle(next, residual, features);
            std::string text = "order: cycle";
            for (const std::string& name : cycle)
                text += " -> " + name;
            if (cycle.empty())
                text = "order: cycle among the residual arms";
            AddDiagnostic(result, text);
        }

        // Any diagnostic that is an error empties the order: install nothing.
        for (const std::string& diagnostic : result.diagnostics)
        {
            if (diagnostic.find("arm skipped") == std::string::npos)
            {
                result.order.clear();
                break;
            }
        }
        result.valid = !result.order.empty() || activeCount == 0;
        return result;
    }
} // namespace opp
