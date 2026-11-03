// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.patch - signature scanning, the scan/address helpers and the body ABI.
//
// The arm writes themselves are plain injector calls (FusionFix's shape); this module
// keeps only what the arms and the offline dump genuinely share: signature
// normalisation and scanning, the match/address readers, the branch encoder, the arm
// trace helpers' scan diagnostics, and the resolver the x64 parity dump uses for
// OPP_BODY.

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
import opp.log;

export namespace opp
{
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
            pattern.get_first<uint8_t>(offset)));
    }

    // A rel32 target inside the current match: the displacement at
    // `displacementOffset`, measured from the instruction at `nextOffset`.
    inline uint32_t MatchTarget(hook::pattern& pattern, ptrdiff_t displacementOffset,
                                     ptrdiff_t nextOffset)
    {
        return MatchAddress(pattern, nextOffset) +
               *pattern.get_first<uint32_t>(displacementOffset);
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
            *pattern.get_first<uint32_t>(offset)));
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

    // A five-byte branch written as opcode + rel32, the way the original's collector
    // writes it. injector::MakeCALL/MakeJMP first call GetBranchDestination, which for a
    // `FF 15 <disp32>` site dereferences the operand; that read is discarded here, the
    // only difference from the original. Use this where the dereference would read memory
    // the site does not point into (e.g. a byte of the operand is overwritten by the same
    // arm). Only WriteMemoryRaw exists in every injector shim, so the rel32 is built here.
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

    // The FusionFix compatibility gate lives in opp.game with the other module
    // probes.
    bool NewFilesConflictModulesPresent();

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

export namespace opp::ops
{
    // One-byte opcodes the arms write by hand. The name is the mnemonic with
    // its operand form.
    inline constexpr uint8_t kJmpRel8 = 0xEB;    // EB cb
    inline constexpr uint8_t kJzRel8 = 0x74;     // 74 cb
} // namespace opp::ops

namespace opp
{
    namespace
    {
        constexpr uint32_t kNibbleMask = 0xFu;      // one hex digit

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        size_t PatternLength(std::string_view normalized) noexcept
        {
            // "AA " per signature byte.
            return normalized.size() / 3 + 1;
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

            // One signature byte.
            value = static_cast<int>((negative ? 0u - accumulated : accumulated) & 0xFFu);
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

        const ModuleView view = module;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        auto pattern = hook::make_range_pattern(begin, begin + view.size, normalized);
        pattern.count_hint(1);
        if (pattern.size() == 0)
            return false;

        output.address = static_cast<const uint8_t*>(pattern.get_first<uint8_t>());
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

        const ModuleView view = module;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        auto pattern = hook::make_range_pattern(begin, begin + view.size, normalized);
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

    bool ExpectMatchCount(hook::pattern& pattern, size_t expected, const char* what) noexcept
    {
        if (pattern.size() == expected)
            return true;
        Log("arm %s: %s: %zu match(es), expected %zu: skipped", CurrentArm(),
            what != nullptr ? what : "multi-match scans", pattern.size(), expected);
        return false;
    }

    bool NewFilesConflictModulesPresent()
    {
        return GetModuleHandleA("GTAC.dll") != nullptr ||
               GetModuleHandleA("JacksIVFixes.asi") != nullptr;
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


// The x64 parity dump's body resolver
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

// Shared body helpers (32-bit only): the bounded formatter, the native-script
// call layer and the secure-copy slot. The helpers the bodies call and the
// compiler inlines live in source/body_helpers.inc (one static copy per
// module); the naked resolver and cookie-check bodies live in the module's
// implementation unit (source/patch_impl.cpp).
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
    const auto handler =
        opp::Rebased<void(__cdecl*)(void*)>(zpatch_script_resolve_with_block(hash, block));
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
