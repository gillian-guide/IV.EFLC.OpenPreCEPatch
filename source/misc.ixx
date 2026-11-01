// zp.misc - The remaining fixes and the compatibility stubs.
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

export module zp.misc;

import zp.common;
import zp.game;
import zp.useless_checks;

export
{
void PublishCopSirensFlag(const PatchContext& ctx);
void PublishPopupHeadlightsFlag(const PatchContext& ctx);
void PublishSpeedupBugFix(const PatchContext& ctx);
void PublishDrivebyFOVFix(const PatchContext& ctx);
void PublishPatch4RemoveModChecks(const PatchContext& ctx);
void ApplyRemoveUselessChecks(const PatchContext& ctx);
void ApplyCopSirensFlag(const PatchContext& ctx);
void ApplyMiscFixes(const PatchContext& ctx);
void ApplyPatch6and7RemoveModChecks(const PatchContext& ctx);
void ApplyPopupHeadlightsFlag(const PatchContext& ctx);
void ApplySpeedupBugFix(const PatchContext& ctx);
void ApplyPatch7RemoveModChecks(const PatchContext& ctx);
void ApplyDrivebyFOVFix(const PatchContext& ctx);
void ApplyPatch4RemoveModChecks(const PatchContext& ctx);
}  // export

// Patch6and7RemoveModChecks / Patch7RemoveModChecks (original handler
// sub_10044710): the packed entry-stub jump targets, per gated build. The
// handler writes `lea edi,[eax+<site>]; lea ebx,[eax+<target>];
// mov byte [edi],0E9h`; the entry target (0x3A09) is a 1.0.6.0 row, the mid and
// case targets (0x38AE / 0x39E9) are 1.0.7.0 rows.
constexpr GameRva kModChecksEntryJmp_1060{0x003A09u};
constexpr GameRva kModChecksMidJmp_1070{0x0038AEu};
constexpr GameRva kModChecksCaseJmp_1070{0x0039E9u};
constexpr std::uint32_t kTrueValue = 1u;                    // mov eax,1
constexpr std::uint8_t kMiscJmpDispC9 = 0xC9u;          // jmp +0xC9
constexpr std::uint8_t kMiscJmpDisp0D = 0x0Du;          // jmp +0x0D

