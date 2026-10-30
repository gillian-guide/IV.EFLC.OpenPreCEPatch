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
            {"windowed", Improvement::windowed},
            {"borderless", Improvement::windowed},
            {"dontpause", Improvement::windowed},
            {"failsafe", Improvement::failsafe},
            {"failsafes", Improvement::failsafe},
            {"guards", Improvement::failsafe},
            {"null", Improvement::failsafe},
            {"garage", Improvement::garage_entry},
            {"garageentry", Improvement::garage_entry},
            {"garage-entry", Improvement::garage_entry},
            {"garage_entry", Improvement::garage_entry},
            {"cutscene", Improvement::cutscene_entry},
            {"cutsceneentry", Improvement::cutscene_entry},
            {"cutscene-entry", Improvement::cutscene_entry},
            {"cutscene_entry", Improvement::cutscene_entry},
            {"nms", Improvement::nms_thunk},
            {"nmsthunk", Improvement::nms_thunk},
            {"nms-thunk", Improvement::nms_thunk},
            {"nms_thunk", Improvement::nms_thunk},
            {"alpha", Improvement::alpha_nop},
            {"alphanop", Improvement::alpha_nop},
            {"alpha_nop", Improvement::alpha_nop},
            // The canonical group names use underscores; accept them too so the
            // names printed by ImprovementName() are always parseable.
            {"new_saves", Improvement::new_saves},
            {"model_index", Improvement::model_index},
            {"skip_intro", Improvement::skip_intro},
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
            // `all` inside a list means the folded groups, so
            // `all,cutscene_entry` keeps the folded improvements on. `entry`
            // expands to every opt-in entry-point rewrite, so one word covers
            // their A/B run.
            if (EqualsIgnoreCase(name, "all"))
                return DefaultImprovements();
            if (EqualsIgnoreCase(name, "entry") || EqualsIgnoreCase(name, "entries"))
                return EntryImprovements();
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
        constexpr uint32_t all = DefaultImprovements();
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
            return DefaultImprovements();
        return ParseImprovements(value);
#endif
    }

    uint32_t ResolveImprovements(uint32_t environment, const char* iniValue) noexcept
    {
        if (iniValue == nullptr)
            return environment;
        const std::string_view text = Trim(iniValue);
        if (text.empty())
            return environment;
        if (text == "1" || EqualsIgnoreCase(text, "all") ||
            EqualsIgnoreCase(text, "on") || EqualsIgnoreCase(text, "true"))
        {
            // The default set already excludes the allocator rewrite: its live
            // run OOM'd on exit (the game's own allocator atexit handler resets
            // the vtable while our allocations are still live). It stays
            // selectable by name (`Improvements = all,entry,nms_thunk`).
            return DefaultImprovements();
        }
        if (text == "0" || EqualsIgnoreCase(text, "none") ||
            EqualsIgnoreCase(text, "off") || EqualsIgnoreCase(text, "false"))
            return 0u;
        return ParseImprovements(iniValue);
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
        case Improvement::windowed: return "windowed";
        case Improvement::failsafe: return "failsafe";
        case Improvement::garage_entry: return "garage_entry";
        case Improvement::cutscene_entry: return "cutscene_entry";
        case Improvement::nms_thunk: return "nms_thunk";
        case Improvement::alpha_nop: return "alpha_nop";
        case Improvement::none: return "none";
        case Improvement::all: return "all";
        }
        return "unknown";
    }

    size_t FormatImprovements(uint32_t mask, char* buffer, size_t size) noexcept
    {
        if (buffer == nullptr || size == 0u)
            return 0u;
        size_t used = 0u;
        buffer[0] = '\0';
        const Improvement groups[] = {
            Improvement::new_saves,   Improvement::ooc,      Improvement::vehicle,
            Improvement::popup,       Improvement::model_index, Improvement::skip_intro,
            Improvement::trace,       Improvement::windowed, Improvement::failsafe,
            Improvement::garage_entry, Improvement::cutscene_entry,
            Improvement::nms_thunk,   Improvement::alpha_nop,
        };
        for (const Improvement group : groups)
        {
            if ((mask & Improve(group)) == 0u)
                continue;
            const char* const name = ImprovementName(group);
            const size_t length = std::strlen(name);
            const size_t separator = used == 0u ? 0u : 2u;  // ", "
            if (used + separator + length + 1u > size)
                break;
            if (separator != 0u)
            {
                buffer[used++] = ',';
                buffer[used++] = ' ';
            }
            std::memcpy(buffer + used, name, length);
            used += length;
            buffer[used] = '\0';
        }
        if (used == 0u && size >= 5u)
        {
            std::memcpy(buffer, "none", 5u);  // includes the NUL
            used = 4u;
        }
        return used;
    }
} // namespace opp
