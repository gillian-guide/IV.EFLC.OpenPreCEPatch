// SPDX-License-Identifier: GPL-3.0-or-later

#include "steam_achievements.hxx"

#include "log.hxx"

#include <windows.h>

#include <cstring>

namespace opp
{
    bool InstallSteamNow() noexcept;

    namespace
    {
        // GTA IV achievement ids and their Steam API names. The standalone
        // SteamAchievements.asi uses the same table (ids 41-50 unused).
        struct AchievementName
        {
            uint32_t id;
            const char* name;
        };

        constexpr AchievementName kAchievementNames[] = {
            {1, "THETOUR"},
            {2, "SCOREDARTGAME"},
            {3, "POOLWIN"},
            {4, "QUBBEAT"},
            {5, "MELEECOUNTER"},
            {6, "STREETRACE"},
            {7, "BIKEWHEELIE"},
            {8, "TURKEY"},
            {9, "SATAXI"},
            {10, "CARROLLS"},
            {11, "LOSERATING"},
            {12, "DRUGDONE"},
            {13, "SAGUNCAR"},
            {14, "CHAINREACTION"},
            {15, "WANTEDLEVEL6STARSURVIVAL"},
            {16, "KEYMISSION1"},
            {17, "IMPORTDONE"},
            {18, "WANTEDDONE"},
            {19, "VIGILDONE"},
            {20, "KEYMISSION0"},
            {21, "TAXIPASSANGER"},
            {22, "ALLHELITOURTAKEN"},
            {23, "DATING"},
            {24, "SAHELI"},
            {25, "HALFMILLION"},
            {26, "KEYMISSION2"},
            {27, "OPENEDALDERNEY"},
            {28, "SMSTHEFTDONE"},
            {29, "USJDONE"},
            {30, "ASSASSIN"},
            {31, "HIDDENPACKAGES"},
            {32, "UNDERALLBRIDGES"},
            {33, "SABOMBCAR"},
            {34, "KEYMISSION3"},
            {35, "FRIENDSLIKEYOU"},
            {36, "METRANDOM"},
            {37, "KEYMISSION4"},
            {38, "STORYDONE"},
            {39, "STREETLIGHTSDOWN"},
            {40, "GAMECOMPLETION100"},
            {51, "E1INTRO"},
            {52, "E1BECOMELEADER"},
            {53, "E1FINISHSTORY"},
            {54, "E1KNOCKOFF"},
            {55, "E1BUDDYSTAT"},
            {56, "E2BASEJUMP"},
            {57, "E2TRINITY"},
            {58, "E2GOLF"},
            {59, "E2FIGHTC"},
            {60, "E2DANCE"},
            {61, "E2DRUGS"},
            {62, "E2FREEFALL"},
            {63, "E2STORY"},
            {64, "E2FAIRFLAIR"},
            {65, "E2ALLFLAIR"},
        };

        struct XUserAchievement
        {
            uint32_t userIndex;
            uint32_t achievementId;
        };

        using WriteAchievementsFn = uint32_t(__stdcall*)(uint32_t, uint32_t,
                                                         const XUserAchievement*,
                                                         void*);
#if defined(_M_IX86)
#define OPCEP_THISCALL __thiscall
#else
#define OPCEP_THISCALL
#endif
        using SetAchievementFn = bool(OPCEP_THISCALL*)(void*, const char*);
        using StoreStatsFn = bool(OPCEP_THISCALL*)(void*);
        using RunCallbacksFn = void(__cdecl*)();

        WriteAchievementsFn g_write_original = nullptr;
        SetAchievementFn g_set_achievement = nullptr;
        StoreStatsFn g_store_stats = nullptr;
        RunCallbacksFn g_run_callbacks = nullptr;
        void* g_user_stats = nullptr;
        bool g_mirrored[66] = {};  // ids 1..65
        bool g_installed = false;
        bool g_thread_started = false;

        const char* NameForId(uint32_t id) noexcept
        {
            for (const AchievementName& entry : kAchievementNames)
            {
                if (entry.id == id)
                    return entry.name;
            }
            return nullptr;
        }

