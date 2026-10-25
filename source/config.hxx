// SPDX-License-Identifier: GPL-3.0-or-later
//
// INI access. Values are read through ThirteenAG/IniReader (mINI), which gives
// the same last-value-wins and inline-comment behaviour the original Feather INI
// reader had. Option semantics stay the original's: a value must parse to
// exactly 1, and an absent key falls back to the default the installer asks for.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <IniReader.h>

namespace opp
{
    class Config final
    {
    public:
        explicit Config(const std::filesystem::path& path);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        [[nodiscard]] bool loaded() const noexcept { return loaded_; }
        [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

        // True when the key exists in the section, even when its value is empty.
        [[nodiscard]] bool Has(std::string_view section, std::string_view key) const;

        // The original's INI gate: a present value must parse to exactly 1, and
        // an absent key uses `defaultValue`. std::stoi's prefix acceptance and
        // its throw on a non-numeric value are preserved.
        [[nodiscard]] bool Option(const char* key, const char* defaultValue) const;

        // The same read without the FusionFix gate. Used only for the one-time
        // notice that lists options the user enabled but the gate suppresses.
        [[nodiscard]] bool RawOption(const char* key, const char* defaultValue) const;

        [[nodiscard]] std::string String(std::string_view section, std::string_view key,
                                         std::string_view defaultValue) const;

    private:
        static constexpr std::string_view kMissing = "\x01openprecepatch-missing";

        mutable CIniReader reader_;
        std::filesystem::path path_;
        bool loaded_{};
    };
} // namespace opp
