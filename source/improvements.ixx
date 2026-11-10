module;

#include <common.hxx>

#include <cctype>
#include <cstring>

export module improvements;

import common;
import game;

// The OpenPreCEPatch improvement groups, reproduced in this tree's idiom.
//
// The original 1:1 path is the default. Every group below is a selectable
// overlay: nothing changes unless OPCEP_IMPROVEMENTS names it. That is this
// tree's reading of the OpenPreCEPatch policy - the install-time write set is
// 1:1 in every configuration, and a group is the only thing that can change a
// runtime fault outcome.
//
//   unset / empty                -> none (1:1)
//   none / off / 0               -> none
//   all / on / 1                 -> the folded groups (currently failsafe)
//   only:a,b / a,b               -> the named groups
//   except:a,b                   -> the folded groups except the named ones
//
// `all` is the folded set, not every enum value, exactly as in OpenPreCEPatch:
// the entry-point rewrites that move a write to a new site stay outside `all`
// so a default run keeps the original write set. With only `failsafe` folded,
// `OPCEP_IMPROVEMENTS=except:failsafe` is the documented 1:1 configuration, and
// `OPCEP_IMPROVEMENTS=all` is the default-on failsafe the fault-parity tests
// compare against.
export enum class Improvement : uint32_t
{
    none = 0,

    // Runtime-pointer failsafes. A preamble pointer or game-published lookup a
    // body would dereference, or a site an arm would write, is checked first. A
    // missing one makes the arm keep the game's original path (the un-patched
    // value) instead of faulting.
    failsafe = 1u << 0,

    // The folded groups: what `all` means. `except:<group>` complements this set.
    all = failsafe,
};

export constexpr uint32_t Improve(Improvement value) noexcept
{
    return static_cast<uint32_t>(value);
}

// Pure parser, used by the loader and by the native tests.
export uint32_t ParseImprovements(const char* value) noexcept;

// Resolves OPCEP_IMPROVEMENTS once per process. Unset means the 1:1 path.
// OPCEP_PARITY_BASELINE forces every group off regardless of the environment.
export uint32_t EnabledImprovements() noexcept;

export bool ImprovementEnabled(Improvement group) noexcept;

// Test seam: replaces the resolved set for the rest of the process. The tests
// use it to run the same input under both configurations without re-executing
// the environment resolver's one-time cache.
export void SetImprovementsForTest(uint32_t groups) noexcept;

// Test seam: forgets the resolved set, so the next query reads the environment
// again. Used by the switch's own test to exercise the resolver, not just the
// parser.
export void ResetImprovementsForTest() noexcept;

export const char* ImprovementName(Improvement group) noexcept;

// Test seam: names a guard's `what` string as missing even when its value is
// present, so a test can drive one arm's failsafe decision on the real image
// without corrupting the image. Cleared by ClearMissingForTest.
export void SetMissingForTest(const char* what) noexcept;
export void ClearMissingForTest() noexcept;
export bool ForcedMissingForTest(const char* what) noexcept;

namespace ImprovementsDetail
{
    std::atomic<uint32_t> resolved{ 0xFFFFFFFFu };
    constexpr size_t kForcedMissingSlots = 16;
    const char* forcedMissing[kForcedMissingSlots] = {};
    size_t forcedMissingCount = 0;
}

export namespace Failsafe
{
    // The one decision every guard asks: is the value a body would dereference
    // missing? A false answer keeps the original instruction stream. A true
    // answer is only reachable with the failsafe group on, so with the group off
    // this is a plain `return false` and the original fault is unchanged.
    inline bool Missing(const char* what, bool missing) noexcept
    {
        const bool absent = missing || ForcedMissingForTest(what);
        if (!absent)
            return false;
        if (!ImprovementEnabled(Improvement::failsafe))
            return false;

        // Trace builds open the log; the shipped ASI has it closed, so this is a
        // branch and no write, like every other diagnostic in the tree.
        Log::Print("failsafe: {} is missing, continuing without the fix", what);
        return true;
    }

    // An arm's write site: the address and the bytes it covers must lie inside
    // the game image. With the group off this answers true and the original
    // write happens (an out-of-image site faults, as it does in the original).
    inline bool Site(const char* what, uintptr_t address, size_t bytes) noexcept
    {
        const auto& game = Game::Get();
        const auto begin = game.ImageBase();
        const auto end = begin + game.ImageSize();
        const bool inside = bytes != 0 && address >= begin && address < end &&
                            bytes <= end - address;
        if (inside)
            return true;
        if (!ImprovementEnabled(Improvement::failsafe))
            return true;

        Log::Print("failsafe: {} site {:#010x}+{:#x} is outside the image; skipped",
            what, address, bytes);
        return false;
    }

    // An arm's destination body: a body that did not resolve is a jump to zero
    // in the original. The failsafe refuses the install instead.
    inline bool Body(const char* what, uintptr_t body) noexcept
    {
        if (body != 0)
            return true;
        if (!ImprovementEnabled(Improvement::failsafe))
            return true;

        Log::Print("failsafe: {} body did not resolve; skipped", what);
        return false;
    }
}

namespace
{
    struct GroupName
    {
        const char* alias;
        Improvement group;
    };

