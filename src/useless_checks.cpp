#include "pch.h"
// RemoveUselessChecks, the original way: three write shapes over the handler's
// own RVA tables, plus its singles. Every entry is `game base + RVA`, written
// unconditionally (the original verifies nothing before writing) and only on the
// builds the original gated it to.

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "common.h"
#include "useless_checks.h"

#if defined(ZP_DUMP_TOOL)
#include "../tests/dump_injector/injector/injector.hpp"
#elif defined(ZP_TRACE_WRITES)
#include "../tests/trace_injector/injector/injector.hpp"
#else
#include "injector/injector.hpp"
#endif

namespace
{
// The original's tables, one uint32 game RVA per site (generated data).
#include "useless_checks_data.inc"

static_assert(std::size(kUselessBuild9Nop5) == 2230u);
static_assert(std::size(kUselessBuild9MovAl1) == 45u);
static_assert(std::size(kUselessBuild9XorAlAl) == 56u);
static_assert(std::size(kUselessBuild8Nop5) == 2288u);
static_assert(std::size(kUselessBuild5Nop5) == 2466u);
static_assert(std::size(kUselessSingles9) == 19u);
static_assert(std::size(kUselessSingles8) == 2u);
static_assert(std::size(kUselessSingles5) == 2u);

constexpr std::uint8_t kNop = 0x90;
// Table 0x10106448: `mov al,1` followed by three NOPs.
constexpr std::uint8_t kMovAl1[] = {0xB0, 0x01, kNop, kNop, kNop};
// Table 0x10106368: `xor al,al` followed by three NOPs.
constexpr std::uint8_t kXorAlAl[] = {0x32, 0xC0, kNop, kNop, kNop};

// One 5-byte protected write per site: the handler's NOP-table loop.
void SweepNop5(GameImage image, const std::uint32_t* rvas, std::size_t count)
{
	constexpr std::uint8_t kNop5[] = {kNop, kNop, kNop, kNop, kNop};
	for (std::size_t i = 0; i < count; ++i)
	{
		injector::WriteMemoryRaw(image.base + rvas[i], const_cast<std::uint8_t*>(kNop5),
		                         sizeof(kNop5), true); //nop
	}
}

// Five 1-byte protected writes per site: the 45/56 tables protect each byte
// separately, so this must not collapse into one 5-byte write.
void SweepBytes(GameImage image, const std::uint32_t* rvas, std::size_t count,
                const std::uint8_t (&bytes)[5])
{
	for (std::size_t i = 0; i < count; ++i)
	{
		for (std::size_t b = 0; b < sizeof(bytes); ++b)
			injector::WriteMemory<std::uint8_t>(image.base + rvas[i] + b, bytes[b], true); //byte
	}
}

// One protected write of `length` bytes per single (the NOPs use MemoryFill,
// the two 0xEB bytes a byte store), in the handler's order.
void SweepSingles(GameImage image, const UselessCheckSingle* rows, std::size_t count)
{
	for (std::size_t i = 0; i < count; ++i)
	{
		if (rows[i].byte == kNop)
			injector::MakeNOP(image.base + rows[i].rva, rows[i].length, true); //nop
		else
			injector::WriteMemory<std::uint8_t>(image.base + rows[i].rva, rows[i].byte, true); //jmp
	}
}
} // namespace

void ApplyRemoveUselessChecks(GameBuild build, GameImage image)
{
	// The handler's order: the singles first, then that build's tables.
	switch (build)
	{
	case GameBuild::v1080:
		SweepSingles(image, kUselessSingles9, std::size(kUselessSingles9));
		SweepNop5(image, kUselessBuild9Nop5, std::size(kUselessBuild9Nop5));
		SweepBytes(image, kUselessBuild9MovAl1, std::size(kUselessBuild9MovAl1), kMovAl1);
		SweepBytes(image, kUselessBuild9XorAlAl, std::size(kUselessBuild9XorAlAl), kXorAlAl);
		break;
	case GameBuild::v1070:
		SweepSingles(image, kUselessSingles8, std::size(kUselessSingles8));
		SweepNop5(image, kUselessBuild8Nop5, std::size(kUselessBuild8Nop5));
		break;
	case GameBuild::v1040:
		SweepSingles(image, kUselessSingles5, std::size(kUselessSingles5));
		SweepNop5(image, kUselessBuild5Nop5, std::size(kUselessBuild5Nop5));
		break;
	default:
		break;
	}
}
