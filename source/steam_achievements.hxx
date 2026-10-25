// SPDX-License-Identifier: GPL-3.0-or-later
//
// Steam achievement mirror, implemented independently of the game-code
// detours used by the standalone SteamAchievements.asi.
//
// It hooks xlive.dll!XUserWriteAchievements in the game's import table, calls
// the original first (so GFWL achievements keep working), and then mirrors each
// achievement id to Steam's ISteamUserStats. Because the hook sits at the GFWL
// API boundary, other game-code patches (TryToSkipAllErrors, VSyncFix) cannot
// move or overwrite the site.

#pragma once

#include "common.hxx"

namespace opp
{
    // Installs the XUserWriteAchievements IAT hook and initialises Steam.
    // Returns false when steam_api.dll/Steam is unavailable or the import is
    // absent; the game then keeps its normal GFWL-only behaviour.
    bool InstallSteamAchievementsBridge(GameImage image) noexcept;
} // namespace opp
