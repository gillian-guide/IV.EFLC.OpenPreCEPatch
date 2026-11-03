// zp.stability - The crash fixes: the shim bodies plus the arms that install them.

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "patterns.h"
#include "hooks.h"
#include "rebased.h"
#include "hook_bodies.h"
#include "xlive/xliveless.h"

export module zp.stability;

import zp.common;
import zp.game;

export
{
void PublishBadPlayerModelCrashFix(const PatchContext& ctx);
void PublishBlackscreenFix(const PatchContext& ctx);
void PublishCollisionDeadlockFix(const PatchContext& ctx);
void PublishEpisodeSwitchFreezeFix(const PatchContext& ctx);
void PublishFreezeGunFix(const PatchContext& ctx);
void PublishGroupHackFix(const PatchContext& ctx);
void PublishPathNodeCrashFix(const PatchContext& ctx);
void PublishPlaneCrashFix(const PatchContext& ctx);
void PublishPlayerAsPedComponentsFix(const PatchContext& ctx);
void PublishSMPA60Fix(const PatchContext& ctx);
void PublishWantedLevelCrashFix(const PatchContext& ctx);
void PublishCrashMsgFix(const PatchContext& ctx);
void ApplyAimProjectileCrashFix(const PatchContext& ctx);
void ApplyBadPlayerModelCrashFix(const PatchContext& ctx);
void ApplyBlackscreenFix(const PatchContext& ctx);
void ApplyCollisionDeadlockFix(const PatchContext& ctx);
void ApplyEpisodeSwitchFreezeFix(const PatchContext& ctx);
void ApplyFreezeGunFix(const PatchContext& ctx);
void ApplyGroupHackFix(const PatchContext& ctx);
void ApplyHighFPSSpeedupFix(const PatchContext& ctx);
void ApplyMPNikoCrashFix(const PatchContext& ctx);
void ApplyPathNodeCrashFix(const PatchContext& ctx);
void ApplyPlaneCrashFix(const PatchContext& ctx);
void ApplyPlayerAsPedComponentsFix(const PatchContext& ctx);
void ApplyRemoveAllWeaponsProtection(const PatchContext& ctx);
void ApplySCOSignatureFix(const PatchContext& ctx);
void ApplySMPA60Fix(const PatchContext& ctx);
void ApplyTryToSkipAllErrors(const PatchContext& ctx);
void ApplyWantedLevelCrashFix(const PatchContext& ctx);
void ApplyCrashMsgFix(const PatchContext& ctx);
}  // export

void PublishBadPlayerModelCrashFix(const PatchContext& ctx)
{
	Trace("publish BadPlayerModelCrashFix");
	if (ctx.Option("BadPlayerModelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
		{
			zpatch_bpm_resume = MatchAddress(pattern, 10);
		}
	}
}

void PublishBlackscreenFix(const PatchContext& ctx)
{
	Trace("publish BlackscreenFix");
	if (ctx.Option("BlackscreenFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
		{
			// match+82 is the rel32 of the `jmp` at match+81; the original publishes
			// that target and the dword its first instruction loads (at target+1).
			const std::uint32_t resume = MatchTarget(pattern, 82, 86);
			zpatch_blackscreen_resume = resume;
			const auto valueAddress = static_cast<std::uintptr_t>(resume) + 1;
			if (InImage(image, valueAddress))
				zpatch_blackscreen_value_ptr = *reinterpret_cast<const std::uint32_t*>(valueAddress);
		}
	}
}

void PublishCollisionDeadlockFix(const PatchContext& ctx)
{
	Trace("publish CollisionDeadlockFix");
	if (ctx.Option("CollisionDeadlockFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
		{
			zpatch_collision_deadlock_should_walk = MatchTarget(pattern, 91, 95);
		}
	}
}

void PublishEpisodeSwitchFreezeFix(const PatchContext& ctx)
{
	Trace("publish EpisodeSwitchFreezeFix");
	if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_a = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_b = MatchAddress(pattern, 6);
		}

		if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_c = MatchAddress(pattern, 7);
		}
	}
}

