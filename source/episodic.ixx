module;

#include <common.hxx>

export module episodic;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
import patch;
import shared;

// EPISODIC: SCOSignatureFix, EpisodicVehicleSupport, EpisodicWeaponSupport,
// PoliceEpisodicWeaponSupport, TBoGTHeliHeightLimit and PipeBombDropIconSupport.
// The key family was spread over four handler modules (`loading`, `stability`,
// `vehicles`, `audio`); the episode gates all live here now. LoadDLCs belonged to
// this module when the key family moved here; it is the XLivelessAddon
// companion's key, so its arm and discovery pass are not part of this tree.
export namespace Episodic
{
    void InstallSCOSignatureFix();
    void InstallEpisodicVehicleSupport();
    void InstallEpisodicWeaponSupport();
    void InstallPoliceEpisodicWeaponSupport();
    void InstallTBoGTHeliHeightLimit();
    void InstallPipeBombDropIconSupport();
}

namespace
{
    // EpisodicWeaponSupport: the eight episode gates in front of the episodic
    // weapons. The key's build mask is the original's ten-build set (options.inc),
    // so the arm needs no gate of its own.
    struct EpisodeGateSite final
    {
        std::string_view signature;
        ptrdiff_t offset;
        size_t length;
    };

    constexpr EpisodeGateSite kEpisodicWeaponSites[] = {
        { "75 5B 83 3D ? ? ? ? 00 75", 0, 2 },
        { "83 3D ? ? ? ? 02 0F 85 8C 01 00 00", 7, 6 },
        { "83 3D ? ? ? ? 02 0F 8C D5 00", 0, 13 },
        { "83 3D ? ? ? ? 02 75 20 8B CD E8", 7, 2 },
        { "83 3D ? ? ? ? 02 75 50", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 32 8B 41", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", 7, 2 },
    };

    // One recovered sub-gate: is the current build's id a bit of `mask`?
    bool CurrentBuildInMask(uint32_t mask)
    {
        return (mask & (1u << static_cast<uint32_t>(Loader::Current()))) != 0;
    }

    // A captured pattern operand is a 32-bit VA of the image *as loaded*: the
    // loader has already applied the image's relocations, so the dword is a live
    // address and the cast is the whole conversion.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }

    // EpisodicVehicleSupport's own episode word: the game's unk_10158128, an int
    // holding 2, which every `cmp [episode], 2` below tests. The arm hands the game
    // this module's word instead, so the four 1.0.7.0/1.0.8.0 sites read a value
    // this module owns; the dump canonicalises the four published operands.
    uint32_t zpatch_episode_flag = 2;
}

