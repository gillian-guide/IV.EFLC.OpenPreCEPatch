// SPDX-License-Identifier: GPL-3.0-or-later
//
// compatibility
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

export module opp.compatibility;

import opp.steam;
import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

// Hook bodies: the machine-level entry points compatibility installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

namespace
{
// The GFWL update-window version dword in xlive.dll: the original's getnewver()
// returns 0x10000E83 and its handler stores that dword at all four sites (four
// bytes each). It is a version constant, not an instruction: 83 0E 00 10 is its
// little-endian byte image. RVAs recovered from its writer.
constexpr uint32_t kGfwlWindowVersion = 0x10000E83u;
// The four flag RVAs live in hooks.h (kXliveUpdateFlag0Rva .. 3) so the test
// suite can pin them; they are the original handler's decimal immediates.
}  // namespace

extern "C" void abi_gfwl_patch() noexcept
{
    if (zpatch_flag_patch4_modchecks)
    {
        if (zpatch_tls_static_header(zpatch_gfwl_scan_guard))
        {
            // _Init_thread_header claimed the guard; scan then publish the epoch.
            const uint8_t* found =
                scan_game("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
            zpatch_gfwl_site = found == nullptr
                                           ? 0u
                                           : static_cast<uint32_t>(
                                                 reinterpret_cast<uintptr_t>(found));
            zpatch_tls_static_footer(zpatch_gfwl_scan_guard);
        }
        if (zpatch_gfwl_site != 0)
        {
            // The handler's second write dereferences the operand at site+30
            // (the dword holding the update-window flag address) and zeroes it;
            // 0x10005427 does so without a null check.
            constexpr ptrdiff_t kGfwlFlagAddressOperand = 30;
            const uint32_t target = ReadDword(
                reinterpret_cast<const uint8_t*>(
                    static_cast<uintptr_t>(zpatch_gfwl_site)) +
                kGfwlFlagAddressOperand);
            wp_dword(reinterpret_cast<void*>(target), 0);
        }
    }
    if (zpatch_flag_gfwl_popup)
    {
        // @exact 0x10005421: four dword stores into xlive.dll's update-window
        // flags, with no base check; with xlive.dll absent the base is 0 and the
        // original faults on that page.
        const auto base = static_cast<uintptr_t>(zpatch_xlive_base);
        // Without xlive.dll there is no GFWL update popup to remove: the four
        // writes have no meaning at base 0, so the failsafe just skips them.
        if (zpatch_failsafe_skip("RemoveGFWLUpdatePopup xlive.dll", base == 0u))
            return;
        wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlag0Rva),
                 kGfwlWindowVersion);
        wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlag1Rva),
                 kGfwlWindowVersion);
        wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlag2Rva),
                 kGfwlWindowVersion);
        wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlag3Rva),
                 kGfwlWindowVersion);
    }
}

// 0x10003e50 AllowHashesForIDE: the game's model-name hash function, replaced so
// that "hash:<number>" literals are accepted (see GameHash). The original is
// __cdecl(char*) -> unsigned int; this wrapper keeps that ABI.
unsigned int __cdecl abi_game_hash(const char* text)
{
    return GameHash(text ? std::string_view{text} : std::string_view{});
}

// @exact 0x10038570 SavegameFix: xor eax,eax / ret 8 (__stdcall, two arguments).
int __stdcall abi_savegame_fix_null(int, int) { return 0; }

// 0x10045580 SavegameFix close: push [ebp+8] / call CloseHandle / xor eax,eax /
// ret 4 (__stdcall, one argument).
int __stdcall abi_savegame_fix_close(void* handle)
{
    if (handle) CloseHandle(handle);
    return 0;
}

// 0x100455e0 SavegameFix mutex: if (!out) return 1;
// *out = CreateMutexA(0,0,0); return 0; __stdcall, two arguments.
int __stdcall abi_savegame_fix_mutex(void*, void** out_handle)
{
    if (!out_handle) return 1;
    *out_handle = CreateMutexA(nullptr, FALSE, nullptr);
    return 0;
}

