#pragma once
// RemoveUselessChecks data: one NOP write per site, per build.

#include <cstddef>
#include <cstdint>

struct UselessCheck
{
	std::uint8_t build;
	std::uint32_t rva;
	std::uint8_t length;
	std::uint8_t bytes[5];
};

extern const UselessCheck kUselessChecks[];
extern const std::size_t kUselessCheckCount;
