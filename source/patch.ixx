// zp.patch - the two primitives every install arm goes through: the recovered
// signature scan (one candidate per site, all hits for the collectors) and the
// mapped-image write.
//
// The scan is the original's: whitespace-separated hex tokens, `?`/`??` the
// only wildcards, a token's low byte the compared value. The scanner itself is
// the vendored Hooking.Patterns (`hook::make_range_pattern` over the caller's
// image view), and a write goes through the injector.

module;

#include "common.hxx"
#include "common.h"

export module zp.patch;

import zp.common;

export
{
std::string NormalizeSignature(std::string_view signature);

bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept;

std::size_t FindAllPatterns(ModuleView module, std::string_view signature,
                            std::vector<const std::uint8_t*>& output) noexcept;

// The option pass's scan/write helpers (the steps every arm shares). Declared in
// `src/patches.h`; the arms in the feature modules call them through that header.
bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds);

hook::pattern Pattern(const GameImage& image, std::string_view signature);

// The default offset lives in src/patches.h only: repeating it in an exported
// declaration collides with that header in a consumer that includes both.
std::uint32_t MatchAddress(hook::pattern& pattern, std::ptrdiff_t offset);

std::uint32_t MatchTarget(hook::pattern& pattern, std::ptrdiff_t displacementOffset,
                          std::ptrdiff_t nextOffset);

std::uint32_t ReadDword(const std::uint8_t* address);

std::uint32_t Field(const std::uint8_t* match, std::ptrdiff_t offset);

std::uint8_t* MatchPointer(hook::pattern& pattern, std::ptrdiff_t offset);

std::uint32_t AddressOf(const void* pointer);

bool InImage(const GameImage& image, std::uintptr_t address);

std::uintptr_t BranchTarget(const std::uint8_t* match, std::ptrdiff_t siteOffset);

void WriteBranchNoRead(std::uint8_t* site, std::uint8_t opcode, void* destination);

void WriteCallNoRead(std::uint8_t* site, void* destination);

bool FusionFixGateClosed();

bool NewFilesConflictModulesPresent();

// The instruction bytes the arms store. An opcode is not data: naming them keeps
// `0xEB` at a site readable as "jmp rel8".
inline constexpr std::size_t kWordLength = 2;
inline constexpr std::size_t kBranchLength = 5;  // E8/E9 <rel32>
inline constexpr std::uint8_t kCallOpcode = 0xE8;
inline constexpr std::uint8_t kRetOpcode = 0xC3;
inline constexpr std::uint8_t kMovEaxOpcode = 0xB8;  // mov eax, imm32
inline constexpr std::uint8_t kPushadOpcode = 0x60;
inline constexpr std::uint8_t kXorEaxOpcode = 0x33;    // xor
inline constexpr std::uint8_t kXorEaxEaxModrm = 0xC0;  // eax, eax
inline constexpr std::uint8_t kJeRel32First = 0x0F;    // jcc rel32, two-byte opcode
inline constexpr std::uint8_t kJeRel32Second = 0x84;   // je
// 66 0F 6E C0 / 66 0F 6E C8 = movd xmm0, eax / movd xmm1, eax.
inline constexpr std::uint8_t kMovdXmm0Eax[] = {0x66, 0x0F, 0x6E, 0xC0};
inline constexpr std::uint8_t kMovdXmm1Eax[] = {0x66, 0x0F, 0x6E, 0xC8};
}  // export

