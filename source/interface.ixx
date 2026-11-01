// SPDX-License-Identifier: GPL-3.0-or-later
//
// interface
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

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

// Hook bodies: the machine-level entry points interface installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
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
        // The original randomises the track push immediate to 1..5:
        // `rand() % kDeathMusicTrackCount + kFirstDeathMusicTrack`.
        constexpr int kDeathMusicTrackCount = 5;
        constexpr int kFirstDeathMusicTrack = 1;
        auto* site = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(zpatch_death_music_site) + 1);
        injector::WriteMemory<uint8_t>(  // push imm8
            injector::raw_ptr(site),
            static_cast<uint8_t>(std::rand() % kDeathMusicTrackCount +
                                 kFirstDeathMusicTrack),
            true);
    }
    return 1;
}
}  // extern "C"

#endif  // !defined(_WIN64)

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

                    // The nine-byte argument-setup sequence before the
                    // `cmp [ebp+18], 1C` check is NOP'd by the original.
                    constexpr ptrdiff_t kPipeBombNopOffset = -9;
                    constexpr size_t kPipeBombNopLength = 9;
                    NopAt(ctx, match + kPipeBombNopOffset, kPipeBombNopLength);
                }
            }
        }

    }


    // RestoreDeathMusic

    void InstallRestoreDeathMusic(PatchContext& ctx)
    {

        TraceArm("RestoreDeathMusic");

        // RestoreDeathMusic: 3 siginstalls. The first two call sites are 13
        // bytes before their matches (the E8 replaced there); the third is the
        // call at the match itself.
        if (ctx.Option("RestoreDeathMusic", "0"))
        {
            const GameImage& image = ctx.image;
            constexpr ptrdiff_t kCallBeforeFirstSite = -13;
            constexpr ptrdiff_t kCallBeforeSecondSite = -13;

            if (auto pattern = Pattern(image, "0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match + kCallBeforeFirstSite,
                       OPP_BODY(abi_death_music_install));
            }

            if (auto pattern = Pattern(image, "C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match + kCallBeforeSecondSite,
                       OPP_BODY(abi_death_music_enabled));
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match, OPP_BODY(abi_death_music_enabled));
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
            InstallEvent().Add(&InstallPipeBombDropIconSupport, "InstallPipeBombDropIconSupport");
            InstallEvent().Add(&InstallRestoreDeathMusic, "InstallRestoreDeathMusic");
        }
    } interfaceFeatures;
} // namespace

} // namespace opp::features
