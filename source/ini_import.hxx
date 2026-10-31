// SPDX-License-Identifier: GPL-3.0-or-later
//
// First-run INI creation and migration. The ASI owns
// `IV.EFLC.OpenPreCEPatch.ini`; when it is absent the mod writes the shipped
// template, and when an old `ZolikaPatch.ini` is present the old values are
// ported into the grouped file before it is read.
//
// The import is a text transform over the template, so it can only carry keys
// the template has. A legacy key with no row is reported (and logged) instead of
// being dropped silently: see UnsupportedLegacyKeys (not implemented in this
// tree) and CompanionLegacyKeys (installed by the XLivelessAddon companion).

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opp
{
    // The outcome of a legacy import: one entry per legacy option key that could
    // not be carried into the grouped file, with the reason.
    struct LegacyImportReport
    {
        struct DroppedKey
        {
            std::string key;
            std::string reason;
        };
        std::vector<DroppedKey> dropped;
    };

    // Resolve the config path next to the ASI. Creates the grouped file from an
    // old flat file, or from the template when neither exists. Falls back to the
    // old file if the new one cannot be written. When `report` is set, the keys
    // the import could not carry over are recorded there.
    std::filesystem::path EnsureConfigFile(const std::filesystem::path& moduleDirectory,
                                           LegacyImportReport* report = nullptr);

    // Original options this tree does not implement. A legacy INI that sets one
    // is imported with the key left out and a log line, never silently.
    std::span<const std::string_view> UnsupportedLegacyKeys() noexcept;

    // Original options owned by the XLivelessAddon companion module, which reads
    // the same legacy keys from its own INI. Listed separately so the log does
    // not call them "unimplemented".
    std::span<const std::string_view> CompanionLegacyKeys() noexcept;
} // namespace opp
