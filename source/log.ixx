// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.log — the synchronous trace log.
//
// The original mod wrote a trace log next to the ASI; the line shape (stage,
// build, option, result) is unchanged so existing diagnostics keep working.
// This module is a leaf: it depends only on the CRT, so every other module may
// log without creating a cycle.

module;

#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

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
        // The longest line an arm emits (a signature miss plus its arm name); a
        // longer one is truncated, never split.
        constexpr size_t kLogLineCapacity = 2048;

        // The trace log is one owned file. std::ofstream keeps it open for the
        // process lifetime and one mutex-guarded write per line keeps lines from
        // interleaving (the CRT used to provide that lock through stdio).
        class LogFile final
        {
        public:
            void Open(const std::filesystem::path& path)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stream_.is_open())
                    return;
                stream_.open(path, std::ios::binary | std::ios::trunc);
                if (stream_)
                {
                    stream_ << "OpenPreCEPatch log\n";
                    stream_.flush();
                }
            }

            void Line(std::string_view line)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stream_)
                    return;
                stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
                stream_.flush();
            }

        private:
            std::mutex mutex_;
            std::ofstream stream_;
        };

        LogFile g_log;
    } // namespace

    void OpenLog(const std::filesystem::path& path)
    {
        g_log.Open(path);
    }

    void Log(const char* format, ...)
    {
        std::array<char, kLogLineCapacity> line{};
        va_list args;
        va_start(args, format);
        const int used = std::vsnprintf(line.data(), line.size(), format, args);
        va_end(args);
        if (used < 0)
            return;
        size_t length = static_cast<size_t>(used);
        if (length >= line.size())
            length = line.size() - 1;
        line[length++] = '\n';
        g_log.Line(std::string_view(line.data(), length));
    }
} // namespace opp
