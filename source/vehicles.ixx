// zp.vehicles - Vehicle behaviour: steering, engine, plane parts, driveby, car shadows,
// teleport, no-livery, the model/struct limits and the spawn probe.
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

export module zp.vehicles;

import zp.common;
import zp.game;

export
{
void PublishDualVehicleHeadlights(const PatchContext& ctx);
void PublishHighFPSBikePhysicsFix(const PatchContext& ctx);
void PublishNoLiveryLimit(const PatchContext& ctx);
void PublishCarPartsShadowFix(const PatchContext& ctx);
void PublishForceCarHeadlightShadows(const PatchContext& ctx);
void PublishFreezeCarFix(const PatchContext& ctx);
void PublishCarDynamicShadowFix(const PatchContext& ctx);
void PublishGarageModelFix(const PatchContext& ctx);
void PublishMovingSteeringWheelSupport(const PatchContext& ctx);
void PublishShakingEngineBlock(const PatchContext& ctx);
void PublishMovingPlaneParts(const PatchContext& ctx);
void ApplyBikeFeetFix(const PatchContext& ctx);
void ApplyBikePhoneAnimsFix(const PatchContext& ctx);
void ApplyDualVehicleHeadlights(const PatchContext& ctx);
void ApplyEpisodicVehicleSupport(const PatchContext& ctx);
void ApplyHighFPSBikePhysicsFix(const PatchContext& ctx);
void ApplyInfiniteCarBottomDistance(const PatchContext& ctx);
void ApplyNoLiveryLimit(const PatchContext& ctx);
void ApplyPipeBombDropIconSupport(const PatchContext& ctx);
void ApplyReversingLightFix(const PatchContext& ctx);
void ApplyTeleportProtection(const PatchContext& ctx);
void ApplyCarPartsShadowFix(const PatchContext& ctx);
void ApplyForceCarHeadlightShadows(const PatchContext& ctx);
void ApplyFreezeCarFix(const PatchContext& ctx);
void ApplyCarDynamicShadowFix(const PatchContext& ctx);
void ApplyGarageModelFix(const PatchContext& ctx);
void ApplyMovingSteeringWheelSupport(const PatchContext& ctx);
void ApplyShakingEngineBlock(const PatchContext& ctx);
void ApplyMovingPlaneParts(const PatchContext& ctx);
void ApplyCarDecalsFix(const PatchContext& ctx);
void ApplyCarDynamicShadowFixNoE(const PatchContext& ctx);
void ApplyDualVehicleHeadlightsOld(const PatchContext& ctx);
void ApplyDualVehicleHeadlightsNew(const PatchContext& ctx);
void ApplyCarPartsShadowFixStandalone(const PatchContext& ctx);
void ApplyIncreaseVehicleModelLimit(const PatchContext& ctx);
void ApplyIncreaseVehicleStructLimit(const PatchContext& ctx);
}  // export

constexpr std::uint16_t kShadowPushA = 0x0504u;
constexpr std::uint16_t kShadowPushB = 0x0104u;
constexpr std::uint16_t kRet8Imm = 0x08C2u;     // C2 08 = ret 8
constexpr std::uint8_t kRet8High = 0x00u;       // high byte of the imm16
constexpr std::uint16_t kCutsceneBranchDisp = 0x009Eu;  // je +0x9E
constexpr std::uint8_t kCutsceneJmpDisp = 0x2Au;        // jmp +0x2A
constexpr std::uint32_t kVehicleStructLimit = 100u;     // 0x64: struct count
constexpr std::uint32_t kPtrNodeInitial = 0x400u;       // 0x400
constexpr std::uint32_t kModelLimitIncrease = 0x100u;   // 0x100
struct SpawnCase final
{
	std::uint32_t offset;
	void (*body)();
};
constexpr SpawnCase kSpawnCases[] = {
	{0x24u, &abi_spawns_case_0x24},
	{0x3Du, &abi_spawns_case_0x3D},
	{0x56u, &abi_spawns_case_0x56},
	{0x6Fu, &abi_spawns_case_0x6F},
	{0x88u, &abi_spawns_case_0x88},
	{0x9Eu, &abi_spawns_case_0x9E},
	{0xB4u, &abi_spawns_case_0xB4},
	{0xCAu, &abi_spawns_case_0xCA},
	{0xE0u, &abi_spawns_case_0xE0},
	{0xF6u, &abi_spawns_case_0xF6},
};