void PublishCopSirensFlag(const PatchContext& ctx)
{
	Trace("publish CopSirensFlag");
	// CopSirensFlag: 3 slots
	if (ctx.Option("CopSirensFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
		{
			zpatch_copsirens_resume = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
		{
			zpatch_copsirens_resume_b = MatchAddress(pattern, 28);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
}

void PublishPopupHeadlightsFlag(const PatchContext& ctx)
{
	Trace("publish PopupHeadlightsFlag");
	// PopupHeadlightsFlag: 5 slots
	if (ctx.Option("PopupHeadlightsFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			zpatch_popup_resume_b = MatchAddress(pattern, 10);
			zpatch_popup_resume_a = MatchAddress(pattern, 12);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}

		if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			zpatch_popup_resume_b = MatchAddress(pattern, 10);
			zpatch_popup_resume_a = MatchAddress(pattern, 12);
		}
	}
}

void PublishSpeedupBugFix(const PatchContext& ctx)
{
	Trace("publish SpeedupBugFix");
	// SpeedupBugFix: 8 slots
	if (ctx.Option("SpeedupBugFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
		{
			zpatch_speedup_t50 = *pattern.get(0).get<std::uint32_t>(26);
			zpatch_speedup_t64 = *pattern.get(0).get<std::uint32_t>(165);
			zpatch_speedup_t60 = *pattern.get(0).get<std::uint32_t>(188);
			zpatch_speedup_t5c = *pattern.get(0).get<std::uint32_t>(226);
			zpatch_speedup_t68 = *pattern.get(0).get<std::uint32_t>(234);
			zpatch_speedup_t4c = *pattern.get(0).get<std::uint32_t>(249);
			zpatch_speedup_t54 = *pattern.get(0).get<std::uint32_t>(264);
			zpatch_speedup_t58 = *pattern.get(0).get<std::uint32_t>(276);
		}
	}
}

void PublishDrivebyFOVFix(const PatchContext& ctx)
{
	Trace("publish DrivebyFOVFix");
	// DrivebyFOVFix: 2 slots
	if (ctx.Option("DrivebyFOVFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
		{
			zpatch_driveby_resume = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "08 0B C2 8B F0 E8"); pattern.size() > 0)
		{
			zpatch_driveby_resolver = MatchTarget(pattern, 6, 10);
		}
	}
}

void PublishPatch4RemoveModChecks(const PatchContext& ctx)
{
	Trace("publish Patch4RemoveModChecks");
	// Patch4RemoveModChecks: 1 slot
	if (ctx.Option("Patch4RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			zpatch_gfwl_resume = MatchAddress(pattern, 12);
		}
	}
}

void ApplyRemoveUselessChecks(const PatchContext& ctx)
{
	Trace("RemoveUselessChecks");
	if (ctx.Option("RemoveUselessChecks", "0"))
		zp::ApplyRemoveUselessChecks(ctx.build, ctx.image);
}

void ApplyCopSirensFlag(const PatchContext& ctx)
{
	Trace("CopSirensFlag");
	// CopSirensFlag: 3 siginstalls, 2 sigjumps
	if (ctx.Option("CopSirensFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_copsirens_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 22, reinterpret_cast<void*>(&abi_copsirens_b), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
}

void ApplyMiscFixes(const PatchContext& ctx)
{
	Trace("MiscFixes");
	// MiscFixes: 12 literals
	if (!zpatch_xliveless_present && ctx.Option("MiscFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kRetOpcode, true); //ret
		}

		if (auto pattern = Pattern(image, "83 C4 ? 3B 46 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 12, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "83 F8 01 75 30 A3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kMovEaxOpcode, true); // cmp eax,1 -> mov eax,1
			injector::WriteMemory<std::uint32_t>(match + 1, kTrueValue, true); // cmp eax,1 -> mov eax,1
		}

		if (auto pattern = Pattern(image, "8B 54 24 04 52 A3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// `jmp +0xC9` written as its two bytes (there is no short-jump API).
			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true);      //jmp
			injector::WriteMemory<std::uint8_t>(match + 1, kMiscJmpDispC9, true);  // +0xC9
		}

		if (auto pattern = Pattern(image, "C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// `jmp +0x0D` written as its two bytes (there is no short-jump API).
			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true);      //jmp
			injector::WriteMemory<std::uint8_t>(match + 1, kMiscJmpDisp0D, true);  // +0x0D
		}

		if (auto pattern = Pattern(image, "C7 06 ? ? ? ? 74 19"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 04 32"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 07 A0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 18"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}
	}
}

void ApplyPatch6and7RemoveModChecks(const PatchContext& ctx)
{
	Trace("Patch6and7RemoveModChecks");
	// Patch6and7RemoveModChecks: 22 literals, 3 rvas
	if (ctx.Option("Patch6and7RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the recovered candidate with exactly one
			// hit at its site on this build.
			if (auto pattern = Pattern(image, "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x0038AC
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); // stub byte -> 0
			}
			if (auto pattern = Pattern(image, "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00392E
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			if (auto pattern = Pattern(image, "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00397E
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); // stub byte -> 0
			}
			if (auto pattern = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x003982
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksEntryJmp_1060.site(ctx.gameBase)), true); //jmp
			}
			if (auto pattern = Pattern(image, "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x003A14
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			// The original NOPs the seven bytes at 0x42E7..0x42ED. One scan at the
			// instruction plus match+0..+6 is equivalent and order-independent; the
			// old per-byte windows relied on the ascending write order.
			if (auto pattern = Pattern(image, "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x0042E7..0x0042ED
				injector::MakeNOP(site, 7, true); //nop x7
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the recovered candidate with exactly one
			// hit at its site on this build.
			if (auto pattern = Pattern(image, "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00388C
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); // stub byte -> 0
			}
			if (auto pattern = Pattern(image, "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00389C
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksMidJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto pattern = Pattern(image, "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00390E
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			if (auto pattern = Pattern(image, "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00395E
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); // stub byte -> 0
			}
			if (auto pattern = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x003962
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksCaseJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto pattern = Pattern(image, "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x0039F4
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			// The original NOPs the seven bytes at 0x42C7..0x42CD; one scan at the
			// instruction plus match+0..+6 replaces the seven per-byte windows
			// 
			if (auto pattern = Pattern(image, "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x0042C7..0x0042CD
				injector::MakeNOP(site, 7, true); //nop x7
			}
		}

	}
}

void ApplyPopupHeadlightsFlag(const PatchContext& ctx)
{
	Trace("PopupHeadlightsFlag");
	// PopupHeadlightsFlag: 3 siginstalls, 1 sigjump
	if (ctx.Option("PopupHeadlightsFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_popup_headlights), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 10, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 82, AddressOf(&zpatch_popup_angle),
			                                    true); // fld operand -> our angle
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_popup_headlights_edx), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 10, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 82, AddressOf(&zpatch_popup_angle),
			                                    true); // fld operand -> our angle
		}
	}
}

void ApplySpeedupBugFix(const PatchContext& ctx)
{
	Trace("SpeedupBugFix");
	// SpeedupBugFix: 1 siginstall
	if (ctx.Option("SpeedupBugFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 21, reinterpret_cast<void*>(&abi_speedup_body), true); //jmp
		}
	}
}

void ApplyPatch7RemoveModChecks(const PatchContext& ctx)
{
	Trace("Patch7RemoveModChecks");
	// Patch7RemoveModChecks: 3 rvas
	if (ctx.Option("Patch7RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate the recovered rows verified as exactly one
			// hit at its site on this build; the scans run in ascending address
			// order so no candidate is read after an earlier site was overwritten.
			if (auto pattern = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x003982
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksEntryJmp_1060.site(ctx.gameBase)), true); //jmp
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate the recovered rows verified as exactly one
			// hit at its site on this build; the scans run in ascending address
			// order so no candidate is read after an earlier site was overwritten.
			if (auto pattern = Pattern(image, "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x00389C
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksMidJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto pattern = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x003962
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksCaseJmp_1070.site(ctx.gameBase)), true); //jmp
			}
		}
	}
}

void ApplyDrivebyFOVFix(const PatchContext& ctx)
{
	Trace("DrivebyFOVFix");
	// DrivebyFOVFix: 1 sigjump
	if (ctx.Option("DrivebyFOVFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_driveby_fov_shim), true); //jmp
		}
	}
}

void ApplyPatch4RemoveModChecks(const PatchContext& ctx)
{
	Trace("Patch4RemoveModChecks");
	// Patch4RemoveModChecks: 1 siginstall
	if (ctx.Option("Patch4RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_gfwl_stub), true); //jmp
		}
	}
}
