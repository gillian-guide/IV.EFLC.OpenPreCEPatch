// zp.launcher — the launch-slot rewrite data.
//
// LaunchWithPlayGTAIV overwrites the game's `LaunchGTAIV.exe\0` text at the two
// launch slots with `PlayGTAIV.exe` and leaves the slot's last two bytes as the
// original does. The capital P is the original's own byte (the CMake port once
// had it lowercase; the dump cannot reach this arm because it only runs with
// PlayGTAIV.exe present, so the exact bytes are pinned by a native test here).
//
// The scan is one wildcard-free 16-byte signature; on the five builds the
// original's table covered it hits exactly the two table RVAs, and on
// 1.0.0.4/1.0.1.0/EFLC it hits slots the original never wrote,
// which is why the five-build gate is load-bearing.

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