namespace Episodic
{

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped. The `sco_anchor`
    // group instead scans a signature that ends on the `jz` itself; a build where
    // the old -32 distance does not hold would otherwise flip an unrelated `jz`.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        // `mov ds:X,edi` (6 bytes) + `test ebp,ebp` (2) put the `jz` opcode
        // at +8. The signature ends on the branch it flips, instead of scanning
        // a downstream anchor and relying on a fixed -32 distance.
        auto anchored = find_pattern("89 3D ? ? ? ? 85 ED 74 1E");
        if (anchored.empty())
            return;
        injector::WriteMemory<std::byte>(
            anchored.get_first<std::byte>(8), Bytes::ShortJmp, true);
    }

    void InstallEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("EpisodicWeaponSupport"))
            return;

        for (const auto& site : kEpisodicWeaponSites)
        {
            auto pattern = find_pattern(site.signature);
            if (!pattern.empty())
                injector::MakeNOP(pattern.get_first(site.offset), site.length, true);
        }
    }

    // PoliceEpisodicWeaponSupport: the police-vehicle episode gate; the two-byte
    // branch after the `cmp [episode],2` at the anchor is NOPed.
    void InstallPoliceEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("PoliceEpisodicWeaponSupport"))
            return;

        auto pattern = find_pattern("39 2D ? ? ? ? 75 3E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);
    }

    // TBoGTHeliHeightLimit: the helicopter ceiling's episode gate -- the branch
    // after the `cmp [episode],2` at the anchor is NOPed, so the ceiling applies
    // on every episode.
    void InstallTBoGTHeliHeightLimit()
    {
        if (!Loader::Enabled("TBoGTHeliHeightLimit"))
            return;

        auto pattern = find_pattern("83 3D ? ? ? ? 02 7C 0A F3 0F");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted, so
    // the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D 18 1C 75 14");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(-9), 9, true);
    }

    // EpisodicVehicleSupport: the episode test at each site is deleted, so every
    // vehicle check that is gated on "not episodic" runs on the episodic builds.
    //
    // Two parts, because the original's handler has two:
    //   * 1.0.7.0 and 1.0.8.0 each retarget the absolute operand of two
    //     `cmp dword ptr [episode], 2` sites (the first also a `jnz`, the second a
    //     `jne rel32`) at this module's episode word. The original's recovered table
    //     does that as four *RVA installs* -- a rebased pointer store at a fixed RVA
    //     (build 8: 0x21500 and 0x7D91AE; build 9: 0x95040 and 0x684ABE), not a
    //     pattern hit. The store value proves it: it is this module's own pointer,
    //     which a signature could not name. Spelling the game's absolute operand out
    //     in a signature would also bake in the preferred-base VA, which the loader
    //     rewrites on every run (the executable carries DYNAMIC_BASE), so such an
    //     arm could never match the live image and would never fire in the game;
    //   * every gated build then gets the 23 signature-driven NOP rows, in the
    //     original's row order. The first two rows write every hit of a multi-hit
    //     scan (two each on this image) and two rows carry a second window.
    void InstallEpisodicVehicleSupport()
    {
        if (!Loader::Enabled("EpisodicVehicleSupport"))
            return;

        const auto flag = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_episode_flag));
        struct FlagStore final
        {
            Build build;
            uint32_t rva;
        };
        static constexpr FlagStore kEpisodeFlagStores[] = {
            { Build::v1070, 0x021500u },
            { Build::v1070, 0x7D91AEu },
            { Build::v1080, 0x095040u },
            { Build::v1080, 0x684ABEu },
        };
        for (const auto& store : kEpisodeFlagStores)
        {
            if (Loader::Current() != store.build)
                continue;

            auto* const site =
                reinterpret_cast<std::byte*>(Game::Get().ImageBase() + store.rva);
            if (!Failsafe::Site("EpisodicVehicleSupport episode word",
                    reinterpret_cast<uintptr_t>(site), sizeof(uint32_t)))
                continue;
            injector::WriteMemory<uint32_t>(site, flag, true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
        }

        // Row 1: the `jl` of a loop that only runs on the episodic builds. Two
        // sites, both rewritten (the original writes hits[0] and hits[1]).
        auto pattern = find_pattern<2>("7C 18 0F BF 4E 2E");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport component window"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(-6), 8, true);
        }

        // Row 2: the same `cmp [episode], 2` shape twice in two functions.
        pattern = find_pattern<2>("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 08");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport episode test"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(0), 9, true);
        }

        // Rows 3-20: one window each.
        pattern = find_pattern("39 35 ?? ?? ?? ?? 75 0E 80 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);

        struct EpisodeWindow
        {
            const char* signature;
            int offset;
            uint8_t length;
            int secondOffset;
            uint8_t secondLength;
        };
        static constexpr EpisodeWindow kEpisodeWindows[] = {
            { "74 17 83 3D ?? ?? ?? ?? 02 75 0E F3 0F 10 05", 2, 9, 0, 0 },
            { "7C 22 83 F8 28 75 1D", 0, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 0F 8C 0E 05 00 00", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 08 3B 05", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 0C", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 1F 8B 44 24 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 21 8B 96 20 08 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 24 F6 87 6C 02 00 00 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 35 0F BF 46 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 38 8B 16", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 11 8B 4E", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 1F 8B", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 26 8B 85 40 0B 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 35 0F BF 43 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 43", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 4A 0F BF 46 2E", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 89 44 24 38", 11, 6, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 8A 0D", 0, 7, 17, 2 },
            { "83 3D ?? ?? ?? ?? 02 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 20 75 4C", 21, 2, 0, 0 },
            { "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18", -15, 7, 10, 2 },
        };
        for (const auto& row : kEpisodeWindows)
        {
            pattern = find_pattern(row.signature);
            if (pattern.empty())
                continue;
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match + row.offset, row.length, true);
            if (row.secondLength != 0)
                injector::MakeNOP(match + row.secondOffset, row.secondLength, true);
        }
    }

}
