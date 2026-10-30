// SPDX-License-Identifier: GPL-3.0-or-later
//
// ZMenuIV version guard. The old IVMenuAPI registration surface (AddOptionsInMenu)
// was removed; only the guard remains.

#pragma once

#include <cstdint>

#include "common.hxx"

namespace opp
{
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
