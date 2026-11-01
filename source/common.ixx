// zp.common - the pieces every module and the entry translation unit share.
//
// The header (`common.hxx`) carries the includes; this unit carries the
// declarations.

module;

#include "common.hxx"

#if defined(ZP_DUMP_TOOL)
// The write-set tool's own image; the linker provides this symbol.
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

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
// The whitespace the INI reader and the signature parser skip: space, tab, CR, LF,
// FF, VT.
bool IsWhite(char c);

// Trim both ends of the same whitespace set (the INI reader's rule).
std::string_view Trim(std::string_view value);

// The recovered code's byte arithmetic: a low-byte mask and a low-nibble mask.
inline constexpr std::uint8_t kByteMask = 0xFFu;    // low byte of a value
inline constexpr std::uint8_t kNibbleMask = 0x0Fu;  // low hex digit

// The four bytes of the little-endian dwords the original compares.
inline constexpr std::size_t kDwordLength = sizeof(std::uint32_t);

// The short-jump opcode the NewSaves flag toggle chooses; its twin kJeRel8Opcode
// lives with the body that writes it.
inline constexpr std::uint8_t kShortJmpOpcode = 0xEBu;  // jmp rel8

// A value an installer publishes for the game. The dump (ZP_DUMP_TOOL) substitutes
// a fixed stand-in for its own addresses; the ASI publishes the value unchanged.
std::uintptr_t PublishedAddress(std::uintptr_t value) noexcept;
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

#if defined(ZP_DUMP_TOOL)
namespace
{
// The dump never runs the game, so a pointer into the tool itself is never usable
// there: publishing one would only record where the tool happened to load. A fixed
// stand-in keeps the write visible at the same site with a layout-independent value.
constexpr std::uint32_t kDumpToolPointer = 0xD0000000u;

bool IsToolPointer(std::uintptr_t address) noexcept
{
	const auto* base = reinterpret_cast<const std::uint8_t*>(&__ImageBase);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base);
	return address >= start && address < start + nt->OptionalHeader.SizeOfImage;
}
}  // namespace
#endif

std::uintptr_t PublishedAddress(std::uintptr_t value) noexcept
{
#if defined(ZP_DUMP_TOOL)
	return IsToolPointer(value) ? std::uintptr_t{kDumpToolPointer} : value;
#else
	return value;
#endif
}
