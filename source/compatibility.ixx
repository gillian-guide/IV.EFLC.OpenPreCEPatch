// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.compatibility - the pre-patch compatibility arms.
//
// One namespace per game subsystem: its windows, its bodies and its feature class
// sit together, and the class subscribes itself to the publish/install tiers from a
// file-scope instance. Every arm here replaces a handler outright (the GFWL
// stand-ins have no original call), so none of them calls through.
//
// The arm names and the OPP_BODY names are the original loader's: the write-set
// matrix attributes every byte to an arm name, and the x64 parity dump resolves
// a body name to the original handler VA.

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

using namespace opp;

#if !defined(_WIN64)
#include "body_helpers.inc"
#endif

// ---- AllowHashesForIDE ----

namespace opp::features::AllowHashes
{
#if !defined(_WIN64)
    // 0x10003e50: the game's model-name hash, __cdecl(char*) -> unsigned.
    // Replaced so that "hash:<number>" literals are accepted (see GameHash).
    unsigned int __cdecl abi_game_hash(const char* text)
    {
        return GameHash(text != nullptr ? std::string_view{text} : std::string_view{});
    }
#endif

    class AllowHashes
    {
    public:
        AllowHashes()
        {
            InstallEvent() += []
            {
                TraceArm("InstallAllowHashesForIDE");
                if (!Game::Option("AllowHashesForIDE", "1"))
                    return;

                auto pattern = find_pattern("8B 4C 24 08 56 8B 74 24 08 80 3E 22");
                if (pattern.size() == 0)
                    return;
                injector::MakeJMP(pattern.get_first<uint8_t>(), OPP_BODY(abi_game_hash), true);
            };
        }
    } allowHashes;
} // namespace opp::features::AllowHashes


// ---- RemoveModChecks / RemoveGFWLUpdatePopup ----

namespace opp::features::ModChecks
{
    // RemoveModChecks is the unified key. The old per-patch keys stay readable
    // as aliases, so an existing INI keeps working without a rewrite.
    bool ModChecksEnabled()
    {
        return Game::Option("RemoveModChecks", "0") ||
               Game::Option("Patch4RemoveModChecks", "0") ||
               Game::Option("Patch6and7RemoveModChecks", "0") ||
               Game::Option("Patch7RemoveModChecks", "0");
    }

    // "Patch 4" in GTA IV's numbering is 1.0.4.0 (v1040), not 1.0.0.4 (v1004):
    // the port read the INI's "patch 4" as 1.0.0.4 and gated the modded-file
    // check to the wrong build. The original installs it from a pattern with no
    // build compare (its recovered rows carry no build id), so the narrowed gate
    // stays, but on the build the check exists on. Pre-patch-6, the same stub
    // serves the update/sign-out popup removal half.
    bool WantsGfwlStub()
    {
        if (Game::Get().build == GameBuild::v1040 && ModChecksEnabled())
            return true;
        return BuildIs(Game::Get().build,
                       {GameBuild::v1000, GameBuild::v1004, GameBuild::v1040}) &&
               Game::Option("RemoveGFWLUpdatePopup", "0");
    }

#if !defined(_WIN64)
    // The stub's helper: the two flag-gated halves the original ran under
    // pushad/popad, so it keeps __cdecl with no arguments and may only touch the
    // scratch each flag names.
    extern "C" void abi_gfwl_patch() noexcept
    {
        if (zpatch_flag_patch4_modchecks)
        {
            if (zpatch_tls_static_header(zpatch_gfwl_scan_guard))
            {
                // _Init_thread_header claimed the guard; scan then publish the
                // epoch. The window is the handler's /GS prologue, its
                // `mov [esp+0x1A8],eax` and the first three callee-saved pushes:
                // the pre-write instruction window of the patch-4 check.
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
                // The handler's second write dereferences the address operand
                // and zeroes it; the original does so without a null check. The
                // operand is the immediate 30 bytes into the handler, i.e. 15
                // bytes past the scanned window's end, so the scan does not cover
                // the value it reads. Widening the window to 35 bytes would make
                // the offset derived.
                const auto* const site = Rebased<const uint8_t*>(zpatch_gfwl_site);
                wp_dword(reinterpret_cast<void*>(ReadDword(site + 30)), 0);
            }
        }
        if (zpatch_flag_gfwl_popup)
        {
            // @exact 0x10005421: four dword stores into xlive.dll's update-window
            // flags, with no base check; with xlive.dll absent the base is 0 and
            // the original faults on that page.
            const auto base = static_cast<uintptr_t>(zpatch_xlive_base);
            // Without xlive.dll there is no GFWL update popup to remove: the four
            // writes have no meaning at base 0, so the failsafe just skips them.
            if (zpatch_failsafe_skip("RemoveGFWLUpdatePopup xlive.dll", base == 0u))
                return;
            // The version dword the original's getnewver() returns (0x10000E83);
            // its little-endian byte image is 83 0E 00 10. The four dwords it
            // writes are xlive.dll's update-window flags, in the original's store
            // order; the RVAs live in hooks.h so the test suite can pin them.
            for (const uintptr_t rva : {kXliveUpdateFlag0Rva, kXliveUpdateFlag1Rva,
                                        kXliveUpdateFlag2Rva, kXliveUpdateFlag3Rva})
                wp_dword(reinterpret_cast<void*>(base + rva), 0x10000E83u);
        }
    }

