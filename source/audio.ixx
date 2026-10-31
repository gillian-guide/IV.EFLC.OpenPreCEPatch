// zp.audio - Radio and the death-music restore.
//
// One arm per INI option, in the original loader's order. `Publish*` publishes
// the game addresses a body reads (the whole publish stage runs before any arm
// writes a site); `Apply*` installs the option. Each block is the shipped mod's
// arm verbatim: the recovered pattern, its offsets and the bytes it stores.

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "hooks.h"
#include "rebased.h"
#include "hook_bodies.h"
#include "xlive/xliveless.h"

export module zp.audio;

import zp.common;
import zp.game;

export
{
void ApplyCombinedRadioFreezeFix(const PatchContext& ctx);
void ApplyRestoreDeathMusic(const PatchContext& ctx);
}  // export

void ApplyCombinedRadioFreezeFix(const PatchContext& ctx)
{
	Trace("CombinedRadioFreezeFix");
	// CombinedRadioFreezeFix: 1 siginstall
	if (ctx.Option("CombinedRadioFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_combined_radio_freeze_fix), true); //call
		}
	}
}

void ApplyRestoreDeathMusic(const PatchContext& ctx)
{
	Trace("RestoreDeathMusic");
	// RestoreDeathMusic: 3 siginstalls
	if (bMenuIntegration || ctx.Option("RestoreDeathMusic", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 13, reinterpret_cast<void*>(&abi_death_music_install), true); //call
		}

		if (auto pattern = Pattern(image, "C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 13, reinterpret_cast<void*>(&abi_death_music_enabled), true); //call
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_death_music_enabled), true); //call
		}
	}
}