void PublishFreezeGunFix(const PatchContext& ctx)
{
	Trace("publish FreezeGunFix");
	if (ctx.Option("FreezeGunFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
		{
			zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
		}
	}
}

void PublishGroupHackFix(const PatchContext& ctx)
{
	Trace("publish GroupHackFix");
	if (ctx.Option("GroupHackFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
		{
			zpatch_group_hack_resume = MatchAddress(pattern, 11);
		}
	}
}

void PublishPathNodeCrashFix(const PatchContext& ctx)
{
	Trace("publish PathNodeCrashFix");
	if (ctx.Option("PathNodeCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
		{
			zpatch_path_node_resume_ok = MatchAddress(pattern);
			zpatch_path_node_resume_fail = MatchAddress(pattern, 59);
		}
	}
}

void PublishPlaneCrashFix(const PatchContext& ctx)
{
	Trace("publish PlaneCrashFix");
	if (ctx.Option("PlaneCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
		{
			zpatch_plane_crash_resume_b = MatchAddress(pattern, 14);
			zpatch_plane_crash_resume_a = MatchAddress(pattern, -705);
		}
	}
}

void PublishPlayerAsPedComponentsFix(const PatchContext& ctx)
{
	Trace("publish PlayerAsPedComponentsFix");
	if (ctx.Option("PlayerAsPedComponentsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
		{
			zpatch_player_ped_resume = MatchAddress(pattern, 14);
		}

		if (auto pattern = Pattern(image, kPedModelLookupSignature); pattern.size() > 0)
		{
			zpatch_model_lookup = MatchAddress(pattern);
		}
	}
}

void PublishSMPA60Fix(const PatchContext& ctx)
{
	Trace("publish SMPA60Fix");
	// The recovered gate is keyed on the primary pattern, so
	// one scan both gates and supplies the match+136 resume.
	if (ctx.Option("SMPA60Fix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto primary = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
		    primary.size() > 0)
		{
			if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
			{
				zpatch_smpa60_resume_a = MatchAddress(pattern, 28);
				zpatch_smpa60_resume_b = MatchAddress(pattern, 14);
			}
			zpatch_smpa60_resume_c = MatchAddress(primary, 136);
		}
	}
}

void PublishWantedLevelCrashFix(const PatchContext& ctx)
{
	Trace("publish WantedLevelCrashFix");
	if (ctx.Option("WantedLevelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
		{
			zpatch_wanted_level_resume = MatchAddress(pattern, 12);
		}
	}
}

void PublishCrashMsgFix(const PatchContext& ctx)
{
	Trace("publish CrashMsgFix");
	if (ctx.Option("CrashMsgFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
		{
			// match-4 holds the VA of the `jmp rel32` the arm replaces; the original
			// resolves that E9's target (rel32 at +1, next instruction at +5).
			const auto* site = reinterpret_cast<const std::uint8_t*>(
			    static_cast<std::uintptr_t>(*pattern.get(0).get<std::uint32_t>(-4)));
			if (InImage(image, reinterpret_cast<std::uintptr_t>(site)))
				zpatch_crash_msg_getter = static_cast<std::uint32_t>(BranchTarget(site, -1));
		}
	}
}

void ApplyAimProjectileCrashFix(const PatchContext& ctx)
{
	Trace("AimProjectileCrashFix");
	if (ctx.Option("AimProjectileCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_aim_projectile_crash_fix), true); //jmp
		}
	}
}

void ApplyBadPlayerModelCrashFix(const PatchContext& ctx)
{
	Trace("BadPlayerModelCrashFix");
	if (ctx.Option("BadPlayerModelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_bad_player_model), true); //jmp
		}
	}
}

void ApplyBlackscreenFix(const PatchContext& ctx)
{
	Trace("BlackscreenFix");
	if (ctx.Option("BlackscreenFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 81, reinterpret_cast<void*>(&abi_blackscreen_shim), true); //jmp
		}
	}
}

void ApplyCollisionDeadlockFix(const PatchContext& ctx)
{
	Trace("CollisionDeadlockFix");
	if (ctx.Option("CollisionDeadlockFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_collision_deadlock_walk), true); //jmp
		}
	}
}

void ApplyEpisodeSwitchFreezeFix(const PatchContext& ctx)
{
	Trace("EpisodeSwitchFreezeFix");
	if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_shim_c), true); //jmp
		}

		if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_store_shim), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 41 10 03 41 0C C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_sum), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_shim_b), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_vcall), true); //jmp
		}
	}
}

void ApplyFreezeGunFix(const PatchContext& ctx)
{
	Trace("FreezeGunFix");
	if (ctx.Option("FreezeGunFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 21, reinterpret_cast<void*>(&abi_freeze_gun_shim), true); //jmp
		}
	}
}