    // The stub the patch-4 check and the pre-6 popup removal detour to; its
    // machine-level body is in the tail section below.
    static void abi_gfwl_stub();
#endif

    // The GFWL update stub's window, shared by both arms: the handler's two
    // arithmetic ops and the two stores of the caller's frame slots.
    static constexpr std::string_view kGfwlStubWindow =
        "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08";
    // The stub replays the match's first twelve bytes (two arithmetic ops and
    // two stores) and tail-jumps there.
    static constexpr ptrdiff_t kGfwlResumeOffset = 12;

    void Publish()
    {
        TraceArm("PublishRemoveModChecks");
        if (!WantsGfwlStub())
            return;

        auto pattern = find_pattern(kGfwlStubWindow);
        if (pattern.size() == 0 || !ExpectMatchCount(pattern, 1, "GFWL stub window"))
            return;
        zpatch_gfwl_resume = MatchAddress(pattern, kGfwlResumeOffset);
    }

    // The windows 1.0.6.0 and 1.0.7.0 share byte-for-byte; only the jumps and
    // the short-jump-A tail differ per build.
    static constexpr std::string_view kSkipWholeCheck =
        "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8";
    // Destination delta from a scan: +0x87 skips the whole check (1.0.6.0
    // 0x003982->0x003A09, 1.0.7.0 0x003962->0x0039E9).
    static constexpr uintptr_t kSkipWholeCheckDelta = 0x87;
    // 1.0.7.0's shorter skip of one branch (+0x12, 0x00389C->0x0038AE).
    static constexpr uintptr_t kShortSkipDelta = 0x12;
    // A cleared flag byte (the recovery's "the check is off" value).
    static constexpr uint8_t kFlagOff = 0x00;

    // The scanned sites of one build. Every scan of a build happens before its
    // first write: the windows overlap (the NOP run sits inside the others), so
    // a scan after a write would miss the neighbours.
    struct Sites final
    {
        uint8_t* check = nullptr;      // the whole-check jump
        uint8_t* shortJump = nullptr;  // 1.0.7.0's skip of one branch
        uint8_t* clearA = nullptr;
        uint8_t* clearB = nullptr;
        uint8_t* nopRun = nullptr;
        uint8_t* shortA = nullptr;
        uint8_t* shortB = nullptr;
    };

    // One scan of a recovered window. `nullptr` means "leave this site alone":
    // either the build does not carry it or the image is not the one the window
    // was proved on (a count mismatch).
    uint8_t* FindSite(std::string_view window, const char* what)
    {
        auto pattern = find_pattern(window);
        if (pattern.size() == 0 || !ExpectMatchCount(pattern, 1, what))
            return nullptr;
        return pattern.get_first<uint8_t>();
    }

    Sites FindSites(GameBuild build)
    {
        Sites sites;
        sites.clearA = FindSite(
            "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74",
            "RemoveModChecks flag clear A");
        sites.clearB = FindSite(
            "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8",
            "RemoveModChecks flag clear B");
        sites.nopRun = FindSite(
            "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8",
            "RemoveModChecks NOP run");
        sites.shortB = FindSite(
            "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F",
            "RemoveModChecks short jump B");
        sites.check = FindSite(kSkipWholeCheck, "RemoveModChecks whole-check jump");
        if (build == GameBuild::v1060)
        {
            // One jump over the whole check.
            sites.shortA = FindSite(
                "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68",
                "RemoveModChecks short jump A");
        }
        else
        {
            // 1.0.7.0 skips one branch first, then the whole check.
            sites.shortJump = FindSite(
                "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D",
                "RemoveModChecks short jump");
            sites.shortA = FindSite(
                "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68",
                "RemoveModChecks short jump A");
        }
        return sites;
    }

