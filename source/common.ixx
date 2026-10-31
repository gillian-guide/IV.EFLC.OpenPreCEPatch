// zp.common - the pieces every module and the entry translation unit share.
//
// The header (`common.hxx`) carries the includes; this unit carries the actual
// declarations. Layout follows GTAIV.EFLC.FusionFix: `module;` global fragment
// with the includes, then `export module`, then the exported declarations.

module;

#include "common.hxx"

export module zp.common;

export namespace zp
{
// A pointer below this is not a mapped image (the fault tracer's filter and the
// driveby diagnostic's plausibility check).
inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000u;

// The 32-bit user-mode address-space ceiling.
inline constexpr std::uintptr_t kUserModeCeiling = 0x80000000u;
}  // namespace zp

export
{
// The whitespace the INI reader and the signature parser skip: space, tab, CR,
// LF, FF, VT.
bool IsWhite(char c);

// Trim both ends of the same whitespace set (the INI reader's rule).
std::string_view Trim(std::string_view value);

// The recovered code's byte arithmetic: a low-byte mask and a low-nibble mask.
inline constexpr std::uint8_t kByteMask = 0xFFu;    // low byte of a value
inline constexpr std::uint8_t kNibbleMask = 0x0Fu;  // low hex digit

// The four bytes of the little-endian dwords the original compares.
inline constexpr std::size_t kDwordLength = sizeof(std::uint32_t);
}  // export

namespace
{
constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
}  // namespace

bool IsWhite(char c)
{
	return kWhitespace.find(c) != std::string_view::npos;
}

std::string_view Trim(std::string_view value)
{
	while (!value.empty() && IsWhite(value.front()))
		value.remove_prefix(1);
	while (!value.empty() && IsWhite(value.back()))
		value.remove_suffix(1);
	return value;
}
