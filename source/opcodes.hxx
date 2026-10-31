// SPDX-License-Identifier: GPL-3.0-or-later
//
// Named x86 byte values for the hand-written stores in hooks.cpp.
//
// Most writes in this tree go through injector's Make* helpers, which name the
// operation. A few sites deliberately store a single opcode or a recovered
// instruction window; those values are spelled here so a reader sees which byte
// is an opcode, which is an operand and which is fill, instead of decoding a
// packed integer by eye.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opp::ops
{
    // One-byte opcodes used by a hand-written store. The name is the mnemonic
    // with its operand form.
    inline constexpr uint8_t kCallRel32 = 0xE8;  // E8 cd
    inline constexpr uint8_t kJmpRel32 = 0xE9;   // E9 cd
    inline constexpr uint8_t kJmpRel8 = 0xEB;    // EB cb
    inline constexpr uint8_t kJzRel8 = 0x74;     // 74 cb
    inline constexpr uint8_t kJnzRel8 = 0x75;    // 75 cb
    inline constexpr uint8_t kNop = 0x90;        // 90
    inline constexpr uint8_t kRet = 0xC3;        // C3

    // A recovered instruction window: `size` raw bytes copied verbatim over the
    // site. Windows are given as bytes because the original stores them as
    // packed integers, where the opcode/operand/fill split is invisible and the
    // meaning depends on little-endianness.
    template <size_t N>
    struct InstructionWindow
    {
        std::array<uint8_t, N> bytes{};

        [[nodiscard]] static constexpr size_t Size() noexcept { return N; }
    };

    // The little-endian dword the first four bytes encode: what the original
    // source's `0x86110FF3u`-style literal meant. Used to `static_assert` a byte
    // window back to the recovered integer.
    template <size_t N>
    constexpr uint32_t PackedLowDword(const InstructionWindow<N>& window) noexcept
    {
        static_assert(N >= 4, "a dword needs four bytes");
        return static_cast<uint32_t>(window.bytes[0]) |
               (static_cast<uint32_t>(window.bytes[1]) << 8) |
               (static_cast<uint32_t>(window.bytes[2]) << 16) |
               (static_cast<uint32_t>(window.bytes[3]) << 24);
    }

    template <size_t N>
    constexpr uint32_t PackedHighDword(const InstructionWindow<N>& window) noexcept
    {
        static_assert(N >= 8, "a high dword needs eight bytes");
        return static_cast<uint32_t>(window.bytes[4]) |
               (static_cast<uint32_t>(window.bytes[5]) << 8) |
               (static_cast<uint32_t>(window.bytes[6]) << 16) |
               (static_cast<uint32_t>(window.bytes[7]) << 24);
    }

    // The little-endian word the first two bytes encode (a short branch, a
    // 16-bit operand).
    template <size_t N>
    constexpr uint16_t PackedLowWord(const InstructionWindow<N>& window) noexcept
    {
        static_assert(N >= 2, "a word needs two bytes");
        return static_cast<uint16_t>(window.bytes[0]) |
               static_cast<uint16_t>(static_cast<uint16_t>(window.bytes[1]) << 8);
    }

    // F3 0F 11 <modrm> <disp32>: `movss dword ptr [esi+disp], xmm<N>`. The
    // displacement is built from the field offset, so a wrong offset is a
    // compile-time failure against the recovered packed constant below.
    constexpr InstructionWindow<8> MovssFieldStore(uint8_t modrm, uint32_t displacement) noexcept
    {
        return {{0xF3, 0x0F, 0x11, modrm,
                 static_cast<uint8_t>(displacement & 0xFFu),
                 static_cast<uint8_t>((displacement >> 8) & 0xFFu),
                 static_cast<uint8_t>((displacement >> 16) & 0xFFu),
                 static_cast<uint8_t>((displacement >> 24) & 0xFFu)}};
    }

    // A run of `N` one-byte NOPs, the "no fix this frame" branch of a window.
    template <size_t N>
    constexpr InstructionWindow<N> NopWindow() noexcept
    {
        InstructionWindow<N> window{};
        for (size_t i = 0; i < N; ++i)
            window.bytes[i] = kNop;
        return window;
    }
} // namespace opp::ops
