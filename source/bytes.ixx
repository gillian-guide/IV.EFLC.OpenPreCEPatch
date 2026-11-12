module;

#include <common.hxx>

export module bytes;

import common;

// The opcode bytes the arms write, as bytes rather than as integers: the type is
// what says "this is one machine-code byte", and the name says which one. Every
// arm writes through these, so no arm carries a bare numeric opcode.
export namespace Bytes
{
    inline constexpr std::byte Nop{ 0x90 };
    inline constexpr std::byte Call{ 0xE8 };       // call rel32, for a site whose old form must not be read
    inline constexpr std::byte ShortJmp{ 0xEB };
    inline constexpr std::byte Ret{ 0xC3 };
    inline constexpr std::byte Slot60{ 0x60 };  // a vtable/member displacement of 0x60
    inline constexpr std::byte Zero{ 0x00 };
    inline constexpr std::byte RetImm16{ 0xC2 };      // ret imm16, low byte of the immediate follows
    inline constexpr std::byte RetImm16High{ 0x08 };  // the immediate: 8 bytes of arguments
    inline constexpr std::byte JumpOverClamp{ 0x2A }; // displacement of the bottom-distance skip

    // A run of NOPs, for the sites the original fills rather than patches.
    inline constexpr std::array FiveNops{ Nop, Nop, Nop, Nop, Nop };
    inline constexpr std::array MovAl1{ std::byte{ 0xB0 }, std::byte{ 0x01 }, Nop, Nop, Nop };
    inline constexpr std::array XorAlAl{ std::byte{ 0x32 }, std::byte{ 0xC0 }, Nop, Nop, Nop };
}
