// zp.loading - DLC, new files, saves, the launcher stages and the load-time fixes.
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

export module zp.loading;

import zp.common;
import zp.game;

export
{
void PublishLoadDLCs(const PatchContext& ctx);
void PublishLocalSettingsPath(const PatchContext& ctx);
void PublishNewFilesCompatibility(const PatchContext& ctx);
void PublishNewMemorySystem(const PatchContext& ctx);
void PublishNewSavesCompatibility(const PatchContext& ctx);
void PublishSkipIntro(const PatchContext& ctx);
void PublishSkipMenu(const PatchContext& ctx);
void PublishBenchmarkFix(const PatchContext& ctx);
void PublishRemoveGFWLUpdatePopup(const PatchContext& ctx);
void ApplyLoadDLCs(const PatchContext& ctx);
void ApplyLocalSettingsPath(const PatchContext& ctx);
void ApplyNewFilesCompatibility(const PatchContext& ctx);
void ApplyNewMemorySystem(const PatchContext& ctx);
void ApplyNewSavesCompatibility(const PatchContext& ctx);
void ApplyRemoveRGSCLoginRequirement(const PatchContext& ctx);
void ApplySavegameFix(const PatchContext& ctx);
void ApplySkipIntro(const PatchContext& ctx);
void ApplySkipLauncherOption(const PatchContext& ctx);
void ApplySkipMenu(const PatchContext& ctx);
void ApplyBenchmarkFix(const PatchContext& ctx);
void ApplyRemoveGFWLUpdatePopup(const PatchContext& ctx);
void ApplyFastLoading(const PatchContext& ctx);
void ApplyStartupTimeFix(const PatchContext& ctx);
void ApplyNoLoadingSleep(const PatchContext& ctx);
void ApplyOverrideGTADatPath(const PatchContext& ctx);
void ApplyVSyncFix(const PatchContext& ctx);
}  // export

// LoadDLCs (original handler sub_100234E0): per build, the loader callback the
// handler stores in dword_1015BDDC and the two DLC entry points it puts in
// dword_1015804C (register) and dword_101580D0 (apply). The suffix is the game
// version the row belongs to (1.0.6.0 = build 7, 1.0.7.0 = build 8); the
// handler's own `lea edi,[eax+<rva>]` rows hold the same values.
constexpr GameRva kDlcLoadCallback_1060{0x414560u};
constexpr GameRva kDlcRegister_1060{0x4136E0u};
constexpr GameRva kDlcApply_1060{0x414080u};
constexpr GameRva kDlcLoadCallback_1070{0x414300u};
constexpr GameRva kDlcRegister_1070{0x413480u};
constexpr GameRva kDlcApply_1070{0x413E20u};
constexpr GameRva kDlcLoadCallback_1080{0x490460u};
constexpr GameRva kDlcRegister_1080{0x48F440u};
constexpr GameRva kDlcApply_1080{0x48FE00u};
// The LoadDLCs rail: the call/jump targets the handler writes after patching the
// loader (target = site + 5 + rel32; the sites are the scans in ApplyLoadDLCs).
constexpr GameRva kLoadDlcsJmpA_1060{0x4144B8u};   // 0x4144B8: jmp target
constexpr GameRva kLoadDlcsJmpB_1060{0x41452Cu};   // 0x41452C: jmp target
constexpr GameRva kLoadDlcsCall_1060{0x413E30u};   // 0x413E30: call target
constexpr GameRva kLoadDlcsJmpA_1070{0x414258u};   // 0x414258: jmp target
constexpr GameRva kLoadDlcsJmpB_1070{0x4142CCu};   // 0x4142CC: jmp target
constexpr GameRva kLoadDlcsCall_1070{0x413BD0u};   // 0x413BD0: call target
constexpr GameRva kLoadDlcsJmpA_1080{0x4903B8u};   // 0x4903B8: jmp target
constexpr GameRva kLoadDlcsJmpB_1080{0x49042Cu};   // 0x49042C: jmp target
constexpr GameRva kLoadDlcsCall_1080{0x48FBB0u};   // 0x48FBB0: call target

