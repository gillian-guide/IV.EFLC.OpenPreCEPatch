// SPDX-License-Identifier: GPL-3.0-or-later
//
// interface
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

export module opp.interface;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

namespace opp::features
{

    // PipeBombDropIconSupport

    void InstallPipeBombDropIconSupport(PatchContext& ctx)
    {

        TraceArm("PipeBombDropIconSupport");

        // PipeBombDropIconSupport: 1 literal
        if (ctx.Option("PipeBombDropIconSupport", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                         GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                         GameBuild::v1130, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "83 7D 18 1C 75 14"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    NopAt(ctx, match - 9, 9);
                }
            }
        }

    }


    // RestoreDeathMusic

    void InstallRestoreDeathMusic(PatchContext& ctx)
    {

        TraceArm("RestoreDeathMusic");

        // RestoreDeathMusic: 3 siginstalls
        if (ctx.Option("RestoreDeathMusic", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 13, "abi_death_music_install");
            }

            if (auto pattern = Pattern(image, "C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match - 13, "abi_death_music_enabled");
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match, "abi_death_music_enabled");
            }
        }

    }

namespace
{
    class InterfaceFeatures
    {
    public:
        InterfaceFeatures()
        {
            RegisterFeature({Phase::install, "InstallPipeBombDropIconSupport", &InstallPipeBombDropIconSupport});
            RegisterFeature({Phase::install, "InstallRestoreDeathMusic", &InstallRestoreDeathMusic});
        }
    } interfaceFeatures;
} // namespace

} // namespace opp::features

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points interface installs.
//
// Each body keeps the original site's calling convention and register
// contract; the comment above it names the original destination VA. They are
// 32-bit only (the x64 parity dump resolves names to the original VAs).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

// RestoreDeathMusic (original sub_10050A50): the death-music gate, gated on
// byte_10159B86 and answered through the shipped hash-salt script calls.
extern "C" uint8_t abi_death_music_enabled() noexcept
{
    if (!zpatch_flag_death_music) return 0;
    uint32_t queried = 0;
    const uint32_t query_args[2] = {
        zpatch_script_call(zpatch_salt_5c, 0, nullptr),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&queried))};
    zpatch_script_call(zpatch_salt_54, 2, query_args);
    if (zpatch_script_call_bool(zpatch_salt_28, 1, &queried) != 0) return 1;
    if (zpatch_script_call_bool(zpatch_salt_a8, 0, nullptr) != 0 &&
        zpatch_script_call_bool(zpatch_salt_50, 0, nullptr) != 0)
        return 1;
    return 0;
}

// RestoreDeathMusic (original sub_10050AD0): resolve "6A 06 68 ? ? ? ? 8D 8C"
// once and randomize the push immediate to a track 1..5 (rand() % 5 + 1).
extern "C" char abi_death_music_install() noexcept
{
    if (!abi_death_music_enabled()) return 0;
    if (zpatch_death_music_site == 0)
    {
        const auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
        if (module)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE)
            {
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    module + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE)
                {
                    Match match;
                    const ModuleView view = ExecutableView({module, nt->OptionalHeader.SizeOfImage});
                    if (FindPattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
                        zpatch_death_music_site = static_cast<uint32_t>(
                            reinterpret_cast<uintptr_t>(match.address));
                }
            }
        }
    }
    if (zpatch_death_music_site != 0)
    {
        auto* site = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(zpatch_death_music_site) + 1);
        injector::WriteMemory<uint8_t>(  // push imm8
            injector::raw_ptr(site), static_cast<uint8_t>(std::rand() % 5 + 1), true);
    }
    return 1;
}

// BorderlessWindowed: the two window-proc shims (0x10005890 = Unicode /
// 0x10005900 = ANSI). The installer replaces the game's `call [DefWindowProc]`
// with E8 -> body and NOPs the leftover byte; the body does the one-time style
// fixup then defers to the real DefWindowProc. Its `ret 0x10` consumes the
// DefWindowProc argument slots like the replaced call, so the game's own
// `pop edi/esi/ebp/ebx; add esp,0x10; ret 0x10` still unwinds correctly.
extern "C" LRESULT __stdcall abi_borderless_w(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
    if (!zpatch_borderless_w_done)
    {
        SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
        // Clear caption/sysmenu/thickframe/minimise/maximise (0xFF30FFFF) and
        // the topmost extended bit; both are the original's style masks.
        constexpr LONG kBorderlessStyleMask = static_cast<LONG>(0xFF30FFFFu);
        static_assert(kBorderlessStyleMask ==
                      ~static_cast<LONG>(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                         WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
        SetWindowLongA(hwnd, GWL_STYLE,
                       GetWindowLongA(hwnd, GWL_STYLE) & kBorderlessStyleMask);
        SetWindowLongA(hwnd, GWL_EXSTYLE,
                       GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(WS_EX_TOPMOST));
        zpatch_borderless_w_done = 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
    if (!zpatch_borderless_a_done)
    {
        SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
        // Clear caption/sysmenu/thickframe/minimise/maximise (0xFF30FFFF) and
        // the topmost extended bit; both are the original's style masks.
        constexpr LONG kBorderlessStyleMask = static_cast<LONG>(0xFF30FFFFu);
        static_assert(kBorderlessStyleMask ==
                      ~static_cast<LONG>(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                         WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
        SetWindowLongA(hwnd, GWL_STYLE,
                       GetWindowLongA(hwnd, GWL_STYLE) & kBorderlessStyleMask);
        SetWindowLongA(hwnd, GWL_EXSTYLE,
                       GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(WS_EX_TOPMOST));
        zpatch_borderless_a_done = 1;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}
}  // extern "C"

namespace
{
// The name -> body rows this module registers. The reference VA is the
// original handler destination (0 when the body has none); it is provenance,
// not an install address.
const opp::HookBodyRow kInterfaceHookBodies[] = {
    {"abi_borderless_a", 0x10005900u, reinterpret_cast<opp::HookBody>(&abi_borderless_a)},
    {"abi_borderless_w", 0x10005890u, reinterpret_cast<opp::HookBody>(&abi_borderless_w)},
    {"abi_death_music_enabled", 0x10050A50u, reinterpret_cast<opp::HookBody>(&abi_death_music_enabled)},
    {"abi_death_music_install", 0x10050AD0u, reinterpret_cast<opp::HookBody>(&abi_death_music_install)},
};

struct InterfaceHookBodyRegistration
{
    InterfaceHookBodyRegistration() noexcept
    {
        opp::RegisterHookBodies(kInterfaceHookBodies);
    }
} g_interfaceHookBodies;
}  // namespace

#endif  // !defined(_WIN64)
