module;

#include <common.hxx>

#include <cstdlib>

export module warnings;

import common;
import config;
import game;

// OpenPreCEPatch's startup warnings, ported as a selectable group.
//
// The original opened message boxes from DllMain. This module keeps that shape
// (synchronous, like the install path) instead of the successor's posted task
// dialogs: the group is opt-in, so the 1:1 startup never shows one. The probes
// are the substantive part:
//   * a second copy of the ASI is detected with a named mutex, not a signature
//     miss (which false-positives on a different image);
//   * 1.2.0.1..1.2.9.99 are refused with the original's 0x8000FFFF exit, with
//     1.2.0.43 the one recovered exception;
//   * a loader without Ultimate ASI Loader's marker is reported (the dialog is a
//     warning, not an exit);
//   * the FusionFix notice lists the enabled-but-gated options, is rewritten on
//     every launch, and is shown only while its file does not exist.

export namespace Warnings
{
    // True for the complete-edition range except 1.2.0.43, the one build this
    // tree has recovered gates for.
    bool IsUnsupportedVersion(const Game::Version& version) noexcept;

    // Claims a process-wide named mutex. False when another copy of this ASI
    // already claimed it; the caller shows the loaded-twice warning and returns.
    bool ClaimSingleInstance(const wchar_t* name) noexcept;

    // A synchronous warning. `exitCode` non-zero terminates the process after
    // the user closes the box (the caller decides; this never returns then).
    void Show(const wchar_t* title, const std::wstring& text,
        uint32_t exitCode = 0) noexcept;

    // Writes the FusionFix notice next to the ASI and returns its path. The
    // caller shows it only when `existed` was false, which is the one-time rule.
    std::filesystem::path WriteFusionFixNotice(const std::filesystem::path& directory,
        std::span<const std::string_view> gatedOptions, bool& existed) noexcept;
}

namespace
{
    HANDLE g_singleInstance = nullptr;
}

namespace Warnings
{
    bool IsUnsupportedVersion(const Game::Version& version) noexcept
    {
        const auto compact = std::atoi(CompactVersion(version).c_str());
        if (compact == 12043)
            return false;
        return compact >= 1201 && compact <= 12999;
    }

    bool ClaimSingleInstance(const wchar_t* name) noexcept
    {
        if (g_singleInstance != nullptr)
            return true;

        const std::wstring mutexName =
            std::wstring(L"Local\\") + (name != nullptr ? name : L"ZolikaPatch-opcep") +
            L"-" + std::to_wstring(GetCurrentProcessId());
        g_singleInstance = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        if (g_singleInstance == nullptr)
            return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(g_singleInstance);
            g_singleInstance = nullptr;
            return false;
        }
        return true;
    }

    void Show(const wchar_t* title, const std::wstring& text, uint32_t exitCode) noexcept
    {
        MessageBoxW(nullptr, text.c_str(), title, MB_OK | MB_ICONWARNING);
        if (exitCode != 0)
            TerminateProcess(GetCurrentProcess(), exitCode);
    }

    std::filesystem::path WriteFusionFixNotice(const std::filesystem::path& directory,
        std::span<const std::string_view> gatedOptions, bool& existed) noexcept
    {
        const auto path = directory / L"ZolikaPatch.fusionfix-notice.txt";
        std::error_code ignored;
        existed = std::filesystem::exists(path, ignored);

        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
            return {};

        file << "ZolikaPatch options enabled in the INI but disabled because "
                "GTAIV.EFLC.FusionFix.asi is installed:\r\n\r\n";
        for (const auto option : gatedOptions)
            file << "  " << option << "\r\n";
        file << "\r\nDelete this file to show the notice again on the next launch.\r\n";
        return path;
    }
}

