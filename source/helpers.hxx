// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers shared by the per-option installers. These are thin, side-effect-free
// wrappers over injector and Hooking.Patterns: the feature files read like the
// original loader's arms, with the plumbing kept here.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>

#include "common.hxx"
#include "hooks.h"
#include "pattern.hxx"

namespace opp
{
    inline constexpr size_t kWordLength = 2;
    inline constexpr size_t kBranchLength = 5; // E8/E9 <rel32>
    inline constexpr uint8_t kCallOpcode = 0xE8;
    inline constexpr uint32_t kFusionFixProbeRva = 0x20D7u + 4u;

    // The original's hook::pattern, over the image the drivers hand in. The
    // original scans module base..end of the last executable section and the arms
    // only read the first match, so the scan is limited to that range and stops
    // at the first hit. Arms that also patch match 1 use PatternAll below.
    inline hook::pattern Pattern(const GameImage& image, std::string_view signature)
    {
        const std::string normalized = NormalizeSignature(signature);
        if (normalized.empty())
            return hook::pattern{};
        const ModuleView view = ExecutableView({image.base, image.size});
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        return hook::make_range_pattern(begin, begin + view.size, normalized)
            .count_hint(1);
    }

    // Every match in the executable range. The original's multi-hit arms
    // (BuildingDynamicShadows' hash pair, NoImpostorHeightCutoff, BenchmarkFix,
    // EpisodicVehicleSupport's two-match NOPs and StartupTimeFix's literal loop)
    // read match 1 or iterate all matches.
    inline hook::pattern PatternAll(const GameImage& image, std::string_view signature)
    {
        const std::string normalized = NormalizeSignature(signature);
        if (normalized.empty())
            return hook::pattern{};
        const ModuleView view = ExecutableView({image.base, image.size});
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        return hook::make_range_pattern(begin, begin + view.size, normalized);
    }

    // The address of a site inside the current match.
    inline uint32_t MatchAddress(hook::pattern& pattern, ptrdiff_t offset = 0)
    {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
            pattern.get(0).get<uint8_t>(offset)));
    }

    // A rel32 target inside the current match: the displacement at
    // `displacementOffset`, measured from the instruction at `nextOffset`.
    inline uint32_t MatchTarget(hook::pattern& pattern, ptrdiff_t displacementOffset,
                                     ptrdiff_t nextOffset)
    {
        return MatchAddress(pattern, nextOffset) +
               *pattern.get(0).get<uint32_t>(displacementOffset);
    }

    inline uint32_t ReadDword(const uint8_t* address)
    {
        uint32_t value = 0;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    inline uint32_t Field(const uint8_t* match, ptrdiff_t offset)
    {
        return ReadDword(match + offset);
    }

    inline uint8_t* MatchPointer(hook::pattern& pattern, ptrdiff_t offset)
    {
        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
            *pattern.get(0).get<uint32_t>(offset)));
    }

#if defined(OPCEP_DUMP_TOOL)
    // The dump records every module-relative address an installer emits, so the
    // write-set baseline can canonicalise those operands. Module layout shifts
    // when unrelated code is added; the pointer target does not change meaning.
    void RecordDumpPointer(uint32_t value);
#endif

    inline uint32_t AddressOf(const void* pointer)
    {
        const auto value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
#if defined(OPCEP_DUMP_TOOL)
        RecordDumpPointer(value);
#endif
        return value;
    }

    // Where the branch at `siteOffset` goes: the original re-points these.
    inline uintptr_t BranchTarget(const uint8_t* match, ptrdiff_t siteOffset)
    {
        const auto displacement = static_cast<int32_t>(Field(match, siteOffset + 2));
        return reinterpret_cast<uintptr_t>(match) + siteOffset + 6 +
               static_cast<uintptr_t>(displacement);
    }

    // A five-byte branch written the way the original's collector writes it
    // (opcode + rel32). injector::MakeCALL/MakeJMP first call GetBranchDestination,
    // which for a `FF 15 <disp32>` site dereferences the operand; that read is
    // discarded, and it is the only difference from the original. Use this where
    // the dereference would read memory the site does not point into (e.g. a byte
    // of the operand is overwritten by the same arm). Only WriteMemoryRaw is
    // guaranteed by every injector shim, so the rel32 is built here.
    inline void WriteBranchNoRead(uint8_t* site, uint8_t opcode, void* destination)
    {
        uint8_t bytes[kBranchLength];
        bytes[0] = opcode;
        const auto displacement = static_cast<int32_t>(
            reinterpret_cast<intptr_t>(destination) -
            reinterpret_cast<intptr_t>(site + kBranchLength));
        std::memcpy(bytes + 1, &displacement, sizeof(displacement));
        injector::WriteMemoryRaw(site, bytes, sizeof(bytes), true);
    }

    inline void WriteCallNoRead(uint8_t* site, void* destination)
    {
        WriteBranchNoRead(site, kCallOpcode, destination);
    }

    // The FusionFix compatibility probe: a build of the fix we cannot share a
    // module with keeps its own bytes at 0x20D3/0x20D7.
    bool FusionFixGateClosed();
    bool NewFilesConflictModulesPresent();

    // RemoveUselessChecks: the per-build NOP list, written before any scan so the
    // later arms see the same bytes the original did.
    void ApplyUselessChecks(const PatchContext& ctx);

    // Debug-only arm trace; a no-op in release builds.
    void TraceArm(const char* name);

    // AddOptionsInMenu && IVMenuAPI (set by the loader before the option pass).
    extern bool bMenuIntegration;

    // Address of a machine-level body by name (see hooks/bodies.cpp).
    inline void* Abi(const char* name) { return AbiByName(name); }
} // namespace opp

