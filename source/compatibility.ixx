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

#include <span>

export module opp.compatibility;

import opp.steam;
import opp.patch;
import opp.game;
import opp.common;
import opp.config;
import opp.log;

// Hook bodies: the machine-level entry points compatibility installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

extern "C" void abi_gfwl_patch() noexcept
{
    if (zpatch_flag_patch4_modchecks)
    {
        if (zpatch_tls_static_header(zpatch_gfwl_scan_guard))
        {
            // _Init_thread_header claimed the guard; scan then publish the epoch.
            // The window is the handler's /GS prologue, its `mov [esp+0x1A8],eax`
            // and the first three callee-saved pushes: the pre-write instruction
            // window of the patch-4 check.
            const uint8_t* const found = scan_game(
                "33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
            zpatch_gfwl_site = found == nullptr
                                   ? 0u
                                   : static_cast<uint32_t>(
                                         reinterpret_cast<uintptr_t>(found));
            zpatch_tls_static_footer(zpatch_gfwl_scan_guard);
        }
        if (zpatch_gfwl_site != 0)
        {
            // The handler's second write dereferences the address operand and
            // zeroes it; the original does so without a null check. The operand is
            // the immediate 30 bytes into the handler, i.e. 15 bytes past the
            // scanned window's end, so the scan does not cover the value it reads.
            // Widening the window to 35 bytes would make the offset derived.
            const auto* const site = reinterpret_cast<const uint8_t*>(
                static_cast<uintptr_t>(zpatch_gfwl_site));
            wp_dword(reinterpret_cast<void*>(ReadDword(site + 30)), 0);
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
        // The version dword the original's getnewver() returns (0x10000E83); its
        // little-endian byte image is 83 0E 00 10. The four dwords it writes are
        // xlive.dll's update-window flags, in the original's store order; the RVAs
        // live in hooks.h so the test suite can pin them.
        for (const uintptr_t rva : {kXliveUpdateFlag0Rva, kXliveUpdateFlag1Rva,
                                    kXliveUpdateFlag2Rva, kXliveUpdateFlag3Rva})
            wp_dword(reinterpret_cast<void*>(base + rva), 0x10000E83u);
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
// It stands in for _XLiveQueryProtectedDataInformation@8 (query id + result
// slot): the two arguments stay unnamed because the body ignores them.
int __stdcall abi_savegame_fix_null(int, int) { return 0; }

// 0x10045580 SavegameFix close: push [ebp+8] / call CloseHandle / xor eax,eax /
// ret 4 (__stdcall, one argument) - _XLiveCloseProtectedDataContext@4.
int __stdcall abi_savegame_fix_close(void* handle)
{
    if (handle) CloseHandle(handle);
    return 0;
}

// 0x100455e0 SavegameFix mutex: if (!out) return 1;
// *out = CreateMutexA(0,0,0); return 0; __stdcall, two arguments -
// _XLiveCreateProtectedDataContext@8.
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
        // The caller's frame slots the replayed stores write: +0Ch / +10h are
        // the two dwords the original's pair of `mov [esp+..]` instructions use.
        mov  dword ptr [esp + 0Ch], eax
        mov  dword ptr [esp + 10h], edx
        jmp  dword ptr [zpatch_gfwl_resume]
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}

// 0x100455a0 SavegameFix read/copy: if (*out >= size) { if (dst) memcpy(dst, src, size);
// *out = size; return 0; } else { *out = size; return 0x8007007A; }
// __stdcall, five arguments (the fifth is unused by the original) -
// _XLiveUnprotectData@20.
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

            if (auto pattern = Pattern(image, "8B 4C 24 08 56 8B 74 24 08 80 3E 22");
                pattern.size() > 0)
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
        // The GFWL update stub both patch-4 modded-file checks and the pre-6
        // popup removal detour.
        constexpr std::string_view kGfwlStubWindow =
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08";
        // The stub replays the match's first twelve bytes (two arithmetic ops and
        // two stores into the caller's frame) and tail-jumps there.
        constexpr ptrdiff_t kGfwlResumeOffset = 12;
        static_assert(kGfwlResumeOffset == 4 + 4 + 4);

        // The whole-check skip: 1.0.6.0's only jump and 1.0.7.0's second.
        constexpr std::string_view kModCheckSkipCheck =
            "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8";
        // Destination delta from a scan: +0x87 skips the whole check (1.0.6.0
        // 0x003982->0x003A09, 1.0.7.0 0x003962->0x0039E9).
        constexpr uintptr_t kModCheckSkipCheckDelta = 0x87;
        // A cleared flag byte (the recovery's "the check is off" value).
        constexpr uint8_t kModCheckFlagOff = 0x00;

        // Per build: the two windows that are not shared and the delta each jump
        // lands at. 1.0.6.0 has one jump over the check, 1.0.7.0 two (the short
        // +0x12 one first, then the whole-check one).
        struct ModCheckRow final
        {
            GameBuild        build;
            std::string_view jump;
            uintptr_t        jump_delta;
            std::string_view jump_alt;  // empty when the build has one jump
            uintptr_t        jump_alt_delta;
            std::string_view short_jump_a;  // its tail differs per build
        };
        constexpr ModCheckRow kModCheckRows[] = {
            {GameBuild::v1060, kModCheckSkipCheck, kModCheckSkipCheckDelta, {}, 0,
             "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68"},
            {GameBuild::v1070,
             "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D",
             18,  // 1.0.7.0's shorter skip of one branch (+0x12, 0x00389C->0x0038AE)
             kModCheckSkipCheck, kModCheckSkipCheckDelta,
             "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68"},
        };

        const ModCheckRow* FindModCheckRow(GameBuild build) noexcept
        {
            for (const ModCheckRow& row : kModCheckRows)
            {
                if (row.build == build)
                    return &row;
            }
            return nullptr;
        }

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

        // The scanned sites of one row. Every scan of a row happens before its
        // first write: the windows overlap (the NOP run sits inside the others),
        // so a scan after a write would miss the neighbours.
        struct ModCheckSites final
        {
            uint8_t* jump = nullptr;
            uint8_t* jump_alt = nullptr;
            uint8_t* clear_a = nullptr;
            uint8_t* clear_b = nullptr;
            uint8_t* nop_run = nullptr;
            uint8_t* short_jump_a = nullptr;
            uint8_t* short_jump_b = nullptr;
        };

        // One scan of a recovered window. `nullptr` means "leave this site
        // alone": either the build does not carry it (an empty window) or the
        // image is not the one the window was proved on (a count mismatch).
        uint8_t* FindSite(const GameImage& image, std::string_view window,
                          const char* what)
        {
            if (window.empty())
                return nullptr;
            auto pattern = Pattern(image, window);
            if (pattern.size() == 0 || !ExpectMatchCount(pattern, 1, what))
                return nullptr;
            return pattern.get(0).get<uint8_t>();
        }

        ModCheckSites FindModCheckSites(const GameImage& image, const ModCheckRow& row)
        {
            // The flag-clear, NOP-run and short-jump-B windows are the bytes 1.0.6.0
            // and 1.0.7.0 share byte-for-byte; only the jumps and the short-jump-A
            // tail are per build (the ModCheckRow fields).
            ModCheckSites sites;
            sites.jump = FindSite(image, row.jump, "RemoveModChecks jump");
            sites.jump_alt = FindSite(image, row.jump_alt, "RemoveModChecks whole-check jump");
            sites.clear_a = FindSite(
                image,
                "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74",
                "RemoveModChecks flag clear A");
            sites.clear_b = FindSite(
                image,
                "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8",
                "RemoveModChecks flag clear B");
            sites.nop_run = FindSite(
                image,
                "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8",
                "RemoveModChecks NOP run");
            sites.short_jump_a = FindSite(image, row.short_jump_a, "RemoveModChecks short jump A");
            sites.short_jump_b = FindSite(
                image,
                "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F",
                "RemoveModChecks short jump B");
            return sites;
        }

        // The two builds' writes, in the original order.
        void ApplyModCheckSites(PatchContext& ctx, const ModCheckRow& row,
                                const ModCheckSites& sites)
        {
            if (sites.jump != nullptr)
                JumpTo(ctx, sites.jump, sites.jump + row.jump_delta);
            if (sites.jump_alt != nullptr)
                JumpTo(ctx, sites.jump_alt, sites.jump_alt + row.jump_alt_delta);
            if (sites.clear_a != nullptr)
                WriteValue<uint8_t>(ctx, sites.clear_a, kModCheckFlagOff);
            if (sites.clear_b != nullptr)
                WriteValue<uint8_t>(ctx, sites.clear_b, kModCheckFlagOff);
            if (sites.nop_run != nullptr)
            {
                // The run the original carries is seven one-byte stores.
                for (size_t i = 0; i < 7; ++i)
                    NopAt(ctx, sites.nop_run + i, 1);
            }
            if (sites.short_jump_a != nullptr)
                WriteValue<uint8_t>(ctx, sites.short_jump_a, opp::ops::kJmpRel8);
            if (sites.short_jump_b != nullptr)
                WriteValue<uint8_t>(ctx, sites.short_jump_b, opp::ops::kJmpRel8);
        }
    } // namespace

    void PublishRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("publish RemoveModChecks");
        if (!WantsGfwlStub(ctx))
            return;
        if (auto pattern = Pattern(ctx.image, kGfwlStubWindow);
            pattern.size() > 0 && ExpectMatchCount(pattern, 1, "GFWL stub window"))
            zpatch_gfwl_resume = MatchAddress(pattern, kGfwlResumeOffset);
    }

    void InstallRemoveModChecks(PatchContext& ctx)
    {
        TraceArm("RemoveModChecks");

        // Patch 4 / pre-6 GFWL update popup share one stub.
        if (WantsGfwlStub(ctx))
        {
            if (auto pattern = Pattern(ctx.image, kGfwlStubWindow);
                pattern.size() > 0 && ExpectMatchCount(pattern, 1, "GFWL stub window"))
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_gfwl_stub));
            }
        }

        if (!ModChecksEnabled(ctx))
            return;

        // The build gate stays because several windows also resolve on builds the
        // original did not patch here; each site is a pre-write instruction window,
        // so a build whose bytes do not match installs nothing instead of writing
        // a stale address.
        const ModCheckRow* const row = FindModCheckRow(ctx.build);
        if (row != nullptr)
            ApplyModCheckSites(ctx, *row, FindModCheckSites(ctx.image, *row));
    }


    // RemoveRGSCLoginRequirement

    void InstallRemoveRGSCLoginRequirement(PatchContext& ctx)
    {

        TraceArm("RemoveRGSCLoginRequirement");

        // The original installs the arm unconditionally (handler 0x10045490);
        // this tree gates it on the option. One window covers every build but
        // 1.0.8.0, which has its own instruction sequence at RVA 0x498460 (the
        // scanned fallback below).
        if (ctx.Option("RemoveRGSCLoginRequirement", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(
                    image,
                    "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
                pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_rgsc_login_removed));
            }
            else if (ctx.build == GameBuild::v1080)
            {
                // Scanning the fallback keeps the same fail-safe (a build that
                // does not match installs nothing) instead of writing a bare RVA.
                if (auto fallback = Pattern(
                        image,
                        "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 ?? ?? 8B 0D");
                    fallback.size() > 0)
                {
                    JumpTo(ctx, fallback.get(0).get<uint8_t>(), OPP_BODY(abi_rgsc_login_removed));
                }
            }
        }

    }


    // SavegameFix

    namespace
    {
        // SavegameFix redirects the calls the game's save reader (sub_61EC60)
        // and writer (sub_61EB10) make into GFWL's save-protection API. A delta
        // is relative to its block's match and was verified on 1.0.8.0 against
        // the callee IDA resolves at the site; positive = after the match.
        struct XLiveRedirect final
        {
            ptrdiff_t delta;
            void*     body;
        };

        void RedirectXLiveCalls(PatchContext& ctx, const uint8_t* match,
                                std::span<const XLiveRedirect> redirects)
        {
            for (const XLiveRedirect& redirect : redirects)
                CallTo(ctx, match + redirect.delta, redirect.body);
        }
    } // namespace

    void InstallSavegameFix(PatchContext& ctx)
    {

        TraceArm("SavegameFix");

        // SavegameFix: two blocks of redirects (the reader's calls plus the
        // NOP'd loop branch, then the writer's calls). The trailing comment on a
        // row is the callee the original had at the site (1.0.8.0 VA).
        if (ctx.Option("SavegameFix", "1"))
        {
            const GameImage& image = ctx.image;
            const XLiveRedirect kReaderCalls[]{
                // _XLiveCloseProtectedDataContext@4
                {-73, OPP_BODY(abi_savegame_fix_close)},   // 0x61ED6C
                // _XLiveUnprotectData@20
                {-143, OPP_BODY(abi_savegame_fix_copy)},   // 0x61ED26
                {-176, OPP_BODY(abi_savegame_fix_copy)},   // 0x61ED05
                // _XLiveCreateProtectedDataContext@8
                {-280, OPP_BODY(abi_savegame_fix_mutex)},  // 0x61EC9D
                // _XLiveQueryProtectedDataInformation@8
                {-240, OPP_BODY(abi_savegame_fix_null)},   // 0x61ECC5
            };
            const XLiveRedirect kWriterCalls[]{
                // _XLiveCreateProtectedDataContext@8
                {-86, OPP_BODY(abi_savegame_fix_mutex)},   // 0x61EB4B
                // _XLiveQueryProtectedDataInformation@8
                {-47, OPP_BODY(abi_savegame_fix_null)},    // 0x61EB72
                // _XLiveProtectData@20
                {10, OPP_BODY(abi_savegame_fix_copy)},     // 0x61EBAB
                // _XLiveProtectData@20
                {90, OPP_BODY(abi_savegame_fix_copy)},     // 0x61EBFB
                // _XLiveCloseProtectedDataContext@4
                {128, OPP_BODY(abi_savegame_fix_close)},   // 0x61EC21
            };

            // The reader's 18-chunk loop; its match starts on a two-byte `jnz rel8`,
            // which the fix NOPs.
            if (auto pattern = Pattern(image, "75 ? 83 C1 01 83 C0 01 83 F9 04");
                pattern.size() > 0)
            {
                uint8_t* const match = pattern.get(0).get<uint8_t>();
                RedirectXLiveCalls(ctx, match, kReaderCalls);
                NopAt(ctx, match, 2);
            }

            // The writer's block.
            if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
            {
                auto* const match = pattern.get(0).get<uint8_t>();

                RedirectXLiveCalls(ctx, match, kWriterCalls);
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
                // jz or jmp to a jmp. No xlive image is available to scan, so the
                // RVA is the only anchor and the pre-image check is the failsafe:
                // a different xlive build is left untouched. The site is inside
                // xlive.dll, so the write goes through the module-scoped guard.
                auto* site = reinterpret_cast<uint8_t*>(xlive) + 988677;  // 0xF1605
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
