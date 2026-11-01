// zp.mission - Episodic weapons, heli height, OutOfCommission, the cutscene ped fix.

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "hooks.h"
#include "rebased.h"
#include "hook_bodies.h"
#include "xlive/xliveless.h"

export module zp.mission;

import zp.common;
import zp.game;

export
{
void PublishPedModelsInCutscenesFix(const PatchContext& ctx);
void PublishOutOfCommissionFix(const PatchContext& ctx);
void ApplyEpisodicWeaponSupport(const PatchContext& ctx);
void ApplyPedModelsInCutscenesFix(const PatchContext& ctx);
void ApplyPoliceEpisodicWeaponSupport(const PatchContext& ctx);
void ApplyTBoGTHeliHeightLimit(const PatchContext& ctx);
void ApplyOutOfCommissionFix(const PatchContext& ctx);
}  // export

void PublishPedModelsInCutscenesFix(const PatchContext& ctx)
{
	Trace("publish PedModelsInCutscenesFix");
	// PedModelsInCutscenesFix: 1 slot
	if (ctx.Option("PedModelsInCutscenesFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
		{
			zpatch_model_lookup = MatchAddress(pattern);
		}
	}
}

void PublishOutOfCommissionFix(const PatchContext& ctx)
{
	Trace("publish OutOfCommissionFix");
	// OutOfCommissionFix: 3 slots
	if (ctx.Option("OutOfCommissionFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
		{
			// match+0x11: the movq xmm0 resume the branch body returns to.
			zpatch_ooc_resume = MatchAddress(pattern, 17);
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
		{
			// The logger call is the E8 at match-0x33 (operand -0x32, next -0x2E); the match
			// itself is the E8 whose original target the wrapper replays.
			zpatch_ooc_logger = MatchTarget(pattern, -50, -46);
			zpatch_ooc_orig = MatchTarget(pattern, 1, 5);
		}
	}
}

void ApplyEpisodicWeaponSupport(const PatchContext& ctx)
{
	Trace("EpisodicWeaponSupport");
	// EpisodicWeaponSupport: 8 literals
	if (ctx.Option("EpisodicWeaponSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{
			// The original's eight rows: each site reads `[episode] == 2`, and a row NOPs either
			// the comparison at the match or the branch after the six-byte `cmp [abs],2`.
			struct EpisodicWeaponSite final
			{
				std::string_view signature;
				std::ptrdiff_t offset;
				std::size_t length;
			};
			constexpr EpisodicWeaponSite kSites[] = {
			    {"75 5B 83 3D ? ? ? ? 00 75", 0, 2},
			    {"83 3D ? ? ? ? 02 0F 85 8C 01 00 00", 7, 6},
			    {"83 3D ? ? ? ? 02 0F 8C D5 00", 0, 13},
			    {"83 3D ? ? ? ? 02 75 20 8B CD E8", 7, 2},
			    {"83 3D ? ? ? ? 02 75 50", 7, 2},
			    {"83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", 7, 2},
			    {"83 3D ? ? ? ? 02 7C 32 8B 41", 7, 2},
			    {"83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", 7, 2},
			};
			for (const EpisodicWeaponSite& site : kSites)
			{
				if (auto pattern = Pattern(image, site.signature); pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<std::uint8_t>(site.offset),
					                  site.length, true); //nop
				}
			}
		}
	}
}

void ApplyPedModelsInCutscenesFix(const PatchContext& ctx)
{
	Trace("PedModelsInCutscenesFix");
	// PedModelsInCutscenesFix: 1 sigjump
	if (ctx.Option("PedModelsInCutscenesFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_ped_models_in_cutscenes_check), true); //jmp
		}
	}
}

void ApplyPoliceEpisodicWeaponSupport(const PatchContext& ctx)
{
	Trace("PoliceEpisodicWeaponSupport");
	// PoliceEpisodicWeaponSupport: 1 literal
	if (ctx.Option("PoliceEpisodicWeaponSupport", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "39 2D ? ? ? ? 75 3E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 6, 2, true); //nop
			}
		}
	}
}

void ApplyTBoGTHeliHeightLimit(const PatchContext& ctx)
{
	Trace("TBoGTHeliHeightLimit");
	// TBoGTHeliHeightLimit: 1 literal
	if (ctx.Option("TBoGTHeliHeightLimit", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 0A F3 0F"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}
		}
	}
}

void ApplyOutOfCommissionFix(const PatchContext& ctx)
{
	Trace("OutOfCommissionFix");
	// OutOfCommissionFix: 2 siginstalls
	if (ctx.Option("OutOfCommissionFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 7, reinterpret_cast<void*>(&abi_ooc_branch), true); //jmp
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_ooc_wrap), true); //call
		}
	}
}
