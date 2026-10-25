// SPDX-License-Identifier: GPL-3.0-or-later

#include "improvements.hxx"

#include <cctype>
#include <cstring>
#include <string_view>

#include <windows.h>

namespace opp
{
    namespace
    {
        struct ImprovementNameEntry
        {
            const char* alias;
            Improvement group;
        };

        constexpr ImprovementNameEntry kNames[] = {
            {"newsaves", Improvement::new_saves},
            {"new-saves", Improvement::new_saves},
            {"saves", Improvement::new_saves},
            {"ooc", Improvement::ooc},
            {"outofcommission", Improvement::ooc},
            {"vehicle", Improvement::vehicle},
            {"vehicles", Improvement::vehicle},
            {"components", Improvement::vehicle},
            {"moving", Improvement::vehicle},
            {"popup", Improvement::popup},
            {"popuplights", Improvement::popup},
            {"fchs", Improvement::popup},
            {"index", Improvement::model_index},
            {"modelindex", Improvement::model_index},
            {"model-name", Improvement::model_index},
            {"models", Improvement::model_index},
            {"skipintro", Improvement::skip_intro},
            {"intro", Improvement::skip_intro},
            {"trace", Improvement::trace},
            {"arms", Improvement::trace},
            {"logging", Improvement::trace},
        };

        bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
        {
            if (left.size() != right.size()) return false;
            for (size_t i = 0; i < left.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(left[i])) !=
                    std::tolower(static_cast<unsigned char>(right[i])))
                    return false;
            }
            return true;
        }

        uint32_t NamedGroup(std::string_view name) noexcept
        {
            for (const ImprovementNameEntry& entry : kNames)
            {
                if (EqualsIgnoreCase(name, entry.alias))
                    return Improve(entry.group);
            }
            return 0;
        }

        std::string_view Trim(std::string_view text) noexcept
        {
            while (!text.empty() &&
                   std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() &&
                   std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }

        uint32_t ParseList(std::string_view text) noexcept
        {
            uint32_t groups = 0;
            while (!text.empty())
            {
                const size_t separator = text.find_first_of(",; ");
                const std::string_view token =
                    separator == std::string_view::npos ? text : text.substr(0, separator);
                groups |= NamedGroup(Trim(token));
                if (separator == std::string_view::npos)
                    break;
                text.remove_prefix(separator + 1);
            }
            return groups;
        }
    } // namespace

    uint32_t ParseImprovements(const char* value) noexcept
    {
        constexpr uint32_t all = Improve(Improvement::all);
        if (value == nullptr)
            return all;

        const std::string_view text = Trim(value);
        if (text.empty() || EqualsIgnoreCase(text, "all") ||
            EqualsIgnoreCase(text, "on") || text == "1")
            return all;
        if (EqualsIgnoreCase(text, "none") || EqualsIgnoreCase(text, "off") ||
            text == "0")
            return 0;

        constexpr std::string_view onlyPrefix = "only:";
        constexpr std::string_view exceptPrefix = "except:";
        if (text.size() >= onlyPrefix.size() &&
            EqualsIgnoreCase(text.substr(0, onlyPrefix.size()), onlyPrefix))
            return ParseList(text.substr(onlyPrefix.size()));
        if (text.size() >= exceptPrefix.size() &&
            EqualsIgnoreCase(text.substr(0, exceptPrefix.size()), exceptPrefix))
            return all & ~ParseList(text.substr(exceptPrefix.size()));

        // A bare list is an "only" list: the most useful form for a targeted
        // live A/B (e.g. OPCEP_IMPROVEMENTS=skipintro).
        return ParseList(text);
    }

    uint32_t EnabledImprovements() noexcept
    {
#if defined(OPCEP_PARITY_BASELINE)
        return 0;
#else
        char value[256] = {};
        const DWORD length = GetEnvironmentVariableA(
            "OPCEP_IMPROVEMENTS", value, static_cast<DWORD>(sizeof(value)));
        if (length == 0 || length >= sizeof(value))
            return Improve(Improvement::all);
        return ParseImprovements(value);
#endif
    }

    const char* ImprovementName(Improvement value) noexcept
    {
        switch (value)
        {
        case Improvement::new_saves: return "new_saves";
        case Improvement::ooc: return "ooc";
        case Improvement::vehicle: return "vehicle";
        case Improvement::popup: return "popup";
        case Improvement::model_index: return "model_index";
        case Improvement::skip_intro: return "skip_intro";
        case Improvement::trace: return "trace";
        case Improvement::none: return "none";
        case Improvement::all: return "all";
        }
        return "unknown";
    }
} // namespace opp
