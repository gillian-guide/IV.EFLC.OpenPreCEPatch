// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.settings - the config/menu hub.
//
// It re-exports the INI document from opp.config (the typed readers, the
// FusionFix gate, the first-run/legacy import). The ZMenuIV version guard moved
// to opp.game on 2026-11-01: it is generic startup logic (a process/module
// probe), not part of the config hub. LocalSettingsPath is the one path feature
// this module owns, so its bodies live here.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

export module opp.settings;

export import opp.config;

import opp.patch;
import opp.common;
import opp.game;
import opp.log;
// Hook bodies: the machine-level entry points settings installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

// The 0x104-byte path buffer the original's LocalSettingsPath helpers use.
constexpr size_t kLocalPathCapacity = 0x104u;

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320), and the five path
// pointers the detour rewrites.
extern "C" int __stdcall zpatch_local_copy_saves(uint32_t, uint32_t,
                                                 uint32_t, uint32_t,
                                                 char* dest) noexcept
{
    if (dest != nullptr)
        std::snprintf(dest, kLocalPathCapacity, "%s\\saves", zpatch_local_ansi_cwd);
    return 0;
}

// Captures the CWD and fills the call slot the retargeted game call goes
// through (original off_10158064 = sub_10051320).
extern "C" void zpatch_local_settings_capture_cwd() noexcept
{
    GetCurrentDirectoryA(kLocalPathCapacity, zpatch_local_ansi_cwd);
    GetCurrentDirectoryW(kLocalPathCapacity, zpatch_local_wide_cwd);
    zpatch_local_saves_slot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&zpatch_local_copy_saves));
}

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to "<...>\saves\<episode>\" (the
// first ANSI destination is hardcoded "GTA IV"). Called between pushad/popad.
namespace
{
void zpatch_build_ansi_path(char* out, const char* name) noexcept
{
    const char prefix[] = "\\saves\\";
    size_t o = 0;
    for (const char* s = prefix; *s; ++s) out[o++] = *s;
    for (const char* s = name; *s; ++s) out[o++] = *s;
    out[o++] = '\\';
    out[o] = '\0';
}
void zpatch_build_wide_path(wchar_t* out, const wchar_t* name) noexcept
{
    const wchar_t prefix[] = L"\\saves\\";
    size_t o = 0;
    for (const wchar_t* s = prefix; *s; ++s) out[o++] = *s;
    for (const wchar_t* s = name; *s; ++s) out[o++] = *s;
    out[o++] = L'\\';
    out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200); these write the same
// bytes without the static CRT's secure-copy path.
void zpatch_copy_path(char* dst, size_t cap, const char* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != '\0')
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}
void zpatch_copy_wpath(wchar_t* dst, size_t cap, const wchar_t* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != 0)
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = 0;
}
}  // namespace

// The 0x200 cap the original's strcpy_s/wcscpy_s path copies use.
constexpr size_t kSettingsPathCapacity = 0x200u;

// The game's episode selector values (1 = TLAD, 2 = TBoGT; anything else is the
// base game), and the capacities of the three path buffers the step builds.
constexpr int kEpisodeTlad = 1;
constexpr int kEpisodeTbogt = 2;
constexpr size_t kAnsiIvCapacity = 16;
constexpr size_t kAnsiEpisodeCapacity = 32;
constexpr size_t kWideEpisodeCapacity = 32;

extern "C" void zpatch_local_settings_step() noexcept
{
    const int episode = zpatch_local_episode_ptr
                            ? *reinterpret_cast<const int*>(
                                  static_cast<uintptr_t>(
                                      zpatch_local_episode_ptr))
                            : 0;
    const char* ansi_name = "GTA IV";
    const wchar_t* wide_name = L"GTA IV";
    if (episode == kEpisodeTlad)
    {
        ansi_name = "TLaD";
        wide_name = L"TLaD";
    }
    else if (episode == kEpisodeTbogt)
    {
        ansi_name = "TBoGT";
        wide_name = L"TBoGT";
    }
    char ansi_iv[kAnsiIvCapacity];
    char ansi_episode[kAnsiEpisodeCapacity];
    wchar_t wide_episode[kWideEpisodeCapacity];
    // sub_10050D10 copies the literal "\saves\GTA IV\" into ansi_iv, independent
    // of the episode.
    zpatch_build_ansi_path(ansi_iv, "GTA IV");
    zpatch_build_ansi_path(ansi_episode, ansi_name);
    zpatch_build_wide_path(wide_episode, wide_name);
    if (zpatch_local_path_18)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_18)),
                         kSettingsPathCapacity, ansi_iv);
    if (zpatch_local_path_08)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_08)),
                         kSettingsPathCapacity, ansi_episode);
    if (zpatch_local_path_1c)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_1c)),
                          kSettingsPathCapacity, wide_episode);
    if (zpatch_local_path_10)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_10)),
                          kSettingsPathCapacity, wide_episode);
    zpatch_cookie_tail();
}

// @exact 0x100512F0 LocalSettingsPath detour: entered via E9 at the path-setup
// function's epilogue (game 0x856EE6, swallowing pop edi/esi/ebx; ret plus one
// int3 pad), runs the path-rewrite helper between pushad/popad and replays it.
// Contract: no arguments; entered by JMP, never called; pushad/popad preserve
// every GPR and EFLAGS, and the three pops + ret reproduce the overwritten
// epilogue so the caller's stack frame is unchanged.
[[maybe_unused]] static __declspec(naked) void abi_local_settings_detour()
{
    __asm {
        pushad
        call zpatch_local_settings_step
        popad
        pop edi
        pop esi
        pop ebx
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}
// @exact 0x10051300 LocalSettingsPath fallback detour (pattern absent on
// 1.0.8.0): same helper, then re-issues the original 5-arg call through ESI and
// the two-pop epilogue. Contract: as above; ESI holds the call target the
// original epilogue's caller set, and the four pushed zeros + 0x1C replay its
// arguments.
[[maybe_unused]] static __declspec(naked) void abi_local_settings_fallback_detour()
{
    __asm {
        pushad
        call zpatch_local_settings_step
        popad
        push 0
        push 0
        push 1Ch
        push 0
        call esi
        pop esi
        pop edi
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}
}  // extern "C"


#endif  // !defined(_WIN64)
