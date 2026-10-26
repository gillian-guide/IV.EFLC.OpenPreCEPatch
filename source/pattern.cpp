// SPDX-License-Identifier: GPL-3.0-or-later

#include "pattern.hxx"

namespace opp
{
    namespace
    {
        constexpr size_t kSignatureTokenWidth = 3;

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        size_t PatternLength(std::string_view normalized) noexcept
        {
            return normalized.size() / kSignatureTokenWidth + 1;
        }

        bool ParseToken(std::string_view token, int& value, bool& wildcard)
        {
            if (token == "?" || token == "??")
            {
                wildcard = true;
                value = 0;
                return true;
            }

            wildcard = false;
            size_t i = 0;
            while (i < token.size() && IsWhite(token[i]))
                ++i;

            bool negative = false;
            if (i < token.size() && (token[i] == '+' || token[i] == '-'))
            {
                negative = token[i] == '-';
                ++i;
            }

            if (i + 1 < token.size() && token[i] == '0' &&
                (token[i + 1] == 'x' || token[i + 1] == 'X'))
                i += 2;

            uint32_t accumulated = 0;
            for (; i < token.size(); ++i)
            {
                const char c = token[i];
                int digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    break;

                accumulated = accumulated * 16u + static_cast<uint32_t>(digit);
            }

            value = static_cast<int>((negative ? 0u - accumulated : accumulated) & 0xFFu);
            return true;
        }

        bool Normalize(std::string_view signature, std::string& out)
        {
            out.clear();
            out.reserve(signature.size());

            size_t i = 0;
            bool any = false;
            while (i < signature.size())
            {
                while (i < signature.size() && IsWhite(signature[i]))
                    ++i;
                if (i == signature.size())
                    break;

                const size_t start = i;
                while (i < signature.size() && !IsWhite(signature[i]))
                    ++i;
                const std::string_view token = signature.substr(start, i - start);

                int value = 0;
                bool wildcard = false;
                ParseToken(token, value, wildcard);

                if (!out.empty())
                    out.push_back(' ');

                if (wildcard)
                {
                    out.push_back('?');
                }
                else
                {
                    static const char kHex[] = "0123456789ABCDEF";
                    out.push_back(kHex[(value >> 4) & 0xFu]);
                    out.push_back(kHex[value & 0xFu]);
                }
                any = true;
            }
            return any;
        }
    } // namespace

    std::string NormalizeSignature(std::string_view signature)
    {
        std::string normalized;
        return Normalize(signature, normalized) ? normalized : std::string{};
    }

    ModuleView ExecutableView(ModuleView image) noexcept
    {
        const ModuleView whole{image.base, image.size};
        if (!image.base || image.size < sizeof(IMAGE_DOS_HEADER))
            return whole;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return whole;

        const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
        if (nt_offset + sizeof(IMAGE_NT_HEADERS) > image.size)
            return whole;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.base + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return whole;

        const size_t section_table = static_cast<size_t>(
            reinterpret_cast<uintptr_t>(IMAGE_FIRST_SECTION(nt)) -
            reinterpret_cast<uintptr_t>(image.base));
        const size_t section_count = nt->FileHeader.NumberOfSections;
        if (section_count == 0 || section_table < nt_offset ||
            section_table + section_count * sizeof(IMAGE_SECTION_HEADER) > image.size)
            return whole;

        const auto* sections =
            reinterpret_cast<const IMAGE_SECTION_HEADER*>(image.base + section_table);
        size_t end = 0;
        for (size_t i = 0; i < section_count; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const size_t section_end = static_cast<size_t>(section.VirtualAddress) +
                                       section.Misc.VirtualSize;
            if (section_end > end)
                end = section_end;
        }

        // No executable section, or headers that point outside the mapped image:
        // keep the old whole-image behaviour rather than silently scanning less.
        if (end == 0 || end > image.size)
            return whole;
        return {image.base, end};
    }

    bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept
    {
        output = {};
        if (!module.base || module.size == 0 || signature.empty())
            return false;

        std::string normalized;
        if (!Normalize(signature, normalized))
            return false;

        // The scanner subtracts the pattern length from the end before iterating,
        // so reject a pattern longer than the view instead of relying on underflow.
        if (PatternLength(normalized) > module.size)
            return false;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(module.base);
        auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
        pattern.count_hint(1);
        if (pattern.size() == 0)
            return false;

        output.address = static_cast<const uint8_t*>(pattern.get(0).get<uint8_t>());
        return true;
    }

    size_t FindAllPatterns(ModuleView module, std::string_view signature,
                                std::vector<const uint8_t*>& output) noexcept
    {
        output.clear();
        if (!module.base || module.size == 0 || signature.empty())
            return 0;

        std::string normalized;
        if (!Normalize(signature, normalized))
            return 0;
        if (PatternLength(normalized) > module.size)
            return 0;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(module.base);
        auto pattern = hook::make_range_pattern(begin, begin + module.size, normalized);
        const size_t count = pattern.size();
        output.reserve(count);
        for (size_t i = 0; i < count; ++i)
            output.push_back(static_cast<const uint8_t*>(pattern.get(i).get<uint8_t>()));
        return output.size();
    }

    bool InImage(GameImage image, uintptr_t address) noexcept
    {
        const auto base = reinterpret_cast<uintptr_t>(image.base);
        return address >= base && address + sizeof(uint32_t) <= base + image.size;
    }
} // namespace opp
