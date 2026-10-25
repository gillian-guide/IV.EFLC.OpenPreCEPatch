// SPDX-License-Identifier: GPL-3.0-or-later

#include "log.hxx"

#include <cstdarg>
#include <cstdio>

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

        va_list args;
        va_start(args, format);
        std::vfprintf(g_log, format, args);
        va_end(args);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
} // namespace opp
