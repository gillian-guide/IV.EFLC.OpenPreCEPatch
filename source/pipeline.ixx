// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.pipeline - the install tiers and the two loader passes.
//
// The schedule is the FusionFix event model: the pipeline owns three tiers
// (publish, install, post); each feature module subscribes its arms at static
// init time and the pipeline runs the tiers in order. There is no central arm
// list, phase enum or dependency graph: the tier is the ordering contract.
//
// Inside a tier the arms run in *name* order, not subscription order: the
// subscription order is the executable's static-init (link) order, which differs
// between the ASI, the dump and the tests, and two arms that patch the same site
// must not depend on which binary runs them. Name order is deterministic in every
// binary; the 96-cell matrix and the 18 crossbuild write sets prove it is
// write-equivalent to the old shipping schedule (the only visible difference is
// which of two identical writers is credited for the five shared VEHICLES bytes).
// tests/order_golden.txt freezes the per-tier arm set.
//
// This file also keeps the install driver (the old patches.*), the
// unconditional pointer preamble (preamble.*) and the launcher-name post step.

module;

#include "common.hxx"
#include "rebased.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module opp.pipeline;

import opp.common;
import opp.config;
import opp.game;
import opp.log;
import opp.patch;

export namespace opp
{
    // ---------------------------------------------------------------------
    // The install tiers
    //
    // A tier is a callback list. `name` is the arm's debug/order-dump label and
    // is optional so a test can subscribe an unnamed callback. Registration
    // happens at static-init time (file-scope objects in the feature modules),
    // so the list is complete before DllMain runs and no work happens at
    // registration.
    // ---------------------------------------------------------------------
    class Event
    {
    public:
        using Callback = void (*)(PatchContext&);

        // Appends `callback`; an overflow is logged and the arm skipped rather
        // than corrupting the list. `operator+=` is the unnamed form the native
        // tests use for a throwaway subscription.
        void Add(Callback callback, const char* name = nullptr) noexcept;
        void operator+=(Callback callback) noexcept { Add(callback, nullptr); }

        size_t size() const noexcept { return count_; }
        Callback callback(size_t index) const noexcept
        {
            return index < count_ ? entries_[index].callback : nullptr;
        }
        const char* name(size_t index) const noexcept
        {
            return index < count_ ? entries_[index].name : nullptr;
        }

    private:
        struct Entry
        {
            Callback callback{};
            const char* name{};
        };
        // 122 arms today; room for a new one without reallocation, which would
        // be illegal inside DllMain's load path.
        static constexpr size_t kCapacity = 256;
        Entry entries_[kCapacity]{};
        size_t count_ = 0;
    };

    // The three tiers, run in this order by PatchGame. The publish tier is
    // load-bearing: a site's original bytes must be read (and its resume
    // published) before any install overwrites them.
    Event& PublishEvent() noexcept;
    Event& InstallEvent() noexcept;
    Event& PostEvent() noexcept;
} // namespace opp

export namespace opp
{
    // Optional per-arm instrumentation for the offline dump tool. The ASI never
    // installs an observer, so the release path stays allocation- and call-free.
    struct PatchObserver
    {
        void* user{};
        void (*begin)(void* user, const char* name){};
        void (*end)(void* user, const char* name){};
    };

    // Applies every subscribed arm for `build`, tier by tier. When `observer` is
    // set, its hooks bracket every arm, in install order.
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer = {});

    // The loader's unconditional preamble: seven scans whose results the option
    // bodies read, plus the salt fallback when the build probe misses.
    struct PreambleReport
    {
        size_t resolved{};
        size_t missing{};
    };

    PreambleReport FindGamePointers(GameImage image) noexcept;

    // True when the game was started through PlayGTAIV.exe (the original's
    // process probe). The offline parity dump cannot have that process, so the
    // dump pins the answer from OPCEP_LAUNCHER_PROBE=1.
    bool LauncherProbe() noexcept;

    // Test seam: force the launcher probe (-1 = use the real process/env probe).
    // The native tests use it; the shipped ASI never sets it.
    void SetLauncherProbeOverride(bool forced) noexcept;

    // LaunchWithPlayGTAIV: rewrites the game's two "LaunchGTAIV.exe" name slots
    // to the PlayGTAIV launcher name. Runs after every arm, through the same
    // guarded writes as the arms.
    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx);
} // namespace opp

