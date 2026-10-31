// zp.graphics - Rendering: shadows, alpha, emissive lerp, reflections, LOD, shader
// streaming, VRAM, the cutscene chain and the window style.
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

export module zp.graphics;

import zp.common;
import zp.game;
import zp.patch;
import zp.log;
import zp.catalogue;
import zp.useless_checks;

export
{
void PublishBuildingDynamicShadows(const PatchContext& ctx);
void PublishEmissiveLerpFix(const PatchContext& ctx);
void PublishSuperLODFix(const PatchContext& ctx);
void PublishHighQualityReflections(const PatchContext& ctx);
void PublishCutsceneFixes(const PatchContext& ctx);
void ApplyBuildingAlphaFix(const PatchContext& ctx);
void ApplyBuildingDynamicShadows(const PatchContext& ctx);
void ApplyEmissiveLerpFix(const PatchContext& ctx);
void ApplyForceDynamicShadowsEverywhere(const PatchContext& ctx);
void ApplyForceShadowsOnObjects(const PatchContext& ctx);
void ApplyImprovedShaderStreaming(const PatchContext& ctx);
void ApplyRemoveBoundingBoxCulling(const PatchContext& ctx);
void ApplyRemoveStaticCarShadows(const PatchContext& ctx);
void ApplyRemoveStaticCutsceneCarShadows(const PatchContext& ctx);
void ApplyVRAMFix(const PatchContext& ctx);
void ApplySuperLODFix(const PatchContext& ctx);
void ApplyHighQualityReflections(const PatchContext& ctx);
void ApplyCutsceneFixes(const PatchContext& ctx);
void ApplyBorderlessWindowed(const PatchContext& ctx);
void ApplyNoImpostorHeightCutoff(const PatchContext& ctx);
void ApplyNoLODLightHeightCutoff(const PatchContext& ctx);
}  // export

constexpr GameRva kSuperLodFail_1080{0x4A3D37u};
constexpr GameRva kSuperLodOk_1080{0x4A3CEFu};
constexpr GameRva kSuperLodLookup_1080{0x58AAE0u};
constexpr GameRva kSuperLodFail_1070{0x540E67u};
constexpr GameRva kSuperLodOk_1070{0x540E1Fu};
constexpr GameRva kSuperLodLookup_1070{0x4DD2D0u};

void PublishBuildingDynamicShadows(const PatchContext& ctx)
{
	Trace("publish BuildingDynamicShadows");
	// BuildingDynamicShadows: 4 slots
	if (ctx.Option("BuildingDynamicShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
		{
			zpatch_bds_resume_a = MatchAddress(pattern, 15);
			zpatch_bds_resume_b = MatchAddress(pattern, 112);
		}

		// The two hash slots take the same field of the FIRST and the SECOND
		// match (recovered rows: dword_10159C1C index 0, dword_10159C24 index 1).
		// The two matches hold different pointers (0xD8FA2C / 0xDCF48C on 1080),
		// so publishing both from the first is not the original's state.
		auto hashes = Pattern(
		    image, "8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
		if (hashes.size() > 0)
			zpatch_bds_hash_a = *hashes.get(0).get<std::uint32_t>(20);
		if (hashes.size() > 1)
			zpatch_bds_hash_b = *hashes.get(1).get<std::uint32_t>(20);
	}
}

void PublishEmissiveLerpFix(const PatchContext& ctx)
{
	Trace("publish EmissiveLerpFix");
	// EmissiveLerpFix: 2 slots
	if (ctx.Option("EmissiveLerpFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
		{
			zpatch_emissive_const = MatchAddress(pattern, -16);
			zpatch_emissive_resume = MatchAddress(pattern, -12);
		}
	}
}

void PublishSuperLODFix(const PatchContext& ctx)
{
	Trace("publish SuperLODFix");
	// SuperLODFix: 6 slots
	if (ctx.Option("SuperLODFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1070)
		{
			zpatch_super_lod_fail = AddressOf(kSuperLodFail_1080.site_ptr(image.base));
			zpatch_super_lod_ok = AddressOf(kSuperLodOk_1080.site_ptr(image.base));
			zpatch_super_lod_lookup = AddressOf(kSuperLodLookup_1080.site_ptr(image.base));
		}

		if (ctx.build == GameBuild::v1080)
		{
			zpatch_super_lod_fail = AddressOf(kSuperLodFail_1070.site_ptr(image.base));
			zpatch_super_lod_ok = AddressOf(kSuperLodOk_1070.site_ptr(image.base));
			zpatch_super_lod_lookup = AddressOf(kSuperLodLookup_1070.site_ptr(image.base));
		}
	}
}

void PublishHighQualityReflections(const PatchContext& ctx)
{
	Trace("publish HighQualityReflections");
	// HighQualityReflections: 2 slots
	if (ctx.Option("HighQualityReflections", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
		{
			zpatch_hqr_resume = MatchAddress(pattern, 19);
			zpatch_hqr_builder = MatchTarget(pattern, 15, 19);
		}
	}
}

void PublishCutsceneFixes(const PatchContext& ctx)
{
	Trace("publish CutsceneFixes");
	// CutsceneFixes: 2 slots
	if (ctx.Option("CutsceneFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
		{
			zpatch_cutscene_target = MatchAddress(pattern) + *pattern.get(0).get<std::uint32_t>(-4);
			zpatch_cutscene_poke = MatchTarget(pattern, -4, 79);
		}
	}
}

void ApplyBuildingAlphaFix(const PatchContext& ctx)
{
	Trace("BuildingAlphaFix");
	// BuildingAlphaFix: 1 literal, 1 siginstall
	if (ctx.Option("BuildingAlphaFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 84"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			if (InImage(image, reinterpret_cast<std::uintptr_t>(match + 7)))
				injector::MakeJMP(match + 7, reinterpret_cast<void*>(BranchTarget(match, 7)), true); //jmp
		}

		if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 13, true); //nop
		}
	}
}

void ApplyBuildingDynamicShadows(const PatchContext& ctx)
{
	Trace("BuildingDynamicShadows");
	// BuildingDynamicShadows: 1 sigjump, 1 sigwrite
	if (ctx.Option("BuildingDynamicShadows", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(2), kZeroByte, true); //patch
			}
		}

		if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_building_dynamic_shadows), true); //jmp
		}
	}
	// RemoveUselessChecks: the original handler applies its singles and its RVA
	// tables in one go, as the loader's 27th option read (right after
	// BuildingDynamicShadows). The port's arm list is alphabetical, so this is
	// the nearest equivalent slot. The early position is load-bearing: the tables
	// overwrite one fixed byte of MiscFixes' and RestoreDeathMusic's signatures,
	// so those two later installs are suppressed exactly as in the original.
}

