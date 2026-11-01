// zp.input - Mouse/focus behaviour.

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "hooks.h"
#include "rebased.h"
#include "hook_bodies.h"
#include "xlive/xliveless.h"

export module zp.input;

import zp.common;
import zp.game;

export
{
void PublishMouseFix(const PatchContext& ctx);
void ApplyMouseFix(const PatchContext& ctx);
void ApplyDoNotPauseOnMinimize(const PatchContext& ctx);
}  // export

constexpr std::uint8_t kMinimizePauseTick = 0x06u;  // DoNotPauseOnMinimize

void PublishMouseFix(const PatchContext& ctx)
{
	Trace("publish MouseFix");
	// MouseFix: 6 slots
	if (ctx.Option("MouseFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
		{
			zpatch_mf_x = *pattern.get(0).get<std::uint32_t>(13);
			zpatch_mf_y = *pattern.get(0).get<std::uint32_t>(20);
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
		{
			zpatch_mf_flag = *pattern.get(0).get<std::uint32_t>(20);
		}

		if (auto pattern = Pattern(image, "83 EC 08 DB 05 ? ? ? ? 68"); pattern.size() > 0)
		{
			zpatch_mf_time = MatchAddress(pattern);
		}

		if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
		{
			zpatch_mf_resume_a = MatchAddress(pattern);
			zpatch_mf_resume_b = MatchAddress(pattern, 1878);
		}
	}
}

void ApplyMouseFix(const PatchContext& ctx)
{
	Trace("MouseFix");
	// MouseFix: 8 siginstalls
	if (ctx.Option("MouseFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_float), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_neg), true); //jmp
		}

		// The two sites differ only in the axis operands (6A 00 / 8B CE and 6A 01 / 8B CB);
		// each gets the movd replacement at +5/+33 and the two-byte nops that disable the
		// original cvtsi2ss prologue at +3/+31.
		constexpr std::string_view kMovdSitePatterns[] = {
		    "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
		    "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07",
		};
		for (const std::string_view site : kMovdSitePatterns)
		{
			if (auto pattern = Pattern(image, site); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemoryRaw(injector::raw_ptr(match + 5),
				                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
				                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
				injector::WriteMemoryRaw(injector::raw_ptr(match + 33),
				                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
				                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
				injector::MakeNOP(match + 3, 2, true); //nop
				injector::MakeNOP(match + 31, 2, true); //nop
			}
		}

		if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemoryRaw(injector::raw_ptr(match + 5),
			                         const_cast<std::uint8_t*>(kMovdXmm1Eax),
			                         sizeof(kMovdXmm1Eax), true); //movd xmm1, eax
			injector::MakeNOP(match + 3, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_flag), true); //jmp
		}

		if (auto pattern = Pattern(image, "56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 51, reinterpret_cast<void*>(&abi_mouse_axis_int), true); //call
			injector::MakeCALL(match + 90, reinterpret_cast<void*>(&abi_mouse_axis_int), true); //call
		}

		if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 15, reinterpret_cast<void*>(&abi_mouse_angle), true); //jmp
			injector::MakeJMP(match + 1862, reinterpret_cast<void*>(&abi_mouse_advance), true); //jmp
		}
	}
}

void ApplyDoNotPauseOnMinimize(const PatchContext& ctx)
{
	Trace("DoNotPauseOnMinimize");
	// DoNotPauseOnMinimize: 2 sigwrites
	if (ctx.Option("DoNotPauseOnMinimize", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "6A 0A 56 50 FF D2 A1"); pattern.size() > 0)
		{
			injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(1),
			                                    kMinimizePauseTick, true); // wait 10 -> 6
		}

		if (auto pattern = Pattern(image, "75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15"); pattern.size() > 0)
		{
			injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 2, true); //nop
		}
	}
}
