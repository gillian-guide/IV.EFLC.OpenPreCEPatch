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
    inline constexpr size_t kWordLength = 2;   // sizeof(uint16_t), a PE word
    inline constexpr size_t kBranchLength = 5; // E8/E9 <rel32>
    inline constexpr uint8_t kCallOpcode = 0xE8;
    // GTAIV.EFLC.FusionFix.asi's version probe: the fix compares the pair of
    // dwords at module+0x20D3 / +0x20D7 (game.cpp ModuleLacksSignature), so this
    // is the byte just after the second comparison's operand.
    inline constexpr uint32_t kFusionFixProbeRva = 0x20D7u + 4u;

    // The original's hook::pattern, over the image the drivers hand in. The
    // original scans module base..end of the last executable section and the arms
    // only read the first match, so the scan is limited to that range and stops
    // at the first hit. Arms that also patch match 1 use PatternAll below.
    //
    // A scan with no hit is the normal way an option declines to install on a
    // build it does not fit; it is logged once (with the arm that asked) so a
    // partially-matching image is diagnosable instead of silent.
    void LogPatternMiss(std::string_view signature) noexcept;

    inline hook::pattern Pattern(const GameImage& image, std::string_view signature)
    {
        const std::string normalized = NormalizeSignature(signature);
        if (normalized.empty())
        {
            LogPatternMiss(signature);
            return hook::pattern{};
        }
        const ModuleView view = ExecutableView({image.base, image.size});
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        auto pattern =
            hook::make_range_pattern(begin, begin + view.size, normalized).count_hint(1);
        if (pattern.size() == 0)
            LogPatternMiss(signature);
        return pattern;
    }

    // Every match in the executable range. The original's multi-hit arms
    // (BuildingDynamicShadows' hash pair, NoImpostorHeightCutoff, BenchmarkFix,
    // EpisodicVehicleSupport's two-match NOPs and StartupTimeFix's literal loop)
    // read match 1 or iterate all matches.
    inline hook::pattern PatternAll(const GameImage& image, std::string_view signature)
    {
        const std::string normalized = NormalizeSignature(signature);
        if (normalized.empty())
        {
            LogPatternMiss(signature);
            return hook::pattern{};
        }
        const ModuleView view = ExecutableView({image.base, image.size});
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        auto pattern = hook::make_range_pattern(begin, begin + view.size, normalized);
        if (pattern.size() == 0)
            LogPatternMiss(signature);
        return pattern;
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
        injector::WriteMemoryRaw(injector::raw_ptr(site), bytes, sizeof(bytes), true);
    }

    inline void WriteCallNoRead(uint8_t* site, void* destination)
    {
        WriteBranchNoRead(site, kCallOpcode, destination);
    }

    // The FusionFix compatibility probe: a build of the fix we cannot share a
    // module with keeps its own bytes at 0x20D3/0x20D7.
    bool FusionFixGateClosed();
    bool NewFilesConflictModulesPresent();

    // Name of the arm currently installing, for the skip log below. Set by
    // TraceArm, which every Install*/Publish* arm calls first.
    const char* CurrentArm() noexcept;

    // Install trace. Records the arm name for CurrentArm() and, with install
    // tracing on, writes one log line before the arm runs.
    void TraceArm(const char* name);

    // ---------------------------------------------------------------------
    // Arm failsafes
    //
    // Every arm write goes through one of these instead of calling
    // injector:: directly. Each checks, before touching the image:
    //   * the site and every byte it would overwrite is inside the game image,
    //     or (for an option that deliberately patches another module, e.g.
    //     xlive.dll) inside one committed allocation;
    //   * the destination body resolved (a withheld or unverified body skips
    //     instead of installing a jump to 0).
    // A failed check writes one log line naming the arm and leaves the image
    // untouched: that is what the original loader does when a signature does
    // not match, so an unhealthy image degrades to a clean skip, never a
    // fault. On a healthy image every check passes and the write set is
    // byte-identical to the unchecked form.
    //
    // The arm's own option and build gate stays in the arm, where the original
    // handler had it; these helpers only make the write itself safe.
    bool SiteWritable(const PatchContext& ctx, const void* site, size_t length) noexcept;

    // A site inside another loaded module (the GFWL option patches xlive.dll).
    // The write is only accepted when the site lies inside that module's own
    // allocation; everything else still goes through SiteWritable's image check.
    bool ModuleSiteWritable(const void* moduleBase, const void* site,
                            size_t length) noexcept;

    template <typename T>
    inline void WriteModuleValue(PatchContext& ctx, const void* moduleBase,
                                 const void* site, T value)
    {
        (void)ctx;
        if (!ModuleSiteWritable(moduleBase, site, sizeof(T)))
            return;
        injector::WriteMemory<T>(injector::raw_ptr(const_cast<void*>(site)), value, true);
    }

    // Abi(name) with the skip log: nullptr means "no verified body for this
    // destination", which the caller turns into a skip.
    void* AbiTarget(const char* name) noexcept;

    // One skip line for a write that never reached the image.
    void SkipArmWrite(const char* reason) noexcept;

    // Address of a machine-level body by name (see hooks/bodies.cpp). Prefer
    // AbiTarget/JumpToBody/CallToBody, which also guard the write.
    inline void* Abi(const char* name) { return AbiByName(name); }

    inline void JumpTo(PatchContext& ctx, const void* site, void* target)
    {
        if (target == nullptr)
        {
            SkipArmWrite("null branch destination");
            return;
        }
        if (!SiteWritable(ctx, site, kBranchLength))
            return;
        injector::MakeJMP(injector::raw_ptr(const_cast<void*>(site)), injector::raw_ptr(target),
                          true);
    }

    inline void CallTo(PatchContext& ctx, const void* site, void* target)
    {
        if (target == nullptr)
        {
            SkipArmWrite("null call destination");
            return;
        }
        if (!SiteWritable(ctx, site, kBranchLength))
            return;
        injector::MakeCALL(injector::raw_ptr(const_cast<void*>(site)), injector::raw_ptr(target),
                           true);
    }

    inline void JumpToBody(PatchContext& ctx, const void* site, const char* body)
    {
        if (void* const target = AbiTarget(body))
            JumpTo(ctx, site, target);
    }

    inline void CallToBody(PatchContext& ctx, const void* site, const char* body)
    {
        if (void* const target = AbiTarget(body))
            CallTo(ctx, site, target);
    }

    inline void NopAt(PatchContext& ctx, const void* site, size_t length)
    {
        if (!SiteWritable(ctx, site, length))
            return;
        injector::MakeNOP(injector::raw_ptr(const_cast<void*>(site)), length, true);
    }

    // A `ret` (C3) or `ret N` (C2 imm16) through the same guarded write as the
    // other helpers. injector::MakeRET stores the opcode and the immediate at
    // their exact widths, so the encoded instruction is one or three bytes.
    inline void MakeReturn(PatchContext& ctx, const void* site, uint16_t pop = 0)
    {
        if (!SiteWritable(ctx, site, pop ? 3u : 1u))
            return;
        injector::MakeRET(injector::raw_ptr(const_cast<void*>(site)), pop, true);
    }

    inline void WriteRaw(PatchContext& ctx, const void* site, const void* data, size_t length)
    {
        if (!SiteWritable(ctx, site, length))
            return;
        injector::WriteMemoryRaw(injector::raw_ptr(const_cast<void*>(site)),
                                     const_cast<void*>(data), length, true);
    }

    template <typename T>
    inline void WriteValue(PatchContext& ctx, const void* site, T value)
    {
        if (!SiteWritable(ctx, site, sizeof(T)))
            return;
        injector::WriteMemory<T>(injector::raw_ptr(const_cast<void*>(site)), value, true);
    }

    // Counters for the install summary line: writes applied and writes skipped
    // by the checks above. Both include publish arms; bodies that run later do
    // not count.
    size_t ArmWritesApplied() noexcept;
    size_t ArmWritesSkipped() noexcept;

    // Copy `size` bytes of `source` to `destination` and re-point every `call
    // rel32` whose E8 opcode sits at one of `callOffsets` at the absolute target
    // it had in the source. A non-E8 byte, an out-of-range offset or a
    // displacement that does not fit fails the whole copy, so a relocated body
    // is either exact or not installed. Lives in helpers.cpp (not hooks.cpp) so
    // the 64-bit dump, which stubs the body registry, still links it.
    extern "C" bool zpatch_relocate_calls(void* destination, const void* source,
                                          size_t size, const uint32_t* callOffsets,
                                          size_t callCount) noexcept;
} // namespace opp