void PublishDualVehicleHeadlights(const PatchContext& ctx)
{
	Trace("publish DualVehicleHeadlights");
	// DualVehicleHeadlights: 5 slots
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			zpatch_dual_bcc = *pattern.get(0).get<std::uint32_t>(207);
			zpatch_dual_bd4 = MatchTarget(pattern, 331, 335);
			zpatch_dual_bd0 = MatchTarget(pattern, 140, 144);
			zpatch_dual_be4 = MatchTarget(pattern, 151, 155);
			zpatch_dual_be0 = MatchAddress(pattern, 338);
		}
	}
}

void PublishHighFPSBikePhysicsFix(const PatchContext& ctx)
{
	Trace("publish HighFPSBikePhysicsFix");
	// HighFPSBikePhysicsFix: 1 slot
	if (ctx.Option("HighFPSBikePhysicsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
		{
			zpatch_bike_resume = MatchAddress(pattern, 9);
		}
	}
}

void PublishNoLiveryLimit(const PatchContext& ctx)
{
	Trace("publish NoLiveryLimit");
	// NoLiveryLimit: 5 slots
	if (ctx.Option("NoLiveryLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
		{
			zpatch_nolivery_fn = MatchTarget(pattern, -6, -2);
			zpatch_nolivery_resume_d = MatchAddress(pattern, 90);
		}

		if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
		{
			zpatch_nolivery_resume = MatchAddress(pattern, 13);
		}

		if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
		{
			zpatch_nolivery_fn = MatchTarget(pattern, -8, -4);
			zpatch_nolivery_resume_d = MatchAddress(pattern, 84);
		}
	}
}

void PublishCarPartsShadowFix(const PatchContext& ctx)
{
	Trace("publish CarPartsShadowFix");
	// CarPartsShadowFix: 2 slots
	if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			zpatch_carparts_resume_if = MatchAddress(pattern, 2);
			zpatch_carparts_resume_else = MatchAddress(pattern, 69);
		}
	}
}

void PublishForceCarHeadlightShadows(const PatchContext& ctx)
{
	Trace("publish ForceCarHeadlightShadows");
	// ForceCarHeadlightShadows: 1 slot
	if (ctx.Option("ForceCarHeadlightShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
		{
			zpatch_fchs_resume = MatchAddress(pattern, 12);
		}
	}
}

void PublishFreezeCarFix(const PatchContext& ctx)
{
	Trace("publish FreezeCarFix");
	// FreezeCarFix: 12 slots
	if (ctx.Option("FreezeCarFix", "1") &&
	    Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
		{
			zpatch_spawns_orig_a = MatchTarget(pattern, 8, 12);
		}

		if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
		{
			zpatch_spawns_orig_b = MatchTarget(pattern, 12, 16);
		}

		if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
		{
			zpatch_spawns_case9_resume = MatchTarget(pattern, 247, 251);
			zpatch_spawns_case6_resume = MatchTarget(pattern, 181, 185);
			zpatch_spawns_case1_resume = MatchTarget(pattern, 62, 66);
			zpatch_spawns_case2_resume = MatchTarget(pattern, 87, 91);
			zpatch_spawns_case4_resume = MatchTarget(pattern, 137, 141);
			zpatch_spawns_case7_resume = MatchTarget(pattern, 203, 207);
			zpatch_spawns_case3_resume = MatchTarget(pattern, 112, 116);
			zpatch_spawns_case5_resume = MatchTarget(pattern, 159, 163);
			zpatch_spawns_case8_resume = MatchTarget(pattern, 225, 229);
			zpatch_spawns_case0_resume = MatchTarget(pattern, 37, 41);
		}
	}
}

void PublishCarDynamicShadowFix(const PatchContext& ctx)
{
	Trace("publish CarDynamicShadowFix");
	// CarDynamicShadowFix: 5 slots
	if (ctx.Option("CarDynamicShadowFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			zpatch_carparts_resume_2 = MatchAddress(pattern, 11);
			zpatch_carparts_shadow_fn = MatchTarget(pattern, 7, 11);
		}

		if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			zpatch_carparts_resume_2 = MatchAddress(pattern, 13);
			zpatch_carparts_shadow_fn = MatchTarget(pattern, 9, 13);
		}

		if (auto pattern = Pattern(image, "8B 45 10 8B 16 8B 92 8C 00 00 00"); pattern.size() > 0)
		{
			zpatch_car_dynamic_d = MatchAddress(pattern);
		}
	}
}