// @exact 0x100054E0 Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub: runs the
// flag-gated helper between pushad/popad, replays the original's first two
// instructions (its 64-bit sub of ECX:ESI from EDX:EAX into the caller's stack
// slots) and tail-jumps to the handler-published continuation (match+12).
static __declspec(naked) void abi_gfwl_stub()
{
    __asm {
        pushad
        call abi_gfwl_patch
        popad
        sub  eax, ecx
        sbb  edx, esi
        mov  dword ptr [esp + 0Ch], eax
        mov  dword ptr [esp + 10h], edx
        jmp  dword ptr [zpatch_gfwl_resume]
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}

// 0x100455a0 SavegameFix read/copy: if (*out >= size) { if (dst) memcpy(dst, src, size);
// *out = size; return 0; } else { *out = size; return 0x8007007A; }
// __stdcall, five arguments (the fifth is unused by the original).
int __stdcall abi_savegame_fix_copy(const void* src, uint32_t size, void* dst,
                                    uint32_t* out_size, int)
{
    if (*out_size >= size)
    {
        if (dst) std::memcpy(dst, src, size);
        *out_size = size;
        return 0;
    }
    *out_size = size;
    // 0x8007007A is HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), the value
    // the original's copy helper returns.
    return static_cast<int>(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
}
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{

    // AllowHashesForIDE

    void InstallAllowHashesForIDE(PatchContext& ctx)
    {

        TraceArm("AllowHashesForIDE");

        // AllowHashesForIDE: 1 sigjump
        if (ctx.Option("AllowHashesForIDE", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 4C 24 08 56 8B 74 24 08 80 3E 22"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_game_hash));
            }
        }

    }


    // RemoveModChecks / RemoveGFWLUpdatePopup (patch 4 and patch 6/7 modded-file
    // checks plus the pre-patch-6 GFWL update popup).

    namespace
    {
        constexpr const char* kGfwlStubPattern =
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08";

        bool ModChecksEnabled(const PatchContext& ctx)
        {
            // RemoveModChecks is the unified key. The old per-patch keys stay
            // readable as aliases, so an existing INI keeps working without the
            // user having to rewrite it.
            return ctx.Option("RemoveModChecks", "0") ||
                   ctx.Option("Patch4RemoveModChecks", "0") ||
                   ctx.Option("Patch6and7RemoveModChecks", "0") ||
                   ctx.Option("Patch7RemoveModChecks", "0");
        }

        bool PrePatch6(GameBuild build) noexcept
        {
            return BuildIs(build, {GameBuild::v1000, GameBuild::v1004, GameBuild::v1040});
        }

        bool WantsGfwlStub(const PatchContext& ctx)
        {
            // "Patch 4" in GTA IV's numbering is 1.0.4.0 (v1040), not 1.0.0.4
            // (v1004): the port read the INI's "patch 4" as 1.0.0.4 and gated the
            // modded-file check to the wrong build. The original installs it from
            // a pattern with no build compare (recovered rows carry no build id),
            // so the narrowed gate stays, but on the build the check exists on.
            if (ctx.build == GameBuild::v1040 && ModChecksEnabled(ctx))
                return true;
            // Pre-patch-6: the update/sign-out popup removal half.
            if (PrePatch6(ctx.build) && ctx.Option("RemoveGFWLUpdatePopup", "0"))
                return true;
            return false;
        }
    } // namespace

    void PublishRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("publish RemoveModChecks");
        if (!WantsGfwlStub(ctx))
            return;
        if (auto pattern = Pattern(ctx.image, kGfwlStubPattern); pattern.size() > 0)
            zpatch_gfwl_resume = MatchAddress(pattern, 12);
    }

    void InstallRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("RemoveModChecks");

        // Patch 4 / pre-6 GFWL update popup share one stub.
        if (WantsGfwlStub(ctx))
        {
            if (auto pattern = Pattern(ctx.image, kGfwlStubPattern); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_gfwl_stub));
            }
        }

        if (!ModChecksEnabled(ctx))
            return;

        const GameImage& image = ctx.image;

        // The recovered handler wrote per-build RVAs; each site is now the pre-write
        // instruction window at that RVA (the rows proved by tools/opcep_sigcheck.py on the
        // on-disk builds), so a build whose bytes do not match installs nothing instead of
        // writing a stale address. The build gate stays because several windows also resolve
        // on builds the original did not patch here.
        //
        // Every site is matched before the first write: the seven adjacent one-byte NOPs
        // live inside each other's windows, so a scan after a write would lose the
        // neighbours (the double-scan defect).
        //
        // Destination deltas from a scan to the trampoline: +0x87 skips the whole check
        // (1.0.6.0 0x003982->0x003A09, 1.0.7.0 0x003962->0x0039E9), +0x12 is 1.0.7.0's
        // shorter skip (0x00389C->0x0038AE). Both are the same on every gated build.
        constexpr uintptr_t kModCheckJumpDelta = 0x87;
        constexpr uintptr_t kModCheckShortJumpDelta = 0x12;
        // A cleared flag byte (the recovery's "the check is off" value).
        constexpr uint8_t kFlagOff = 0x00;
        // The modded-file check instruction run carries seven one-byte stores.
        constexpr size_t kModCheckNopCount = 7;
        const auto findSite = [&image](const char* signature) -> uint8_t* {
            auto pattern = Pattern(image, signature);
            return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
        };

        // The two builds' windows: the same actions in the original order.
        // 1.0.6.0 has one jump over the check, 1.0.7.0 two (the short +0x12 and
        // the +0x87 one); the remaining five windows are shared except for the
        // two short-jump windows' tails.
        struct ModCheckRow final
        {
            const char* jump;
            uintptr_t jump_delta;
            const char* jump_alt;  // 1.0.7.0's second jump; null for 1.0.6.0
            uintptr_t jump_alt_delta;
            const char* clear_a;
            const char* clear_b;
            const char* nop_run;
            const char* short_jump_a;
            const char* short_jump_b;
        };
        constexpr ModCheckRow kRows[] = {
            // 1.0.6.0 (1060): one jump over the check, two flag bytes cleared,
            // seven operand bytes NOPed, two short jumps forced.
            {"75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8",
             kModCheckJumpDelta, nullptr, 0,
             "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74",
             "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8",
             "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8",
             "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68",
             "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"},
            // 1.0.7.0 (1070): the same shape with two jumps, and its own
            // RVAs/signatures.
            {"75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D",
             kModCheckShortJumpDelta,
             "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8", kModCheckJumpDelta,
             "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74",
             "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8",
             "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8",
             "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68",
             "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"},
        };

        // The scanned sites. Every scan of a row happens before its first write:
        // the windows overlap (the NOP run sits inside the others), so a scan
        // after a write would miss the neighbours.
        struct ModCheckSites final
        {
            uint8_t* jump;
            uint8_t* jump_alt;
            uint8_t* clear_a;
            uint8_t* clear_b;
            uint8_t* nop_run;
            uint8_t* short_jump_a;
            uint8_t* short_jump_b;
        };

        const auto find_row = [&](const ModCheckRow& row) {
            ModCheckSites sites{};
            sites.jump = findSite(row.jump);
            if (row.jump_alt != nullptr)
                sites.jump_alt = findSite(row.jump_alt);
            sites.clear_a = findSite(row.clear_a);
            sites.clear_b = findSite(row.clear_b);
            sites.nop_run = findSite(row.nop_run);
            sites.short_jump_a = findSite(row.short_jump_a);
            sites.short_jump_b = findSite(row.short_jump_b);
            return sites;
        };

        const auto apply_row = [&](const ModCheckRow& row, const ModCheckSites& sites) {
            if (sites.jump != nullptr)
                JumpTo(ctx, sites.jump, sites.jump + row.jump_delta);
            if (sites.jump_alt != nullptr)
                JumpTo(ctx, sites.jump_alt, sites.jump_alt + row.jump_alt_delta);
            if (sites.clear_a != nullptr)
                WriteValue<uint8_t>(ctx, sites.clear_a, kFlagOff);
            if (sites.clear_b != nullptr)
                WriteValue<uint8_t>(ctx, sites.clear_b, kFlagOff);
            if (sites.nop_run != nullptr)
            {
                for (size_t i = 0; i < kModCheckNopCount; ++i)
                    NopAt(ctx, sites.nop_run + i, 1);
            }
            if (sites.short_jump_a != nullptr)
                WriteValue<uint8_t>(ctx, sites.short_jump_a, opp::ops::kJmpRel8);
            if (sites.short_jump_b != nullptr)
                WriteValue<uint8_t>(ctx, sites.short_jump_b, opp::ops::kJmpRel8);
        };

        if (ctx.build == GameBuild::v1060)
        {
            const ModCheckSites sites = find_row(kRows[0]);
            apply_row(kRows[0], sites);
        }
        if (ctx.build == GameBuild::v1070)
        {
            const ModCheckSites sites = find_row(kRows[1]);
            apply_row(kRows[1], sites);
        }
    }


    // RemoveRGSCLoginRequirement

    void InstallRemoveRGSCLoginRequirement(PatchContext& ctx)
    {

        TraceArm("RemoveRGSCLoginRequirement");

        // RemoveRGSCLoginRequirement: the original installs it unconditionally;
        // this tree gates it on the option. The pattern handles every build;
        // 1.0.8.0 has an RVA fallback because the pattern is absent there
        // (original handler 0x10045490).
        if (ctx.Option("RemoveRGSCLoginRequirement", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_rgsc_login_removed));
            }
            else if (ctx.build == GameBuild::v1080)
            {
                // The 1.0.8.0 site (RVA 0x498460) has its own instruction
                // sequence; scanning it keeps the same fail-safe (a build that
                // does not match installs nothing) instead of writing a bare RVA.
                if (auto fallback = Pattern(image, "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 ?? ?? 8B 0D"); fallback.size() > 0)
                {
                    JumpTo(ctx, fallback.get(0).get<uint8_t>(), OPP_BODY(abi_rgsc_login_removed));
                }
            }
        }

    }


    // SavegameFix

    void InstallSavegameFix(PatchContext& ctx)
    {

        TraceArm("SavegameFix");

        // SavegameFix: 2 siginstalls, 1 sigwrite. Offsets are relative to the
        // two matches: the five call sites before the loop's match, the jnz pair
        // NOP'd at it, and the five calls around the second match.
        if (ctx.Option("SavegameFix", "1"))
        {
            const GameImage& image = ctx.image;
            constexpr ptrdiff_t kCloseBeforeLoop = -73;
            constexpr ptrdiff_t kCopyBeforeLoopA = -143;
            constexpr ptrdiff_t kCopyBeforeLoopB = -176;
            constexpr ptrdiff_t kMutexBeforeLoop = -280;
            constexpr ptrdiff_t kNullBeforeLoop = -240;
            constexpr size_t kLoopBranchLength = 2;
            constexpr ptrdiff_t kMutexBeforeSecond = -86;
            constexpr ptrdiff_t kNullBeforeSecond = -47;
            constexpr ptrdiff_t kCopyAtSecond = 10;
            constexpr ptrdiff_t kCopyAfterSecond = 90;
            constexpr ptrdiff_t kCloseAfterSecond = 128;

            {
                if (auto pattern = Pattern(image, "75 ? 83 C1 01 83 C0 01 83 F9 04"); pattern.size() > 0)
                {
                    uint8_t* match = pattern.get(0).get<uint8_t>();
                    CallTo(ctx, match + kCloseBeforeLoop, OPP_BODY(abi_savegame_fix_close));
                    CallTo(ctx, match + kCopyBeforeLoopA, OPP_BODY(abi_savegame_fix_copy));
                    CallTo(ctx, match + kCopyBeforeLoopB, OPP_BODY(abi_savegame_fix_copy));
                    CallTo(ctx, match + kMutexBeforeLoop, OPP_BODY(abi_savegame_fix_mutex));
                    CallTo(ctx, match + kNullBeforeLoop, OPP_BODY(abi_savegame_fix_null));
                    NopAt(ctx, match, kLoopBranchLength);
                }
            }

            if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match + kMutexBeforeSecond, OPP_BODY(abi_savegame_fix_mutex));
                CallTo(ctx, match + kNullBeforeSecond, OPP_BODY(abi_savegame_fix_null));
                CallTo(ctx, match + kCopyAtSecond, OPP_BODY(abi_savegame_fix_copy));
                CallTo(ctx, match + kCopyAfterSecond, OPP_BODY(abi_savegame_fix_copy));
                CallTo(ctx, match + kCloseAfterSecond, OPP_BODY(abi_savegame_fix_close));
            }
        }

    }


    // GFWLProtectionDisabler (standalone GFWLProtectionDisabler2019.asi)

    void InstallGFWLProtectionDisabler(PatchContext& ctx)
    {

        TraceArm("GFWLProtectionDisabler");

        if (ctx.Option("GFWLProtectionDisabler", "1"))
        {
            HMODULE xlive = GetModuleHandleA("xlive.dll");
            if (xlive != nullptr)
            {
                // The standalone ASI flips xlive.dll+0xF1605 (988677) from its
                // jz or jmp to a jmp. Keep the same pre-image check so a
                // different xlive build is left untouched. The site is inside
                // xlive.dll, so the write goes through the module-scoped guard.
                constexpr uintptr_t kXliveProtectionBranchRva = 0xF1605;
                auto* site = reinterpret_cast<uint8_t*>(xlive) + kXliveProtectionBranchRva;
                const uint8_t current = *site;
                if (current == opp::ops::kJzRel8 || current == opp::ops::kJmpRel8)
                    WriteModuleValue<uint8_t>(ctx, xlive, site, opp::ops::kJmpRel8);
            }
        }

    }


    // SteamAchievements (XUserWriteAchievements -> ISteamUserStats mirror)

    void InstallSteamAchievements(PatchContext& ctx)
    {

        TraceArm("SteamAchievements");

        if (ctx.Option("SteamAchievements", "1"))
            (void)InstallSteamAchievementsBridge(ctx.image);

    }


namespace
{
    class CompatibilityFeatures
    {
    public:
        CompatibilityFeatures()
        {
            PublishEvent().Add(&PublishRemoveModChecks, "PublishRemoveModChecks");
            InstallEvent().Add(&InstallAllowHashesForIDE, "InstallAllowHashesForIDE");
            InstallEvent().Add(&InstallRemoveModChecks, "InstallRemoveModChecks");
            InstallEvent().Add(&InstallRemoveRGSCLoginRequirement, "InstallRemoveRGSCLoginRequirement");
            InstallEvent().Add(&InstallSavegameFix, "InstallSavegameFix");
            InstallEvent().Add(&InstallGFWLProtectionDisabler, "InstallGFWLProtectionDisabler");
            InstallEvent().Add(&InstallSteamAchievements, "InstallSteamAchievements");
        }
    } compatibilityFeatures;
} // namespace

} // namespace opp::features