namespace opp
{
    Event& PublishEvent() noexcept
    {
        static Event event;
        return event;
    }

    Event& InstallEvent() noexcept
    {
        static Event event;
        return event;
    }

    Event& PostEvent() noexcept
    {
        static Event event;
        return event;
    }

    void Event::Add(Callback callback, const char* name) noexcept
    {
        if (callback == nullptr)
            return;
        if (count_ == kCapacity)
        {
            Log("tier: full, ignoring %s", name != nullptr ? name : "(unnamed)");
            return;
        }
        entries_[count_].callback = callback;
        entries_[count_].name = name;
        ++count_;
    }
} // namespace opp

namespace opp
{
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer)
    {
        PatchContext ctx{image, reinterpret_cast<uintptr_t>(image.base), build, config};

        const auto run = [&](Event& event, const char* tier) {
            Log("tier %s: %zu arms", tier, event.size());
            const size_t applied = ArmWritesApplied();
            const size_t skipped = ArmWritesSkipped();
            // The subscription order is the executable's static-init order, which
            // differs between the ASI, the dump and the tests. Run a tier in name
            // order so every binary sees the same sequence: the matrix then proves
            // it is write-equivalent to the original shipping schedule, and no
            // particular link order is load-bearing.
            std::vector<size_t> order(event.size());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t left, size_t right) {
                const char* a = event.name(left);
                const char* b = event.name(right);
                if (a == nullptr || b == nullptr)
                    return a != nullptr;  // unnamed arms last
                return std::strcmp(a, b) < 0;
            });
            for (const size_t index : order)
            {
                const char* name = event.name(index);
                if (observer.begin != nullptr)
                    observer.begin(observer.user, name != nullptr ? name : tier);
                event.callback(index)(ctx);
                if (observer.end != nullptr)
                    observer.end(observer.user, name != nullptr ? name : tier);
            }
            Log("tier %s: %zu applied, %zu skipped", tier, ArmWritesApplied() - applied,
                ArmWritesSkipped() - skipped);
        };

        run(PublishEvent(), "publish");
        run(InstallEvent(), "install");
        run(PostEvent(), "post");

        // The launcher write is gated by a process probe, not by an arm, so it
        // runs after the tiers but is still bracketed for the dump.
        if (observer.begin != nullptr)
            observer.begin(observer.user, "ApplyLaunchWithPlayGTAIV");
        ApplyLaunchWithPlayGTAIV(ctx);
        if (observer.end != nullptr)
            observer.end(observer.user, "ApplyLaunchWithPlayGTAIV");

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }
} // namespace opp

namespace opp
{
    namespace
    {
        // -1 = ask the process (or the dump's env override), 0/1 = forced.
        int g_launcherProbeOverride = -1;
    }

    void SetLauncherProbeOverride(bool forced) noexcept
    {
        g_launcherProbeOverride = forced ? 1 : 0;
    }

    bool LauncherProbe() noexcept
    {
        if (g_launcherProbeOverride >= 0)
            return g_launcherProbeOverride != 0;
#if defined(OPCEP_DUMP_TOOL)
        // The offline harness has no PlayGTAIV.exe process; the parity driver
        // pins the probe so the launcher writes stay observable.
        constexpr DWORD kLauncherProbeValueCapacity = 8;
        char value[kLauncherProbeValueCapacity]{};
        const DWORD length = GetEnvironmentVariableA(
            "OPCEP_LAUNCHER_PROBE", value, kLauncherProbeValueCapacity);
        return length > 0 && value[0] == '1';
#else
        return FindGameProcess() != 0;
#endif
    }

    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx)
    {
        // The original handler installs this on five builds only; the string
        // exists twice on each of them (and once on the EFLC builds, which the
        // original never patched, so the gate stays).
        if (!ctx.Option("LaunchWithPlayGTAIV", "0"))
            return;
        if (!BuildIs(ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1060,
                                 GameBuild::v1070, GameBuild::v1080}))
            return;
        if (!LauncherProbe())
            return;

        // Exactly the game's launcher-name string ("LaunchGTAIV.exe\0"); rows
        // LaunchName in tests/parity/address_sites.tsv prove two hits per gated
        // build. The replacement is the 16 bytes the original strcpy_s leaves in
        // the slot: "PlayGTAIV.exe\0" followed by the surviving "e\0" of the old
        // name.
        constexpr char kLaunchNameSignature[] =
            "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";
        constexpr char kLauncherNamePatch[16] = {
            'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};
        // The original's handler writes exactly its two table slots. Requiring
        // the exact count keeps a modded image with a third "LaunchGTAIV.exe\0"
        // untouched instead of writing a slot the original never wrote.
        constexpr size_t kLauncherSlotCount = 2;

        std::vector<const uint8_t*> matches;
        FindAllPatterns({ctx.image.base, ctx.image.size}, kLaunchNameSignature, matches);
        if (matches.size() != kLauncherSlotCount)
        {
            Log("launcher: %zu LaunchGTAIV name slot(s), expected %zu: skipped",
                matches.size(), kLauncherSlotCount);
            return;
        }
        for (const uint8_t* site : matches)
            WriteRaw(ctx, site, kLauncherNamePatch, sizeof(kLauncherNamePatch));
        Log("launcher: %zu PlayGTAIV name slot(s) patched", matches.size());
    }
} // namespace opp

