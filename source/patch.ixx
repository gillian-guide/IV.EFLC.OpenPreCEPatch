// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.patch - signature scanning, the guarded arm writes and the body ABI.
//
// This is the FusionFix-minimal patch module: signature normalisation and
// scanning, the guarded writes every install arm uses (SiteWritable, JumpTo,
// CallTo, NopAt, WriteValue, WriteRaw and their no-read variants), the Pattern
// wrappers, the arm trace and counters, the shared body helpers several modules
// call, and the resolver the x64 parity dump uses for OPP_BODY.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#if !defined(_WIN64)
#include "driveby_seh.h"
#endif
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <fstream>
#include <initializer_list>
#include <cstdio>

export module opp.patch;

import opp.common;
import opp.game;
import opp.log;

export namespace opp
{
    std::string NormalizeSignature(std::string_view signature);

    // The range the original's hook::pattern(str) scans: module base up to the end
    // of the last executable section. The installers used to hand Hooking.Patterns
    // the whole SizeOfImage, which scans .rdata/.data/.rsrc too (2.6x more bytes on
    // 1.0.8.0) and can match outside code. Falls back to the whole image when the
    // view is not a readable PE32 image, so synthetic test buffers keep working.
    ModuleView ExecutableView(ModuleView image) noexcept;

    bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept;

    size_t FindAllPatterns(ModuleView module, std::string_view signature,
                                std::vector<const uint8_t*>& output) noexcept;

    // True when `address` is inside the game image (defined here with the
    // scanner, exported by this module; common.hxx no longer declares it).
    bool InImage(GameImage image, uintptr_t address) noexcept;
} // namespace opp

export namespace opp
{
    // The x64 parity dump's body resolver. Hook bodies are 32-bit only, so the
    // dump cannot link them; OPP_BODY(name) (hooks.h) asks this resolver for the
    // ORIGINAL handler VA instead, which is what the installer's rel32 must point
    // at for a byte-for-byte comparison. tests/dump_stubs.cpp installs the table
    // in a static initializer; the 32-bit ASI never sets a resolver (its
    // OPP_BODY expands to the body's real address).
    using AbiResolver = void* (*)(const char* name);
    void SetAbiResolver(AbiResolver resolver) noexcept;
} // namespace opp

export namespace opp
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

    // A multi-match arm whose original read or patched a fixed number of sites
    // (FusionFix's P13): a different count is a view mismatch, so the arm logs it
    // and installs nothing rather than patching a partial set. Call it only after
    // the zero-hit case is handled (`pattern.size() > 0 && ExpectMatchCount(...)`),
    // so a build the arm does not fit stays a silent skip.
    bool ExpectMatchCount(hook::pattern& pattern, size_t expected, const char* what) noexcept;

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

    inline uint32_t AddressOf(const void* pointer)
    {
        const auto value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
#if defined(OPCEP_DUMP_TOOL)
        opp_record_dump_pointer(value);
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

    // The destination of a `call rel32` at `instruction`: the displacement at
    // `instruction + operandOffset` is measured from the end of the five-byte
    // instruction (operandOffset + 4). Use this when the call is not at the
    // match start; BranchTarget keeps its original-site meaning.
    inline uint32_t BranchDestination(const uint8_t* instruction, ptrdiff_t operandOffset)
    {
        const auto displacement = static_cast<int32_t>(Field(instruction, operandOffset));
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(instruction) + operandOffset + 4u +
            static_cast<uint32_t>(displacement));
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

    // One skip line for a write that never reached the image.
    void SkipArmWrite(const char* reason) noexcept;

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
    extern "C" bool zpatch_relocate_calls(
        void* destination, std::span<const uint8_t> source,
        std::span<const uint32_t> callOffsets) noexcept;
} // namespace opp

namespace opp
{
    namespace
    {
        constexpr size_t kSignatureTokenWidth = 3;  // "AA " per signature byte
        constexpr uint32_t kByteMask = 0xFFu;       // one signature byte
        constexpr uint32_t kNibbleMask = 0xFu;      // one hex digit

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        size_t PatternLength(std::string_view normalized) noexcept
        {
            return normalized.size() / kSignatureTokenWidth + 1;
        }

