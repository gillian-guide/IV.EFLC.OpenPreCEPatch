// SPDX-License-Identifier: GPL-3.0-or-later
//
// IVMenuAPI integration: the two toggles the original registers through
// AddIVMenuOption, and the ZMenuIV version guard.

#pragma once

#include <cstddef>
#include <cstdint>

#include "common.hxx"

namespace opp
{
    enum class MenuProbeResult
    {
        disabled,
        already_loaded,
        loaded,
        unavailable,
    };

    struct MenuIntegrationState final
    {
        bool optionEnabled{true};
        bool modulePresent{};
        bool attemptMade{};
        size_t loadAttempts{};
    };

    MenuProbeResult ProbeMenuApi(bool optionEnabled, bool modulePresent, bool attemptAlreadyMade,
                                 bool loadSucceeded) noexcept;
    MenuProbeResult MenuIntegrationProbe(MenuIntegrationState& state, bool loadSucceeded) noexcept;

    struct MenuRegistration final
    {
        const char* family;
        const wchar_t* title;
        const char* display;
        const char* pref;
        int a;
        int b;
        uintptr_t getter;
        uintptr_t setter;
    };

    const MenuRegistration* MenuRegistrations(size_t& count) noexcept;
    size_t RegisterMenuOptions() noexcept;

    constexpr uint32_t kZMenuMinVersion = 0x35DD1u;
    constexpr const char* kZMenuModuleName = "ZMenuIV.asi";
    constexpr const char* kZMenuProcName = "GetZMenuVersion";
    constexpr const char* kZMenuRejectText =
        "Incompatible/outdated version of ZMenu detected! The game will now exit.";
    constexpr const char* kZMenuRejectCaption = "";
    constexpr uint32_t kZMenuRejectFlags = 0x10u;
    constexpr uint32_t kZMenuTerminateExitCode = 0u;

    enum class ZMenuGuardResult : uint8_t
    {
        module_absent,
        export_absent,
        version_ok,
        version_rejected,
    };

    constexpr bool ZMenuVersionAccepted(uint32_t version) noexcept
    {
        return version >= kZMenuMinVersion;
    }

    ZMenuGuardResult ZMenuVersionGuard(uint32_t& version) noexcept;
} // namespace opp