namespace opp
{
    namespace
    {
        constexpr uint32_t kCallOperandOffset = 6u;
        // Byte distance from each recovered scan hit back to that function's
        // entry; the original's preamble published the entry address.
        constexpr uint32_t kPreFn59C74Offset = 42u;
        constexpr uint32_t kPreFn5D63COffset = 48u;
        constexpr uint32_t kPreFn59CDCOffset = 48u;

        uint32_t PointerValue(const void* pointer)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
        }
    } // namespace

    PreambleReport FindGamePointers(GameImage image) noexcept
    {
        PreambleReport report;
        if (!image.base || image.size == 0)
            return report;

        const ModuleView view = ExecutableView({image.base, image.size});
        const auto resolve = [&](std::string_view signature) -> const uint8_t* {
            Match found;
            return FindPattern(view, signature, found) ? found.address : nullptr;
        };
        // The seven counted scans; the build-probe salt fallback below is not one.
        const auto scan = [&](std::string_view signature) -> const uint8_t* {
            if (const uint8_t* hit = resolve(signature))
            {
                ++report.resolved;
                return hit;
            }
            ++report.missing;
            return nullptr;
        };

        if (const uint8_t* hit = scan("08 0B C2 8B F0 E8"))
        {
            // The E8 at the hit's offset+5 calls the script resolver; its rel32
            // operand sits at +6.
            zpatch_driveby_resolver = BranchDestination(hit, kCallOperandOffset);
        }

        if (const uint8_t* hit = scan("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23"))
            zpatch_pre_fn_59c74 = PointerValue(hit) - kPreFn59C74Offset;

        if (const uint8_t* hit =
                scan("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"))
            zpatch_model_lookup = PointerValue(hit);

        if (const uint8_t* hit = scan("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07"))
            zpatch_pre_ped_array = ReadDword(hit - sizeof(uint32_t));

        if (const uint8_t* hit =
                scan("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C"))
            zpatch_pre_fn_59b88 = PointerValue(hit);

        if (const uint8_t* hit =
                scan("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3"))
            zpatch_pre_fn_5d63c = PointerValue(hit) - kPreFn5D63COffset;

        if (const uint8_t* hit =
                scan("0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0"))
            zpatch_pre_fn_59cdc = PointerValue(hit) - kPreFn59CDCOffset;

        // Build-probe signature: absent on an unknown build, so all ten hash salts
        // fall back to these values.
        if (resolve("68 ? ? ? ? 68 A9 54 14 51 E8") == nullptr)
        {
            // The recovered fingerprint constants of the original's build table:
            // no derivation exists, the preamble's build switch selects the row.
            zpatch_salt_28 = 0x931F73A2u;
            zpatch_salt_3c = 0xA4EA0691u;
            zpatch_salt_40 = 0xC9DBDA90u;
            zpatch_salt_50 = 0x6B86410Eu;
            zpatch_salt_54 = 0x6DCD115Au;
            zpatch_salt_5c = 0x1CC648EAu;
            zpatch_salt_a8 = 0x7F6A60D3u;  // recovered fingerprint constant
            zpatch_salt_b8 = 0x62BFDB37u;  // recovered fingerprint constant
            zpatch_salt_c8 = 0x406B4B20u;  // recovered fingerprint constant
            zpatch_salt_d4 = 0x09399528u;  // recovered fingerprint constant
        }
        return report;
    }
} // namespace opp
