// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.pipeline - the install schedule and the two loader passes.
//
// This is the FusionFix-minimal pipeline module: the phase model and the
// registration table (order.*), the one-time registry (feature_registry.*),
// the install driver (patches.*) and the unconditional pointer preamble
// (preamble.*). The schedule and its named edges are unchanged this phase;
// the Phase/Register rewrite is a separate step.

module;

#include "common.hxx"
#include "config.hxx"
#include "rebased.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <iterator>
#include <limits>
#include <queue>
#include <utility>
#include <unordered_map>
#include <cstring>
#include <optional>

export module opp.pipeline;

import opp.common;
import opp.game;
import opp.log;
import opp.patch;

export namespace opp
{
    // The pipeline stages. The registry currently holds `publish` and `install`
    // arms; `probe`, `config` and `post` are owned by dllmain's SetStuff, where
    // the original loader had them, and exist so a future arm can declare itself
    // instead of being called out of band.
    enum class Phase : uint8_t
    {
        probe = 0,
        config = 1,
        publish = 2,
        install = 3,
        post = 4,
    };

    const char* PhaseName(Phase phase) noexcept;

    // One arm as registered by a feature module. The dependency lists are copied
    // into the registry, so a caller may build the row from a temporary.
    struct FeatureRegistration
    {
        Phase phase{};
        std::string_view name{};
        void (*install)(PatchContext&){};
        std::vector<std::string_view> after{};
        std::vector<std::string_view> needs{};  // hard edges: absent target => skipped
    };

    // One arm in the computed order.
    struct Feature
    {
        Phase phase{};
        const char* name{};
        void (*install)(PatchContext&){};
    };

    struct OrderResult
    {
        std::vector<Feature> order;
        std::vector<std::string> diagnostics;  // stable "order: " prefixes
        bool valid{};
    };

    // Deterministic: phase, then schedule position, then name. A cycle, a
    // duplicate name, an unknown name or a phase inversion makes the result
    // invalid and clears `order` (the caller installs nothing).
    OrderResult BuildOrder(std::span<const FeatureRegistration> features);
} // namespace opp

export namespace opp
{
    void RegisterFeature(const FeatureRegistration& feature);

    // The registered arms in effective order, computed once. Empty when the
    // order is invalid (a cycle, an unknown or duplicate name); the reason is in
    // the log.
    const std::vector<Feature>& RegisteredFeatures();
} // namespace opp

export namespace opp
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

namespace opp
{
    namespace
    {
        std::vector<FeatureRegistration>& Registry()
        {
            static std::vector<FeatureRegistration> registry;
            return registry;
        }

        std::vector<Feature> ComputeOrder()
        {
            const OrderResult result = BuildOrder(Registry());
            for (const std::string& diagnostic : result.diagnostics)
                Log("%s", diagnostic.c_str());
            if (!result.valid)
            {
                // A programming error (a cycle or a name the schedule does not
                // know) must not install a partial, order-dependent patch set.
                Log("install: refused, order invalid");
                return {};
            }
            return result.order;
        }
    } // namespace

    void RegisterFeature(const FeatureRegistration& feature)
    {
        Registry().push_back(feature);
    }

    const std::vector<Feature>& RegisteredFeatures()
    {
        static const std::vector<Feature> order = ComputeOrder();
        return order;
    }
} // namespace opp

