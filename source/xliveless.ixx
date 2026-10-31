// zp.xliveless - the SkipLauncher/DFA/ROM launcher bypass.
//
// SkipLauncher: XLivelessAddon's launcher/DRM bypass as reimplemented by
// ZolikaPatch 7.65. The tables follow the original handlers' order: the main
// signature table (0x10045CE0), the two multi-hit collectors, the DFA file-API
//
// The tables and the hook bodies live in `source/xliveless.cpp`, the module's
// implementation unit: MSVC rejects a naked `__declspec` body in an imported
// interface (C2488), and this unit's ROM body is naked assembly. The types stay
// in `src/xlive/xliveless.h`; the declarations below are exported so the plain
// translation unit `dllmain.cpp` and the feature modules link the same symbols.

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "xlive/xliveless.h"

export module zp.xliveless;

import zp.common;
import zp.patch;

export
{
const SkipLauncherEntry* SkipLauncherEntries(std::size_t& count) noexcept;
const SkipLauncherCollector* SkipLauncherCollectors(std::size_t& count) noexcept;
const SkipLauncherInstall* SkipLauncherDfaInstalls(std::size_t& count) noexcept;
const SkipLauncherInstall* SkipLauncherRomInstalls(std::size_t& count) noexcept;
const SkipLauncherCollectedInstall* SkipLauncherDfaCollected(std::size_t& count) noexcept;

PatchResult ApplySkipLauncher(GameImage image, SkipHookResolver resolve, void* resolver_user,
                              SkipLauncherReport& report,
                              SkipLauncherStage stages) noexcept;

#if defined(_WIN32)
// Records the CreateFile IAT slot captured from the game; 0 makes the hook call
// CreateFileA directly.
void SetCreateFileSlot(std::uint32_t slot_va) noexcept;

// Resolves a SkipHook to the reimplemented body; 0 for an unknown hook.
std::uintptr_t ResolveSkipHook(SkipHook hook, void* user) noexcept;
#endif
}  // export