void PublishGarageModelFix(const PatchContext& ctx)
{
	Trace("publish GarageModelFix");
	// GarageModelFix: 2 slots
	if (ctx.Option("GarageModelFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
		{
			zpatch_garage_resume_a = MatchAddress(pattern, 6);
		}

		if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
		{
			zpatch_garage_resume_b = MatchAddress(pattern, 5);
		}
	}
}

void PublishMovingSteeringWheelSupport(const PatchContext& ctx)
{
	Trace("publish MovingSteeringWheelSupport");
	// MovingSteeringWheelSupport: 5 slots
	if (ctx.Option("MovingSteeringWheelSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
}

void PublishShakingEngineBlock(const PatchContext& ctx)
{
	Trace("publish ShakingEngineBlock");
	// ShakingEngineBlock: 5 slots
	if (ctx.Option("ShakingEngineBlock", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
}

void PublishMovingPlaneParts(const PatchContext& ctx)
{
	Trace("publish MovingPlaneParts");
	// MovingPlaneParts: 5 slots
	if (ctx.Option("MovingPlaneParts", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
}

void ApplyBikeFeetFix(const PatchContext& ctx)
{
	Trace("BikeFeetFix");
	// BikeFeetFix: 1 literal
	if (ctx.Option("BikeFeetFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "39 82 54 13 00 00 0F 85 94 04 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 12, 12, true); //nop
		}
	}
}

void ApplyBikePhoneAnimsFix(const PatchContext& ctx)
{
	Trace("BikePhoneAnimsFix");
	// BikePhoneAnimsFix: 1 literal
	if (ctx.Option("BikePhoneAnimsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 33, 6, true); //nop
		}
	}
}

void ApplyDualVehicleHeadlights(const PatchContext& ctx)
{
	Trace("DualVehicleHeadlights");
	// DualVehicleHeadlights
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
	}
}

void ApplyEpisodicVehicleSupport(const PatchContext& ctx)
{
	Trace("EpisodicVehicleSupport");
	// EpisodicVehicleSupport: 23 literals, 4 rvas, 4 sigwrites
	if (ctx.Option("EpisodicVehicleSupport", "1"))
	{
		const GameImage& image = ctx.image;

		// The original wrote these four slots by RVA (0x021500/0x7D91AE on 1.0.7.0,
		// 0x095040/0x684ABE on 1.0.8.0). The recovered signatures are unique on
		// every gated build (research 08), so scan instead: same site, same bytes.
		if (ctx.build == GameBuild::v1070)
		{
			if (auto pattern = Pattern(image, "44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
			if (auto pattern = Pattern(image, "44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			if (auto pattern = Pattern(image, "54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
			if (auto pattern = Pattern(image, "54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
		}

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			{
				if (auto pattern = Pattern(image, "7C 18 0F BF 4E 2E"); pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<std::uint8_t>() - 6, 8, true); //nop
					if (pattern.size() > 1)
						injector::MakeNOP(pattern.get(1).get<std::uint8_t>() - 6, 8, true); //nop
				}
			}

			{
				if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08"); pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 9, true); //nop
					if (pattern.size() > 1)
						injector::MakeNOP(pattern.get(1).get<std::uint8_t>(), 9, true); //nop
				}
			}

			if (auto pattern = Pattern(image, "39 35 ? ? ? ? 75 0E 80 3D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 6, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 2, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "7C 22 83 F8 28 75 1D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C 0E 05 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 13, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 08 3B 05"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 1F 8B 44 24 04"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 35 0F BF 46 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 38 8B 16"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 11 8B 4E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 1F 8B"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 35 0F BF 43 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 43"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 13, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 89 44 24 38"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 11, 6, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 8A 0D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 7, true); //nop
				injector::MakeNOP(match + 17, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 21, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match - 15, 7, true); //nop
				injector::MakeNOP(match + 10, 2, true); //nop
			}
		}
	}
}

void ApplyHighFPSBikePhysicsFix(const PatchContext& ctx)
{
	Trace("HighFPSBikePhysicsFix");
	// HighFPSBikePhysicsFix: 1 sigjump
	if (ctx.Option("HighFPSBikePhysicsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_bike_physics_detour), true); //jmp
		}
	}
}

void ApplyInfiniteCarBottomDistance(const PatchContext& ctx)
{
	Trace("InfiniteCarBottomDistance");
	// InfiniteCarBottomDistance: 1 sigwrite
	if (ctx.Option("InfiniteCarBottomDistance", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "0F 2F D0 76 25 0F 28 C2"); pattern.size() > 0)
			{
				// `jmp +0x2A` written as its two bytes (there is no short-jump API).
				auto* site = pattern.get(0).get<std::uint8_t>(3);
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true);     //jmp
				injector::WriteMemory<std::uint8_t>(site + 1, kCutsceneJmpDisp, true); // +0x2A
			}
		}
	}
}

void ApplyNoLiveryLimit(const PatchContext& ctx)
{
	Trace("NoLiveryLimit");
	// NoLiveryLimit: 1 siginstall, 3 sigjumps
	if (ctx.Option("NoLiveryLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 237, reinterpret_cast<void*>(&abi_nolivery_b), true); //jmp
			injector::MakeJMP(match - 199, reinterpret_cast<void*>(&abi_nolivery_c), true); //jmp
			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_nolivery_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_nolivery_d), true); //jmp
		}

		if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_nolivery_e), true); //jmp
		}
	}
}