        bool ParseToken(std::string_view token, int& value, bool& wildcard)
        {
            if (token == "?" || token == "??")
            {
                wildcard = true;
                value = 0;
                return true;
            }

            wildcard = false;
            size_t i = 0;
            while (i < token.size() && IsWhite(token[i]))
                ++i;

            bool negative = false;
            if (i < token.size() && (token[i] == '+' || token[i] == '-'))
            {
                negative = token[i] == '-';
                ++i;
            }

            if (i + 1 < token.size() && token[i] == '0' &&
                (token[i + 1] == 'x' || token[i + 1] == 'X'))
                i += 2;

            uint32_t accumulated = 0;
            for (; i < token.size(); ++i)
            {
                const char c = token[i];
                int digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    break;

                accumulated = accumulated * 16u + static_cast<uint32_t>(digit);
            }

            value = static_cast<int>((negative ? 0u - accumulated : accumulated) & kByteMask);
            return true;
        }

        bool Normalize(std::string_view signature, std::string& out)
        {
            out.clear();
            out.reserve(signature.size());

            size_t i = 0;
            bool any = false;
            while (i < signature.size())
            {
                while (i < signature.size() && IsWhite(signature[i]))
                    ++i;
                if (i == signature.size())
                    break;

                const size_t start = i;
                while (i < signature.size() && !IsWhite(signature[i]))
                    ++i;
                const std::string_view token = signature.substr(start, i - start);

                int value = 0;
                bool wildcard = false;
                ParseToken(token, value, wildcard);

                if (!out.empty())
                    out.push_back(' ');

                if (wildcard)
                {
                    out.push_back('?');
                }
                else
                {
                    static const char kHex[] = "0123456789ABCDEF";
                    out.push_back(kHex[(value >> 4) & kNibbleMask]);
                    out.push_back(kHex[value & kNibbleMask]);
                }
                any = true;
            }
            return any;
        }
    } // namespace

    std::string NormalizeSignature(std::string_view signature)
    {
        std::string normalized;
        return Normalize(signature, normalized) ? normalized : std::string{};
    }

    ModuleView ExecutableView(ModuleView image) noexcept
    {
        const ModuleView whole{image.base, image.size};
        if (!image.base || image.size < sizeof(IMAGE_DOS_HEADER))
            return whole;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return whole;

        const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
        if (nt_offset + sizeof(IMAGE_NT_HEADERS) > image.size)
            return whole;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.base + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return whole;

        const size_t section_table = static_cast<size_t>(
            reinterpret_cast<uintptr_t>(IMAGE_FIRST_SECTION(nt)) -
            reinterpret_cast<uintptr_t>(image.base));
        const size_t section_count = nt->FileHeader.NumberOfSections;
        if (section_count == 0 || section_table < nt_offset ||
            section_table + section_count * sizeof(IMAGE_SECTION_HEADER) > image.size)
            return whole;

        const auto* sections =
            reinterpret_cast<const IMAGE_SECTION_HEADER*>(image.base + section_table);
        size_t end = 0;
        for (size_t i = 0; i < section_count; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const size_t section_end = static_cast<size_t>(section.VirtualAddress) +
                                       section.Misc.VirtualSize;
            if (section_end > end)
                end = section_end;
        }

        // No executable section, or headers that point outside the mapped image:
        // keep the old whole-image behaviour rather than silently scanning less.
        if (end == 0 || end > image.size)
            return whole;
        return {image.base, end};
    }

    bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept
    {
        output = {};
        if (!module.base || module.size == 0 || signature.empty())
            return false;

        std::string normalized;
        if (!Normalize(signature, normalized))
            return false;

        // The scanner subtracts the pattern length from the end before iterating,
        // so reject a pattern longer than the view instead of relying on underflow.
        if (PatternLength(normalized) > module.size)
            return false;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(module.base);
        auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
        pattern.count_hint(1);
        if (pattern.size() == 0)
            return false;

        output.address = static_cast<const uint8_t*>(pattern.get(0).get<uint8_t>());
        return true;
    }