void PublishLoadDLCs(const PatchContext& ctx)
{
	Trace("publish LoadDLCs");
	// LoadDLCs: 9 rvas
	if (ctx.Option("LoadDLCs", "1"))
	{
		if (ctx.build == GameBuild::v1060)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1060.site(ctx.gameBase));
			DlcRegisterRva() = kDlcRegister_1060.value;
			DlcApplyRva() = kDlcApply_1060.value;
		}

		if (ctx.build == GameBuild::v1070)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1070.site(ctx.gameBase));
			DlcRegisterRva() = kDlcRegister_1070.value;
			DlcApplyRva() = kDlcApply_1070.value;
		}

		if (ctx.build == GameBuild::v1080)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1080.site(ctx.gameBase));
			DlcApplyRva() = kDlcApply_1080.value;
			DlcRegisterRva() = kDlcRegister_1080.value;
		}
	}
}

void PublishLocalSettingsPath(const PatchContext& ctx)
{
	Trace("publish LocalSettingsPath");
	// LocalSettingsPath: 7 slots
	if (ctx.Option("LocalSettingsPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3"); pattern.size() > 0)
		{
			zpatch_local_path_1c = *pattern.get(0).get<std::uint32_t>(-178);
			zpatch_local_path_10 = *pattern.get(0).get<std::uint32_t>(-190);
			zpatch_local_episode_ptr = *pattern.get(0).get<std::uint32_t>(-151);
			zpatch_local_path_18 = *pattern.get(0).get<std::uint32_t>(-444);
			zpatch_local_path_08 = *pattern.get(0).get<std::uint32_t>(-432);
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3"); pattern.size() > 0)
		{
			zpatch_local_path_18 = *pattern.get(0).get<std::uint32_t>(-160);
			zpatch_local_path_1c = *pattern.get(0).get<std::uint32_t>(-55);
		}
	}
}

void PublishNewFilesCompatibility(const PatchContext& ctx)
{
	Trace("publish NewFilesCompatibility");
	// NewFilesCompatibility: 1 slot
	if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
		{
			zpatch_newfiles_register_slot = MatchTarget(pattern, 6, 10);
		}
	}
}

void PublishNewMemorySystem(const PatchContext& ctx)
{
	Trace("publish NewMemorySystem");
	// NewMemorySystem: 1 slot
	if (ctx.Option("NewMemorySystem", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "6A 01 B9 00 00 40 06"); pattern.size() > 0)
		{
			zpatch_nms_object = *pattern.get(0).get<std::uint32_t>(8);
		}
	}
}

void PublishNewSavesCompatibility(const PatchContext& ctx)
{
	Trace("publish NewSavesCompatibility");
	// NewSavesCompatibility: 3 slots
	if (ctx.Option("NewSavesCompatibility", "1") && Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08"); pattern.size() > 0)
		{
			zpatch_new_saves_src = *pattern.get(0).get<std::uint32_t>(1);
		}

		if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
		{
			zpatch_new_saves_resume = MatchAddress(pattern, 13);
		}
	}
}

void PublishSkipIntro(const PatchContext& ctx)
{
	Trace("publish SkipIntro");
	// SkipIntro: 1 slot
	if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
		{
			zpatch_skip_intro_match = MatchAddress(pattern);
		}
	}
}

void PublishSkipMenu(const PatchContext& ctx)
{
	Trace("publish SkipMenu");
	// SkipMenu: 3 slots
	if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 24, 28);
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
		}
	}
}

void PublishBenchmarkFix(const PatchContext& ctx)
{
	Trace("publish BenchmarkFix");
	// BenchmarkFix: 2 slots. The handler compares the build id before it reads
	// the INI (cmp 9 / cmp 0Dh), so 1.0.8.0 and 1.1.3.0 publish nothing either.
	if (ctx.Option("BenchmarkFix", "1") &&
	    !BuildIs(ctx.build, {GameBuild::v1080, GameBuild::v1130}))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
		{
			zpatch_benchmark_resume_a = MatchAddress(pattern, 13);
			zpatch_benchmark_resume_b = MatchAddress(pattern, 13);
		}
	}
}

void PublishRemoveGFWLUpdatePopup(const PatchContext& ctx)
{
	Trace("publish RemoveGFWLUpdatePopup");
	// RemoveGFWLUpdatePopup: 1 slot
	if (ctx.Option("RemoveGFWLUpdatePopup", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			zpatch_gfwl_resume = MatchAddress(pattern, 12);
		}
	}

}

