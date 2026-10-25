// SPDX-License-Identifier: GPL-3.0-or-later

#include "vehicle_lights.hxx"

#include "game.hxx"

#include <cerrno>
#include <cstdlib>
#include <string>

namespace opp
{
    PatchResult LoadVehicleLightTables(const Config& config, VehicleLightTables& output) noexcept
    {
        try
        {
            VehicleLightTables parsed;
            const char* sections[] = {"PopupLightAngles", "HeadlightConeOffsets"};
            const char* suffixes[] = {"Angle", "Offset"};

            for (unsigned table = 0; table < std::size(sections); ++table)
            {
                auto& slots = table == 0 ? parsed.popupAngles : parsed.headlightOffsets;
                for (unsigned i = 0; i < slots.size(); ++i)
                {
                    const std::string prefix = "Car" + std::to_string(i + 1);
                    const std::string name =
                        config.String(sections[table], prefix + "Name", "");
                    if (name.empty())
                        continue;

                    const std::string value =
                        config.String(sections[table], prefix + suffixes[table], "");
                    if (value.empty())
                        return {PatchResultCode::parse_error, "missing vehicle light value"};

                    char* end = nullptr;
                    errno = 0;
                    const float numeric = std::strtof(value.c_str(), &end);
                    if (end == value.c_str() || errno == ERANGE)
                        return {PatchResultCode::parse_error, "invalid vehicle light value"};

                    // The popup table is written in degrees and read in radians.
                    VehicleLightValue& slot = slots[i];
                    slot.modelHash = GameHash(name);
                    slot.populated = true;
                    slot.value = table == 0
                                     ? static_cast<float>(numeric * 3.141592653589793 / 180.0)
                                     : numeric;
                }
            }

            output = parsed;
            return {};
        }
        catch (...)
        {
            return {PatchResultCode::invalid_input, "vehicle table allocation failed"};
        }
    }

    PopupLightSelection SelectPopupLight(const VehicleLightTables& tables,
                                         const std::array<uint32_t, 512>& supportedModels,
                                         int modelIndex, ModelIndexResolver resolve,
                                         void* context) noexcept
    {
        PopupLightSelection result;
        if (!resolve || modelIndex < 0)
            return result;

        constexpr uint32_t kSpecialScriptHash = 0xEB9F21D3u;
        const int specialIndex = resolve(kSpecialScriptHash, context);
        if (specialIndex >= 0 && specialIndex == modelIndex)
        {
            result.supported = true;
            return result;
        }

        for (const uint32_t hash : supportedModels)
        {
            if (!hash)
                break;
            if (resolve(hash, context) != modelIndex)
                continue;

            result.supported = true;
            // The original scans all 512 slots; the last duplicate wins.
            for (const VehicleLightValue& entry : tables.popupAngles)
            {
                if (entry.populated && entry.modelHash != 0 && entry.modelHash == hash)
                    result.angleRadians = entry.value;
            }
            break;
        }
        return result;
    }

    float SelectHeadlightOffset(const VehicleLightTables& tables, int modelIndex,
                                ModelIndexResolver resolve, void* context) noexcept
    {
        if (!resolve || modelIndex < 0)
            return 2.0f;

        // FUN_1001ad20 stops at the first zero hash rather than skipping holes.
        for (const VehicleLightValue& entry : tables.headlightOffsets)
        {
            if (!entry.modelHash)
                break;
            if (entry.populated && resolve(entry.modelHash, context) == modelIndex)
                return entry.value;
        }
        return 2.0f;
    }
} // namespace opp
