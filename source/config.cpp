// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.hxx"

#include "fusionfix_compat.hxx"

namespace opp
{
    namespace
    {
        // The grouped layout. Keys are unique across sections, so Option can find
        // a key without a generated key->section table. [Options] stays first so
        // an old flat ZolikaPatch.ini keeps working unchanged.
        constexpr const char* kOptionSections[] = {
            "Options",      "STABILITY", "FIXES",     "PERFORMANCE", "GRAPHICS",
            "VEHICLES",     "INPUT",       "MULTIPLAYER", "EPISODIC", "LOADING",
            "COMPATIBILITY", "MISC",       "XLIVELESS",   "INTERFACE", "DEBUG",
        };
    } // namespace

    Config::Config(const std::filesystem::path& path)
        : reader_(std::filesystem::absolute(path)), path_(std::filesystem::absolute(path)),
          loaded_(std::filesystem::exists(path_))
    {
    }

    bool Config::Has(std::string_view section, std::string_view key) const
    {
        return reader_.ReadString(section, key, kMissing) != kMissing;
    }

    bool Config::Option(const char* key, const char* defaultValue) const
    {
        if (IsFusionFixGated(key))
            return false;
        return RawOption(key, defaultValue);
    }

    bool Config::RawOption(const char* key, const char* defaultValue) const
    {
        std::string value = defaultValue;
        for (const char* section : kOptionSections)
        {
            if (Has(section, key))
            {
                value = reader_.ReadString(section, key, defaultValue);
                break;
            }
        }
        return std::stoi(value, nullptr, 10) == 1;
    }

    std::string Config::String(std::string_view section, std::string_view key,
                               std::string_view defaultValue) const
    {
        if (section.empty())
        {
            for (const char* candidate : kOptionSections)
            {
                if (Has(candidate, key))
                    return reader_.ReadString(candidate, key, defaultValue);
            }
            return std::string(defaultValue);
        }
        return reader_.ReadString(section, key, defaultValue);
    }
} // namespace opp
