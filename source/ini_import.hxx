// SPDX-License-Identifier: GPL-3.0-or-later
//
// First-run INI creation and migration. The ASI owns
// `IV.EFLC.OpenPreCEPatch.ini`; when it is absent the mod writes the shipped
// template, and when an old `ZolikaPatch.ini` is present the old values are
// ported into the grouped file before it is read.

#pragma once

#include <filesystem>

namespace opp
{
    // Resolve the config path next to the ASI. Creates the grouped file from an
    // old flat file, or from the template when neither exists. Falls back to the
    // old file if the new one cannot be written.
    std::filesystem::path EnsureConfigFile(const std::filesystem::path& moduleDirectory);
} // namespace opp
