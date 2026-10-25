// SPDX-License-Identifier: GPL-3.0-or-later

#include "menu.hxx"

#include "hooks.h"

namespace opp
{
    namespace
    {
        const MenuRegistration kMenuRegistrations[] = {
            {"PREF_DEATHMUSIC", L"Death Music", "MENU_DISPLAY_ON_OFF", "PREF_MUSIC_VOLUME", 1, 0,
             reinterpret_cast<uintptr_t>(&abi_menu_death_get),
             reinterpret_cast<uintptr_t>(&abi_menu_death_set)},
            {"PREF_FOCUSLOSS", L"Pause Game On Focus Loss", "MENU_DISPLAY_ON_OFF", "PREF_VSYNC", 1,
             0, reinterpret_cast<uintptr_t>(&abi_menu_focus_get),
             reinterpret_cast<uintptr_t>(&abi_menu_focus_set)},
        };
    } // namespace

    MenuProbeResult ProbeMenuApi(bool optionEnabled, bool modulePresent, bool attemptAlreadyMade,
                                 bool loadSucceeded) noexcept
    {
        if (!optionEnabled)
            return MenuProbeResult::disabled;
        if (modulePresent)
            return MenuProbeResult::already_loaded;
        if (!attemptAlreadyMade && loadSucceeded)
            return MenuProbeResult::loaded;
        return MenuProbeResult::unavailable;
    }

    MenuProbeResult MenuIntegrationProbe(MenuIntegrationState& state, bool loadSucceeded) noexcept
    {
        if (!state.optionEnabled)
            return MenuProbeResult::disabled;
        if (state.modulePresent)
            return MenuProbeResult::already_loaded;
        if (state.attemptMade)
            return MenuProbeResult::unavailable;

        ++state.loadAttempts;
        if (loadSucceeded)
        {
            state.modulePresent = true;
            return MenuProbeResult::loaded;
        }

        state.attemptMade = true; // DAT_101a843c = 1, so it is not retried
        return MenuProbeResult::unavailable;
    }

    const MenuRegistration* MenuRegistrations(size_t& count) noexcept
    {
        count = sizeof(kMenuRegistrations) / sizeof(kMenuRegistrations[0]);
        return kMenuRegistrations;
    }

    size_t RegisterMenuOptions() noexcept
    {
        const HMODULE menu = GetModuleHandleA("IVMenuAPI.asi");
        if (!menu)
            return 0;

        using AddOptionFn =
            int(__cdecl*)(const char*, const wchar_t*, const char*, const char*, int, int, int, int);
        const auto add = reinterpret_cast<AddOptionFn>(
            reinterpret_cast<void*>(GetProcAddress(menu, "AddIVMenuOption")));
        if (!add)
            return 0;

        size_t count = 0;
        size_t total = 0;
        const MenuRegistration* rows = MenuRegistrations(count);
        for (size_t i = 0; i < count; ++i)
        {
            const MenuRegistration& row = rows[i];
            const int result = add(row.family, row.title, row.display, row.pref, row.a, row.b,
                                   static_cast<int>(row.getter), static_cast<int>(row.setter));
            if (result)
                ++total;
        }
        return total;
    }

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
