// SPDX-License-Identifier: GPL-3.0-or-later

#include "menu.hxx"

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