void ApplyLoadDLCs(const PatchContext& ctx)
{
	Trace("LoadDLCs");
	// LoadDLCs: 18 rvas, 6 rvajumps
	if (ctx.Option("LoadDLCs", "1"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4137CD
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41445C
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1060.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4144D4
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41450E
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1060.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41453A
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1060.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41453F
				injector::MakeNOP(site, 12, true); //nop
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41356D
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4141FC
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x414274
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142AE
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142DA
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1070.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142DF
				injector::MakeNOP(site, 12, true); //nop
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 4C 24 ?? 51 8D 94 24 ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x48F558
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49035C
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1080.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4903D4
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49040E
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1080.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49043A
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1080.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49043F
				injector::MakeNOP(site, 12, true); //nop
			}
		}
	}
}

void ApplyLocalSettingsPath(const PatchContext& ctx)
{
	Trace("LocalSettingsPath");
	// LocalSettingsPath: 3 siginstalls
	if (ctx.Option("LocalSettingsPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "53 8B 5C 24 08 56 6A 00 6A 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 16, AddressOf(&zpatch_local_saves_slot), true); //patch
			injector::WriteMemory<std::uint32_t>(match + 32, AddressOf(&zpatch_local_iv_suffix), true); //patch
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 14, reinterpret_cast<void*>(&abi_local_settings_detour), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 13, reinterpret_cast<void*>(&abi_local_settings_fallback_detour), true); //jmp
		}
	}
}

void ApplyNewFilesCompatibility(const PatchContext& ctx)
{
	Trace("NewFilesCompatibility");
	// NewFilesCompatibility: 2 siginstalls, 1 sigjump
	if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_newfiles_update_a), true); //call
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_newfiles_update_b), true); //call
		}

		if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 28, reinterpret_cast<void*>(&abi_newfiles_register_body), true); //jmp
		}
	}
}

void ApplyNewMemorySystem(const PatchContext& ctx)
{
	Trace("NewMemorySystem");
	// NewMemorySystem: 1 siginstall
	if (ctx.Option("NewMemorySystem", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3D 80 99 99 59 73 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 34, reinterpret_cast<void*>(&abi_nms_apply), true); //call
			injector::MakeJMP(match - 29, reinterpret_cast<void*>(match - 16), true); //jmp
		}
	}
}

void ApplyNewSavesCompatibility(const PatchContext& ctx)
{
	Trace("NewSavesCompatibility");
	// NewSavesCompatibility: 1 siginstall, 1 sigjump
	if (ctx.Option("NewSavesCompatibility", "1") && Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_new_saves_compat), true); //jmp
		}
	}
}

void ApplyRemoveRGSCLoginRequirement(const PatchContext& ctx)
{
	Trace("RemoveRGSCLoginRequirement");
	// RemoveRGSCLoginRequirement: 1 rva, 1 rvajump, 1 sigjump
	if (ctx.Option("RemoveRGSCLoginRequirement", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1080)
		{
			// The original derives the site from 0x498460 and writes the same jump
			// twice (the duplicate write is kept). The recovered signature is unique
			// at that RVA on 1.0.8.0, so the scan replaces the address (research 11).
			if (auto pattern = Pattern(image, "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 00"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x498460
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
				// The original writes the same jump twice (duplicate-write rule).
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
			}
		}

		if (auto pattern = Pattern(image, "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
		}
	}
}

void ApplySavegameFix(const PatchContext& ctx)
{
	Trace("SavegameFix");
	// SavegameFix: 2 siginstalls, 1 sigwrite
	if (ctx.Option("SavegameFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "75 ? 83 C1 01 83 C0 01 83 F9 04"); pattern.size() > 0)
			{
				std::uint8_t* match = pattern.get(0).get<std::uint8_t>();
				injector::MakeCALL(match - 73, reinterpret_cast<void*>(&abi_savegame_fix_close), true); //call
				injector::MakeCALL(match - 143, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
				injector::MakeCALL(match - 176, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
				injector::MakeCALL(match - 280, reinterpret_cast<void*>(&abi_savegame_fix_mutex), true); //call
				injector::MakeCALL(match - 240, reinterpret_cast<void*>(&abi_savegame_fix_null), true); //call
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 2, true); //nop
			}
		}

		if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 86, reinterpret_cast<void*>(&abi_savegame_fix_mutex), true); //call
			injector::MakeCALL(match - 47, reinterpret_cast<void*>(&abi_savegame_fix_null), true); //call
			injector::MakeCALL(match + 10, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
			injector::MakeCALL(match + 90, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
			injector::MakeCALL(match + 128, reinterpret_cast<void*>(&abi_savegame_fix_close), true); //call
		}
	}
}

void ApplySkipIntro(const PatchContext& ctx)
{
	Trace("SkipIntro");
	// SkipIntro: 1 siginstall
	if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_skip_intro_body), true); //jmp
		}
	}
}