void ApplyGroupHackFix(const PatchContext& ctx)
{
	Trace("GroupHackFix");
	if (ctx.Option("GroupHackFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_group_hack_shim), true); //jmp
		}
	}
}
void ApplyHighFPSSpeedupFix(const PatchContext& ctx)
{
	Trace("HighFPSSpeedupFix");
	if (ctx.Option("HighFPSSpeedupFix", "1"))
	{
		const GameImage& image = ctx.image;

		// The nine sites the original patches inside CTimer::Update (1.0.8.0
		// 0x7CBF00, match 0x7CBFE9): the cutscene frame-time clamps. +224 becomes a
		// `jmp` (skipping the following short jump), the other eight are NOPs.
		struct SpeedupSite final
		{
			std::ptrdiff_t offset;
			bool shortJump;
		};
		constexpr SpeedupSite kSites[] = {
		    {-7, false},  // 0x7CBFE2: `jbe` of the frame-time clamp
		    {3, false},   // 0x7CBFEC: the paired `jbe`
		    {224, true},  // 0x7CC0C9: `ja` after hasCutsceneFinished -> `jmp`
		    {231, false}, // 0x7CC0D0: `jbe` of the non-cutscene clamp
		    {367, false}, // 0x7CC158: `jbe` of the second clamp block
		    {377, false}, // 0x7CC162: its paired `jbe`
		    {607, false}, // 0x7CC248: `ja` after hasCutsceneFinished
		    {614, false}, // 0x7CC24F: `jbe` of the third block's clamp
		    {764, false}, // 0x7CC2E5: `jbe` of the fourth block's clamp
		};
		if (auto pattern = Pattern(image, "0F 2F C1 76 03 0F 28 C8 F3 0F 10 15");
		    pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();
			for (const SpeedupSite& site : kSites)
			{
				if (site.shortJump)
					injector::WriteMemory<std::uint8_t>(match + site.offset, kShortJmpOpcode,
					                                    true); //jmp
				else
					injector::MakeNOP(match + site.offset, 2, true); //nop
			}
		}
	}
}

void ApplyMPNikoCrashFix(const PatchContext& ctx)
{
	Trace("MPNikoCrashFix");
	if (ctx.Option("MPNikoCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplyPathNodeCrashFix(const PatchContext& ctx)
{
	Trace("PathNodeCrashFix");
	if (ctx.Option("PathNodeCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 53, reinterpret_cast<void*>(&abi_path_node_shim), true); //jmp
		}
	}
}

void ApplyPlaneCrashFix(const PatchContext& ctx)
{
	Trace("PlaneCrashFix");
	if (ctx.Option("PlaneCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 3, reinterpret_cast<void*>(&abi_plane_crash_shim), true); //jmp
		}
	}
}

void ApplyPlayerAsPedComponentsFix(const PatchContext& ctx)
{
	Trace("PlayerAsPedComponentsFix");
	if (ctx.Option("PlayerAsPedComponentsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 5, reinterpret_cast<void*>(&abi_player_ped_components_shim), true); //jmp
		}
	}
}

void ApplyRemoveAllWeaponsProtection(const PatchContext& ctx)
{
	Trace("RemoveAllWeaponsProtection");
	if (ctx.Option("RemoveAllWeaponsProtection", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 5, 12, true); //nop
		}
	}
}

void ApplySCOSignatureFix(const PatchContext& ctx)
{
	Trace("SCOSignatureFix");
	if (ctx.Option("SCOSignatureFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match - 32, kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplySMPA60Fix(const PatchContext& ctx)
{
	Trace("SMPA60Fix");
	if (ctx.Option("SMPA60Fix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto primary = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
		    primary.size() > 0)
		{
			auto* match = primary.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 123, reinterpret_cast<void*>(&abi_smpa60_tail_shim), true); //jmp
			injector::WriteMemory<std::uint8_t>(match - 38, kShortJmpOpcode, true); //jmp

			if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
			{
				auto* shim = pattern.get(0).get<std::uint8_t>();

				injector::MakeJMP(shim, reinterpret_cast<void*>(&abi_smpa60_shim), true); //jmp
			}
		}
	}
}

void ApplyTryToSkipAllErrors(const PatchContext& ctx)
{
	Trace("TryToSkipAllErrors");
	if (ctx.Option("TryToSkipAllErrors", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kRetOpcode, true); //ret
		}
	}
}

void ApplyWantedLevelCrashFix(const PatchContext& ctx)
{
	Trace("WantedLevelCrashFix");
	if (ctx.Option("WantedLevelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_wanted_level_shim), true); //jmp
		}
	}
}

void ApplyCrashMsgFix(const PatchContext& ctx)
{
	Trace("CrashMsgFix");
	if (ctx.Option("CrashMsgFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
		{
			std::uint8_t* site = MatchPointer(pattern, -4);
			injector::MakeCALL(site, reinterpret_cast<void*>(&abi_crash_msg_shim), true); //call
		}
	}
}
