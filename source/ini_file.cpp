// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one plain translation unit that includes the vendored mINI/IniReader.
// Everything else in this tree is a module interface unit or `dllmain.cpp`; the
// vendor header stays here so its macros (`MINI_CASE_SENSITIVE`, its own
// `WIN32_LEAN_AND_MEAN` include) never leak into a module purview.

#include "ini_file.hxx"

#include <IniReader.h>

#include <utility>

namespace opp
{
    namespace
    {
        CIniReader& Reader(void* reader) noexcept
        {
            return *static_cast<CIniReader*>(reader);
        }
    } // namespace

    LegacyIniFile::LegacyIniFile(const std::filesystem::path& path)
        : reader_(new CIniReader(std::filesystem::absolute(path)))
    {
    }

    LegacyIniFile::~LegacyIniFile()
    {
        delete static_cast<CIniReader*>(reader_);
    }

    std::string LegacyIniFile::ReadString(std::string_view section, std::string_view key,
                                          std::string_view defaultValue)
    {
        return Reader(reader_).ReadString(section, key, defaultValue);
    }
} // namespace opp
