#pragma once
// SkipLauncher: the launcher/SecuROM bypass descended from XLivelessAddon,
// reimplemented from ZolikaPatch's handler at 0x10045ce0.
// It patches the launcher/SecuROM checks by signature so the game starts
// without PlayGTAIV.exe. Writes target a caller-owned mapped image; the DFA
// file-API hooks (0x10004850) and ROM patches (0x10004620) are separate stages.

#include "common.h"
#include "patches.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

// One in-place edit of a matched launcher window: the recovered handler
// overwrites the launcher check at the match (a NOP run, an xor, a one-byte
// branch or a short jump), so nothing here is callable and there is no ABI.
struct SkipLauncherWrite final
{
	enum class Kind : std::uint8_t
	{
		write,  // store `bytes` at match + offset
		fill,   // store `count` copies of `fill` (the recovered rows are NOPs)
		jump,   // rewrite the rel32 at match + offset to `jumpToEntry`'s match
	};

	Kind kind{};
	std::int32_t offset{};
	std::span<const std::uint8_t> bytes{};  // write: the exact bytes (its length)
	std::uint16_t count{};                  // fill: the byte count
	std::uint8_t fill{};                    // fill: the repeated byte (NOP in the data)
	std::uint32_t jumpToEntry{};            // jump: the target entry index
};

// The row builders: a byte row's length is its own data's size, and a fill row
// says how many bytes it wipes, so no row repeats a length.
inline constexpr std::uint8_t kLauncherNopByte = 0x90;

[[nodiscard]] constexpr SkipLauncherWrite WriteBytes(
    std::int32_t offset, std::span<const std::uint8_t> bytes) noexcept
{
	return {.kind = SkipLauncherWrite::Kind::write, .offset = offset, .bytes = bytes};
}

[[nodiscard]] constexpr SkipLauncherWrite NopFill(std::int32_t offset,
                                                  std::uint16_t count) noexcept
{
	return {
	    .kind = SkipLauncherWrite::Kind::fill, .offset = offset, .count = count,
	    .fill = kLauncherNopByte};
}

[[nodiscard]] constexpr SkipLauncherWrite Jump(std::int32_t offset,
                                               std::uint32_t entry) noexcept
{
	return {.kind = SkipLauncherWrite::Kind::jump, .offset = offset, .jumpToEntry = entry};
}

struct SkipLauncherEntry final
{
	std::string_view signature;
	std::span<const SkipLauncherWrite> writes;  // empty for the jump-target row
};

// Multi-hit collector: the original patches the first two matches of one
// pattern and the last two of another with "mov al,1; ret; nop". `stub` is the
// exact window it stores; its size is the store length.
struct SkipLauncherCollector final
{
	enum class Target : std::uint8_t
	{
		first_two,
		last_two,
	};
	std::string_view pattern;
	Target target;
	std::span<const std::uint8_t> stub;
};

// Reimplemented hook bodies the installers jump to; the original installs E8/E9
// jumps from the game module into its own.
enum class SkipHook : std::uint8_t
{
	dfa_init,           // 0x10004780: return 1
	create_file,        // 0x10004820: CreateFileA through the captured slot
	close_handle,       // 0x10004790: CloseHandle with an exception guard
	set_file_pointer,   // 0x10004760
	get_file_size_ex,   // 0x10004720
	read_file,          // 0x10004740
	rom_reset,          // 0x10004570: ROM window setup + object reset
	rom_set,            // 0x10004610: *(u32*)(a1+32) = *(u32*)(a1+28); return 1
};

using SkipHookResolver = std::uintptr_t (*)(SkipHook hook, void* user);

struct SkipLauncherInstall final
{
	std::string_view signature;
	std::int32_t offset;
	std::uint8_t opcode;  // 0xE8 call or 0xE9 jump
	SkipHook hook;
};

struct SkipLauncherCollectedTarget final
{
	std::string_view targetSignature;
	std::uint32_t index;  // collected match index the jump is installed at
};

struct SkipLauncherCollectedInstall final
{
	std::string_view collector;
	std::span<const SkipLauncherCollectedTarget> targets;
};

// Table accessors: the recovered rows as read-only spans.
std::span<const SkipLauncherEntry> SkipLauncherEntries() noexcept;
std::span<const SkipLauncherCollector> SkipLauncherCollectors() noexcept;
std::span<const SkipLauncherInstall> SkipLauncherDfaInstalls() noexcept;
std::span<const SkipLauncherInstall> SkipLauncherRomInstalls() noexcept;
std::span<const SkipLauncherCollectedInstall> SkipLauncherDfaCollected() noexcept;

// The entry whose match feeds the jump install: the original reads the second
// entry's match into its jump-target slot.
inline constexpr std::uint32_t kSkipLauncherJumpTargetEntry = 1;

struct SkipLauncherReport final
{
	std::size_t entriesFound{};
	std::size_t entriesMissing{};
	std::size_t writes{};
	std::size_t collectorsFound{};
	std::size_t collectorWrites{};
	bool jumpInstalled{};
	std::size_t dfaInstalls{};        // DFA hook jumps written
	std::size_t dfaMissing{};         // DFA signatures absent this build
	std::size_t romInstalls{};        // ROM hook jumps written
	std::size_t romMissing{};
	std::size_t collectedInstalls{};  // DFA collected-site jumps
	std::uint32_t createFileSlot{};   // captured IAT slot VA (0 when absent)
};

// The original's stages: main writes, then DFA (0x10004850) under its
// XLivelessAddon/GTAC.dll/EFLC/one-shot guards, then ROM (0x10004620) always;
// MiscFixes calls DFA again (0x10024018).
enum class SkipLauncherStage : std::uint8_t
{
	main = 1,
	dfa = 2,
	rom = 4,
};
constexpr SkipLauncherStage operator|(SkipLauncherStage a, SkipLauncherStage b) noexcept
{
	return static_cast<SkipLauncherStage>(static_cast<std::uint8_t>(a) |
	                                      static_cast<std::uint8_t>(b));
}
constexpr bool HasStage(SkipLauncherStage set, SkipLauncherStage stage) noexcept
{
	return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(stage)) != 0;
}

// Applies the patches selected by `stages` to `image`. Missing signatures are
// skipped, never guessed, exactly like the original; the hook stages need
// `resolve` and do not run without it. Every caller names its stages: the old
// `= all` default cannot be repeated in the module's exported declaration
// (MSVC C2572 in the implementation unit), and no caller relied on it.
PatchResult ApplySkipLauncher(GameImage image, SkipHookResolver resolve, void* resolver_user,
                              SkipLauncherReport& report, SkipLauncherStage stages) noexcept;

// Windows hook bodies; the resolver feeds ApplySkipLauncher and the ROM service
// starts from rom_reset (the original 0x10004570 -> 0x100043D0 path).

#if defined(_WIN32)

// Records the CreateFile IAT slot captured from the game; 0 makes the hook call
// CreateFileA directly.
void SetCreateFileSlot(std::uint32_t slot_va) noexcept;

// Resolves a SkipHook to the reimplemented body; 0 for an unknown hook.
std::uintptr_t ResolveSkipHook(SkipHook hook, void* user) noexcept;

#endif  // _WIN32
