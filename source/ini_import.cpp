// SPDX-License-Identifier: GPL-3.0-or-later

#include "ini_import.hxx"

#include "config.hxx"
#include "ini_template.hxx"
#include "log.hxx"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace opp
{
    namespace
    {
        constexpr std::string_view kMissing = "\x01opcep-missing";
        constexpr const char* kOwnName = "IV.EFLC.OpenPreCEPatch.ini";
        constexpr const char* kLegacyName = "ZolikaPatch.ini";

        constexpr const char* kSections[] = {
            "Options",      "STABILITY", "PERFORMANCE", "GRAPHICS",
            "VEHICLES",     "INPUT",       "MULTIPLAYER", "LOADING",   "COMPATIBILITY",
            "XLIVELESS",    "INTERFACE",   "DEBUG",
        };

        constexpr const char* kXlivelessKeys[] = {
            "RemoveDataIntegrityChecks",
            "SkipTokenActivation",
            "SkipFirstTimePlayAsk",
            "SkipInternetConnectionCheck",
            "SkipHealthCheck",
        };

        std::string Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return std::string(text);
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

    std::filesystem::path EnsureConfigFile(const std::filesystem::path& moduleDirectory)
    {
        if (moduleDirectory.empty())
            return {};

        const std::filesystem::path own = moduleDirectory / kOwnName;
        if (std::filesystem::exists(own))
            return own;

        const std::filesystem::path legacy = moduleDirectory / kLegacyName;
        if (std::filesystem::exists(legacy))
        {
            CIniReader reader(std::filesystem::absolute(legacy));
            const std::string text = BuildFromLegacy(reader);
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
