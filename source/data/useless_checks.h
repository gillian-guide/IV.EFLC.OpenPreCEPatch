// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// RemoveUselessChecks data: one NOP write per site, per build.

#include <cstddef>
#include <cstdint>

struct UselessCheck
{
    uint8_t build;
    uint32_t rva;
    uint8_t length;
    uint8_t bytes[5];
};

extern const UselessCheck kUselessChecks[];
extern const size_t kUselessCheckCount;
