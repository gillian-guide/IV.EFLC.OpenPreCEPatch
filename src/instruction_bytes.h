#pragma once
// The instruction bytes the install arms store. One definition, shared by the
// arms (through src/patches.h) and by zp.patch's own writers; naming them keeps
// `0xEB` readable as "jmp rel8".
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t kWordLength = 2;
inline constexpr std::size_t kBranchLength = 5;  // E8/E9 <rel32>
inline constexpr std::uint8_t kCallOpcode = 0xE8;
inline constexpr std::uint8_t kRetOpcode = 0xC3;
inline constexpr std::uint8_t kMovEaxOpcode = 0xB8;  // mov eax, imm32
inline constexpr std::uint8_t kPushadOpcode = 0x60;
inline constexpr std::uint8_t kXorEaxOpcode = 0x33;    // xor
inline constexpr std::uint8_t kXorEaxEaxModrm = 0xC0;  // eax, eax
inline constexpr std::uint8_t kJeRel32First = 0x0F;    // jcc rel32, two-byte opcode
inline constexpr std::uint8_t kJeRel32Second = 0x84;   // je
// 66 0F 6E C0 / 66 0F 6E C8 = movd xmm0, eax / movd xmm1, eax.
inline constexpr std::uint8_t kMovdXmm0Eax[] = {0x66, 0x0F, 0x6E, 0xC0};
inline constexpr std::uint8_t kMovdXmm1Eax[] = {0x66, 0x0F, 0x6E, 0xC8};
