// SPDX-License-Identifier: GPL-3.0-or-later

#include "ini_file.h"

#include <IniReader.h>

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace opp::ini
{
    namespace
    {
        // A value no INI line can contain; CIniReader cannot otherwise tell an
        // absent key from one whose value equals the caller's default.
        constexpr std::string_view kMissing = "\x01opp-ini-missing";
    }

    std::vector<Entry> ReadDocument(const std::filesystem::path& path)
    {
        std::vector<Entry> entries;

        const mINI::INIFile file(path);
        mINI::INIStructure document;
        if (!file.read(document))
            return entries;

        for (const auto& section : document)
        {
            const std::string& sectionName = std::get<0>(section);
            std::unordered_map<std::string, std::size_t> seen;
            for (const auto& item : std::get<1>(section))
            {
                const std::string& key = std::get<0>(item);
                const std::string& value = std::get<1>(item);
                const auto found = seen.find(key);
                if (found != seen.end())
                {
                    entries[found->second].value = value;
                    continue;
                }
                seen.emplace(key, entries.size());
                entries.push_back({sectionName, key, value});
            }
        }
        return entries;
    }

    struct Reader::Impl
    {
        explicit Impl(const std::filesystem::path& path) : reader(path) {}
        CIniReader reader;
    };

    Reader::Reader(const std::filesystem::path& path) : m_impl(std::make_unique<Impl>(path)) {}

    Reader::~Reader() = default;
    Reader::Reader(Reader&&) noexcept = default;
    Reader& Reader::operator=(Reader&&) noexcept = default;

    bool Reader::Has(std::string_view section, std::string_view key, std::string& value) const
    {
        value = m_impl->reader.ReadString(section, key, kMissing);
        return value != kMissing;
    }

    std::string Reader::Read(std::string_view section, std::string_view key,
                             std::string_view fallback) const
    {
        return m_impl->reader.ReadString(section, key, fallback);
    }
} // namespace opp::ini
