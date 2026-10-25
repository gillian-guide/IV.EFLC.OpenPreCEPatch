// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two per-model light tables filled from [PopupLightAngles] and
// [HeadlightConeOffsets].

#pragma once

#include <array>
#include <cstdint>

#include "common.hxx"

namespace opp
{
    struct VehicleLightValue
    {
        uint32_t modelHash{};
        float value{};
        bool populated{};
    };

    struct VehicleLightTables
    {
        std::array<VehicleLightValue, 512> popupAngles{};      // radians
        std::array<VehicleLightValue, 512> headlightOffsets{};
    };

    // FUN_100523c0's two 512-slot INI loops. Missing model names leave holes;
    // numeric parsing follows strtof's prefix acceptance.
    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept;

    using ModelIndexResolver = int (*)(uint32_t, void*);

    struct PopupLightSelection
    {
        float angleRadians{1.5707963705062866f};
        bool supported{};
    };

    PopupLightSelection SelectPopupLight(const VehicleLightTables&,
                                         const std::array<uint32_t, 512>& supportedModels,
                                         int modelIndex, ModelIndexResolver,
                                         void* context = nullptr) noexcept;
    float SelectHeadlightOffset(const VehicleLightTables&, int modelIndex, ModelIndexResolver,
                                void* context = nullptr) noexcept;
} // namespace opp
