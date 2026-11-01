// zp.vehicle_lights - the two 512-slot vehicle light tables the loader reads once.
//
// `[PopupLightAngles]` / `[HeadlightConeOffsets]` are parsed once from the document,
// the popup angle is converted to radians and the model name hashed with the game's
// own name hash. A slot with no `Car<N>Name` (or an empty one) stores nothing.
//
// Original: sub_100523C0, slot loop 0x100524B0..0x100528FB. The value goes through
// std::stof (strtof at 0x10052763, checked at 0x10052772/0x1005277E) and the throw
// is uncaught, so a missing or out-of-range value faults at boot. Preserve the throw;
// a skip is a divergence.

module;

#include "common.hxx"
#include "common.h"

export module zp.vehicle_lights;

import zp.game;

export
{
PatchResult LoadVehicleLightTables(const ConfigDocument& config,
                                   VehicleLightTables& output);

PopupLightSelection SelectPopupLight(
    const VehicleLightTables& tables,
    const std::array<std::uint32_t, kVehicleLightSlotCount>& supportedModels, int modelIndex,
    ModelIndexResolver resolve, void* context) noexcept;

float SelectHeadlightOffset(const VehicleLightTables& tables, int modelIndex,
                            ModelIndexResolver resolve, void* context) noexcept;
}  // export

namespace
{
// Degrees in, radians out: the original multiplies by pi then divides by 180 in
// double (that order matters for the last bit) and stores a float.
constexpr double kPi = 3.141592653589793;
// FUN_1001AD20's value when no [HeadlightConeOffsets] row matches.
constexpr float kDefaultHeadlightOffset = 2.0f;
static_assert(std::bit_cast<std::uint32_t>(kDefaultHeadlightOffset) == 0x40000000u);

// One row of the two 512-slot tables: "Car<N>Name" or "Car<N>Angle"/"Car<N>Offset",
// N in 1..512, no leading zero. Anything else in the section is ignored, exactly
// like the per-slot lookups this replaces.
struct VehicleLightKey
{
	std::size_t slot; // 0-based
	bool isName;
};

bool ParseVehicleLightKey(std::string_view key, std::string_view valueSuffix,
                          VehicleLightKey& output) noexcept
{
	constexpr std::string_view kPrefix = "Car";
	if (key.size() <= kPrefix.size() || key.substr(0, kPrefix.size()) != kPrefix)
		return false;

	std::size_t at = kPrefix.size();
	const std::size_t digits = at;
	std::size_t number = 0;
	while (at < key.size() && key[at] >= '0' && key[at] <= '9')
	{
		number = number * 10 + static_cast<std::size_t>(key[at] - '0');
		if (number > kVehicleLightSlotCount)
			return false;
		++at;
	}
	if (at == digits || number == 0)
		return false;
	if (at - digits > 1 && key[digits] == '0')
		return false; // "Car01Name" never matched a slot before; keep that

	const std::string_view rest = key.substr(at);
	if (rest == "Name")
		output = {number - 1, true};
	else if (rest == valueSuffix)
		output = {number - 1, false};
	else
		return false;
	return true;
}
} // namespace

PatchResult LoadVehicleLightTables(const ConfigDocument& config,
                                   VehicleLightTables& output)
{
	VehicleLightTables parsed;
	constexpr std::string_view kSections[] = {"PopupLightAngles", "HeadlightConeOffsets"};
	constexpr std::string_view kValueSuffixes[] = {"Angle", "Offset"};

	// One pass over the document: the original asks 2 x 512 key pairs, but the section
	// only ever holds those rows, so walk it once keeping the last entry per key.
	const ConfigEntry* names[std::size(kSections)][kVehicleLightSlotCount]{};
	const ConfigEntry* values[std::size(kSections)][kVehicleLightSlotCount]{};
	for (const ConfigEntry& entry : config.entries)
	{
		std::size_t table = std::size(kSections);
		for (std::size_t candidate = 0; candidate < std::size(kSections); ++candidate)
		{
			if (entry.section == kSections[candidate])
			{
				table = candidate;
				break;
			}
		}
		if (table == std::size(kSections))
			continue;

		VehicleLightKey key{};
		if (!ParseVehicleLightKey(entry.key, kValueSuffixes[table], key))
			continue;
		if (key.isName)
			names[table][key.slot] = &entry;
		else
			values[table][key.slot] = &entry;
	}

	for (std::size_t table = 0; table < std::size(kSections); ++table)
	{
		auto& slots = table == 0 ? parsed.popupAngles : parsed.headlightOffsets;
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			const ConfigEntry* name = names[table][i];
			if (name == nullptr || name->value.empty())
				continue; // no row: the original's empty-name branch stores nothing

			// std::stof is unguarded, as in the original: a missing or bad value throws (header).
			const ConfigEntry* value = values[table][i];
			const std::string_view text = value ? std::string_view{value->value} : std::string_view{};
			const float numeric = std::stof(std::string(text));

			VehicleLightValue& slot = slots[i];
			slot.modelHash = GameHash(name->value);
			slot.populated = true;
			slot.value = table == 0 ? static_cast<float>(numeric * kPi / 180.0) : numeric;
		}
	}

	output = parsed;
	return {};
}

PopupLightSelection SelectPopupLight(
    const VehicleLightTables& tables,
    const std::array<std::uint32_t, kVehicleLightSlotCount>& supportedModels, int modelIndex,
    ModelIndexResolver resolve, void* context) noexcept
{
	PopupLightSelection result;
	if (!resolve || modelIndex < 0)
		return result;

	// One built-in special model is handled before the +popuplights list.
	constexpr std::uint32_t kSpecialScriptHash = 0xEB9F21D3u;
	const int specialIndex = resolve(kSpecialScriptHash, context);
	if (specialIndex >= 0 && specialIndex == modelIndex)
	{
		result.supported = true;
		return result;
	}

	for (const std::uint32_t hash : supportedModels)
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
		return kDefaultHeadlightOffset;

	// FUN_1001ad20 stops at the first zero hash rather than skipping holes.
	for (const VehicleLightValue& entry : tables.headlightOffsets)
	{
		if (!entry.modelHash)
			break;
		if (entry.populated && resolve(entry.modelHash, context) == modelIndex)
			return entry.value;
	}
	return kDefaultHeadlightOffset;
}