void ApplyPipeBombDropIconSupport(const PatchContext& ctx)
{
	Trace("PipeBombDropIconSupport");
	// PipeBombDropIconSupport: 1 literal
	if (ctx.Option("PipeBombDropIconSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "83 7D 18 1C 75 14"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match - 9, 9, true); //nop
			}
		}
	}
}

void ApplyReversingLightFix(const PatchContext& ctx)
{
	Trace("ReversingLightFix");
	// ReversingLightFix: 1 literal
	if (ctx.Option("ReversingLightFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 8, kPushadOpcode, true); //patch
		}
	}
}

void ApplyTeleportProtection(const PatchContext& ctx)
{
	Trace("TeleportProtection");
	// TeleportProtection: 1 literal
	if (ctx.Option("TeleportProtection", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// The original writes exactly three bytes here: C2 08 over the two
			// opcode bytes and 00 over the byte after the immediate. injector::
			// MakeRET is NOT equivalent: its header stores the opcode through a
			// 4-byte int store, so it also zeroes match+3 (0x83 on 1.0.8.0, the
			// first byte of the next instruction). Keep the three-byte form.
			injector::WriteMemory<std::uint16_t>(injector::raw_ptr(match), kRet8Imm, true); //ret 8
			injector::WriteMemory<std::uint8_t>(injector::raw_ptr(match + 2), kRet8High, true); //ret 8
		}
	}
}

void ApplyCarPartsShadowFix(const PatchContext& ctx)
{
	Trace("CarPartsShadowFix");
	// CarPartsShadowFix: 1 siginstall, 1 sigjump
	if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_carparts_shadow), true); //jmp
		}

		if (auto pattern = Pattern(image, "485 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_carparts_shadow), true); //jmp
		}
	}
}