    // The two builds' writes, in the original order.
    void PatchChecks()
    {
        const Sites sites = FindSites(Game::Get().build);
        if (sites.shortJump != nullptr)
            injector::MakeJMP(sites.shortJump, sites.shortJump + kShortSkipDelta, true);
        if (sites.check != nullptr)
            injector::MakeJMP(sites.check, sites.check + kSkipWholeCheckDelta, true);
        if (sites.clearA != nullptr)
            injector::WriteMemory<uint8_t>(sites.clearA, kFlagOff, true);
        if (sites.clearB != nullptr)
            injector::WriteMemory<uint8_t>(sites.clearB, kFlagOff, true);
        if (sites.nopRun != nullptr)
            injector::MakeNOP(sites.nopRun, 7, true);  // seven one-byte stores in the original
        if (sites.shortA != nullptr)
            injector::WriteMemory<uint8_t>(sites.shortA, opp::ops::kJmpRel8, true);
        if (sites.shortB != nullptr)
            injector::WriteMemory<uint8_t>(sites.shortB, opp::ops::kJmpRel8, true);
    }

    void Install()
    {
        TraceArm("InstallRemoveModChecks");

        // Patch 4 / pre-6 GFWL update popup share one stub.
        if (WantsGfwlStub())
        {
            auto pattern = find_pattern(kGfwlStubWindow);
            if (pattern.size() > 0 && ExpectMatchCount(pattern, 1, "GFWL stub window"))
                injector::MakeJMP(pattern.get_first<uint8_t>(), OPP_BODY(abi_gfwl_stub), true);
        }

        if (!ModChecksEnabled())
            return;

        // The build gate stays because several windows also resolve on builds the
        // original did not patch here; each site is a pre-write instruction
        // window, so a build whose bytes do not match installs nothing instead of
        // writing a stale address.
        if (Game::Get().build == GameBuild::v1060 || Game::Get().build == GameBuild::v1070)
            PatchChecks();
    }

    class ModChecks
    {
    public:
        ModChecks()
        {
            PublishEvent() += []
            {
                Publish();
            };
            InstallEvent() += []
            {
                Install();
            };
        }
    } modChecks;
} // namespace opp::features::ModChecks


// ---- RemoveRGSCLoginRequirement ----

namespace opp::features::RgscLogin
{
    class RgscLogin
    {
    public:
        RgscLogin()
        {
            InstallEvent() += []
            {
                TraceArm("InstallRemoveRGSCLoginRequirement");

                // The original installs the arm unconditionally (handler 0x10045490);
                // this tree gates it on the option. One window covers every build but
                // 1.0.8.0, which has its own instruction sequence at RVA 0x498460.
                if (!Game::Option("RemoveRGSCLoginRequirement", "0"))
                    return;

                if (auto pattern = find_pattern(
                        "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
                    pattern.size() > 0)
                {
                    injector::MakeJMP(pattern.get_first<uint8_t>(),
                                      OPP_BODY(abi_rgsc_login_removed), true);
                    return;
                }

                // Scanning the fallback keeps the same fail-safe (a build that does not
                // match installs nothing) instead of writing a bare RVA.
                if (Game::Get().build != GameBuild::v1080)
                    return;
                if (auto fallback = find_pattern(
                        "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 ?? ?? 8B 0D");
                    fallback.size() > 0)
                {
                    injector::MakeJMP(fallback.get_first<uint8_t>(),
                                      OPP_BODY(abi_rgsc_login_removed), true);
                }
            };
        }
    } rgscLogin;
} // namespace opp::features::RgscLogin


// ---- SavegameFix ----

namespace opp::features::SavegameFix
{
#if !defined(_WIN64)
    // 0x100455a0 _XLiveUnprotectData@20: copy out of the save's protected block.
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

    // 0x10045580 _XLiveCloseProtectedDataContext@4.
    int __stdcall abi_savegame_fix_close(void* handle)
    {
        if (handle) CloseHandle(handle);
        return 0;
    }

    // 0x100455e0 _XLiveCreateProtectedDataContext@8: a mutex stands in for the
    // protected context.
    int __stdcall abi_savegame_fix_mutex(void*, void** out_handle)
    {
        if (!out_handle) return 1;
        *out_handle = CreateMutexA(nullptr, FALSE, nullptr);
        return 0;
    }

    // @exact 0x10038570 _XLiveQueryProtectedDataInformation@8: "nothing is
    // protected", so the save is written as plain data. hooks.h publishes this
    // body: opp.performance's allocator vtable names it by address (row 12).
    extern "C" int __stdcall abi_savegame_fix_null(int, int) { return 0; }
#endif

    // A call the save reader (sub_61EC60) or writer (sub_61EB10) makes into
    // GFWL's save-protection API. `delta` is relative to the block's match and
    // was verified on 1.0.8.0 against the callee IDA resolves at the site;
    // positive = after the match.
    struct XLiveCall final
    {
        ptrdiff_t delta;
        void*     body;
    };

    void Redirect(const uint8_t* block, std::span<const XLiveCall> calls)
    {
        for (const XLiveCall& call : calls)
            injector::MakeCALL(injector::raw_ptr(const_cast<uint8_t*>(block + call.delta)),
                               call.body, true);
    }

