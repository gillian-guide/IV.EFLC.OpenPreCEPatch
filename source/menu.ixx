// zp.menu - the IVMenuAPI integration.
//
// The ZMenuIV version guard is not here: it runs in the startup sequence
// (`src/dllmain.cpp`), so this module only probes the API and registers the toggles.

module;

#include "common.hxx"
#include "common.h"
#include "hooks.h"

export module zp.menu;

export
{
MenuProbeResult ProbeMenuApi(bool optionEnabled, bool modulePresent, bool attemptAlreadyMade,
                             bool loadSucceeded) noexcept;

MenuProbeResult MenuIntegrationProbe(MenuIntegrationState& state, bool loadSucceeded) noexcept;

std::span<const MenuRegistration> MenuRegistrations() noexcept;

std::size_t RegisterMenuOptions() noexcept;
}  // export

namespace
{
// The callbacks are ABI bodies (the menu calls them with the original convention).
const MenuRegistration kMenuRegistrations[] = {
	{"PREF_DEATHMUSIC", L"Death Music", "MENU_DISPLAY_ON_OFF", "PREF_MUSIC_VOLUME", 1, 0,
	 reinterpret_cast<std::uintptr_t>(&abi_menu_death_get),
	 reinterpret_cast<std::uintptr_t>(&abi_menu_death_set)},
	{"PREF_FOCUSLOSS", L"Pause Game On Focus Loss", "MENU_DISPLAY_ON_OFF", "PREF_VSYNC", 1, 0,
	 reinterpret_cast<std::uintptr_t>(&abi_menu_focus_get),
	 reinterpret_cast<std::uintptr_t>(&abi_menu_focus_set)},
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

std::span<const MenuRegistration> MenuRegistrations() noexcept
{
	return kMenuRegistrations;
}

std::size_t RegisterMenuOptions() noexcept
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

	std::size_t total = 0;
	for (const MenuRegistration& row : MenuRegistrations())
	{
		const int result = add(row.family, row.title, row.display, row.pref, row.a, row.b,
		                       static_cast<int>(row.getter), static_cast<int>(row.setter));
		if (result)
			++total;
	}
	return total;
}
