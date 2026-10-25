// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Hook bodies and their C API. Each body keeps the original site's calling
// convention and register contract (the reference VA is in the comment above
// it); the game jumps or calls straight into them.

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)

// Maps a reference destination VA (e.g. 0x10005590) to its body. Returns 0 when
// the destination has no live-ready body; the installer then skips that jump.
uintptr_t ResolveHookBody(uint32_t reference_va, void* user) noexcept;

// Number of reference VAs with a live-ready body (for reporting).
size_t HookBodyCount() noexcept;

// The AddOptionsInMenu integration (original loader tail): with IVMenuAPI.asi
// loaded it registers the Death Music and Pause On Focus Loss toggles through
// AddIVMenuOption and installs the RestoreDeathMusic hooks even with the INI key
// off. The callbacks read/write the same option flags the INI sets.
extern "C" void zpatch_menu_prime_focus_loss() noexcept;
extern "C" uint8_t abi_menu_death_get() noexcept;
extern "C" void abi_menu_death_set(uint32_t, uint32_t) noexcept;
extern "C" uint8_t abi_menu_focus_get() noexcept;
extern "C" void abi_menu_focus_set(uint32_t, uint32_t) noexcept;

// LocalSettingsPath: captures the CWD and arms the four-byte call slot the
// retargeted game call goes through (the original has a static constructor).
extern "C" void zpatch_local_settings_capture_cwd() noexcept;

// OutOfCommissionFix offline self-test: decodes the embedded finale2.sco blob,
// checks its Adler-32 trailer and verifies the golden archive (native tests only).
extern "C" bool zpatch_ooc_selftest() noexcept;

// NewSavesCompatibility chunk scan, split out for the native tests. `handle` is
// a std::FILE*; guarded enables the folded guard checks, seek the seek path.
extern "C" bool zpatch_new_saves_scan(void* handle, int guarded, int seek,
                                      uint32_t* flag9, uint32_t* flag16,
                                      uint32_t* flag13) noexcept;

// NewMemorySystem: the reimplemented CRageAllocator methods (vtable[2]/[3]/[18]
// of the original 0x100B5038) and the vtable the E8 body writes into the game's
// allocator singleton. The other entries are machine-code-transcribed stubs.
extern "C" void* __stdcall zpatch_nms_alloc(uint32_t size, uint32_t align,
                                            uint32_t flags) noexcept;
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept;
extern "C" uint8_t __stdcall zpatch_nms_contains(uint32_t key) noexcept;
extern "C" uintptr_t zpatch_nms_vtable[24];

#endif  // _WIN32

extern "C" void* AbiByName(const char* name) noexcept;

