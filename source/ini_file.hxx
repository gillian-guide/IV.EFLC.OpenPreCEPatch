// SPDX-License-Identifier: GPL-3.0-or-later
//
// The legacy (ZolikaPatch.ini) reader.
//
// The old file is parsed with the vendored mINI/IniReader header, whose grammar
// is the one the original loader used. That header is a plain include-only
// dependency with no module-safe surface, so it lives in exactly one plain
// translation unit (`ini_file.cpp`); no module purview ever sees it. This is the
// same split FusionFix uses for its IniReader wrapper: the feature modules read
// the typed config document (`opp.config`), and only the one-time legacy import
// touches mINI.
//
// `ini_file.cpp` and `dllmain.cpp` are the only two plain TUs in the tree.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace opp
{
    class LegacyIniFile final
    {
    public:
        explicit LegacyIniFile(const std::filesystem::path& path);
        ~LegacyIniFile();

        LegacyIniFile(const LegacyIniFile&) = delete;
        LegacyIniFile& operator=(const LegacyIniFile&) = delete;

        // mINI's ReadString: the value when the section/key exists (an empty
        // value stays empty), `defaultValue` otherwise. Both legacy parse sites
        // (the option keys and the two vehicle tables) go through this call, so
        // the old grammar is implemented once.
        [[nodiscard]] std::string ReadString(std::string_view section, std::string_view key,
                                             std::string_view defaultValue);

    private:
        void* reader_{};
    };
} // namespace opp
