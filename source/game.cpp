// SPDX-License-Identifier: GPL-3.0-or-later

#include "game.hxx"

#include <tlhelp32.h>
#include <winver.h>

#include <cwchar>
#include <new>

namespace opp
{
    namespace
    {
        constexpr uint32_t kHashMultiplier = 0x401u;
        constexpr uint32_t kHashFinalMultiplier = 0x8001u;
        constexpr unsigned kHashShiftA = 6;
        constexpr unsigned kHashShiftB = 11;
        constexpr uint32_t kHashBias = 2;
        constexpr uint32_t kCompatibilityFirstValue = 0xC7C47589u;
        constexpr uint32_t kCompatibilitySecondValue = 0x00002446u;
        constexpr uint32_t kVsFixedFileInfoSignature = 0xfeef04bdu;
        constexpr size_t kDwordLength = sizeof(uint32_t);

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        uint32_t ParseDecOrHex(std::string_view text) noexcept
        {
            size_t pos = 0;
            while (pos < text.size() && IsWhite(text[pos]))
                ++pos;

            bool negative = false;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
            {
                negative = text[pos] == '-';
                ++pos;
            }

            unsigned base = 10;
            if (pos < text.size() && text[pos] == '0')
            {
                base = 8;
                if (pos + 2 < text.size() &&
                    (text[pos + 1] == 'x' || text[pos + 1] == 'X') &&
                    ((text[pos + 2] >= '0' && text[pos + 2] <= '9') ||
                     (text[pos + 2] >= 'a' && text[pos + 2] <= 'f') ||
                     (text[pos + 2] >= 'A' && text[pos + 2] <= 'F')))
                {
                    pos += 2;
                    base = 16;
                }
            }

            uint32_t value = 0;
            bool any = false;
            bool overflow = false;
            for (; pos < text.size(); ++pos)
            {
                const unsigned char c = static_cast<unsigned char>(text[pos]);
                const unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                                       : c >= 'a' && c <= 'z' ? c - 'a' + 10
                                       : c >= 'A' && c <= 'Z' ? c - 'A' + 10
                                                              : 36;
                if (digit >= base)
                    break;

                any = true;
                if (value > (UINT32_MAX - digit) / base)
                    overflow = true;
                else if (!overflow)
                    value = value * base + digit;
            }

            if (!any)
                return 0;
            if (overflow)
                return UINT32_MAX;
            return negative ? 0u - value : value;
        }

        uint32_t HashText(std::string_view text) noexcept
        {
            const bool quoted = !text.empty() && text.front() == '"';
            if (quoted)
                text.remove_prefix(1);

            uint32_t value = 0;
            for (unsigned char c : text)
            {
                if (quoted && c == '"')
                    break;
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<unsigned char>(c + ('a' - 'A'));
                else if (c == '\\')
                    c = '/';

                value = (value + c) * kHashMultiplier;
                value = (value >> kHashShiftA) ^ value;
            }

            value *= 9u;
            value = (value >> kHashShiftB) ^ value;
            value *= kHashFinalMultiplier;
            return value > 1u ? value : value + kHashBias;
        }

        uint32_t ReadDword(const uint8_t* image, size_t offset) noexcept
        {
            uint32_t value = 0;
            for (size_t i = 0; i < kDwordLength; ++i)
                value |= static_cast<uint32_t>(image[offset + i]) << (8u * i);
            return value;
        }
    } // namespace

    uint32_t GameHash(std::string_view text) noexcept
    {
        if (text.size() >= 5 && (text[0] == 'h' || text[0] == 'H') &&
            (text[1] == 'a' || text[1] == 'A') && (text[2] == 's' || text[2] == 'S') &&
            (text[3] == 'h' || text[3] == 'H') && text[4] == ':')
        {
            // The shipped 32-bit strtoul call accepts whitespace, a sign and a
            // numeric prefix; only a zero result falls back to hashing the text.
            const uint32_t parsed = ParseDecOrHex(text.substr(5));
            if (parsed != 0)
                return parsed;
        }
        return HashText(text);
    }

    bool ComposeGameVersion(uint16_t major, uint16_t minor, uint16_t build,
                            uint16_t revision, uint32_t& output) noexcept
    {
        const uint16_t parts[] = {major, minor, build, revision};
        uint64_t value = 0;
        for (const uint16_t part : parts)
        {
            uint32_t scale = 10;
            for (uint16_t n = part; n >= 10; n = static_cast<uint16_t>(n / 10))
                scale *= 10;

            value = value * scale + part;
            if (value > UINT32_MAX)
                return false;
        }

        output = static_cast<uint32_t>(value);
        return true;
    }

