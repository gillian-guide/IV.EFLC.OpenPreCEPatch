// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.log — the synchronous trace log.
//
// The original mod wrote a trace log next to the ASI; the line shape (stage,
// build, option, result) is unchanged so existing diagnostics keep working.
// This module is a leaf: it depends only on the CRT, so every other module may
// log without creating a cycle.

module;

#include <cstdarg>
#include <cstdio>
#include <filesystem>

export module opp.log;

export namespace opp
{
    void OpenLog(const std::filesystem::path& path);
    void Log(const char* format, ...);
}

namespace opp
{
    namespace
    {
        std::FILE* g_log = nullptr;
    }

    void OpenLog(const std::filesystem::path& path)
    {
        if (g_log)
            return;
        g_log = _wfopen(path.c_str(), L"w");
        if (g_log)
        {
            std::fprintf(g_log, "OpenPreCEPatch log\n");
            std::fflush(g_log);
        }
    }

    void Log(const char* format, ...)
    {
        if (!g_log)
            return;

        // The achievement hook and the install pass log from different threads.
        // Format one whole line and emit it with a single call so lines cannot
        // interleave (the CRT locks the stream for the duration of each write).
        // The capacity fits the longest line an arm emits (a signature miss plus
        // its arm name); a longer one is truncated, never split.
        constexpr size_t kLogLineCapacity = 2048;
        char line[kLogLineCapacity];
        va_list args;
        va_start(args, format);
        const int used = std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        if (used < 0)
            return;
        std::size_t length = static_cast<std::size_t>(used);
        if (length >= sizeof(line))
            length = sizeof(line) - 1;
        line[length++] = '\n';
        std::fwrite(line, 1, length, g_log);
        std::fflush(g_log);
    }
} // namespace opp
