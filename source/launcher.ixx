// zp.launcher — the launch-slot rewrite data.
//
// Original: LaunchWithPlayGTAIV overwrites `LaunchGTAIV.exe\0` at the two launch
// slots and leaves the last two bytes as "e\0". The capital P is the original's
// own byte; a native test pins it because the dump cannot reach this arm.
//
// The scan is one wildcard-free 16-byte signature. On the five builds the
// original's table covered it hits exactly the two slot RVAs; on
// 1.0.0.4/1.0.1.0/EFLC it matches slots the original never wrote, so the build
// gate is load-bearing.

module;

#include "common.hxx"

export module zp.launcher;

export namespace zp
{
// The 16 bytes written at each launch slot: "PlayGTAIV.exe\0e\0".
inline constexpr std::array<char, 16> kLauncherNamePatch = {
    'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};

// The slot text the scan looks for: "LaunchGTAIV.exe\0" as a byte signature.
inline constexpr std::string_view kLauncherSlotSignature =
    "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";
}  // namespace zp
