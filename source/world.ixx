// zp.world - World limits and physics sleep.
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

export module zp.world;

import zp.common;
import zp.game;

export
{
void ApplyNoPhysicsSleep(const PatchContext& ctx);
void ApplyIncreasePtrNodes(const PatchContext& ctx);
}  // export

constexpr std::uint32_t kPtrNodeCount = 320'000u;       // 0x4E200
constexpr std::uint32_t kPtrNodeCountAlt = 64'000u;     // 0xFA00

void ApplyNoPhysicsSleep(const PatchContext& ctx)
{
	Trace("NoPhysicsSleep");
	// NoPhysicsSleep: 1 indirect
	if (ctx.Option("NoPhysicsSleep", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00"); pattern.size() > 0)
		{
			auto* target = MatchPointer(pattern, -38);
			if (target != nullptr)
				injector::WriteMemory<std::uint32_t>(target, 0u, true); //patch
		}
	}
}

void ApplyIncreasePtrNodes(const PatchContext& ctx)
{
	Trace("IncreasePtrNodes");
	// IncreasePtrNodes: 3 literalstores
	if (ctx.Option("IncreasePtrNodes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "6A 08 ? ? ? ? ? 68 80 38 01 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCount, true); //patch
		}

		if (auto pattern = Pattern(image, "6A 10 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCountAlt, true); //patch
		}

		if (auto pattern = Pattern(image, "6A 14 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCountAlt, true); //patch
		}
	}
}
