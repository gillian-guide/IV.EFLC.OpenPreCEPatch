// SPDX-License-Identifier: GPL-3.0-or-later
//
// The vendored IniReader is header-only and all-inline upstream, so a module
// interface unit must not include it. This plain translation unit owns the reader
// and exposes ordinary declarations: ReadDocument returns mINI's parsed document
// in file order for the one-time config copy, and Reader keeps CIniReader's
// ReadString (with an explicit absent result) for the legacy import.
//
// Adjusts to the vendored API; it does not patch it.

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace opp::ini
{
    struct Entry
    {
        std::string section;
        std::string key;
        std::string value;
    };

    // Sections and keys keep their first-appearance order; a duplicate key keeps the
    // last value, which is CIniReader's last-duplicate-wins rule.
    std::vector<Entry> ReadDocument(const std::filesystem::path& path);

    class Reader
    {
    public:
        explicit Reader(const std::filesystem::path& path);
        ~Reader();

        Reader(Reader&&) noexcept;
        Reader& operator=(Reader&&) noexcept;
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;

        [[nodiscard]] bool Has(std::string_view section, std::string_view key,
                               std::string& value) const;
        [[nodiscard]] std::string Read(std::string_view section, std::string_view key,
                                       std::string_view fallback) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace opp::ini
