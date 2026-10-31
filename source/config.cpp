// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parse-once INI document. See config.hxx for the grammar and the option
// semantics; this file wraps the bundled IniReader, owns the flat index and the
// typed value reader, and also holds the FusionFix overlap gate and the
// first-run/legacy INI import.

#include "config.hxx"

#include "ini_template.hxx"

import opp.log;

#include <IniReader.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace opp
{
    namespace
    {
        // The grouped layout, in search order. Kept in one place so the reader,
        // the lookup and the test suite agree; the first matching section wins.
        constexpr std::string_view kOptionSections[] = {
            "Options",     "STABILITY", "FIXES",      "PERFORMANCE", "GRAPHICS",
            "VEHICLES",    "INPUT",     "MULTIPLAYER", "EPISODIC",    "LOADING",
            "COMPATIBILITY", "MISC",    "XLIVELESS",  "INTERFACE",   "DEBUG",
        };

        constexpr std::string_view kWhitespace{" \t\n\r\f\v"};

        // mINI's trim: leading/trailing space, tab, newline, CR, form feed, VT.
        std::string_view Trim(std::string_view text) noexcept
        {
            const size_t first = text.find_first_not_of(kWhitespace);
            if (first == std::string_view::npos)
                return {};
            const size_t last = text.find_last_not_of(kWhitespace);
            return text.substr(first, last - first + 1);
        }

        // mINI removes one quote pair at ReadString time. A value that is a
        // single quote makes mINI's own `at(size()-1)` throw and fall back to
        // the caller's default, i.e. the key behaves as absent; that case is
        // reported as absent so Has()/Option() keep the old answer.
        std::string_view StripQuotes(std::string_view value, bool& absent) noexcept
        {
            absent = false;
            if (value.empty())
                return value;
            if (value.front() == '"' || value.front() == '\'')
            {
                value.remove_prefix(1);
                if (value.empty())
                {
                    absent = true;
                    return {};
                }
            }
            if (value.back() == '"' || value.back() == '\'')
                value.remove_suffix(1);
            return value;
        }

        uint32_t HashKey(std::string_view section, std::string_view key) noexcept
        {
            // FNV-1a, then a length mix so "ab"+"c" and "a"+"bc" differ.
            uint32_t hash = 2166136261u;
            for (const char c : section)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            hash ^= 0u;
            hash *= 16777619u;
            for (const char c : key)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            hash ^= static_cast<uint32_t>(key.size());
            hash *= 16777619u;
            return hash;
        }

        bool SameKey(std::string_view leftSection, std::string_view leftKey,
                     std::string_view rightSection, std::string_view rightKey) noexcept
        {
            return leftSection == rightSection && leftKey == rightKey;
        }
    } // namespace

    IntResult ParseIntPrefix(std::string_view text) noexcept
    {
        size_t i = 0;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
                                   text[i] == '\r' || text[i] == '\f' || text[i] == '\v'))
            ++i;
        if (i == text.size())
            return {{}, IntStatus::empty};

        bool negative = false;
        if (text[i] == '+' || text[i] == '-')
        {
            negative = text[i] == '-';
            ++i;
        }

        if (i == text.size() || text[i] < '0' || text[i] > '9')
            return {{}, IntStatus::not_a_number};

        // Accumulate in 64 bits, then clamp: the only consumer compares to 1, so
        // a saturated value is outside the range intentionally rather than an
        // out_of_range exception (std::stoi threw here).
        int64_t value = 0;
        for (; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c < '0' || c > '9')
                break;
            value = value * 10 + (c - '0');
            if (value > std::numeric_limits<int32_t>::max())
                value = std::numeric_limits<int32_t>::max();
        }
        if (negative)
            value = -value;
        return {static_cast<int32_t>(value), IntStatus::ok};
    }

    bool FlagExactlyOne(std::string_view text) noexcept
    {
        const IntResult result = ParseIntPrefix(text);
        return result.status == IntStatus::ok && result.value == 1;
    }

    Config::Config(const std::filesystem::path& path)
        : path_(std::filesystem::absolute(path)),
          loaded_(std::filesystem::exists(std::filesystem::absolute(path)))
    {
        if (!loaded_)
            return;
        Load();
        BuildIndex();
    }

    void Config::Load()
    {
        // The bundled reader parses the whole file here, once. Its document is
        // copied into the arena/index below; the reader is then discarded.
        CIniReader reader(path_);

        struct Pending
        {
            uint32_t sectionOffset;
            uint32_t sectionLength;
            uint32_t keyOffset;
            uint32_t keyLength;
            uint32_t valueOffset;
            uint32_t valueLength;
        };
        std::vector<Pending> pending;

        arena_.reserve(1024);
        const auto append = [this](std::string_view text) {
            const uint32_t offset = static_cast<uint32_t>(arena_.size());
            arena_.insert(arena_.end(), text.begin(), text.end());
            arena_.push_back('\0');
            return std::pair{offset, static_cast<uint32_t>(text.size())};
        };

        // mINI's document is iterated in file order; its parser has already
        // trimmed the names and collapsed duplicate keys last-wins.
        for (const std::string& sectionName : reader.GetSections())
        {
            for (const std::string& keyName : reader.GetKeys(sectionName))
            {
                const std::string raw = reader.GetRawValue(sectionName, keyName);
                bool absent = false;
                const std::string_view value = StripQuotes(raw, absent);
                if (absent)
                    continue;  // a lone quote: mINI falls back to the default

                const auto [sectionOffset, sectionLength] = append(sectionName);
                const auto [keyOffset, keyLength] = append(keyName);
                const auto [valueOffset, valueLength] = append(value);
                pending.push_back({sectionOffset, sectionLength, keyOffset, keyLength,
                                   valueOffset, valueLength});
            }
        }

        entries_.reserve(pending.size());
        for (const Pending& item : pending)
        {
            entries_.push_back(
                {std::string_view(arena_.data() + item.sectionOffset, item.sectionLength),
                 std::string_view(arena_.data() + item.keyOffset, item.keyLength),
                 std::string_view(arena_.data() + item.valueOffset, item.valueLength)});
        }
    }

    void Config::BuildIndex()
    {
        size_t capacity = 64;
        while (capacity < entries_.size() * 2)
            capacity <<= 1;
        index_.assign(capacity, 0);
        const uint32_t mask = static_cast<uint32_t>(capacity - 1);

        for (uint32_t i = 0; i < static_cast<uint32_t>(entries_.size()); ++i)
        {
            const Entry& entry = entries_[i];
            uint32_t slot = HashKey(entry.section, entry.key) & mask;
            while (index_[slot] != 0)
            {
                const Entry& existing = entries_[index_[slot] - 1];
                if (SameKey(existing.section, existing.key, entry.section, entry.key))
                    break;
                slot = (slot + 1) & mask;
            }
            // Overwrite on collision with the same key: the last duplicate wins,
            // exactly like the mINI assignment the reader does.
            index_[slot] = i + 1;
        }
    }

    std::span<const std::string_view> OptionSections() noexcept
    {
        return kOptionSections;
    }

    const char* Config::Find(std::string_view section, std::string_view key) const noexcept
    {
        if (index_.empty())
            return nullptr;
        const uint32_t mask = static_cast<uint32_t>(index_.size() - 1);
        uint32_t slot = HashKey(section, key) & mask;
        while (index_[slot] != 0)
        {
            const Entry& entry = entries_[index_[slot] - 1];
            if (SameKey(entry.section, entry.key, section, key))
                return entry.value.data();
            slot = (slot + 1) & mask;
        }
        return nullptr;
    }

    const char* Config::FindAny(std::string_view key) const noexcept
    {
        for (const std::string_view section : kOptionSections)
        {
            if (const char* value = Find(section, key))
                return value;
        }
        return nullptr;
    }

    bool Config::Has(std::string_view section, std::string_view key) const noexcept
    {
        return Find(section, key) != nullptr;
    }

    const char* Config::Value(std::string_view section, std::string_view key) const noexcept
    {
        if (section.empty())
            return FindAny(key);
        return Find(section, key);
    }

    bool Config::Option(const char* key, const char* defaultValue) const noexcept
    {
        return Resolve(key, defaultValue, /*gated=*/true);
    }

    bool Config::RawOption(const char* key, const char* defaultValue) const noexcept
    {
        return Resolve(key, defaultValue, /*gated=*/false);
    }

    bool Config::Resolve(const char* key, const char* defaultValue, bool gated) const noexcept
    {
        if (gated && IsFusionFixGated(key))
            return false;

        const char* value = FindAny(key);
        if (value == nullptr)
            return FlagExactlyOne(defaultValue);

        const IntResult result = ParseIntPrefix(value);
        if (result.status == IntStatus::ok)
            return result.value == 1;

        // A present but unparseable value (`Key=`, `Key=abc`) used to throw
        // std::invalid_argument out of DllMain. Fall back to the same default an
        // absent key uses and record it for the one-line config summary.
        ++invalidValues_;
        Log("config: invalid value for %s: \"%s\", using default %s", key, value,
            defaultValue);
        return FlagExactlyOne(defaultValue);
    }

    std::string Config::String(std::string_view section, std::string_view key,
                               std::string_view defaultValue) const
    {
        const char* value = Value(section, key);
        return value != nullptr ? std::string(value) : std::string(defaultValue);
    }

    // -----------------------------------------------------------------------
    // FusionFix overlap gate
    // -----------------------------------------------------------------------

    namespace
    {
        // Each row is a subsystem FusionFix already replaces. The reason names the
        // FusionFix module/setting a maintainer can check when revisiting the row.
        constexpr FusionFixConflict kConflicts[] = {
            {"BikeFeetFix", "FusionFix fixes.ixx (feet on bikes)"},
            {"BikePhoneAnimsFix", "FusionFix fixes.ixx (phone/bike animation)"},
            {"BuildingAlphaFix", "FusionFix fixes.ixx (LOD fade-in)"},
            {"BuildingDynamicShadows", "FusionFix shadows.ixx"},
            {"CarDynamicShadowFix", "FusionFix shadows.ixx / nightshadows.ixx"},
            {"CarPartsShadowFix", "FusionFix shadows.ixx"},
            {"CutsceneFixes", "FusionFix cutscenecam.ixx / frameratevigilante.ixx"},
            {"EmissiveLerpFix", "FusionFix fixes.ixx / shaders.ixx (emissive state)"},
            {"EpisodicVehicleSupport", "FusionFix episodiccontent.ixx (EpisodicVehicles)"},
            {"EpisodicWeaponSupport", "FusionFix episodiccontent.ixx (EpisodicWeapons)"},
            {"ForceCarHeadlightShadows", "FusionFix nightshadows.ixx"},
            {"ForceShadowsOnObjects", "FusionFix shadows.ixx"},
            {"HighFPSBikePhysicsFix", "FusionFix frameratevigilante.ixx (vehicle physics)"},
            {"HighFPSSpeedupFix", "FusionFix frameratevigilante.ixx (time scale)"},
            {"IncreasePtrNodes", "FusionFix limits.ixx (PtrNode Double pool)"},
            {"IncreaseVehicleModelLimit", "FusionFix limits.ixx"},
            {"IncreaseVehicleStructLimit", "FusionFix limits.ixx (VehicleStruct pool)"},
            {"MouseFix", "FusionFix rawinput.ixx / sniper.ixx"},
            {"NoImpostorHeightCutoff", "FusionFix lodlights.ixx / fixes.ixx"},
            {"NoLODLightHeightCutoff", "FusionFix lodlights.ixx"},
            {"NoLiveryLimit", "FusionFix limits.ixx (LiveriesLimit)"},
            {"NoLoadingSleep", "FusionFix loadingdelays.ixx patches the same push 0x1388 immediate to 0"},
            {"OutOfCommissionFix", "FusionFix frameratevigilante.ixx / fixes.ixx (high-FPS QTE)"},
            {"PipeBombDropIconSupport", "FusionFix episodiccontent.ixx"},
            {"PoliceEpisodicWeaponSupport", "FusionFix episodiccontent.ixx (TBoGTPoliceWeapons)"},
            {"ReversingLightFix", "FusionFix fixes.ixx (reversing light bone)"},
            {"RestoreDeathMusic", "FusionFix deathmusic.ixx (DeathMusic)"},
            {"SCOSignatureFix", "FusionFix episodiccontent.ixx (RemoveSCOSignatureCheck)"},
            {"SpeedupBugFix", "FusionFix frameratevigilante.ixx (time scale)"},
            {"TBoGTHeliHeightLimit", "FusionFix episodiccontent.ixx NOPs the same height check"},
            {"VSyncFix", "FusionFix framelimit.ixx / windowed.ixx"},
        };

        bool bFusionFixPresent = false;

        bool EqualsIgnoreCase(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
                return false;
            for (size_t i = 0; i < left.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(left[i])) !=
                    std::tolower(static_cast<unsigned char>(right[i])))
                    return false;
            }
            return true;
        }

        bool LooksLikeFusionFix(const std::filesystem::path& path)
        {
            std::string name = path.filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return name.find("fusionfix") != std::string::npos &&
                   path.extension() == ".asi";
        }

        const FusionFixConflict* FindConflict(const char* option) noexcept
        {
            if (option == nullptr)
                return nullptr;
            for (const FusionFixConflict& conflict : kConflicts)
            {
                if (EqualsIgnoreCase(conflict.option, option))
                    return &conflict;
            }
            return nullptr;
        }
    } // namespace

    const FusionFixConflict* FusionFixConflicts(size_t& count) noexcept
    {
        count = sizeof(kConflicts) / sizeof(kConflicts[0]);
        return kConflicts;
    }

    bool DetectFusionFix(const std::filesystem::path& moduleDirectory)
    {
        bool present = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi") != nullptr ||
                       GetModuleHandleW(L"GTAIV.FusionFix.asi") != nullptr;

        if (!present && !moduleDirectory.empty())
        {
            std::error_code error;
            for (const auto& entry :
                 std::filesystem::directory_iterator(moduleDirectory, error))
            {
                if (!entry.is_regular_file(error))
                    continue;
                if (LooksLikeFusionFix(entry.path()))
                {
                    present = true;
                    break;
                }
            }
        }

        SetFusionFixPresent(present);
        return present;
    }

    void SetFusionFixPresent(bool present) noexcept
    {
        bFusionFixPresent = present;
    }

    bool FusionFixPresent() noexcept
    {
        return bFusionFixPresent;
    }

    bool IsFusionFixGated(const char* option) noexcept
    {
        return bFusionFixPresent && FindConflict(option) != nullptr;
    }

    const char* FusionFixGateReason(const char* option) noexcept
    {
        const FusionFixConflict* conflict = FindConflict(option);
        return conflict ? conflict->reason : nullptr;
    }

    // -----------------------------------------------------------------------
    // First-run INI creation and legacy import
    // -----------------------------------------------------------------------

    namespace
    {
        constexpr std::string_view kMissing = "\x01opcep-missing";
        constexpr const char* kOwnName = "IV.EFLC.OpenPreCEPatch.ini";
        constexpr const char* kLegacyName = "ZolikaPatch.ini";

        constexpr const char* kSections[] = {
            "Options",      "STABILITY", "FIXES",     "PERFORMANCE", "GRAPHICS",
            "VEHICLES",     "INPUT",       "MULTIPLAYER", "EPISODIC", "LOADING",
            "COMPATIBILITY", "MISC",       "XLIVELESS",   "INTERFACE", "DEBUG",
        };

        constexpr const char* kXlivelessKeys[] = {
            "RemoveDataIntegrityChecks",
            "SkipTokenActivation",
            "SkipFirstTimePlayAsk",
            "SkipInternetConnectionCheck",
            "SkipHealthCheck",
        };

        // Original options this tree does not implement. Importing one silently
        // would make the mod look configured when it is not, so each is logged
        // and reported.
        constexpr std::string_view kUnsupportedLegacyKeys[] = {
            "AddOptionsInMenu",
            "FixPCIDKickbug",
            "ForceDynamicShadowsEverywhere",
            "HighQualityReflections",
            "NoPhysicsSleep",
            "RemoveBoundingBoxCulling",
            "RemoveUselessChecks",
        };

        // Options the XLivelessAddon companion implements; its own INI reads the
        // same legacy keys, so the import deliberately does not copy them.
        constexpr std::string_view kCompanionLegacyKeys[] = {
            "BorderlessWindowed",
            "DoNotPauseOnMinimize",
            "LocalSettingsPath",
            "SkipIntro",
            "SkipLauncher",
            "SkipMenu",
            "VRAMFix",
        };

        // Legacy keys the import maps onto another key instead of copying them:
        // the per-patch mod-check aliases (read by the RemoveModChecks arm) and
        // the old MiscFixes bundle (whose value seeds the xliveless keys).
        constexpr std::string_view kRemappedLegacyKeys[] = {
            "MiscFixes",
            "Patch4RemoveModChecks",
            "Patch6and7RemoveModChecks",
            "Patch7RemoveModChecks",
        };

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }

        // True for the two vehicle tables' generated rows, which the import
        // ports separately: `Car<1..512><Name|Angle|Offset>`.
        bool IsVehicleTableRow(std::string_view key) noexcept
        {
            if (key.size() < 7 || key.substr(0, 3) != "Car")
                return false;
            size_t digits = 3;
            while (digits < key.size() && key[digits] >= '0' && key[digits] <= '9')
                ++digits;
            if (digits == 3)
                return false;
            const std::string_view suffix = key.substr(digits);
            return suffix == "Name" || suffix == "Angle" || suffix == "Offset";
        }

        bool Contains(std::span<const std::string_view> list, std::string_view key) noexcept
        {
            for (const std::string_view candidate : list)
            {
                if (candidate == key)
                    return true;
            }
            return false;
        }

        std::vector<std::string> TemplateKeys()
        {
            std::vector<std::string> keys;
            std::istringstream input(kDefaultIni);
            std::string line;
            while (std::getline(input, line))
            {
                if (line.empty() || line[0] == '[' || line[0] == ';')
                    continue;
                const size_t equals = line.find('=');
                if (equals == std::string::npos)
                    continue;
                keys.push_back(std::string(Trim(std::string_view(line).substr(0, equals))));
            }
            std::sort(keys.begin(), keys.end());
            return keys;
        }

        // Every option key the legacy file declares, deduplicated and sorted.
        std::vector<std::string> LegacyKeys(std::string_view text)
        {
            std::vector<std::string> keys;
            size_t start = 0;
            while (start <= text.size())
            {
                const size_t newline = text.find('\n', start);
                const std::string_view line =
                    newline == std::string_view::npos ? text.substr(start)
                                                      : text.substr(start, newline - start);
                start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

                std::string_view trimmed = line;
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
                    trimmed.remove_suffix(1);
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
                    trimmed.remove_prefix(1);
                if (trimmed.empty() || trimmed.front() == '[' || trimmed.front() == ';' ||
                    trimmed.front() == '#')
                    continue;
                const size_t equals = trimmed.find('=');
                if (equals == std::string_view::npos)
                    continue;
                keys.push_back(std::string(Trim(trimmed.substr(0, equals))));
            }
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return keys;
        }

        bool Has(CIniReader& ini, const char* section, const char* key, std::string& value)
        {
            value = ini.ReadString(section, key, kMissing);
            return value != kMissing;
        }

        // The old file keeps every option in [Options]. The grouped file uses
        // subsystem sections, so a conversion must look in both places (and in
        // every section, for a partially converted file).
        std::string ReadLegacy(CIniReader& ini, const char* key, const char* section,
                               const char* defaultValue)
        {
            std::string value;
            if (Has(ini, "Options", key, value))
                return value;
            if (section && *section && Has(ini, section, key, value))
                return value;
            for (const char* candidate : kSections)
            {
                if (Has(ini, candidate, key, value))
                    return value;
            }
            return defaultValue;
        }

        bool WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
            return file.good();
        }

        std::string BuildFromLegacy(CIniReader& legacy)
        {
            std::string misc;
            const bool hasMisc = Has(legacy, "Options", "MiscFixes", misc) ||
                                 Has(legacy, "COMPATIBILITY", "MiscFixes", misc);

            std::ostringstream out;
            std::istringstream input(kDefaultIni);
            std::string line;
            std::string section;
            bool bSkipTable = false;

            while (std::getline(input, line))
            {
                if (!line.empty() && line.front() == '[')
                {
                    const size_t close = line.find(']');
                    if (close != std::string::npos)
                        section = line.substr(1, close - 1);
                    bSkipTable = section == "PopupLightAngles" || section == "HeadlightConeOffsets";
                    if (!bSkipTable)
                        out << line << '\n';
                    continue;
                }

                if (bSkipTable)
                    continue;

                const size_t equals = line.find('=');
                if (equals == std::string::npos || (!line.empty() && line.front() == ';'))
                {
                    out << line << '\n';
                    continue;
                }

                const std::string key = std::string(Trim(std::string_view(line).substr(0, equals)));
                const size_t comment = line.find("//", equals + 1);
                const std::string defaultValue = std::string(
                    Trim(std::string_view(line).substr(equals + 1,
                                                       comment == std::string::npos
                                                           ? std::string::npos
                                                           : comment - equals - 1)));

                std::string value;
                bool xliveless = false;
                for (const char* candidate : kXlivelessKeys)
                    xliveless = xliveless || key == candidate;

                if (xliveless && hasMisc)
                    value = misc;
                else
                    value = ReadLegacy(legacy, key.c_str(), section.c_str(), defaultValue.c_str());

                out << key << " = " << value;
                if (comment != std::string::npos)
                    out << ' ' << line.substr(comment);
                out << '\n';
            }

            auto appendTable = [&](const char* table, const char* suffix) {
                bool any = false;
                for (unsigned i = 1; i <= 512; ++i)
                {
                    const std::string prefix = "Car" + std::to_string(i);
                    const std::string name =
                        legacy.ReadString(table, prefix + "Name", "");
                    if (name.empty())
                        continue;

                    if (!any)
                    {
                        out << '\n' << '[' << table << "]\n";
                        any = true;
                    }

                    const std::string value = legacy.ReadString(table, prefix + suffix, "0");
                    out << prefix << "Name = " << name << '\n';
                    out << prefix << suffix << " = " << value << '\n';
                }
            };

            appendTable("PopupLightAngles", "Angle");
            appendTable("HeadlightConeOffsets", "Offset");

            // The undocumented ALIAS is not in the template; keep it readable by
            // the option lookup if the old file used it.
            std::string alias;
            if (Has(legacy, "Options", "Patch7RemoveModChecks", alias))
            {
                out << "\n[Options]\nPatch7RemoveModChecks = " << alias << '\n';
            }

            return out.str();
        }
    } // namespace

    std::span<const std::string_view> UnsupportedLegacyKeys() noexcept
    {
        return kUnsupportedLegacyKeys;
    }

    std::span<const std::string_view> CompanionLegacyKeys() noexcept
    {
        return kCompanionLegacyKeys;
    }

    std::filesystem::path EnsureConfigFile(const std::filesystem::path& moduleDirectory,
                                           LegacyImportReport* report)
    {
        if (moduleDirectory.empty())
            return {};

        const std::filesystem::path own = moduleDirectory / kOwnName;
        if (std::filesystem::exists(own))
            return own;

        const std::filesystem::path legacy = moduleDirectory / kLegacyName;
        if (std::filesystem::exists(legacy))
        {
            const std::string legacyText = ReadText(legacy);
            CIniReader reader(std::filesystem::absolute(legacy));
            const std::string text = BuildFromLegacy(reader);

            // Anything the template has no row for cannot be carried over. Name
            // it instead of letting the old setting silently disappear. The log
            // line is unconditional (the production path passes no report); the
            // report, when asked for, also collects the keys.
            const std::vector<std::string> templateKeys = TemplateKeys();
            for (const std::string& key : LegacyKeys(legacyText))
            {
                if (IsVehicleTableRow(key) ||
                    std::binary_search(templateKeys.begin(), templateKeys.end(), key) ||
                    Contains(kRemappedLegacyKeys, key))
                    continue;

                std::string reason;
                if (Contains(kUnsupportedLegacyKeys, key))
                    reason = "not implemented in OpenPreCEPatch";
                else if (Contains(kCompanionLegacyKeys, key))
                    reason = "provided by the XLivelessAddon companion";
                else
                    reason = "no row in the grouped INI";

                Log("ini: legacy key %s not imported (%s)", key.c_str(), reason.c_str());
                if (report != nullptr)
                    report->dropped.push_back({key, std::move(reason)});
            }

            if (WriteText(own, text))
            {
                Log("ini: ported %s -> %s", legacy.string().c_str(),
                    own.string().c_str());
                return own;
            }

            Log("ini: cannot write %s, reading the legacy file", own.string().c_str());
            return legacy;
        }

        if (WriteText(own, kDefaultIni))
        {
            Log("ini: created %s", own.string().c_str());
            return own;
        }

        return {};
    }
} // namespace opp
