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
