// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game identity: the executable's version, the compact build id the original's
// classifier uses, the game's joaat hash, and the PlayGTAIV process probe.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "common.hxx"

namespace opp
{
    // 0x10003E50. "hash:NNNN" returns the parsed value (a 0 result falls back to
    // hashing); anything else is hashed.
    uint32_t GameHash(std::string_view text) noexcept;

    // 0x10003B80: concatenate the four decimal fields and parse the result as
    // base 10 (1.0.7.0 -> 1070).
    bool ComposeGameVersion(uint16_t major, uint16_t minor, uint16_t build,
                            uint16_t revision, uint32_t& output) noexcept;

    // The switch at 0x10055C70. Anything else is unknown and installs nothing.
    GameBuild ClassifyGameBuild(uint32_t compactVersion) noexcept;

    // Reads the running executable's VS_FIXEDFILEINFO.
    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept;

    struct ProcessEntry
    {
        std::string_view executable;
        uint32_t processId{};
    };

    uint32_t SelectGameProcess(const ProcessEntry* entries, size_t count,
                                    std::string_view name) noexcept;

    // The pid of PlayGTAIV.exe, or 0.
    uint32_t FindGameProcess() noexcept;

    // 0x10003E20: true when the given module lacks the FusionFix bytes at
    // 0x20D3/0x20D7 (the original probes GTAIV.EFLC.FusionFix.asi).
    bool ModuleLacksSignature(const uint8_t* image, size_t imageSize) noexcept;
} // namespace opp