void ApplyForceCarHeadlightShadows(const PatchContext& ctx)
{
	Trace("ForceCarHeadlightShadows");
	// ForceCarHeadlightShadows: 2 siginstalls, 5 sigwrites
	if (ctx.Option("ForceCarHeadlightShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3C 01 75 50 84 DB 75 79"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
			injector::MakeJMP(match + 95, reinterpret_cast<void*>(match + 476), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 379, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 211, AddressOf(&zpatch_fchs_offset), true); //patch
		}

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_fchs_shim), true); //jmp
		}

		{
			if (auto pattern = Pattern(image, "8D 4C 24 50 51 68 04 05 00 00"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6),
				                                    kShadowPushA & kByteMask, true); //patch
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(7),
				                                    (kShadowPushA >> 8) & kByteMask, true); //patch
			}
		}

		{
			if (auto pattern = Pattern(image, "8D 44 24 50 50 68 04 01 00 00"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6),
				                                    kShadowPushB & kByteMask, true); //patch
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(7),
				                                    (kShadowPushB >> 8) & kByteMask, true); //patch
			}
		}

		{
			if (auto pattern = Pattern(image, "F3 0F 11 44 24 1C 0F 84 9E 00 00 00"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(6);
				// `0F 84 9E 00 00 00` = je +0x9E; the arm rewrites the first four
				// bytes (the sixth was already 0).
				injector::WriteMemory<std::uint8_t>(site, kJeRel32First, true);      // 0F
				injector::WriteMemory<std::uint8_t>(site + 1, kJeRel32Second, true); // 84
				injector::WriteMemory<std::uint16_t>(site + 2, kCutsceneBranchDisp, true); // +0x9E
				injector::WriteMemory<std::uint8_t>(site + 4, kZeroByte, true); //jz rel32
			}
		}
	}
}

void ApplyFreezeCarFix(const PatchContext& ctx)
{
	Trace("FreezeCarFix");
	// FreezeCarFix: 7 siginstalls
	if (ctx.Option("FreezeCarFix", "1") &&
	    Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 7, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 11, reinterpret_cast<void*>(&abi_spawns_guard), true); //call
		}

		if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// The ten case bodies inside the probe: the original's jump-table
			// selectors are the byte offsets from the probe match.
			for (const SpawnCase& spawn : kSpawnCases)
				injector::MakeCALL(match + spawn.offset, reinterpret_cast<void*>(spawn.body), true); //call
		}

		if (auto pattern = Pattern(image, "88 87 40 01 00 00 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 6, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 9, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
			injector::MakeCALL(match + 254, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 9, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 7, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}
	}
}

void ApplyCarDynamicShadowFix(const PatchContext& ctx)
{
	Trace("CarDynamicShadowFix");
	// CarDynamicShadowFix: 3 siginstalls, 2 sigwrites
	if (ctx.Option("CarDynamicShadowFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_carparts_shadow_2), true); //jmp
		}

		if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_carparts_shadow_2b), true); //jmp
		}

		if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 3, reinterpret_cast<void*>(match + 30), true); //jmp
		}

		{
			if (auto pattern = Pattern(image, "83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6), kShortJmpOpcode, true); //jmp
			}
		}

		{
			if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(2), kZeroByte, true); //patch
			}
		}

		// Two further writes the original makes under this arm; they are in its
		// write log for a CarDynamicShadowFix-only cell but not in the recovered
		// row tables, so the reconstruction does not make them either. They are
		// the arm's @noFCHS variant: with ForceCarHeadlightShadows on, the
		// original takes the other path and makes neither write (probed:
		// CarDynamic+FCHS writes only 0x8F7422; CarDynamic alone also writes
		// 0x8F73EC and 0x8F719F).
		//
		//  * `0F 84 <rel32>` (jz) becomes `E9 <rel32+1>`: a jmp to the same
		//    target. The displacement grows by one because the jz encoding is one
		//    byte longer than the jmp (match+6+rel == match+5+newRel).
		//  * `push 0x504` becomes `push 0x100` (the shadow size the arm forces),
		//    written as a dword over the immediate like the original does.
		if (!ctx.Option("ForceCarHeadlightShadows", "1"))
		{
			if (auto pattern = Pattern(image, "0F 84 9E 00 00 00 80 7D 28"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				// The jz's own destination, re-encoded as a 5-byte jmp (rel32+1
				// falls out of dest-(match+5)); the old instruction's sixth byte
				// is left untouched, as before.
				const auto* target = reinterpret_cast<std::uint8_t*>(
				    injector::ReadRelativeOffset(injector::raw_ptr(match + 2), 4, true).as_int());
				injector::MakeJMP(injector::raw_ptr(match), injector::raw_ptr(target), true); //jmp
			}

			if (auto pattern = Pattern(image, "51 68 04 05 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(match + 2, kModelLimitIncrease, true); //patch
			}
		}
	}
}