namespace opp
{
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer)
    {
        PatchContext ctx{image, reinterpret_cast<uintptr_t>(image.base), build, config};

        const std::vector<Feature>& features = RegisteredFeatures();
        size_t publishes = 0;
        size_t installs = 0;
        for (const Feature& feature : features)
        {
            if (feature.phase == Phase::publish)
                ++publishes;
            else if (feature.phase == Phase::install)
                ++installs;
        }
        Log("order: %zu features (%zu publish, %zu install)", features.size(), publishes,
            installs);
        if (features.empty())
        {
            // ComputeOrder refuses an invalid graph; installing nothing is the
            // fail-closed outcome and the reason is already in the log.
            Log("install: no arms ran");
            return;
        }

        std::optional<Phase> currentPhase;
        size_t phaseApplied = 0;
        size_t phaseSkipped = 0;

        for (const Feature& feature : features)
        {
            // One line per phase boundary plus one per phase end: a crash in the
            // install pass leaves the phase visible, not just the last arm.
            if (!currentPhase || feature.phase != *currentPhase)
            {
                if (currentPhase)
                {
                    Log("phase %s: %zu applied, %zu skipped", PhaseName(*currentPhase),
                        ArmWritesApplied() - phaseApplied,
                        ArmWritesSkipped() - phaseSkipped);
                }
                currentPhase = feature.phase;
                phaseApplied = ArmWritesApplied();
                phaseSkipped = ArmWritesSkipped();
                Log("phase %s", PhaseName(feature.phase));
            }

            if (observer.begin != nullptr)
                observer.begin(observer.user, feature.name);
            feature.install(ctx);
            if (observer.end != nullptr)
                observer.end(observer.user, feature.name);
        }
        if (currentPhase)
        {
            Log("phase %s: %zu applied, %zu skipped", PhaseName(*currentPhase),
                ArmWritesApplied() - phaseApplied,
                ArmWritesSkipped() - phaseSkipped);
        }

        // The post step the original runs after its arm sequence. It is not a
        // registered arm (the launcher write is gated by a process probe, not by
        // the arm schedule), but it is bracketed for the dump so its writes are
        // attributed and covered by the matrix.
        if (observer.begin != nullptr)
            observer.begin(observer.user, "ApplyLaunchWithPlayGTAIV");
        ApplyLaunchWithPlayGTAIV(ctx);
        if (observer.end != nullptr)
            observer.end(observer.user, "ApplyLaunchWithPlayGTAIV");

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }

    namespace
    {
        // -1 = ask the process (or the dump's env override), 0/1 = forced.
        int g_launcherProbeOverride = -1;
    }

    void SetLauncherProbeOverride(bool forced) noexcept
    {
        g_launcherProbeOverride = forced ? 1 : 0;
    }

    bool LauncherProbe() noexcept
    {
        if (g_launcherProbeOverride >= 0)
            return g_launcherProbeOverride != 0;
#if defined(OPCEP_DUMP_TOOL)
        // The offline harness has no PlayGTAIV.exe process; the parity driver
        // pins the probe so the launcher writes stay observable.
        char value[8]{};
        const DWORD length =
            GetEnvironmentVariableA("OPCEP_LAUNCHER_PROBE", value, sizeof(value));
        return length > 0 && value[0] == '1';
#else
        return FindGameProcess() != 0;
#endif
    }

    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx)
    {
        // The original handler installs this on five builds only; the string
        // exists twice on each of them (and once on the EFLC builds, which the
        // original never patched, so the gate stays).
        if (!ctx.Option("LaunchWithPlayGTAIV", "0"))
            return;
        if (!BuildIs(ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1060,
                                 GameBuild::v1070, GameBuild::v1080}))
            return;
        if (!LauncherProbe())
            return;

        // Exactly the game's launcher-name string ("LaunchGTAIV.exe\0"); rows
        // LaunchName in tests/parity/address_sites.tsv prove two hits per gated
        // build. The replacement is the 16 bytes the original strcpy_s leaves in
        // the slot: "PlayGTAIV.exe\0" followed by the surviving "e\0" of the old
        // name.
        constexpr char kLaunchNameSignature[] =
            "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";
        constexpr char kLauncherNamePatch[16] = {
            'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};
        // The original's handler writes exactly its two table slots. Requiring
        // the exact count keeps a modded image with a third "LaunchGTAIV.exe\0"
        // untouched instead of writing a slot the original never wrote.
        constexpr size_t kLauncherSlotCount = 2;

        std::vector<const uint8_t*> matches;
        FindAllPatterns({ctx.image.base, ctx.image.size}, kLaunchNameSignature, matches);
        if (matches.size() != kLauncherSlotCount)
        {
            Log("launcher: %zu LaunchGTAIV name slot(s), expected %zu: skipped",
                matches.size(), kLauncherSlotCount);
            return;
        }
        for (const uint8_t* site : matches)
            WriteRaw(ctx, site, kLauncherNamePatch, sizeof(kLauncherNamePatch));
        Log("launcher: %zu PlayGTAIV name slot(s) patched", matches.size());
    }
} // namespace opp

