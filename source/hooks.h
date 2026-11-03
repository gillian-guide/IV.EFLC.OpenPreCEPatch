// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Hook bodies and the C ABI they share.
//
// Each body lives in the feature module whose arms install it and the arm takes its
// address through OPP_BODY (no name table). Only symbols more than one module needs are
// here: the shared out-of-line helpers (opp.patch; the helpers the bodies inline live in
// body_helpers.inc), the bodies another module's data references by address
// (NewMemorySystem's allocator vtable), and the bodies the native tests drive.
// Nothing here is exported by a module: the definitions are C-linkage bodies in module
// purviews, which the linker resolves by name.

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)

// A body's address for an installer write. The shipping build uses the body
// itself; the 64-bit parity dump cannot link 32-bit bodies, so it asks the
// resolver (installed by tests/dump_stubs.cpp through opp::SetAbiResolver) for
// the ORIGINAL handler VA instead.
extern "C" void* OppDumpBody(const char* name) noexcept;
#if defined(_WIN64)
#define OPP_BODY(fn) OppDumpBody(#fn)
#else
#define OPP_BODY(fn) reinterpret_cast<void*>(&fn)
#endif

// Shared body constants

// The game's string-print script command hash: the original calls the script
// resolver with it to report a non-ped model (sub_10037AC0) and to print spawns.
inline constexpr uint32_t kPrintScriptHash = 0x79023CFCu;

// The model index field on an entity (+0x2E). Shared by the PlayerAsPedComponents,
// SuperLOD, ForceCarHeadlightShadows and steering bodies.
inline constexpr size_t kModelIndexOffset = 0x2E;

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

// Shared body helpers
//
// The helpers that the bodies call and that the compiler inlined in the single
// hook-bodies translation unit (the failsafe, the guarded stores/scanner, the
// one-time scan guard, the /GS cookie tail and the bool script wrapper) stay
// TU-local: source/body_helpers.inc is included inside each module that needs
// them, so the bodies keep their original machine code. The helpers below are
// the ones that were genuine out-of-line calls and live in opp.patch.

// The secure-copy slot LocalSettingsPath and NoLiveryLimit both use.
using ZpStrcpyS = int(__cdecl*)(char*, size_t, const char*);
extern "C" volatile ZpStrcpyS zpatch_secure_strcpy_s;

// The /GS cookie check the tail jumps to (cookie in ECX). It must preserve EAX.
extern "C" void zpatch_spawns_cookie_check_ecx();
extern "C" uintptr_t __security_cookie;

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, size_t size, const char* format, ...);

// The native-script "call by hash" layer (sub_100038E0 and the sub_1005Bxxx
// wrappers): the resolver takes the hash in ECX/ESI and the 0xA4-byte call block
// in EDX. resolve_seh runs the resolver under SEH; the call builders zero the
// block, invoke the handler and copy the out records back.
extern "C" uint32_t zpatch_script_resolve(uint32_t hash);
extern "C" uint32_t zpatch_script_resolve_with_block(uint32_t hash, void* block);
extern "C" uint32_t __cdecl zpatch_script_resolve_seh(uint32_t hash, uint32_t edx_arg);
extern "C" uint32_t __cdecl zpatch_driveby_resolve_seh(uint32_t hash, uint32_t edx_arg);
extern "C" char zpatch_script_call_by_hash(uint32_t hash, void* block) noexcept;
extern "C" uint32_t zpatch_script_call(uint32_t hash, uint32_t nargs,
                                       const uint32_t* args) noexcept;

// The MP-only model-name lookup (opp.multiplayer owns the catalogue). The spawn
// diagnostics and BadPlayerModelCrashFix both print through it.
extern "C" const char* zpatch_bpm_model_name(uint32_t hash) noexcept;

// Bodies another module's data references

// NewMemorySystem's allocator vtable (opp.performance) names the multiplayer
// sync no-op and the savegame null return, which their own modules own. The
// vtable's third foreign entry, the RGSC login stub, is naked and therefore
// defined next to the vtable (a naked body in an imported module's interface
// fails C2488); performance registers that row itself.
extern "C" int __cdecl abi_better_mp_sync();
extern "C" int __cdecl abi_rgsc_login_removed();
extern "C" int __stdcall abi_savegame_fix_null(int, int);

// Bodies the native tests drive

extern "C" void zpatch_local_settings_capture_cwd() noexcept;

// OutOfCommissionFix offline self-test: decodes the embedded finale2.sco blob,
// checks its Adler-32 trailer and verifies the golden archive (native tests only).
extern "C" bool zpatch_ooc_selftest() noexcept;

// Save-scan types shared with the native test seam. The game-facing hook bodies
// keep their own ABI; this seam is internal.
namespace opp
{
struct SaveScanPolicy final
{
    bool validate;      // check every read and size (the folded guard checks)
    bool skip_payload;  // seek over payloads instead of reading them
};

struct SaveChunkProbes final
{
    bool chunk9_new;
    bool chunk16_new;
    bool chunk13_new;
};

struct SaveScanResult final
{
    bool ok;
    SaveChunkProbes probes;
};
} // namespace opp

// Scans an open save file with `policy`: one typed result instead of three
// out-pointers. `handle` is a std::FILE*; chunk13 gates the p3 hook.
extern "C" opp::SaveScanResult zpatch_new_saves_scan(void* handle,
                                                     opp::SaveScanPolicy policy) noexcept;

// NewMemorySystem: the reimplemented CRageAllocator methods (vtable[2]/[3]/[18]
// of the original 0x100B5038) and the vtable the E8 body writes into the game's
// allocator singleton. The other entries are machine-code-transcribed stubs.
extern "C" void* __stdcall zpatch_nms_alloc(uint32_t size, uint32_t align,
                                            uint32_t flags) noexcept;
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept;
extern "C" uint8_t __stdcall zpatch_nms_contains(uint32_t key) noexcept;
inline constexpr size_t kNmsVtableEntries = 24;  // the recovered table's size
extern "C" uintptr_t zpatch_nms_vtable[kNmsVtableEntries];

// Offline check for the flat allocation registry (native tests only).
extern "C" bool zpatch_nms_registry_selftest() noexcept;

#endif  // _WIN32
