#pragma once
// The option pass (`PatchGame`) and the loader preamble, run in the original
// loader's order.

#include <cstddef>
#include <cstdint>

#include "common.h"

#include <initializer_list>

#include "Hooking.Patterns.h"

struct PatchContext
{
	GameImage image;
	std::uintptr_t gameBase;
	GameBuild build;
	const ConfigDocument& config;

	// An option is on when its value parses to exactly 1. An absent key uses the
	// default the option's own arm substitutes, which is not always "off".
	bool Option(const char* key, const char* defaultValue) const
	{
		const ConfigEntry* entry = config.find("Options", key);
		const char* text = entry ? entry->value.c_str() : defaultValue;
		return std::strtol(text, nullptr, 10) == 1;
	}
};

// The loader's AddOptionsInMenu && IVMenuAPI decision. RestoreDeathMusic installs
// through the menu even with its INI key off, so its arm asks this.
void SetMenuIntegration(bool available) noexcept;

// Applies every INI-gated install for `build`.
void PatchGame(GameBuild build, GameImage image, const ConfigDocument& config);

// The loader's unconditional preamble: seven scans whose results the option
// bodies read, plus the salt fallback when the build probe misses.
struct PreambleReport
{
	std::size_t resolved{};
	std::size_t missing{};
};

PreambleReport FindGamePointers(GameImage image) noexcept;

// The helpers every install arm calls (implemented in zp.patch, except Trace,
// which is `zp.log`). A feature module includes this header; the exported
// definitions keep the plain symbols the header declares.
bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds);

hook::pattern Pattern(const GameImage& image, std::string_view signature);

std::uint32_t MatchAddress(hook::pattern& pattern, std::ptrdiff_t offset = 0);

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

inline constexpr std::size_t kWordLength = 2;
inline constexpr std::size_t kBranchLength = 5;
inline constexpr std::uint8_t kCallOpcode = 0xE8;
inline constexpr std::uint8_t kRetOpcode = 0xC3;
inline constexpr std::uint8_t kMovEaxOpcode = 0xB8;
inline constexpr std::uint8_t kPushadOpcode = 0x60;
inline constexpr std::uint8_t kXorEaxOpcode = 0x33;
inline constexpr std::uint8_t kXorEaxEaxModrm = 0xC0;
inline constexpr std::uint8_t kJeRel32First = 0x0F;
inline constexpr std::uint8_t kJeRel32Second = 0x84;
inline constexpr std::uint8_t kMovdXmm0Eax[] = {0x66, 0x0F, 0x6E, 0xC0};
inline constexpr std::uint8_t kMovdXmm1Eax[] = {0x66, 0x0F, 0x6E, 0xC8};

// AddOptionsInMenu's decision, read by the RestoreDeathMusic arm: with the menu
// integration on, the mod installs that feature's hooks even with its INI key
// off. Defined in dllmain.cpp next to SetMenuIntegration.
extern bool bMenuIntegration;

void Trace(const char* name);
