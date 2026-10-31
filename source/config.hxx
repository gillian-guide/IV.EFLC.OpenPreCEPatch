// SPDX-License-Identifier: GPL-3.0-or-later
//
// INI access. The file is parsed once at load into a stable, NUL-terminated
// arena plus a hash index, so every option read is an O(1) lookup with no
// allocation and no exception. The grammar is the vendored mINI reader's
// (ThirteenAG/IniReader), which is what this tree used before: section/key are
// case-sensitive, the last duplicate wins, a whole-line comment starts with
// `;`, `#` or `//`, an inline comment must be preceded by a space and is
// stripped only after the first `=`, values are trimmed of surrounding
// whitespace and of one matching quote pair, and a UTF-8 BOM is skipped.
//
// Option semantics stay the original's: a present value is enabled when it
// parses to exactly 1 (std::stoi's base-10 prefix grammar: "1abc" is 1), and an
// absent key falls back to the default the installer asks for. The original
// threw std::invalid_argument out of DllMain for `Key=` or `Key=abc`; that is
// the fault this tree replaces with a logged fallback to the same default
// (improvements.md, "install-time failsafes").

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opp
{
    // Parses strtol's base-10 grammar with no locale and no throw: leading
    // whitespace is skipped, an optional sign and decimal digits are consumed,
    // parsing stops at the first other character. "1abc" -> 1, "  +2" -> 2,
    // "0x2" -> 0 (base 10 stops at 'x'), "abc"/"" -> invalid/empty.
    enum class IntStatus : uint8_t
    {
        ok,
        empty,
        not_a_number,
    };

    struct IntResult
    {
        int32_t value{};
        IntStatus status{IntStatus::empty};
    };

    IntResult ParseIntPrefix(std::string_view text) noexcept;

    // The original's option gate (std::stoi(value) == 1, without the throw).
    bool FlagExactlyOne(std::string_view text) noexcept;

    // The grouped layout, in search order: a key is resolved in the first section
    // of this list that contains it. [Options] stays first so an old flat
    // ZolikaPatch.ini keeps working unchanged. Keys are expected to be unique
    // across the searched sections (checked by the test suite).
    [[nodiscard]] std::span<const std::string_view> OptionSections() noexcept;

    class Config final
    {
    public:
        explicit Config(const std::filesystem::path& path);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        [[nodiscard]] bool loaded() const noexcept { return loaded_; }
        [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

        // True when the key exists in the section, even when its value is empty.
        [[nodiscard]] bool Has(std::string_view section, std::string_view key) const noexcept;

        // The original's INI gate: a present value is enabled when it parses to
        // exactly 1; an absent key uses `defaultValue`. A present but
        // unparseable value (empty, "abc") is logged once and also uses the
        // default, so it can never escape the install pass as an exception.
        [[nodiscard]] bool Option(const char* key, const char* defaultValue) const noexcept;

        // The same read without the FusionFix gate. Used only for the one-time
        // notice that lists options the user enabled but the gate suppresses.
        [[nodiscard]] bool RawOption(const char* key, const char* defaultValue) const noexcept;

        [[nodiscard]] std::string String(std::string_view section, std::string_view key,
                                         std::string_view defaultValue) const;

        // The NUL-terminated value, or nullptr when the key is absent. The
        // pointer is stable for the Config's lifetime.
        [[nodiscard]] const char* Value(std::string_view section,
                                        std::string_view key) const noexcept;

        // Number of present values that did not parse as integers (each used
        // the option default). Reported once at the end of the config load.
        [[nodiscard]] size_t InvalidValues() const noexcept { return invalidValues_; }

    private:
        struct Entry
        {
            std::string_view section;
            std::string_view key;
            std::string_view value;
        };

        void Parse(std::string_view bytes);
        void BuildIndex();
        [[nodiscard]] const char* Find(std::string_view section,
                                       std::string_view key) const noexcept;
        [[nodiscard]] const char* FindAny(std::string_view key) const noexcept;
        [[nodiscard]] bool Resolve(const char* key, const char* defaultValue,
                                   bool gated) const noexcept;

        // Backing store for the entry views. NUL-terminated in place so a value
        // can be handed out as `const char*` without copying.
        std::vector<char> arena_;
        std::vector<Entry> entries_;   // file order, duplicates included
        std::vector<uint32_t> index_;  // open addressing: 0 empty, else entry + 1
        std::filesystem::path path_;
        mutable size_t invalidValues_{};
        bool loaded_{};
    };
} // namespace opp