void ApplyEmissiveLerpFix(const PatchContext& ctx)
{
	Trace("EmissiveLerpFix");
	// EmissiveLerpFix: 1 siginstall
	if (ctx.Option("EmissiveLerpFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 24, reinterpret_cast<void*>(&abi_emissive_lerp_shim), true); //jmp
		}
	}
}

void ApplyForceDynamicShadowsEverywhere(const PatchContext& ctx)
{
	Trace("ForceDynamicShadowsEverywhere");
	// ForceDynamicShadowsEverywhere: 1 literal
	if (ctx.Option("ForceDynamicShadowsEverywhere", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 46 4C 00 80 00 00 74"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 7, 2, true); //nop
		}
	}
}

void ApplyForceShadowsOnObjects(const PatchContext& ctx)
{
	Trace("ForceShadowsOnObjects");
	// ForceShadowsOnObjects: 1 literal
	if (ctx.Option("ForceShadowsOnObjects", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 47 40 00 10 00 00 74 07 81 4E 24 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 7, kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplyImprovedShaderStreaming(const PatchContext& ctx)
{
	Trace("ImprovedShaderStreaming");
	// ImprovedShaderStreaming: 1 siginstall
	if (ctx.Option("ImprovedShaderStreaming", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 85,
			                   reinterpret_cast<void*>(&abi_improved_shader_streaming_find_hpe), true); //call
			injector::MakeJMP(match + 98, reinterpret_cast<void*>(match + 245), true); //jmp
			injector::MakeCALL(match + 253,
			                   reinterpret_cast<void*>(&abi_improved_shader_streaming_find_v), true); //call
			injector::MakeJMP(match + 266, reinterpret_cast<void*>(match + 1158), true); //jmp
		}
	}
}

void ApplyRemoveBoundingBoxCulling(const PatchContext& ctx)
{
	Trace("RemoveBoundingBoxCulling");
	// RemoveBoundingBoxCulling: 1 siginstall
	if (ctx.Option("RemoveBoundingBoxCulling", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3C 01 57 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			if (InImage(image, reinterpret_cast<std::uintptr_t>(match + 3)))
				injector::MakeJMP(match + 3, reinterpret_cast<void*>(BranchTarget(match, 3)), true); //jmp
		}
	}
}

void ApplyRemoveStaticCarShadows(const PatchContext& ctx)
{
	Trace("RemoveStaticCarShadows");
	// RemoveStaticCarShadows: 1 literal
	if (ctx.Option("RemoveStaticCarShadows", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 11 4C 24 0C 76 5B D9 45 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplyRemoveStaticCutsceneCarShadows(const PatchContext& ctx)
{
	Trace("RemoveStaticCutsceneCarShadows");
	// RemoveStaticCutsceneCarShadows: 1 literal
	if (ctx.Option("RemoveStaticCutsceneCarShadows", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 11 44 24 0C 76 5B D9 45 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplyVRAMFix(const PatchContext& ctx)
{
	Trace("VRAMFix");
	// VRAMFix: 1 sigjump
	if (ctx.Option("VRAMFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 10 D9 2C 24 83 C4 1C C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_vram_fix_shim), true); //jmp
		}
	}
}

void ApplySuperLODFix(const PatchContext& ctx)
{
	Trace("SuperLODFix");
	// SuperLODFix: 2 rvajumps
	if (ctx.Option("SuperLODFix", "1"))
	{
		const GameImage& image = ctx.image;

		// The original jumped at 0x4A3CE9 (1.0.7.0) / 0x540E19 (1.0.8.0). The
		// recovered signature is the same on both and unique per gated build
		// (research 08), so scan inside the gate instead of naming the RVA.
		if (ctx.build == GameBuild::v1070)
		{
			if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
			{
				injector::MakeJMP(pattern.get(0).get<std::uint8_t>(),
				                  reinterpret_cast<void*>(&abi_super_lod_fix_shim), true); //jmp
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
			{
				injector::MakeJMP(pattern.get(0).get<std::uint8_t>(),
				                  reinterpret_cast<void*>(&abi_super_lod_fix_shim), true); //jmp
			}
		}
	}
}

void ApplyHighQualityReflections(const PatchContext& ctx)
{
	Trace("HighQualityReflections");
	// HighQualityReflections: 1 siginstall
	if (ctx.Option("HighQualityReflections", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_hqr_body), true); //jmp
		}
	}
}

void ApplyCutsceneFixes(const PatchContext& ctx)
{
	Trace("CutsceneFixes");
	// CutsceneFixes: 2 siginstalls, 1 sigwrite
	if (ctx.Option("CutsceneFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 5, reinterpret_cast<void*>(&abi_cutscene_chain_b), true); //call
		}

		if (auto pattern = Pattern(image, "8B CF 88 44 24 0F E8 ? ? ? ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 5, reinterpret_cast<void*>(&abi_cutscene_chain_b), true); //call
		}

		{
			// 1080 has exactly one match of the primary pattern (0xC9A840); the
			// alternate is only for builds where it is absent. Falling back on
			// "<= 1 match" threw that single match away and applied nothing.
			auto pattern = Pattern(image, "74 24 8B 44 24 2C 83 F8 03");
			if (pattern.size() == 0)
				pattern = Pattern(image, "74 0B 83 7C 24 2C 03");
			if (pattern.size() > 0)
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(), kShortJmpOpcode, true); //jmp
		}
	}
}

void ApplyBorderlessWindowed(const PatchContext& ctx)
{
	Trace("BorderlessWindowed");
	// BorderlessWindowed: 2 matchjumps, 2 sigwrites
	if (ctx.Option("BorderlessWindowed", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00"); pattern.size() > 0)
			{
				// The call is installed before the 0x90: offset 5 is the fourth
				// byte of the `FF 15 <disp32>` operand, so writing it first would
				// make a read of the old destination dereference the corrupted
				// operand (0x00D6B460 becomes 0x90D6B460). The original's
				// collector writes opcode+rel32 without reading it, so do that.
				WriteCallNoRead(pattern.get(0).get<std::uint8_t>(), reinterpret_cast<void*>(&abi_borderless_w));
				if (pattern.size() > 1)
					WriteCallNoRead(pattern.get(1).get<std::uint8_t>(), reinterpret_cast<void*>(&abi_borderless_a));
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(5), 1, true); //nop
				if (pattern.size() > 1)
					injector::MakeNOP(pattern.get(1).get<std::uint8_t>(5), 1, true); //nop
			}
		}
	}
}

void ApplyNoImpostorHeightCutoff(const PatchContext& ctx)
{
	Trace("NoImpostorHeightCutoff");
	// NoImpostorHeightCutoff: 2 matchliterals
	if (ctx.Option("NoImpostorHeightCutoff", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09"); pattern.size() > 0)
			{
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), kWordLength, true); //nop
				if (pattern.size() > 1)
					injector::MakeNOP(pattern.get(1).get<std::uint8_t>(), kWordLength, true); //nop
			}
		}
	}
}

void ApplyNoLODLightHeightCutoff(const PatchContext& ctx)
{
	Trace("NoLODLightHeightCutoff");
	// NoLODLightHeightCutoff: 1 literalstore
	if (ctx.Option("NoLODLightHeightCutoff", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, kWordLength, true); //nop
		}
	}
}