    // The names are case-insensitive and hyphen/underscore-tolerant, like
    // OpenPreCEPatch's parser. `all` expands to the folded set, never to the
    // entry-point groups (there are none yet), so `all,<group>` stays readable.
    constexpr GroupName kGroupNames[] = {
        { "failsafe", Improvement::failsafe },
        { "failsafes", Improvement::failsafe },
        { "guards", Improvement::failsafe },
        { "null", Improvement::failsafe },
        { "all", Improvement::all },
    };

    bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (size_t index = 0; index < left.size(); ++index)
        {
            if (std::tolower(static_cast<unsigned char>(left[index])) !=
                std::tolower(static_cast<unsigned char>(right[index])))
                return false;
        }
        return true;
    }

    std::string_view Trim(std::string_view text) noexcept
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return text;
    }

    uint32_t NamedGroup(std::string_view name) noexcept
    {
        for (const auto& entry : kGroupNames)
        {
            if (EqualsIgnoreCase(name, entry.alias))
                return Improve(entry.group);
        }
        return 0;
    }

    uint32_t ParseList(std::string_view text) noexcept
    {
        uint32_t groups = 0;
        while (!text.empty())
        {
            const size_t separator = text.find_first_of(",; ");
            const std::string_view token =
                separator == std::string_view::npos ? text : text.substr(0, separator);
            groups |= NamedGroup(Trim(token));
            if (separator == std::string_view::npos)
                break;
            text.remove_prefix(separator + 1);
        }
        return groups;
    }
}

uint32_t ParseImprovements(const char* value) noexcept
{
    constexpr uint32_t folded = Improve(Improvement::all);
    if (value == nullptr)
        return 0;

    const std::string_view text = Trim(value);
    if (text.empty())
        return 0;
    if (EqualsIgnoreCase(text, "all") || EqualsIgnoreCase(text, "on") || text == "1")
        return folded;
    if (EqualsIgnoreCase(text, "none") || EqualsIgnoreCase(text, "off") || text == "0")
        return 0;

    constexpr std::string_view onlyPrefix = "only:";
    constexpr std::string_view exceptPrefix = "except:";
    if (text.size() >= onlyPrefix.size() &&
        EqualsIgnoreCase(text.substr(0, onlyPrefix.size()), onlyPrefix))
        return ParseList(text.substr(onlyPrefix.size()));
    if (text.size() >= exceptPrefix.size() &&
        EqualsIgnoreCase(text.substr(0, exceptPrefix.size()), exceptPrefix))
        return folded & ~ParseList(text.substr(exceptPrefix.size()));

    // A bare list is an "only" list: the targeted live A/B form.
    return ParseList(text);
}

uint32_t EnabledImprovements() noexcept
{
#if defined(OPCEP_PARITY_BASELINE)
    return 0;
#else
    constexpr uint32_t unresolved = 0xFFFFFFFFu;
    uint32_t current = ImprovementsDetail::resolved.load(std::memory_order_acquire);
    if (current != unresolved)
        return current;

    char value[256] = {};
    const DWORD length = GetEnvironmentVariableA(
        "OPCEP_IMPROVEMENTS", value, static_cast<DWORD>(sizeof(value)));

    // Unset (or too long to resolve) is the 1:1 path. OpenPreCEPatch's own
    // default is `all`; this tree inverts that one default because its governing
    // rule is that the original path is what runs unless an overlay is named.
    const uint32_t groups =
        length == 0 || length >= sizeof(value) ? 0u : ParseImprovements(value);
    ImprovementsDetail::resolved.store(groups, std::memory_order_release);
    return groups;
#endif
}

bool ImprovementEnabled(Improvement group) noexcept
{
    return (EnabledImprovements() & Improve(group)) != 0;
}

void SetImprovementsForTest(uint32_t groups) noexcept
{
    ImprovementsDetail::resolved.store(groups, std::memory_order_release);
}

void ResetImprovementsForTest() noexcept
{
    ImprovementsDetail::resolved.store(0xFFFFFFFFu, std::memory_order_release);
}

const char* ImprovementName(Improvement group) noexcept
{
    switch (group)
    {
    case Improvement::failsafe: return "failsafe";
    case Improvement::none: return "none";
    }
    return "unknown";
}

void SetMissingForTest(const char* what) noexcept
{
    if (what == nullptr || ImprovementsDetail::forcedMissingCount >= ImprovementsDetail::kForcedMissingSlots)
        return;
    ImprovementsDetail::forcedMissing[ImprovementsDetail::forcedMissingCount++] = what;
}

void ClearMissingForTest() noexcept
{
    for (size_t index = 0; index < ImprovementsDetail::forcedMissingCount; ++index)
        ImprovementsDetail::forcedMissing[index] = nullptr;
    ImprovementsDetail::forcedMissingCount = 0;
}

bool ForcedMissingForTest(const char* what) noexcept
{
    if (what == nullptr)
        return false;
    for (size_t index = 0; index < ImprovementsDetail::forcedMissingCount; ++index)
    {
        if (ImprovementsDetail::forcedMissing[index] != nullptr &&
            std::strcmp(ImprovementsDetail::forcedMissing[index], what) == 0)
            return true;
    }
    return false;
}
