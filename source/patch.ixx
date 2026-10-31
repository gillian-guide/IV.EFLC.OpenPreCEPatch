// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.patch - signature scanning and the guarded arm writes.
//
// This is the FusionFix-minimal patch module: pattern normalisation and
// scanning (pattern.hxx/.cpp) plus the helpers every install arm uses to write
// (helpers.hxx/.cpp): the guarded SiteWritable/JumpTo/CallTo/NopAt/WriteValue/
// WriteRaw wrappers, the Pattern/PatternAll wrappers, the arm trace and the
// applied/skipped counters. The ABI registry itself (hooks.h + hooks.cpp)
// stays a plain TU until the hook bodies move into the feature modules.

module;

#include "common.hxx"
#include "hooks.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>
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

    void* AbiTarget(const char* name) noexcept
    {
        void* const target = AbiByName(name);
        if (target == nullptr)
        {
            // A withheld or unverified body: the original handler's install is
            // skipped when its destination has no live body.
            Log("arm %s: skip install: no body for %s", g_currentArm,
                name != nullptr ? name : "(null)");
            ++g_writesSkipped;
        }
        return target;
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
        static std::FILE* log = std::fopen("IV.EFLC.OpenPreCEPatch.trace.log", "a");
        if (log)
        {
            std::fprintf(log, "arm %s\n", name);
            std::fflush(log);
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
extern "C" bool zpatch_relocate_calls(void* destination, const void* source,
                                      size_t size, const uint32_t* callOffsets,
                                      size_t callCount) noexcept
{
    if (destination == nullptr || source == nullptr || callOffsets == nullptr ||
        size < 5u)
        return false;
    std::memcpy(destination, source, size);
    auto* dst = static_cast<uint8_t*>(destination);
    const auto* src = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < callCount; ++i)
    {
        const size_t offset = callOffsets[i];
        if (offset + 5u > size || dst[offset] != 0xE8u)
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