namespace
{
// "AA ?? BB" spells one byte per three characters; there is no separator after
// the last token.
constexpr std::size_t kSignatureTokenWidth = 3;

std::size_t PatternLength(std::string_view normalized) noexcept
{
	return normalized.size() / kSignatureTokenWidth + 1;
}

// The original feeds every whitespace-separated token to strtoul(token, nullptr,
// 16) and compares the low byte; "?" and "??" are the only wildcards. A few
// signatures carry three-digit tokens such as "485", which the original reads as
// 0x85 - reproducing that here matters.
void ParseToken(std::string_view token, int& value, bool& wildcard)
{
	if (token == "?" || token == "??")
	{
		wildcard = true;
		value = 0;
		return;
	}

	wildcard = false;
	std::size_t i = 0;
	while (i < token.size() && IsWhite(token[i]))
		++i;

	bool negative = false;
	if (i < token.size() && (token[i] == '+' || token[i] == '-'))
	{
		negative = token[i] == '-';
		++i;
	}

	if (i + 1 < token.size() && token[i] == '0' && (token[i + 1] == 'x' || token[i + 1] == 'X'))
		i += 2;

	std::uint32_t accumulated = 0;
	for (; i < token.size(); ++i)
	{
		const char c = token[i];
		int digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			break;

		accumulated = accumulated * 16u + static_cast<std::uint32_t>(digit);
	}

	value = static_cast<int>((negative ? 0u - accumulated : accumulated) & kByteMask);
}

// Re-emit the pattern as the strict "AA ?? BB" form Hooking.Patterns expects.
bool Normalize(std::string_view signature, std::string& out)
{
	out.clear();
	out.reserve(signature.size());

	std::size_t i = 0;
	bool any = false;
	while (i < signature.size())
	{
		while (i < signature.size() && IsWhite(signature[i]))
			++i;
		if (i == signature.size())
			break;

		const std::size_t start = i;
		while (i < signature.size() && !IsWhite(signature[i]))
			++i;
		const std::string_view token = signature.substr(start, i - start);

		int value = 0;
		bool wildcard = false;
		ParseToken(token, value, wildcard);

		if (!out.empty())
			out.push_back(' ');

		if (wildcard)
		{
			out.push_back('?');
		}
		else
		{
			static const char kHex[] = "0123456789ABCDEF";
			out.push_back(kHex[(value >> 4) & kNibbleMask]);
			out.push_back(kHex[value & kNibbleMask]);
		}
		any = true;
	}
	return any;
}
} // namespace

std::string NormalizeSignature(std::string_view signature)
{
	std::string normalized;
	return Normalize(signature, normalized) ? normalized : std::string{};
}

bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept
{
	output = {};
	if (!module.base || module.size == 0 || signature.empty())
		return false;

	std::string normalized;
	if (!Normalize(signature, normalized))
		return false;

	// The scanner subtracts the pattern length from the end before iterating, so
	// reject a pattern longer than the view instead of relying on underflow.
	if (PatternLength(normalized) > module.size)
		return false;

	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(module.base);
	auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
	pattern.count_hint(1);
	if (pattern.size() == 0)
		return false;

	output.address = static_cast<const std::uint8_t*>(pattern.get(0).get<std::uint8_t>());
	return true;
}

std::size_t FindAllPatterns(ModuleView module, std::string_view signature,
                            std::vector<const std::uint8_t*>& output) noexcept
{
	output.clear();
	if (!module.base || module.size == 0 || signature.empty())
		return 0;

	std::string normalized;
	if (!Normalize(signature, normalized))
		return 0;
	if (PatternLength(normalized) > module.size)
		return 0;

	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(module.base);
	auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
	const std::size_t count = pattern.size();
	output.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
		output.push_back(static_cast<const std::uint8_t*>(pattern.get(i).get<std::uint8_t>()));
	return output.size();
}

bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds)
{
	for (const GameBuild candidate : builds)
	{
		if (candidate == build)
			return true;
	}
	return false;
}

// The original's hook::pattern, over the image the parity drivers hand in.
hook::pattern Pattern(const GameImage& image, std::string_view signature)
{
	const std::string normalized = NormalizeSignature(signature);
	if (normalized.empty())
		return hook::pattern{};
	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(image.base);
	return hook::make_range_pattern(begin, begin + image.size, normalized);
}

// The address of a site inside the current match.
std::uint32_t MatchAddress(hook::pattern& pattern, std::ptrdiff_t offset)
{
	return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
	    pattern.get(0).get<std::uint8_t>(offset)));
}

