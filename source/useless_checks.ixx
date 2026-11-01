// zp.useless_checks — RemoveUselessChecks as the original handler does it.
//
// Original: sub_10044BD0 writes a per-build list of game RVAs as NOP or one-byte
// patches - 19/2/2 singles first, then its five RVA tables (2230/45/56 on build 9,
// 2288 on build 8, 2466 on build 5). The rows are the generated, SHA-pinned
// `src/useless_checks_data.inc`; each is `game base + RVA`, written unconditionally
// (the original verifies nothing) and only on the builds it gated. The caller owns
// the RemoveUselessChecks INI gate, like every other arm.

module;

#include "common.hxx"
#include "common.h"

export module zp.useless_checks;

export namespace zp
{
// One single write: `length` bytes of `byte` at game base + rva.
struct UselessCheckSingle
{
	std::uint32_t rva;
	std::uint8_t length;
	std::uint8_t byte;
};

// The original handler, applied at one point: the singles first, then the
// build's RVA tables. Writes only on builds 5 (v1040), 8 (v1070) and 9 (v1080);
// every other build writes nothing.
void ApplyRemoveUselessChecks(GameBuild build, GameImage image);
}  // namespace zp

namespace
{
using zp::UselessCheckSingle;

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
// The two five-byte patterns the handler's byte tables store.
constexpr std::array<std::uint8_t, 5> kMovAl1 = {0xB0, 0x01, kNop, kNop, kNop};   // mov al,1
constexpr std::array<std::uint8_t, 5> kXorAlAl = {0x32, 0xC0, kNop, kNop, kNop};  // xor al,al

// One 5-byte protected write per site: the handler's NOP-table loop.
void SweepNop5(GameImage image, std::span<const std::uint32_t> rvas)
{
	constexpr std::array<std::uint8_t, 5> kNop5 = {kNop, kNop, kNop, kNop, kNop};
	for (const std::uint32_t rva : rvas)
	{
		injector::WriteMemoryRaw(image.base + rva, const_cast<std::uint8_t*>(kNop5.data()),
		                         kNop5.size(), true); //nop
	}
}

// Five 1-byte protected writes per site: the 45/56 tables protect each byte
// separately, so this must not collapse into one 5-byte write.
void SweepBytes(GameImage image, std::span<const std::uint32_t> rvas,
                std::span<const std::uint8_t> bytes)
{
	for (const std::uint32_t rva : rvas)
	{
		for (std::size_t b = 0; b < bytes.size(); ++b)
			injector::WriteMemory<std::uint8_t>(image.base + rva + b, bytes[b], true); //byte
	}
}

// One protected write of `length` bytes per single (the NOPs use MakeNOP, the
// two 0xEB bytes a byte store), in the handler's order.
void SweepSingles(GameImage image, std::span<const UselessCheckSingle> rows)
{
	for (const UselessCheckSingle& row : rows)
	{
		if (row.byte == kNop)
			injector::MakeNOP(image.base + row.rva, row.length, true); //nop
		else
			injector::WriteMemory<std::uint8_t>(image.base + row.rva, row.byte, true); //jmp
	}
}

// The original's per-build rows; a build with no row writes nothing (the handler's
// default). A row's spans are its generated tables in the handler's write order:
// singles, the NOP-5 table, then the two five-byte tables.
struct BuildSweep final
{
	GameBuild build;
	std::span<const UselessCheckSingle> singles;
	std::span<const std::uint32_t> nop5;
	std::span<const std::uint32_t> movAl1;
	std::span<const std::uint8_t> movAl1Bytes;
	std::span<const std::uint32_t> xorAlAl;
	std::span<const std::uint8_t> xorAlAlBytes;
};

constexpr BuildSweep kBuildSweeps[] = {
    {GameBuild::v1080, kUselessSingles9, kUselessBuild9Nop5, kUselessBuild9MovAl1, kMovAl1,
     kUselessBuild9XorAlAl, kXorAlAl},
    {GameBuild::v1070, kUselessSingles8, kUselessBuild8Nop5, {}, {}, {}, {}},
    {GameBuild::v1040, kUselessSingles5, kUselessBuild5Nop5, {}, {}, {}, {}},
};
}  // namespace

namespace zp
{
void ApplyRemoveUselessChecks(GameBuild build, GameImage image)
{
	for (const BuildSweep& sweep : kBuildSweeps)
	{
		if (sweep.build != build)
			continue;
		SweepSingles(image, sweep.singles);
		SweepNop5(image, sweep.nop5);
		SweepBytes(image, sweep.movAl1, sweep.movAl1Bytes);
		SweepBytes(image, sweep.xorAlAl, sweep.xorAlAlBytes);
		return;
	}
}
}  // namespace zp