    GameBuild ClassifyGameBuild(uint32_t version) noexcept
    {
        switch (version)
        {
        case 1000: return GameBuild::v1000;
        case 1004: return GameBuild::v1004;
        case 1040: return GameBuild::v1040;
        case 1060: return GameBuild::v1060;
        case 1061: return GameBuild::v1061;
        case 1070: return GameBuild::v1070;
        case 1080: return GameBuild::v1080;
        case 1100: return GameBuild::v1100;
        case 1110: return GameBuild::v1110;
        case 1120: return GameBuild::v1120;
        case 1130: return GameBuild::v1130;
        case 12043: return GameBuild::v12043;
        default: return GameBuild::unknown;
        }
    }

    bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds) noexcept
    {
        for (const GameBuild candidate : builds)
        {
            if (candidate == build)
                return true;
        }
        return false;
    }

    const char* GameBuildName(GameBuild build) noexcept
    {
        switch (build)
        {
        case GameBuild::v1000: return "1000";
        case GameBuild::v1004: return "1004";
        case GameBuild::v1040: return "1040";
        case GameBuild::v1060: return "1060";
        case GameBuild::v1061: return "1061";
        case GameBuild::v1070: return "1070";
        case GameBuild::v1080: return "1080";
        case GameBuild::v1100: return "1100";
        case GameBuild::v1110: return "1110";
        case GameBuild::v1120: return "1120";
        case GameBuild::v1130: return "1130";
        case GameBuild::v12043: return "12043";
        default: return "unknown";
        }
    }

    uint32_t SelectGameProcess(const ProcessEntry* entries, size_t count,
                                    std::string_view name) noexcept
    {
        if (!entries)
            return 0;

        for (size_t i = 0; i < count; ++i)
        {
            if (entries[i].executable.size() == name.size() &&
                std::memcmp(entries[i].executable.data(), name.data(), name.size()) == 0)
                return entries[i].processId;
        }
        return 0;
    }

    uint32_t FindGameProcess() noexcept
    {
        // UNICODE builds map the unsuffixed names to the W variants, matching the
        // original ANSI-name call site's case-sensitive full-name comparison.
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;

        uint32_t found = 0;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (std::wcscmp(entry.szExeFile, L"PlayGTAIV.exe") == 0)
                {
                    found = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    bool ModuleLacksSignature(const uint8_t* image, size_t imageSize) noexcept
    {
        // Machine code at 0x10003e2f / 0x10003e3b shows both comparisons are
        // 32-bit:
        //   cmp DWORD PTR [eax+0x20d3], 0xC7C47589
        //   cmp DWORD PTR [eax+0x20d7], 0x00002446
        constexpr size_t kFirst = 0x20d3;
        constexpr size_t kSecond = 0x20d7;

        if (!image || imageSize < kSecond + kDwordLength)
            return false;

        return ReadDword(image, kFirst) != kCompatibilityFirstValue ||
               ReadDword(image, kSecond) != kCompatibilitySecondValue;
    }

    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept
    {
        compactVersion = 0;
        build = GameBuild::unknown;

        char executable[MAX_PATH]{};
        const DWORD pathLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (pathLength == 0 || pathLength >= MAX_PATH)
            return false;

        DWORD ignored = 0;
        const DWORD infoSize = GetFileVersionInfoSizeA(executable, &ignored);
        if (infoSize == 0)
            return false;

        auto* info = new (std::nothrow) uint8_t[infoSize];
        if (!info)
            return false;

        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT fixedSize = 0;
        const BOOL queried =
            GetFileVersionInfoA(executable, ignored, infoSize, info) &&
            VerQueryValueA(info, "\\", reinterpret_cast<void**>(&fixed), &fixedSize);

        bool result = false;
        if (queried && fixed && fixedSize >= sizeof(VS_FIXEDFILEINFO) &&
            fixed->dwSignature == kVsFixedFileInfoSignature)
        {
            const auto major = static_cast<uint16_t>(fixed->dwProductVersionMS >> 16u);
            const auto minor = static_cast<uint16_t>(fixed->dwProductVersionMS);
            const auto patch = static_cast<uint16_t>(fixed->dwProductVersionLS >> 16u);
            const auto revision = static_cast<uint16_t>(fixed->dwProductVersionLS);

            result = ComposeGameVersion(major, minor, patch, revision, compactVersion);
            if (result)
                build = ClassifyGameBuild(compactVersion);
        }

        delete[] info;
        return result;
    }
} // namespace opp