void ApplyGarageModelFix(const PatchContext& ctx)
{
	Trace("GarageModelFix");
	// GarageModelFix: 2 siginstalls
	if (ctx.Option("GarageModelFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_garage_body_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_garage_body_b), true); //jmp
		}
	}
}

void ApplyMovingSteeringWheelSupport(const PatchContext& ctx)
{
	Trace("MovingSteeringWheelSupport");
	// MovingSteeringWheelSupport: 3 siginstalls
	if (ctx.Option("MovingSteeringWheelSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
}

void ApplyShakingEngineBlock(const PatchContext& ctx)
{
	Trace("ShakingEngineBlock");
	// ShakingEngineBlock: 3 siginstalls
	if (ctx.Option("ShakingEngineBlock", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
}

void ApplyMovingPlaneParts(const PatchContext& ctx)
{
	Trace("MovingPlaneParts");
	// MovingPlaneParts: 3 siginstalls
	if (ctx.Option("MovingPlaneParts", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
}

void ApplyCarDecalsFix(const PatchContext& ctx)
{
	Trace("CarDecalsFix");
	// CarDecalsFix: 1 siginstall
	if (ctx.Option("CarDecalsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_car_decals_fix), true); //jmp
		}
	}
}

void ApplyCarDynamicShadowFixNoE(const PatchContext& ctx)
{
	Trace("CarDynamicShadowFixNoE");
	// CarDynamicShadowFix@noE: 1 siginstall
	if (ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 62, 6, true); //nop
		}
	}
}

void ApplyDualVehicleHeadlightsOld(const PatchContext& ctx)
{
	Trace("DualVehicleHeadlightsOld");
	// DualVehicleHeadlights@old: 1 siginstall
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_dual_headlights_old), true); //jmp
		}
	}
}

void ApplyDualVehicleHeadlightsNew(const PatchContext& ctx)
{
	Trace("DualVehicleHeadlightsNew");
	// DualVehicleHeadlights@new: 1 siginstall
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_dual_headlights_new), true); //jmp
		}
	}
}

void ApplyCarPartsShadowFixStandalone(const PatchContext& ctx)
{
	Trace("CarPartsShadowFixStandalone");
	// CarPartsShadowFix@standalone: 2 sigwrites
	if (ctx.Option("CarPartsShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "83 F8 04 C6 44 24 0E 00 75 0F"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(8), kShortJmpOpcode, true); //jmp
			}
		}

		{
			if (auto pattern = Pattern(image, "83 FA 04 75 0C"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(3), kShortJmpOpcode, true); //jmp
			}
		}
	}
}

void ApplyIncreaseVehicleModelLimit(const PatchContext& ctx)
{
	Trace("IncreaseVehicleModelLimit");
	// IncreaseVehicleModelLimit: 1 indirect
	if (ctx.Option("IncreaseVehicleModelLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C7 01 81 FF 18 79 00 00 7C D2"); pattern.size() > 0)
		{
			auto* target = MatchPointer(pattern, 77);
			if (target != nullptr)
				injector::WriteMemory<std::uint32_t>(target, kPtrNodeInitial, true); //patch
		}
	}
}

void ApplyIncreaseVehicleStructLimit(const PatchContext& ctx)
{
	Trace("IncreaseVehicleStructLimit");
	// IncreaseVehicleStructLimit: 1 literalstore
	if (ctx.Option("IncreaseVehicleStructLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? 6A 32 8B C8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6,
			                                    static_cast<std::uint8_t>(kVehicleStructLimit),
			                                    true); //patch
		}
	}
}
