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

// The four xlive.dll dwords RemoveGFWLUpdatePopup zeroes (original handler
// 0x10005421). Values are the original's own decimal immediates:
// 6748016, 6748020, 7034320, 7034352. The pairs are 4 bytes apart; a rename or
// re-base must keep these exact (pinned by TestRemoveGFWLUpdatePopupSites).
inline constexpr uintptr_t kXliveUpdateFlag0Rva = 0x66F770u;  // 6748016
inline constexpr uintptr_t kXliveUpdateFlag1Rva = 0x66F774u;  // 6748020
inline constexpr uintptr_t kXliveUpdateFlag2Rva = 0x6B55D0u;  // 7034320
inline constexpr uintptr_t kXliveUpdateFlag3Rva = 0x6B55F0u;  // 7034352
static_assert(kXliveUpdateFlag0Rva == 6748016u);
static_assert(kXliveUpdateFlag1Rva == 6748020u);
static_assert(kXliveUpdateFlag2Rva == 7034320u);
static_assert(kXliveUpdateFlag3Rva == 7034352u);

// The AddOptionsInMenu integration is not implemented in this tree: the five
// callbacks that used to be declared here had no definitions, so they were
// removed rather than advertised.

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

// Offline check for the flat allocation registry (native tests only).
extern "C" bool zpatch_nms_registry_selftest() noexcept;

#endif  // _WIN32

extern "C" void* AbiByName(const char* name) noexcept;