    class SavegameFix
    {
    public:
        SavegameFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallSavegameFix");
                if (!Game::Option("SavegameFix", "1"))
                    return;

                // The reader's 18-chunk loop; its match starts on a two-byte `jnz`, which
                // the fix NOPs.
                if (auto pattern = find_pattern("75 ? 83 C1 01 83 C0 01 83 F9 04");
                    pattern.size() > 0)
                {
                    uint8_t* const block = pattern.get_first<uint8_t>();
                    const XLiveCall readerCalls[]{
                        {-73, OPP_BODY(abi_savegame_fix_close)},   // 0x61ED6C close context
                        {-143, OPP_BODY(abi_savegame_fix_copy)},   // 0x61ED26 unprotect
                        {-176, OPP_BODY(abi_savegame_fix_copy)},   // 0x61ED05 unprotect
                        {-280, OPP_BODY(abi_savegame_fix_mutex)},  // 0x61EC9D create context
                        {-240, OPP_BODY(abi_savegame_fix_null)},   // 0x61ECC5 query info
                    };
                    Redirect(block, readerCalls);
                    injector::MakeNOP(block, 2, true);
                }

                // The writer's block.
                if (auto pattern = find_pattern("8D 4C 24 18 51 6A 00 6A 0C 57");
                    pattern.size() > 0)
                {
                    const XLiveCall writerCalls[]{
                        {-86, OPP_BODY(abi_savegame_fix_mutex)},   // 0x61EB4B create context
                        {-47, OPP_BODY(abi_savegame_fix_null)},    // 0x61EB72 query info
                        {10, OPP_BODY(abi_savegame_fix_copy)},     // 0x61EBAB protect
                        {90, OPP_BODY(abi_savegame_fix_copy)},     // 0x61EBFB protect
                        {128, OPP_BODY(abi_savegame_fix_close)},   // 0x61EC21 close context
                    };
                    Redirect(pattern.get_first<uint8_t>(), writerCalls);
                }
            };
        }
    } savegameFix;
} // namespace opp::features::SavegameFix


// ---- GFWLProtectionDisabler ----

namespace opp::features::GfwlProtection
{
    class GfwlProtection
    {
    public:
        GfwlProtection()
        {
            InstallEvent() += []
            {
                TraceArm("InstallGFWLProtectionDisabler");
                if (!Game::Option("GFWLProtectionDisabler", "1"))
                    return;

                const HMODULE xlive = GetModuleHandleA("xlive.dll");
                if (xlive == nullptr)
                    return;

                // The standalone GFWLProtectionDisabler2019.asi flips xlive.dll+0xF1605
                // (988677) from its jz or jmp to a jmp. No xlive image is available to
                // scan, so the RVA is the only anchor and the pre-image check is the
                // failsafe: a different xlive build is left untouched.
                auto* const site = reinterpret_cast<uint8_t*>(xlive) + 988677;
                const uint8_t opcode = *site;
                if (opcode == opp::ops::kJzRel8 || opcode == opp::ops::kJmpRel8)
                    injector::WriteMemory<uint8_t>(site, opp::ops::kJmpRel8, true);
            };
        }
    } gfwlProtection;
} // namespace opp::features::GfwlProtection


// ---- SteamAchievements ----

namespace opp::features::SteamAchievements
{
    class SteamAchievements
    {
    public:
        SteamAchievements()
        {
            InstallEvent() += []
            {
                TraceArm("InstallSteamAchievements");
                if (!Game::Option("SteamAchievements", "1"))
                    return;
                (void)InstallSteamAchievementsBridge(Game::Get().image);
            };
        }
    } steamAchievements;
} // namespace opp::features::SteamAchievements


// ---- machine-level tail ----

#if !defined(_WIN64)
namespace opp::features::ModChecks
{
    // @exact 0x100054E0 Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub: runs
    // the flag-gated helper between pushad/popad, replays the original's first
    // two instructions (its 64-bit sub of ECX:ESI from EDX:EAX into the caller's
    // stack slots) and tail-jumps to the handler-published continuation
    // (match+12).
    static __declspec(naked) void abi_gfwl_stub()
    {
        __asm {
            pushad
            call abi_gfwl_patch
            popad
            sub  eax, ecx
            sbb  edx, esi
            // The caller's frame slots the replayed stores write: +0Ch / +10h are
            // the two dwords the original's pair of `mov [esp+..]` instructions
            // use.
            mov  dword ptr [esp + 0Ch], eax
            mov  dword ptr [esp + 10h], edx
            jmp  dword ptr [zpatch_gfwl_resume]
            __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
        }
    }
} // namespace opp::features::ModChecks
#endif  // !defined(_WIN64)
