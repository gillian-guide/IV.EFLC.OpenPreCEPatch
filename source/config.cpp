// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parse-once INI document. See config.hxx for the grammar and the option
// semantics; this file is the parser, the index and the typed value reader.

#include "config.hxx"

#include "fusionfix_compat.hxx"
#include "log.hxx"

#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace opp
{
    namespace
    {
        // The grouped layout, in search order. Kept in one place so the parser,
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

        // mINI's comment finder: `;` is looked for first and can veto `//`; the
        // marker must be preceded by a space. Returns npos when there is none.
        size_t CommentAt(std::string_view line) noexcept
        {
            size_t at = line.find_last_of(';');
            if (at == std::string_view::npos)
                at = line.rfind("//");
            if (at == std::string_view::npos)
                at = line.find_last_of('#');
            if (at == std::string_view::npos || at == 0 || line[at - 1] != ' ')
                return std::string_view::npos;
            return at;
        }

        // mINI removes one quote pair at ReadString time. A value that is a
        // single quote makes mINI's own `at(size()-1)` throw and fall back to
        // the caller's default, i.e. the key behaves as absent; that case is
        // reported as nullopt so Has()/Option() keep the old answer.
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

        std::ifstream file(path_, std::ios::binary);
        if (!file)
            return;
        std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        Parse(bytes);
        BuildIndex();
    }

    std::span<const std::string_view> OptionSections() noexcept
    {
        return kOptionSections;
    }

    void Config::Parse(std::string_view bytes)
    {
        // A UTF-8 BOM is skipped, like the mINI reader this replaces.
        if (bytes.size() >= 3 && static_cast<uint8_t>(bytes[0]) == 0xEF &&
            static_cast<uint8_t>(bytes[1]) == 0xBB && static_cast<uint8_t>(bytes[2]) == 0xBF)
            bytes.remove_prefix(3);

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
        pending.reserve(bytes.size() / 24 + 1);
        arena_.reserve(bytes.size() + 64);

        const auto append = [this](std::string_view text) {
            const uint32_t offset = static_cast<uint32_t>(arena_.size());
            arena_.insert(arena_.end(), text.begin(), text.end());
            arena_.push_back('\0');
            return std::pair{offset, static_cast<uint32_t>(text.size())};
        };

        std::string scratch;  // only used for a line carrying CR/NUL bytes
        std::string section;
        bool inSection = false;

        while (!bytes.empty())
        {
            const size_t newline = bytes.find('\n');
            std::string_view line =
                newline == std::string_view::npos ? bytes : bytes.substr(0, newline);
            bytes = newline == std::string_view::npos ? std::string_view{}
                                                      : bytes.substr(newline + 1);

            // mINI drops '\r' and '\0' anywhere in a line; do the same, but only
            // allocate for the lines that actually contain one.
            if (line.find_first_of(std::string_view{"\r\0", 2}) != std::string_view::npos)
            {
                scratch.clear();
                for (const char c : line)
                {
                    if (c != '\r' && c != '\0')
                        scratch.push_back(c);
                }
                line = scratch;
            }

            line = Trim(line);
            if (line.empty())
                continue;

            const char first = line.front();
            if (first == '#' || first == ';' ||
                (first == '/' && line.size() > 1 && line[1] == '/'))
                continue;  // whole-line comment

            if (first == '[')
            {
                std::string_view header = line;
                if (const size_t comment = CommentAt(header); comment != std::string_view::npos)
                    header = header.substr(0, comment);
                const size_t close = header.find_last_of(']');
                if (close != std::string_view::npos)
                {
                    // Stored by value: a section name must outlive the line
                    // buffer, and `line` may point at the scratch copy.
                    const std::string_view name = Trim(header.substr(1, close - 1));
                    section.assign(name.begin(), name.end());
                    inSection = true;
                }
                continue;
            }

            // A key line needs a section and an unescaped '='. The escaped form
            // `\=` is two extra-safe steps: hide it to find the split, restore it
            // in the key (mINI's exact algorithm).
            if (!inSection)
                continue;

            size_t equals = std::string_view::npos;
            if (line.find('\\') == std::string_view::npos)
            {
                equals = line.find('=');
            }
            else
            {
                std::string normalized(line);
                for (size_t at = normalized.find("\\="); at != std::string::npos;
                     at = normalized.find("\\=", at + 2))
                    normalized.replace(at, 2, "  ");
                equals = normalized.find('=');
            }
            if (equals == std::string_view::npos)
                continue;  // mINI's PDATA_UNKNOWN

            std::string restoredKey;
            std::string_view key = Trim(line.substr(0, equals));
            if (key.find('\\') != std::string_view::npos)
            {
                restoredKey.assign(key.begin(), key.end());
                for (size_t at = restoredKey.find("\\="); at != std::string::npos;
                     at = restoredKey.find("\\=", at + 2))
                    restoredKey.replace(at, 2, "=");
                key = restoredKey;
            }

            std::string_view value = line.substr(equals + 1);
            if (const size_t comment = CommentAt(line);
                comment != std::string_view::npos && comment > equals)
                value = line.substr(equals + 1, comment - equals - 1);
            value = Trim(value);

            bool absent = false;
            value = StripQuotes(value, absent);
            if (absent)
                continue;  // a lone quote: mINI falls back to the caller's default

            // The key and section views are copied into the arena, so a scratch
            // line buffer does not leak into the entry.
            const auto [sectionOffset, sectionLength] = append(section);
            const auto [keyOffset, keyLength] = append(key);
            const auto [valueOffset, valueLength] = append(value);
            pending.push_back({sectionOffset, sectionLength, keyOffset, keyLength,
                               valueOffset, valueLength});
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
            // exactly like the std::map assignment the old reader did.
            index_[slot] = i + 1;
        }
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
} // namespace opp
