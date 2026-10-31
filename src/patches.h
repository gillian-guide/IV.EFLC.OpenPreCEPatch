#pragma once
// The option pass (`PatchGame`) and the loader preamble, run in the original
// loader's order.

#include <cstddef>
#include <cstdint>

#include "common.h"

struct PatchContext
{
	GameImage image;
	std::uintptr_t gameBase;
	GameBuild build;
	const ConfigDocument& config;

	// An option is on when its value parses to exactly 1. An absent key uses the
	// default the option's own arm substitutes, which is not always "off".
	bool Option(const char* key, const char* defaultValue) const
	{
		const ConfigEntry* entry = config.find("Options", key);
		const char* text = entry ? entry->value.c_str() : defaultValue;
		return std::strtol(text, nullptr, 10) == 1;
	}
};

// The loader's AddOptionsInMenu && IVMenuAPI decision. RestoreDeathMusic installs
// through the menu even with its INI key off, so its arm asks this.
void SetMenuIntegration(bool available) noexcept;

// Applies every INI-gated install for `build`.
void PatchGame(GameBuild build, GameImage image, const ConfigDocument& config);

// The loader's unconditional preamble: seven scans whose results the option
// bodies read, plus the salt fallback when the build probe misses.
struct PreambleReport
{
	std::size_t resolved{};
	std::size_t missing{};
};

PreambleReport FindGamePointers(GameImage image) noexcept;
