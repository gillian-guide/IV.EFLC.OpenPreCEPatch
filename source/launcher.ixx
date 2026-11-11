module;

#include <common.hxx>

// PROCESSENTRY32 / Process32First: the original's sub_10003A10 walks the process
// list for the launcher before it writes anything.
#include <tlhelp32.h>

export module launcher;

import bytes;
import common;
import game;
import loader;
import patch;

// LaunchWithPlayGTAIV, the one arm this module owns: the launcher/SecuROM stages
// the companion carries are XLivelessAddon's, not this tree's.
//
// The recovered handler 0x100441B0 gates its whole build switch on sub_10003A10,
// a Toolhelp32 walk for a live `PlayGTAIV.exe`, and then writes the same 16 bytes
// at two fixed RVAs of the build's case. GTA IV 1.0.8.0's SecuROM initialisation
// (sub_48FC30 at 0x48FC30) lowercases its parent process's module path and
// searches it for the lowercased text of the literal at 0xE5A5E4; a game started
// by a launcher that is not named `LaunchGTAIV.exe` fails that search and leaves
// through `lmsgboxExit("SRLC10")` -- the "Game needs to be launched using
// LaunchGTAIV.exe" dialog, whose own exit path is the null write at 0x456D16.
// Rewriting the two literals to the launcher's name is what makes the search
// succeed, and it is the whole purpose of the key.
//
// Both slots live in .rdata (VA 0xE508F8 / 0xE5A5E4 on 1.0.8.0), outside the
// executable range `find_pattern` covers, so this arm scans the whole image. The
// scan is wildcard-free: on the five gated builds it resolves exactly the two
// recovered slots, on 1.0.0.4 it resolves two slots the original's switch has no
// case for, and on the EFLC builds one.
export namespace Launcher
{
    // The text both slots hold before the write: `LaunchGTAIV.exe\0`.
    inline constexpr std::string_view kSlotSignature =
        "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";

    // The payload: `PlayGTAIV.exe\0` plus the `e\0` of the old, longer name that
    // survives the shorter copy. The oracle builds it in two 8-byte `mov dword
    // ptr [..], imm32` pairs (game_refs/ZolikaPatch_original.asi.off, file
    // 0x43650); the capital `P` is the oracle's, and the game's search lowercases
    // both sides, so the case never decides the match.
    inline constexpr std::array<std::byte, 16> kName = {
        std::byte{ 'P' }, std::byte{ 'l' }, std::byte{ 'a' }, std::byte{ 'y' },
        std::byte{ 'G' }, std::byte{ 'T' }, std::byte{ 'A' }, std::byte{ 'I' },
        std::byte{ 'V' }, std::byte{ '.' }, std::byte{ 'e' }, std::byte{ 'x' },
        std::byte{ 'e' }, Bytes::Zero, std::byte{ 'e' }, Bytes::Zero,
    };

    // The recovered sites (game RVAs) of each case in the handler's switch, in
    // the switch's order. The catalogue mask for the key is these five builds.
    struct SlotPair
    {
        Build build;
        std::array<std::uint32_t, 2> sites;
    };

    inline constexpr SlotPair kSlots[] = {
        { Build::v1000, { 0x9F043Cu, 0x98FA0Cu } },
        { Build::v1040, { 0x9EA184u, 0x9DD8A0u } },
        { Build::v1060, { 0x9F8F40u, 0xA9D0D8u } },
        { Build::v1070, { 0x9F9F70u, 0xA9E118u } },
        { Build::v1080, { 0xA508F8u, 0xA5A5E4u } },
    };

    // The two RVAs of `build`, or an empty span for a build the switch does not
    // cover.
    [[nodiscard]] std::span<const std::uint32_t> SlotsFor(Build build) noexcept;

    // sub_10003A10: the process list holds a live `PlayGTAIV.exe`. The tree builds
    // UNICODE, so the record is the W form of the same entry and the name
    // comparison is `wcscmp`, as the original's ANSI `strcmp` compares the same
    // eight characters.
    [[nodiscard]] bool LauncherRunning() noexcept;