    size_t FindAllPatterns(ModuleView module, std::string_view signature,
                                std::vector<const uint8_t*>& output) noexcept
    {
        output.clear();
        if (!module.base || module.size == 0 || signature.empty())
            return 0;

        std::string normalized;
        if (!Normalize(signature, normalized))
            return 0;
        if (PatternLength(normalized) > module.size)
            return 0;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(module.base);
        auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
        const size_t count = pattern.size();
        output.reserve(count);
        for (size_t i = 0; i < count; ++i)
            output.push_back(static_cast<const uint8_t*>(pattern.get(i).get<uint8_t>()));
        return output.size();
    }

    bool InImage(GameImage image, uintptr_t address) noexcept
    {
        const auto base = reinterpret_cast<uintptr_t>(image.base);
        return address >= base && address + sizeof(uint32_t) <= base + image.size;
    }
} // namespace opp

namespace opp
{
    namespace
    {
        // The arm whose write is being guarded. The install pass is one
        // synchronous thread, so a plain global is enough and allocates
        // nothing inside DllMain.
        const char* g_currentArm = "startup";
        size_t g_writesApplied = 0;
        size_t g_writesSkipped = 0;

        void LogSkippedWrite(const void* site, const char* reason) noexcept
        {
            ++g_writesSkipped;
            Log("arm %s: skip write at %p: %s", g_currentArm, site, reason);
        }
    } // namespace

    void LogPatternMiss(std::string_view signature) noexcept
    {
        // One line per miss: arm, then the signature it asked for. The string
        // view points at a literal in the arm, so nothing is built at runtime.
        Log("arm %s: no match for signature [%.*s]", g_currentArm,
            static_cast<int>(signature.size()), signature.data());
    }

    bool ExpectMatchCount(hook::pattern& pattern, size_t expected, const char* what) noexcept
    {
        if (pattern.size() == expected)
            return true;
        Log("arm %s: %s: %zu match(es), expected %zu: skipped", g_currentArm,
            what != nullptr ? what : "multi-match scans", pattern.size(), expected);
        return false;
    }

    const char* CurrentArm() noexcept
    {
        return g_currentArm;
    }

    size_t ArmWritesApplied() noexcept
    {
        return g_writesApplied;
    }

    size_t ArmWritesSkipped() noexcept
    {
        return g_writesSkipped;
    }

    bool SiteWritable(const PatchContext& ctx, const void* site, size_t length) noexcept
    {
        const auto base = reinterpret_cast<uintptr_t>(ctx.image.base);
        const auto address = reinterpret_cast<uintptr_t>(site);
        if (base == 0 || ctx.image.size == 0 || length == 0)
        {
            LogSkippedWrite(site, "no writable game image");
            return false;
        }
        if (address < base || address - base >= ctx.image.size)
        {
            // Options that patch another loaded module (xlive.dll) go through
            // ModuleSiteWritable instead; a bare foreign address is a bug.
            LogSkippedWrite(site, "site outside the game image");
            return false;
        }
        if (length > ctx.image.size - (address - base))
        {
            LogSkippedWrite(site, "write crosses the end of the game image");
            return false;
        }
        ++g_writesApplied;
        return true;
    }

