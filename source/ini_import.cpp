// SPDX-License-Identifier: GPL-3.0-or-later

#include "ini_import.hxx"

#include "config.hxx"
#include "ini_file.hxx"
#include "ini_template.hxx"
#include "log.hxx"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace opp
{
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

        std::string Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return std::string(text);
        }

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
                keys.push_back(Trim(std::string_view(line).substr(0, equals)));
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
                keys.push_back(Trim(trimmed.substr(0, equals)));
            }
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return keys;
        }

        bool Has(LegacyIniFile& ini, const char* section, const char* key, std::string& value)
        {
            value = ini.ReadString(section, key, kMissing);
            return value != kMissing;
        }

        // The old file keeps every option in [Options]. The grouped file uses
        // subsystem sections, so a conversion must look in both places (and in
        // every section, for a partially converted file).
        std::string ReadLegacy(LegacyIniFile& ini, const char* key, const char* section,
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

        std::string BuildFromLegacy(LegacyIniFile& legacy)
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

                const std::string key = Trim(std::string_view(line).substr(0, equals));
                const size_t comment = line.find("//", equals + 1);
                const std::string defaultValue =
                    Trim(std::string_view(line).substr(equals + 1,
                                                       comment == std::string::npos
                                                           ? std::string::npos
                                                           : comment - equals - 1));

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
            LegacyIniFile reader(std::filesystem::absolute(legacy));
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
