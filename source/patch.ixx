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

PatchResult ApplyMappedOperation(GameImage image, std::string_view signature, std::int32_t offset,
                                 std::uint32_t moduleRva, std::string_view replacement,
                                 std::uint8_t*& writtenAt, std::size_t& written) noexcept;
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
bool ParseToken(std::string_view token, int& value, bool& wildcard)
{
	if (token == "?" || token == "??")
	{
		wildcard = true;
		value = 0;
		return true;
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
	return true;
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

// ---------------------------------------------------------------------------
// mapped image writes
// ---------------------------------------------------------------------------

namespace
{
constexpr std::size_t kMaxPatchLength = 64;

bool ParseHexBytes(std::string_view text, std::uint8_t* out, std::size_t capacity,
                   std::size_t& written)
{
	written = 0;
	std::size_t cursor = 0;
	const auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	};

	while (cursor < text.size())
	{
		const std::size_t space = text.find(' ', cursor);
		const std::string_view token =
			text.substr(cursor, space == std::string_view::npos ? std::string_view::npos
			                                                    : space - cursor);
		if (token.size() != 2 || written >= capacity)
			return false;

		const int high = nibble(token[0]);
		const int low = nibble(token[1]);
		if (high < 0 || low < 0)
			return false;

		out[written++] = static_cast<std::uint8_t>((high << 4) | low);
		if (space == std::string_view::npos)
			break;
		cursor = space + 1;
	}
	return written > 0;
}
} // namespace

PatchResult ApplyMappedOperation(GameImage image, std::string_view signature, std::int32_t offset,
                                 std::uint32_t moduleRva, std::string_view replacement,
                                 std::uint8_t*& writtenAt, std::size_t& written) noexcept
{
	writtenAt = nullptr;
	written = 0;
	if (!image.base || image.size == 0)
		return {PatchResultCode::invalid_input, "invalid mapped image"};

	std::uint8_t bytes[kMaxPatchLength];
	std::size_t length = 0;
	if (!ParseHexBytes(replacement, bytes, sizeof(bytes), length))
		return {PatchResultCode::invalid_input, "invalid replacement text"};

	std::uint8_t* site = nullptr;
	if (moduleRva != 0)
	{
		if (moduleRva + length > image.size)
			return {PatchResultCode::invalid_input, "rva outside image"};
		site = image.base + moduleRva;
	}
	else
	{
		Match match;
		if (!FindPattern({image.base, image.size}, signature, match))
			return {PatchResultCode::not_found, "pattern not found"};

		const auto matchOffset = static_cast<std::ptrdiff_t>(match.address - image.base);
		const auto siteOffset = matchOffset + static_cast<std::ptrdiff_t>(offset);
		if (siteOffset < 0 || static_cast<std::size_t>(siteOffset) + length > image.size)
			return {PatchResultCode::invalid_input, "write outside image"};
		site = image.base + siteOffset;
	}

	injector::WriteMemoryRaw(site, bytes, length, true);
	writtenAt = site;
	written = length;
	return {};
}