    bool ModuleSiteWritable(const void* moduleBase, const void* site,
                            size_t length) noexcept
    {
        const auto reject = [site](const char* reason) {
            LogSkippedWrite(site, reason);
            return false;
        };
        if (moduleBase == nullptr || site == nullptr || length == 0)
            return reject("no module base or empty write");
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(site, &info, sizeof(info)) == 0)
            return reject("site is not a mapped address");
        if (info.State != MEM_COMMIT)
            return reject("site is not committed");
        if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return reject("site is a guard/no-access page");
        // One module image is one allocation, so the allocation base identifies
        // the module and RegionSize bounds the write.
        if (info.AllocationBase != moduleBase)
            return reject("site is outside the module");
        const auto address = reinterpret_cast<uintptr_t>(site);
        const auto region_end =
            reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (address + length > region_end)
            return reject("write crosses the module end");
        return true;
    }

    void SkipArmWrite(const char* reason) noexcept
    {
        LogSkippedWrite(nullptr, reason);
    }

    bool FusionFixGateClosed()
    {
        const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
        if (module == nullptr)
            return false;
        const auto* image = reinterpret_cast<const uint8_t*>(module);
        return ModuleLacksSignature(image, kFusionFixProbeRva);
    }

    bool NewFilesConflictModulesPresent()
    {
        return GetModuleHandleA("GTAC.dll") != nullptr ||
               GetModuleHandleA("JacksIVFixes.asi") != nullptr;
    }

    void TraceArm(const char* name)
    {
        g_currentArm = name != nullptr ? name : "?";
        // The log names every arm before it runs, so a crash during the install
        // pass leaves the last arm visible.
        Log("arm %s", name);
#if defined(OPCEP_TRACE_INSTALLS)
        // The trace file is owned and append-only; a function-local static keeps
        // it open for the whole install pass.
        static std::ofstream trace("IV.EFLC.OpenPreCEPatch.trace.log", std::ios::app);
        if (trace)
        {
            trace << "arm " << (name != nullptr ? name : "?") << '\n';
            trace.flush();
        }
#else
        (void)name;
#endif
    }
} // namespace opp

// Copy `size` bytes and re-point the listed `call rel32` instructions at their
// original absolute targets. The offsets are the E8 opcodes; a non-E8 byte, an
// out-of-range offset or a displacement that does not fit fails the whole copy,
// so a shape mismatch on another build is a clean skip, never a half-fixed copy.
extern "C" bool zpatch_relocate_calls(
    void* destination, std::span<const uint8_t> source,
    std::span<const uint32_t> callOffsets) noexcept
{
    if (destination == nullptr || source.empty() || callOffsets.empty() ||
        source.size() < opp::kBranchLength)
        return false;
    std::memcpy(destination, source.data(), source.size());
    auto* dst = static_cast<uint8_t*>(destination);
    const auto* src = source.data();
    for (const uint32_t offset : callOffsets)
    {
        if (offset + opp::kBranchLength > source.size() ||
            dst[offset] != opp::kCallOpcode)
            return false;
        int32_t old_rel = 0;
        std::memcpy(&old_rel, src + offset + 1u, sizeof(old_rel));
        const uintptr_t target = reinterpret_cast<uintptr_t>(src + offset + 5u) +
                                 static_cast<uintptr_t>(static_cast<intptr_t>(old_rel));
        const intptr_t new_rel = static_cast<intptr_t>(target) -
                                 static_cast<intptr_t>(reinterpret_cast<uintptr_t>(
                                     dst + offset + 5u));
        if (new_rel > INT32_MAX || new_rel < INT32_MIN)
            return false;
        const auto narrowed = static_cast<int32_t>(new_rel);
        std::memcpy(dst + offset + 1u, &narrowed, sizeof(narrowed));
    }
    return true;
}


// ---------------------------------------------------------------------------
// The x64 parity dump's body resolver
// ---------------------------------------------------------------------------
namespace opp
{
    namespace
    {
        AbiResolver g_abiResolver = nullptr;
    } // namespace

    void SetAbiResolver(AbiResolver resolver) noexcept
    {
        g_abiResolver = resolver;
    }

    void* ResolveDumpBody(const char* name) noexcept
    {
        if (g_abiResolver == nullptr || name == nullptr)
            return nullptr;
        return g_abiResolver(name);
    }
} // namespace opp

// The hooks.h OPP_BODY hook for 32-bit-less hosts. The x86 build never calls it
// (its OPP_BODY is the body's own address); the x64 parity dump reaches the
// resolver that tests/dump_stubs.cpp installs.
extern "C" void* OppDumpBody(const char* name) noexcept
{
    return opp::ResolveDumpBody(name);
}

