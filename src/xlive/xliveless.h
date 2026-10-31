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

// One store relative to a signature match: `bytes`, or `fill` repeated, and a
// rel32 to another entry's match when `jumpToEntry` is set (the 33 C0 E9 rel32).
struct SkipLauncherWrite final
{
	std::int32_t offset;
	std::uint16_t length;
	const std::uint8_t* bytes;  // null when fill is used
	std::uint8_t fill;          // e.g. 0x90 for NOP fills
	std::uint32_t jumpToEntry;
};

inline constexpr std::uint32_t kNoJumpTarget = 0xFFFFFFFFu;

struct SkipLauncherEntry final
{
	std::string_view signature;
	const SkipLauncherWrite* writes;
	std::size_t writeCount;
};

// Multi-hit collector: the original patches the first two matches of one
// pattern and the last two of another with "mov al,1; ret; nop".
struct SkipLauncherCollector final
{
	enum class Target : std::uint8_t
	{
		first_two,
		last_two,
	};
	std::string_view pattern;
	Target target;
	const std::uint8_t* bytes;
	std::uint16_t length;
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
	const SkipLauncherCollectedTarget* targets;
	std::size_t targetCount;
};

// Table accessors; `count` receives the row count.
const SkipLauncherEntry* SkipLauncherEntries(std::size_t& count) noexcept;
const SkipLauncherCollector* SkipLauncherCollectors(std::size_t& count) noexcept;
const SkipLauncherInstall* SkipLauncherDfaInstalls(std::size_t& count) noexcept;
const SkipLauncherInstall* SkipLauncherRomInstalls(std::size_t& count) noexcept;
const SkipLauncherCollectedInstall* SkipLauncherDfaCollected(std::size_t& count) noexcept;

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
	all = 7,
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