void ApplySkipLauncherOption(const PatchContext& ctx)
{
	Trace("SkipLauncher");
	// SkipLauncher: 7 literals
	if (ctx.Option("SkipLauncher", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "3B 4E 18 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 3, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "3B 56 18 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 3, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "74 C4 8B 07 3B 46 18 75 BD 68"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "74 EC 38 5E 06 74 E7 68"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 223, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 56 1C 3B 56 20 ? ? 6A 00 6A 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kXorEaxOpcode, true);       //xor
			injector::WriteMemory<std::uint8_t>(match + 1, kXorEaxEaxModrm, true); // eax,eax
		}
	}
}

void ApplySkipMenu(const PatchContext& ctx)
{
	Trace("SkipMenu");
	// SkipMenu: 3 siginstalls
	if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 23, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 2, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 2, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}
	}
}

void ApplyBenchmarkFix(const PatchContext& ctx)
{
	Trace("BenchmarkFix");
	// BenchmarkFix: 2 matchjumps
	if (ctx.Option("BenchmarkFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004, GameBuild::v1060,
		             GameBuild::v1070, GameBuild::v1061, GameBuild::v1100, GameBuild::v1110,
		             GameBuild::v1120, GameBuild::v12043}))
		{

			{
				if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
				{
					injector::MakeJMP(pattern.get(0).get<std::uint8_t>(8),
						reinterpret_cast<void*>(&abi_benchmark_a), true); //jmp
					if (pattern.size() > 1)
						injector::MakeJMP(pattern.get(1).get<std::uint8_t>(8),
							reinterpret_cast<void*>(&abi_benchmark_b), true); //jmp
				}
			}
		}
	}
}

void ApplyRemoveGFWLUpdatePopup(const PatchContext& ctx)
{
	Trace("RemoveGFWLUpdatePopup");
	// RemoveGFWLUpdatePopup: 1 siginstall
	if (ctx.Option("RemoveGFWLUpdatePopup", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_gfwl_stub), true); //jmp
		}
	}
}

void ApplyFastLoading(const PatchContext& ctx)
{
	Trace("FastLoading");
	// FastLoading: 1 sigwrite
	if (ctx.Option("FastLoading", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(1), kZeroByte, true); //patch
			}
		}
	}
}

void ApplyStartupTimeFix(const PatchContext& ctx)
{
	Trace("StartupTimeFix");
	// StartupTimeFix: 1 matchliteral
	if (ctx.Option("StartupTimeFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "68 00 C2 EC 80 68"); pattern.size() > 0)
			{
				for (std::size_t i = 0; i < pattern.size(); ++i)
					injector::WriteMemory<std::uint32_t>(
					    pattern.get(i).get<std::uint8_t>() + 6,
					    static_cast<std::uint32_t>(FindGlobalValue("StartupTimeFix")), true); //patch
			}
		}
	}
}

void ApplyNoLoadingSleep(const PatchContext& ctx)
{
	Trace("NoLoadingSleep");
	// NoLoadingSleep: 1 literalstore
	if (ctx.Option("NoLoadingSleep", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 88 13 00 00 FF 15"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 1, 0u, true); //patch
		}
	}
}

void ApplyOverrideGTADatPath(const PatchContext& ctx)
{
	Trace("OverrideGTADatPath");
	// OverrideGTADatPath: 1 literalstore
	if (ctx.Option("OverrideGTADatPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(
			    match + 1, static_cast<std::uint32_t>(FindGlobalValue("OverrideGTADatPath")), true); //patch
		}
	}
}

void ApplyVSyncFix(const PatchContext& ctx)
{
	Trace("VSyncFix");
	// VSyncFix: 2 literalstores
	if (ctx.Option("VSyncFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
		             GameBuild::v1100, GameBuild::v1110, GameBuild::v1120, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(
			    match + 30, static_cast<std::uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
			}

			if (auto pattern = Pattern(image, "8B 10 50 8B 82 A8 00 00 00 FF D0 83"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(
			    match + 13, static_cast<std::uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
			}
		}
	}
}