// ---------------------------------------------------------------------------
// Shared body helpers (32-bit only): the bounded formatter, the native-script
// call layer and the secure-copy slot. The helpers the bodies call and the
// compiler inlines live in source/body_helpers.inc (one static copy per
// module); the naked resolver and cookie-check bodies live in the module's
// implementation unit (source/patch_impl.cpp).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)
using namespace opp;

extern "C"
{

// Native-script "call by hash" layer (sub_100038E0 and the sub_1005Bxxx
// wrappers): the resolver (dword_1015BDE0, game 0x626AF0 on 1.0.8.0) takes the
// hash in ECX/ESI and the 0xA4-byte call block in EDX; the handler is cdecl with
// one argument. The original always returns (char)1 and skips a null handler.
extern "C" uint32_t zpatch_script_resolve(uint32_t hash);
extern "C" uint32_t zpatch_script_resolve_with_block(uint32_t hash,
                                                           void* block);
extern "C" uint32_t __cdecl zpatch_script_resolve_seh(uint32_t hash,
                                                            uint32_t edx_arg);

struct zpatch_native_block final
{
    uint32_t m_p_return;   // +0x00 = &args
    uint32_t m_arg_count;  // +0x04
    uint32_t m_p_args;     // +0x08 = &args
    uint32_t m_out_count;  // +0x0C
    uint32_t m_out_ptrs[4];  // +0x10
    struct record final { uint32_t v[3]; uint32_t padding; };
    record m_records[4];        // +0x20 (sub_100038C0 zeroes v[0..2] of each)
    uint32_t args[17];     // +0x60 (arg 0 is the return slot)
};

extern "C" char zpatch_script_call_by_hash(uint32_t hash, void* block) noexcept
{
    const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(
        static_cast<uintptr_t>(zpatch_script_resolve_with_block(hash, block)));
    if (handler) handler(block);
    return 1;
}

// One builder covers every wrapper: the sub_1005DE40/DE80 cleanup loops are
// identical (copy out each pending record, then return args[0]; DE80 returns
// only its low byte).
extern "C" uint32_t zpatch_script_call(uint32_t hash, uint32_t nargs,
                                            const uint32_t* args) noexcept
{
    zpatch_native_block block;
    // The original clears the block up to (not including) the last argument:
    // 0xA0 is offsetof(args[16]) and sizeof(block) is 0xA4.
    static_assert(offsetof(zpatch_native_block, args[16]) == 0xA0);
    static_assert(sizeof(zpatch_native_block) == 0xA4);
    std::memset(&block, 0, offsetof(zpatch_native_block, args[16]));
    for (auto& record : block.m_records)
    {
        record.v[0] = 0;
        record.v[1] = 0;
        record.v[2] = 0;
    }
    const auto base = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(block.args));
    block.m_p_return = base;
    block.m_p_args = base;
    block.m_arg_count = nargs;
    for (uint32_t i = 0; i < nargs && i < 17u; ++i) block.args[i] = args[i];
    zpatch_script_call_by_hash(hash, &block);
    while (block.m_out_count != 0u)
    {
        --block.m_out_count;
        auto* destination = reinterpret_cast<uint32_t*>(
            static_cast<uintptr_t>(block.m_out_ptrs[block.m_out_count]));
        const auto& source = block.m_records[block.m_out_count];
        destination[0] = source.v[0];
        destination[1] = source.v[1];
        destination[2] = source.v[2];
    }
    return block.args[0];
}

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, size_t size,
                                     const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
    va_end(args);
    return result;
}

// The original's path/name helpers call the secure CRT (`strcpy_s`/`wcscpy_s`)
// through its IAT; the volatile pointer keeps a distinct secure-copy call.
// The secure-copy slot LocalSettingsPath and NoLiveryLimit both use; the
// volatile pointer keeps a distinct secure-copy call.
volatile ZpStrcpyS zpatch_secure_strcpy_s =
    static_cast<ZpStrcpyS>(&strcpy_s);
}  // extern "C"
#endif  // !defined(_WIN64)
