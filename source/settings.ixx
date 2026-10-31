// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.settings — the config/menu hub.
//
// It owns the INI document (re-exported from opp.config: the typed readers, the
// FusionFix gate, the first-run/legacy import) and the ZMenuIV version guard,
// the one piece of menu integration this tree ships (the old IVMenuAPI
// registration surface was removed). Keeping the guard here avoids a module of
// its own for twenty lines of code; the feature modules and dllmain import
// opp.settings for both.

module;

#include "common.hxx"

export module opp.settings;

export import opp.config;

export namespace opp
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
}

namespace opp
{
    ZMenuGuardResult ZMenuVersionGuard(uint32_t& version) noexcept
    {
        version = 0;
        const HMODULE module = GetModuleHandleA(kZMenuModuleName);
        if (!module)
            return ZMenuGuardResult::module_absent;

        const auto getVersion = reinterpret_cast<int(__cdecl*)(uint32_t*, uint32_t*)>(
            reinterpret_cast<void*>(GetProcAddress(module, kZMenuProcName)));
        if (!getVersion)
            return ZMenuGuardResult::export_absent;

        uint32_t out = 0;
        (void)getVersion(&version, &out); // the original ignores the result
        return ZMenuVersionAccepted(version) ? ZMenuGuardResult::version_ok
                                             : ZMenuGuardResult::version_rejected;
    }
} // namespace opp
