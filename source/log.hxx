// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal, synchronous log. The original mod wrote a trace log next to the ASI;
// the fields stay the same shape (stage, build, option, result) so existing
// diagnostics keep working.

#pragma once

#include <filesystem>

namespace opp
{
    void OpenLog(const std::filesystem::path& path);
    void Log(const char* format, ...);
} // namespace opp
