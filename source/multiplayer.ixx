// zp.multiplayer - MP sync and the model-name hash.
//
// One arm per INI option, in the order this tree has always run its arms (the
// recovered loader's order was never reproduced exactly; see
// docs/architecture.md, "Option blocks"). `Publish*` publishes
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

export module zp.multiplayer;

import zp.common;
import zp.game;

export
{
void ApplyAllowHashesForIDE(const PatchContext& ctx);
void ApplyBetterMPSync(const PatchContext& ctx);
}  // export

void ApplyAllowHashesForIDE(const PatchContext& ctx)
{
	Trace("AllowHashesForIDE");
	// AllowHashesForIDE: 1 sigjump
	if (ctx.Option("AllowHashesForIDE", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 08 56 8B 74 24 08 80 3E 22"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_game_hash), true); //jmp
		}
	}
}

void ApplyBetterMPSync(const PatchContext& ctx)
{
	Trace("BetterMPSync");
	// BetterMPSync: 1 siginstall
	if (ctx.Option("BetterMPSync", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 6, reinterpret_cast<void*>(&abi_better_mp_sync), true); //call
		}
	}
}