// A rel32 target inside the current match: the displacement at
// `displacementOffset`, measured from the instruction at `nextOffset`.
std::uint32_t MatchTarget(hook::pattern& pattern, std::ptrdiff_t displacementOffset,
                          std::ptrdiff_t nextOffset)
{
	return MatchAddress(pattern, nextOffset) +
	       *pattern.get(0).get<std::uint32_t>(displacementOffset);
}

// The dword stored at `address`.
std::uint32_t ReadDword(const std::uint8_t* address)
{
	std::uint32_t value = 0;
	std::memcpy(&value, address, sizeof(value));
	return value;
}

// A dword field inside a match: a call displacement, or a site pointer.
std::uint32_t Field(const std::uint8_t* match, std::ptrdiff_t offset)
{
	return ReadDword(match + offset);
}

// A pointer stored in a dword field of the current match.
std::uint8_t* MatchPointer(hook::pattern& pattern, std::ptrdiff_t offset)
{
	return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(
	    *pattern.get(0).get<std::uint32_t>(offset)));
}

// A pointer as the dword the game and the injector expect (the ASI is x86).
std::uint32_t AddressOf(const void* pointer)
{
	return static_cast<std::uint32_t>(
	    PublishedAddress(reinterpret_cast<std::uintptr_t>(pointer)));
}

bool InImage(const GameImage& image, std::uintptr_t address)
{
	const auto base = reinterpret_cast<std::uintptr_t>(image.base);
	return address >= base && address + kDwordLength <= base + image.size;
}

// Where the branch at `siteOffset` goes: the original re-points these.
std::uintptr_t BranchTarget(const std::uint8_t* match, std::ptrdiff_t siteOffset)
{
	const auto displacement = static_cast<std::int32_t>(Field(match, siteOffset + 2));
	return reinterpret_cast<std::uintptr_t>(match) + siteOffset + 6 +
	       static_cast<std::uintptr_t>(displacement);
}

// A five-byte branch written the way the original's collector writes it
// (opcode + rel32). injector::MakeCALL/MakeJMP first call
// GetBranchDestination, which for a `FF 15 <disp32>` site dereferences the
// operand; that read is discarded, and it is the only difference from the
// original. Use this where the dereference would read memory the site does
// not point into (e.g. a byte of the operand is overwritten by the same arm).
// Only WriteMemoryRaw is guaranteed by every injector shim (the real one, the
// dump shim and the trace shim), so the rel32 is built here.
void WriteBranchNoRead(std::uint8_t* site, std::uint8_t opcode, void* destination)
{
	std::uint8_t bytes[kBranchLength];
	bytes[0] = opcode;
	const auto displacement = static_cast<std::int32_t>(
	    reinterpret_cast<std::intptr_t>(destination) -
	    reinterpret_cast<std::intptr_t>(site + kBranchLength));
	std::memcpy(bytes + 1, &displacement, sizeof(displacement));
	injector::WriteMemoryRaw(site, bytes, sizeof(bytes), true); //write
}

void WriteCallNoRead(std::uint8_t* site, void* destination)
{
	WriteBranchNoRead(site, kCallOpcode, destination);
}

// The FusionFix compatibility probe. ModuleLacksSignature reads the two dwords
// at 0x20D3/0x20D7, so the image must be at least 0x20DB bytes to be probed.
constexpr std::uint32_t kFusionFixProbeImageSize = 0x20D7u + 4u;

bool FusionFixGateClosed()
{
	const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
	if (module == nullptr)
		return false;
	const auto* image = reinterpret_cast<const std::uint8_t*>(module);
	return ModuleLacksSignature(image, kFusionFixProbeImageSize);
}

bool NewFilesConflictModulesPresent()
{
	return GetModuleHandleA("GTAC.dll") != nullptr ||
	       GetModuleHandleA("JacksIVFixes.asi") != nullptr;
}