        bool ReplaceImportOrdinal(HMODULE module, const char* dll_name,
                                  WORD ordinal, void* replacement,
                                  void** original) noexcept
        {
            if (module == nullptr || dll_name == nullptr || replacement == nullptr)
                return false;
            const auto* base = reinterpret_cast<const uint8_t*>(module);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;
            const auto& directory =
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (directory.VirtualAddress == 0)
                return false;

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                const_cast<uint8_t*>(base) + directory.VirtualAddress);
            for (; descriptor->Name != 0; ++descriptor)
            {
                const char* name = reinterpret_cast<const char*>(
                    base + descriptor->Name);
                if (_stricmp(name, dll_name) != 0)
                    continue;
                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    const_cast<uint8_t*>(base) + descriptor->OriginalFirstThunk);
                auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    const_cast<uint8_t*>(base) + descriptor->FirstThunk);
                if (descriptor->OriginalFirstThunk == 0)
                    thunk = slot;
                for (; thunk->u1.AddressOfData != 0; ++thunk, ++slot)
                {
                    if (!IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal) ||
                        IMAGE_ORDINAL(thunk->u1.Ordinal) != ordinal)
                        continue;
                    DWORD old_protect = 0;
                    if (!VirtualProtect(&slot->u1.Function, sizeof(void*),
                                        PAGE_READWRITE, &old_protect))
                        return false;
                    if (original != nullptr)
                        *original = reinterpret_cast<void*>(slot->u1.Function);
                    slot->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                    VirtualProtect(&slot->u1.Function, sizeof(void*), old_protect,
                                   &old_protect);
                    return true;
                }
            }
            return false;
        }

        void Mirror(uint32_t id) noexcept
        {
            if (g_set_achievement == nullptr || id >= 66)
                return;
            if (g_mirrored[id])
                return;
            const char* name = NameForId(id);
            if (name == nullptr)
                return;
            g_mirrored[id] = true;
            if (g_set_achievement(g_user_stats, name) && g_store_stats != nullptr)
                g_store_stats(g_user_stats);
            if (g_run_callbacks != nullptr)
                g_run_callbacks();
        }

        extern "C" uint32_t __stdcall XUserWriteAchievementsHook(
            uint32_t user_index, uint32_t count,
            const XUserAchievement* achievements, void* overlapped) noexcept
        {
            const uint32_t result =
                g_write_original != nullptr
                    ? g_write_original(user_index, count, achievements, overlapped)
                    : 0;
            if (achievements != nullptr)
            {
                for (uint32_t i = 0; i < count; ++i)
                    Mirror(achievements[i].achievementId);
            }
            return result;
        }
    } // namespace

    bool InstallSteamAchievementsBridge(GameImage image) noexcept
    {
        if (g_installed)
            return true;
        if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(image.base))
            return false;
        if (g_thread_started)
            return true;

        // SteamAPI_Init loads libraries and starts threads; doing that under
        // the loader lock (PatchGame runs from DllMain) can deadlock. Let the
        // worker run after the loader releases the lock.
        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(1000);
            (void)InstallSteamNow();
            return 0;
        }, nullptr, 0, nullptr);
        if (thread == nullptr)
            return false;
        CloseHandle(thread);
        g_thread_started = true;
        return true;
    }

    bool InstallSteamNow() noexcept
    {
        if (g_installed)
            return true;

        HMODULE steam = LoadLibraryA("steam_api.dll");
        if (steam == nullptr)
        {
            Log("steam: steam_api.dll not found, achievements bridge disabled");
            return false;
        }

        const auto init = reinterpret_cast<bool(__cdecl*)()>(
            GetProcAddress(steam, "SteamAPI_Init"));
        const auto get_user = reinterpret_cast<int(__cdecl*)()>(
            GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
        const auto find_interface = reinterpret_cast<void*(__cdecl*)(int, const char*)>(
            GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface"));
        const auto run_callbacks = reinterpret_cast<RunCallbacksFn>(
            GetProcAddress(steam, "SteamAPI_RunCallbacks"));
        if (init == nullptr || get_user == nullptr || find_interface == nullptr)
        {
            Log("steam: required exports missing");
            return false;
        }
        if (!init())
        {
            Log("steam: SteamAPI_Init failed (not running through Steam?)");
            return false;
        }

        g_user_stats =
            find_interface(get_user(), "STEAMUSERSTATS_INTERFACE_VERSION012");
        if (g_user_stats == nullptr)
        {
            Log("steam: ISteamUserStats v012 unavailable");
            return false;
        }
        auto* vtable = *reinterpret_cast<void***>(g_user_stats);
        if (vtable == nullptr)
        {
            Log("steam: ISteamUserStats vtable unavailable");
            return false;
        }
        g_set_achievement = reinterpret_cast<SetAchievementFn>(vtable[28 / 4]);
        g_store_stats = reinterpret_cast<StoreStatsFn>(vtable[40 / 4]);
        g_run_callbacks = run_callbacks;
        if (g_set_achievement == nullptr ||
            g_store_stats == nullptr)
        {
            Log("steam: unexpected ISteamUserStats layout");
            return false;
        }

        HMODULE module = GetModuleHandleW(nullptr);
        if (!ReplaceImportOrdinal(module, "xlive.dll", 5278,
                                  reinterpret_cast<void*>(&XUserWriteAchievementsHook),
                                  reinterpret_cast<void**>(&g_write_original)))
        {
            Log("steam: XUserWriteAchievements import not found");
            return false;
        }
        g_installed = true;
        Log("steam: achievements bridge installed (GFWL preserved)");
        return true;
    }
} // namespace opp
