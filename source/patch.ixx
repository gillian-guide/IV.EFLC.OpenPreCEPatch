module;

#include <common.hxx>
#include <cstring>

export module patch;

import common;
import game;

// Signature scanning, byte/introspection helpers and the two patch primitives
// that need more than a direct injector call. Everything else (MakeNOP,
// MakeCALL, MakeJMP, WriteMemory) is called directly by the module that owns the
// site.

// A recovered signature is written the way IDA prints it: two characters per
// byte, `??` for a wildcard, sometimes a three-digit token. Hooking.Patterns'
// TransformPattern pushes one wildcard byte per `?` character, so the library
// would read IDA's two-character `??` as *two* wildcard bytes; collapsing the
// token to one `?` is the one thing this boundary does that the library cannot.
// The normalisation happens here, once -- the feature modules keep the
// recovered text verbatim.
export std::string NormaliseSignature(std::string_view recovered)
{
    std::string result;
    size_t offset = 0;
    while (offset < recovered.size())
    {
        while (offset < recovered.size() && std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;

        const auto start = offset;
        while (offset < recovered.size() && !std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;
        if (start == offset)
            break;

        const auto token = recovered.substr(start, offset - start);
        if (!result.empty())
            result.push_back(' ');

        if (token.find_first_not_of('?') == std::string_view::npos)
        {
            result.push_back('?');
            continue;
        }
        for (const auto character : token)
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return result;
}

// Tries each candidate signature in order and returns the first one that matches
// at least once. A one-string call is a single signature, a two-string call is a
// build variant, a three-string call is three builds. `count` is a scan hint, not
// a minimum: the scan stops after that many hits and returns what it found.
//
// The scan covers the game image's executable range (game.ixx), which is what
// the Hooking.Patterns scanner covers for a module -- and it keeps working when
// the tools have loaded a copy of that image instead of the image itself.
//
// Each call constructs a fresh scanner over the image's executable range
// (~9.9 MB on 1.0.8.0) and scans until the count hint is reached; the scanner
// has no cross-call memo. The publish and install tiers therefore scan a shared
// signature twice. That is a measured startup cost, not a free cache: a
// cross-tier memo would be unsound because an earlier install can overwrite a
// later arm's site.
export template <size_t count = 1, typename... Args>
hook::pattern find_pattern(Args... args)
{
    const auto [begin, end] = ImageScanRange();
    hook::pattern pattern;
    ((pattern = hook::pattern(begin, end, NormaliseSignature(args)), !pattern.count_hint(count).empty()) || ...);
    return pattern;
}

// OpenPreCEPatch's P13 rule: a multi-site arm whose original read or patched a
// fixed set of sites refuses a partial install. A different count is a view that
// does not have the shape the arm was recovered from, and installing the subset
// that happens to match is worse than leaving the image alone.
//
// The multi-hit guard: an arm that expects a fixed number of sites refuses a
// partial install. A build whose signature matches fewer sites does not have the
// shape the arm was recovered from, and installing the subset that happens to
// match is worse than leaving the image alone.
export bool ExpectMatchCount(size_t found, size_t expected, const char* what) noexcept
{
    if (found == expected)
        return true;

    Log::Print("arm {}: {} match(es), expected {}: skipped", what, found, expected);
    return false;
}

export bool ExpectMatchCount(hook::pattern& pattern, size_t expected, const char* what) noexcept
{
    return ExpectMatchCount(pattern.size(), expected, what);
}

// A deliberately small match cache for signatures shared by several arms
// (the IDE slot and the three steer sites). It is opt-in per signature, so a
// generic cross-tier cache is never used; a cached address is returned only
// after a one-match probe at that address still succeeds. The dump resets it
// between per-option cells.
namespace CachedMatchDetail
{
    struct Entry
    {
        std::string signature;
        uintptr_t match;
    };
    inline std::vector<Entry> entries;
}

export void ResetCachedMatchesForTest() noexcept
{
    CachedMatchDetail::entries.clear();
}

export std::byte* CachedMatchFor(std::string_view signature)
{
    const std::string normalised = NormaliseSignature(signature);
    const auto [begin, end] = ImageScanRange();

    for (const auto& entry : CachedMatchDetail::entries)
    {
        if (entry.signature != normalised)
            continue;
        const uintptr_t probeEnd = std::min<uintptr_t>(entry.match + 0x200, end);
        hook::pattern probe(entry.match, probeEnd, normalised);
        if (!probe.count_hint(1).empty())
            return reinterpret_cast<std::byte*>(entry.match);
        break;
    }

    hook::pattern pattern(begin, end, normalised);
    if (pattern.count_hint(1).empty())
        return nullptr;
    const auto* match = pattern.get_first<std::byte>(0);
    for (auto& entry : CachedMatchDetail::entries)
    {
        if (entry.signature == normalised)
        {
            entry.match = reinterpret_cast<uintptr_t>(match);
            return const_cast<std::byte*>(match);
        }
    }
    CachedMatchDetail::entries.push_back({ normalised, reinterpret_cast<uintptr_t>(match) });
    return const_cast<std::byte*>(match);
}


// Install-time failsafes. A transcribed body is never rewritten: a missing
// pointer, a site outside the image or an unresolved body refuses the install,
// which is the un-patched game path.
namespace FailsafeDetail
{
    constexpr size_t kForcedMissingSlots = 16;
    inline const char* forcedMissing[kForcedMissingSlots] = {};
    inline size_t forcedMissingCount = 0;
}

export void SetMissingForTest(const char* what) noexcept
{
    if (what == nullptr || FailsafeDetail::forcedMissingCount >= FailsafeDetail::kForcedMissingSlots)
        return;
    FailsafeDetail::forcedMissing[FailsafeDetail::forcedMissingCount++] = what;
}

export void ClearMissingForTest() noexcept
{
    FailsafeDetail::forcedMissingCount = 0;
}

export bool ForcedMissingForTest(const char* what) noexcept
{
    for (size_t index = 0; index < FailsafeDetail::forcedMissingCount; ++index)
    {
        const char* const forced = FailsafeDetail::forcedMissing[index];
        if (forced != nullptr && what != nullptr && std::strcmp(forced, what) == 0)
            return true;
    }
    return false;
}

export namespace Failsafe
{
    // Is the value a body would dereference missing? A true answer is a logged
    // skip; a false answer keeps the instruction stream.
    inline bool Missing(const char* what, bool missing) noexcept
    {
        if (!missing && !ForcedMissingForTest(what))
            return false;

        Log::Print("failsafe: {} is missing, continuing without the fix", what);
        return true;
    }

    // An arm's write site: the address and the bytes it covers must lie inside
    // the game image.
    inline bool Site(const char* what, uintptr_t address, size_t bytes) noexcept
    {
        const auto& game = Game::Get();
        const auto begin = game.ImageBase();
        const auto end = begin + game.ImageSize();
        const bool inside = bytes != 0 && address >= begin && address < end &&
                            bytes <= end - address;
        if (inside)
            return true;

        Log::Print("failsafe: {} site {:#010x}+{:#x} is outside the image; skipped",
            what, address, bytes);
        return false;
    }

    // An arm's destination body: a body that did not resolve is a jump to zero.
    inline bool Body(const char* what, uintptr_t body) noexcept
    {
        if (body != 0)
            return true;

        Log::Print("failsafe: {} body did not resolve; skipped", what);
        return false;
    }
}