    // The offline harness has no launcher process to find, so it pins the walk's
    // answer: -1 asks the process list, 0 and 1 force the answer. Only the dump
    // and the native suite call this; the shipped ASI never does.
    void SetProbeOverride(std::int32_t forced) noexcept;
    [[nodiscard]] std::int32_t ProbeOverride() noexcept;

    // The write set of one image: both slots of `sites`, and only when the image
    // holds exactly that many occurrences of the old text, each at the recovered
    // offset. A shorter or longer view refuses the whole arm -- a subset install
    // would rewrite a slot the original never touches. Answers the number of
    // stores (0 or sites.size()).
    [[nodiscard]] std::size_t RewriteSlots(std::uint8_t* image, std::size_t size,
                                           std::span<const std::uint32_t> sites) noexcept;

    void Install();
}

namespace LauncherDetail
{
    std::atomic<std::int32_t> probeOverride{ -1 };
}

namespace Launcher
{
    std::span<const std::uint32_t> SlotsFor(Build build) noexcept
    {
        for (const SlotPair& pair : kSlots)
        {
            if (pair.build == build)
                return pair.sites;
        }
        return {};
    }

    bool LauncherRunning() noexcept
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32First(snapshot, &entry) != FALSE)
        {
            do
            {
                if (wcscmp(entry.szExeFile, L"PlayGTAIV.exe") == 0)
                {
                    found = true;
                    break;
                }
            } while (Process32Next(snapshot, &entry) != FALSE);
        }
        CloseHandle(snapshot);
        return found;
    }

    void SetProbeOverride(std::int32_t forced) noexcept
    {
        LauncherDetail::probeOverride.store(forced);
    }

    std::int32_t ProbeOverride() noexcept
    {
        return LauncherDetail::probeOverride.load();
    }

    std::size_t RewriteSlots(std::uint8_t* image, std::size_t size,
                             std::span<const std::uint32_t> sites) noexcept
    {
        if (image == nullptr || size == 0 || sites.empty())
            return 0;

        const std::string signature = NormaliseSignature(kSlotSignature);
        hook::pattern pattern(reinterpret_cast<std::uintptr_t>(image),
                              reinterpret_cast<std::uintptr_t>(image) + size, signature);
        if (!ExpectMatchCount(pattern.size(), sites.size(), "LaunchWithPlayGTAIV name slots"))
            return 0;

        // The scan reports its hits in RVA order; the recovered table keeps the
        // handler's case order (1.0.0.0 and 1.0.4.0 store the higher RVA first),
        // so the two are compared as sets: with the count already matching, every
        // hit being a recovered site means each site is hit exactly once.
        for (std::size_t index = 0; index < sites.size(); ++index)
        {
            const auto offset = static_cast<std::uint32_t>(
                pattern.get(index).get<std::uint8_t>() - image);
            bool recovered = false;
            for (const auto site : sites)
                recovered = recovered || offset == site;
            if (!recovered)
            {
                Log::Print("arm LaunchWithPlayGTAIV: a slot is at {:#x}, not one of the recovered "
                           "sites: skipped", offset);
                return 0;
            }
        }

        for (const std::uint32_t site : sites)
        {
            injector::WriteMemoryRaw(reinterpret_cast<std::uintptr_t>(image + site),
                                     const_cast<std::byte*>(kName.data()), kName.size(), true);
        }
        return sites.size();
    }

    void Install()
    {
        if (!Loader::Enabled("LaunchWithPlayGTAIV"))
            return;

        // The handler's own switch: the key's mask names the same five builds
        // (the regression suite pins the agreement). Asking the arm's table rather
        // than the generated mask keeps the write set tied to the recovered sites.
        const auto sites = SlotsFor(Loader::Current());
        if (sites.empty())
            return;

        const std::int32_t forced = ProbeOverride();
        if (forced >= 0 ? forced == 0 : !LauncherRunning())
            return;

        const auto& game = Game::Get();
        const std::size_t written = RewriteSlots(
            reinterpret_cast<std::uint8_t*>(game.ImageBase()), game.ImageSize(), sites);
        if (written != 0)
            Log::Print("launcher: {} PlayGTAIV name slot(s) patched", written);
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLauncher()
{
    Loader::InstallEvent() += [] { Launcher::Install(); };
}