namespace opp
{
    namespace
    {
        constexpr uint32_t kCallOperandOffset = 6u;
        constexpr uint32_t kResolverSignatureLength = 10u;
        constexpr uint32_t kPreFn59C74Offset = 42u;
        constexpr uint32_t kPreFn5D63COffset = 48u;
        constexpr uint32_t kPreFn59CDCOffset = 48u;

        uint32_t ReadU32(const uint8_t* address)
        {
            uint32_t value = 0;
            std::memcpy(&value, address, sizeof(value));
            return value;
        }

        uint32_t PointerValue(const void* pointer)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
        }
    } // namespace

    PreambleReport FindGamePointers(GameImage image) noexcept
    {
        PreambleReport report;
        if (!image.base || image.size == 0)
            return report;

        const ModuleView view = ExecutableView({image.base, image.size});
        const auto resolve = [&](std::string_view signature) -> const uint8_t* {
            Match found;
            return FindPattern(view, signature, found) ? found.address : nullptr;
        };
        // The seven counted scans; the build-probe salt fallback below is not one.
        const auto scan = [&](std::string_view signature) -> const uint8_t* {
            if (const uint8_t* hit = resolve(signature))
            {
                ++report.resolved;
                return hit;
            }
            ++report.missing;
            return nullptr;
        };

        if (const uint8_t* hit = scan("08 0B C2 8B F0 E8"))
        {
            zpatch_driveby_resolver =
                ReadU32(hit + kCallOperandOffset) + PointerValue(hit) + kResolverSignatureLength;
        }

        if (const uint8_t* hit = scan("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23"))
            zpatch_pre_fn_59c74 = PointerValue(hit) - kPreFn59C74Offset;

        if (const uint8_t* hit =
                scan("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"))
            zpatch_model_lookup = PointerValue(hit);

        if (const uint8_t* hit = scan("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07"))
            zpatch_pre_ped_array = ReadU32(hit - sizeof(uint32_t));

        if (const uint8_t* hit =
                scan("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C"))
            zpatch_pre_fn_59b88 = PointerValue(hit);

        if (const uint8_t* hit =
                scan("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3"))
            zpatch_pre_fn_5d63c = PointerValue(hit) - kPreFn5D63COffset;

        if (const uint8_t* hit =
                scan("0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0"))
            zpatch_pre_fn_59cdc = PointerValue(hit) - kPreFn59CDCOffset;

        // Build-probe signature: absent on an unknown build, so all ten hash salts
        // fall back to these values.
        if (resolve("68 ? ? ? ? 68 A9 54 14 51 E8") == nullptr)
        {
            // The recovered fingerprint constants of the original's build table:
            // no derivation exists, the preamble's build switch selects the row.
            zpatch_salt_28 = 0x931F73A2u;
            zpatch_salt_3c = 0xA4EA0691u;
            zpatch_salt_40 = 0xC9DBDA90u;
            zpatch_salt_50 = 0x6B86410Eu;
            zpatch_salt_54 = 0x6DCD115Au;
            zpatch_salt_5c = 0x1CC648EAu;
            zpatch_salt_a8 = 0x7F6A60D3u;  // recovered fingerprint constant
            zpatch_salt_b8 = 0x62BFDB37u;  // recovered fingerprint constant
            zpatch_salt_c8 = 0x406B4B20u;  // recovered fingerprint constant
            zpatch_salt_d4 = 0x09399528u;  // recovered fingerprint constant
        }
        return report;
    }
} // namespace opp