// SPDX-License-Identifier: GPL-3.0-or-later
//
// Steam achievement mirror, implemented independently of the game-code
// detours used by the standalone SteamAchievements.asi.
//
// It hooks xlive.dll!XUserWriteAchievements (ordinal 5278, a 3-argument
// __stdcall) in the game's import table as soon as the install pass runs, calls
// the original first (so GFWL achievements and xliveless's local achievement
// store keep working), and mirrors each achievement id to Steam's
// ISteamUserStats. Because the hook sits at the GFWL API boundary, other
// game-code patches (TryToSkipAllErrors, VSyncFix) cannot move or overwrite the
// site.
//
// SteamAPI_Init runs on a worker thread after the loader lock is released; ids
// written before it finishes are queued and replayed, so no write is lost.

#pragma once

#include "common.hxx"

namespace opp
{
    // Installs the XUserWriteAchievements import hook and starts the Steam
    // initialisation worker. Returns false when steam_api.dll/Steam is
    // unavailable or the import is absent; the game then keeps its normal
    // GFWL-only behaviour.
    bool InstallSteamAchievementsBridge(GameImage image) noexcept;
} // namespace opp
