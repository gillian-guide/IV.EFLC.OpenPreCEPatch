// zp.common — the pieces every module and the entry translation unit share.
//
// The header (`common.hxx`) carries the includes; this unit carries the actual
// declarations. Layout follows GTAIV.EFLC.FusionFix: `module;` global fragment
// with the includes, then `export module`, then the exported declarations.

module;

#include "common.hxx"

export module zp.common;

export namespace zp
{
// A pointer below this is not a mapped image (the fault tracer's filter and the
// driveby diagnostic's plausibility check).
inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000u;

// The 32-bit user-mode address-space ceiling.
inline constexpr std::uintptr_t kUserModeCeiling = 0x80000000u;
}  // namespace zp
