// SPDX-License-Identifier: GPL-3.0-or-later
//
// Steam achievement mirror.
//
// The game writes GFWL achievements through xlive.dll's
//   DWORD XUserWriteAchievements(DWORD dwNumAchievements,
//                                const XUSER_ACHIEVEMENT *pAchievements,
//                                XOVERLAPPED *pXOverlapped);
// which GTA IV imports by ordinal 5278 (5277 is XUserSetContext). We replace
// that slot in the game's import table, call the original first (so the GFWL or
// xliveless achievement store keeps working), and mirror the same ids through
// ISteamUserStats v012.
//
// The three-argument __stdcall contract is part of the hook ABI. The caller
// pushes 12 bytes and expects the callee to pop them (`ret 0xC`, which is what
// both xlive.dll and xliveless export). A body compiled with an extra argument
// returns `ret 0x10`, pops four bytes the caller owns and shifts every argument
// by one slot - the game then loops over a stack pointer as the achievement
// count and faults. Keep this in sync with the import, not with the old ASI.

#include "steam_achievements.hxx"

#include "log.hxx"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace opp
{
    bool InstallSteamNow() noexcept;

    namespace
    {
        // GTA IV / EFLC achievement ids and their Steam API names. Ids 1..40 are
        // the base-game single-player achievements, 41..50 the multiplayer ones
        // (AWARD_ACHIEVEMENT indices; the script ACHIEVEMENT_IDS enum agrees),
        // 51..55 TLAD and 56..65 TBoGT. The same names appear in Complete
        // Edition's achievement blob and in the standalone SteamAchievements.asi.
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
            {41, "MPTEAMWIN1"},
            {42, "MPCOMPWIN1"},
            {43, "MPRACEWIN1"},
            {44, "MPCOOPWIN1"},
            {45, "MPTEAMWINALL"},
            {46, "MPCOMPWINALL"},
            {47, "MPRACEWINALL"},
            {48, "MPSTARRATING"},
            {49, "MPCOOPWINALL"},
            {50, "VIRALKILLME"},
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

        // XUserWriteAchievements is a 3-argument __stdcall. See the file note.
        using WriteAchievementsFn =
            uint32_t(__stdcall*)(uint32_t, const XUserAchievement*, void*);
#if defined(_M_IX86)
#define OPCEP_THISCALL __thiscall
#else
#define OPCEP_THISCALL
#endif
        using SetAchievementFn = bool(OPCEP_THISCALL*)(void*, const char*);
        using StoreStatsFn = bool(OPCEP_THISCALL*)(void*);
        using RequestStatsFn = bool(OPCEP_THISCALL*)(void*);
        using RunCallbacksFn = void(__cdecl*)();

        constexpr uint32_t kMaxAchievementId = 65;
        constexpr uint32_t kAchievementSlots = kMaxAchievementId + 1;
        // The game writes one achievement per call. Cap the mirror walk so a
        // corrupt count cannot turn the mirror into an out-of-bounds read; the
        // GFWL call itself has already happened with the caller's count.
        constexpr uint32_t kMaxMirrorBatch = 16;

        WriteAchievementsFn g_write_original = nullptr;
        SetAchievementFn g_set_achievement = nullptr;
        StoreStatsFn g_store_stats = nullptr;
        RequestStatsFn g_request_stats = nullptr;
        RunCallbacksFn g_run_callbacks = nullptr;
        void* g_user_stats = nullptr;

        // Guards the state shared by the game's achievement thread and the
        // Steam init worker. SRWLOCK needs no initialisation.
        SRWLOCK g_lock = SRWLOCK_INIT;
        bool g_ready = false;                       // Steam pointers are usable
        bool g_mirrored[kAchievementSlots] = {};    // already sent to Steam
        bool g_pending[kAchievementSlots] = {};     // wrote before Steam was up

        bool g_hook_installed = false;
        bool g_thread_started = false;
        bool g_disabled = false;

        const char* NameForId(uint32_t id) noexcept
        {
            for (const AchievementName& entry : kAchievementNames)
            {
                if (entry.id == id)
                    return entry.name;
            }
            return nullptr;
        }

        // A retail/GFWL-only install has no steam_api.dll. Treat that as "not
        // applicable" and do not load, initialise or hook anything.
        bool SteamApiFilePresent() noexcept
        {
            char module[MAX_PATH] = {};
            if (GetModuleFileNameA(nullptr, module, MAX_PATH) == 0)
                return false;
            char* slash = std::strrchr(module, '\\');
            if (slash == nullptr)
                return false;
            slash[1] = '\0';
            char path[MAX_PATH] = {};
            std::snprintf(path, sizeof(path), "%ssteam_api.dll", module);
            return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
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

        // Called with the lock released, only once g_ready is true.
        void ApplyToSteam(uint32_t id, const char* name) noexcept
        {
            if (g_set_achievement == nullptr)
                return;
            const bool set = g_set_achievement(g_user_stats, name);
            if (set && g_store_stats != nullptr)
                g_store_stats(g_user_stats);
            if (g_run_callbacks != nullptr)
                g_run_callbacks();
            Log("steam: achievement %u (%s): %s", id, name,
                set ? "unlocked" : "rejected by Steam");
        }

        // Runs on the game's achievement thread. Before Steam is ready the id is
        // queued, so a write during initialisation is not lost.
        void Observe(uint32_t id) noexcept
        {
            if (id > kMaxAchievementId)
                return;
            const char* name = NameForId(id);
            if (name == nullptr)
                return;

            bool mirror_now = false;
            bool queued = false;
            AcquireSRWLockExclusive(&g_lock);
            if (!g_mirrored[id])
            {
                if (g_ready)
                {
                    g_mirrored[id] = true;
                    mirror_now = true;
                }
                else if (!g_pending[id])
                {
                    g_pending[id] = true;
                    queued = true;
                }
            }
            ReleaseSRWLockExclusive(&g_lock);

            if (mirror_now)
                ApplyToSteam(id, name);
            else if (queued)
                Log("steam: achievement %u (%s) queued until Steam is ready", id,
                    name);
        }

        // Runs on the Steam init worker after the interface is resolved.
        void PublishReady() noexcept
        {
            uint32_t replay[kAchievementSlots] = {};
            uint32_t replay_count = 0;

            AcquireSRWLockExclusive(&g_lock);
            g_ready = true;
            for (uint32_t id = 0; id < kAchievementSlots; ++id)
            {
                if (!g_pending[id])
                    continue;
                g_pending[id] = false;
                g_mirrored[id] = true;
                replay[replay_count++] = id;
            }
            ReleaseSRWLockExclusive(&g_lock);

            for (uint32_t i = 0; i < replay_count; ++i)
                ApplyToSteam(replay[i], NameForId(replay[i]));
        }

        extern "C" uint32_t __stdcall XUserWriteAchievementsHook(
            uint32_t count, const XUserAchievement* achievements,
            void* overlapped) noexcept
        {
            const uint32_t result =
                g_write_original != nullptr
                    ? g_write_original(count, achievements, overlapped)
                    : 0;
            if (achievements != nullptr && count != 0)
            {
                uint32_t mirror = count;
                if (mirror > kMaxMirrorBatch)
                {
                    Log("steam: XUserWriteAchievements count=%u clamped to %u",
                        count, kMaxMirrorBatch);
                    mirror = kMaxMirrorBatch;
                }
                for (uint32_t i = 0; i < mirror; ++i)
                    Observe(achievements[i].achievementId);
            }
            return result;
        }
    } // namespace

    bool InstallSteamAchievementsBridge(GameImage image) noexcept
    {
        if (g_hook_installed)
            return true;
        if (g_disabled)
            return false;
        if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(image.base))
            return false;
        if (g_thread_started)
            return true;
        if (!SteamApiFilePresent())
        {
            // Retail/GFWL install: simply do not run the Steam path.
            g_disabled = true;
            Log("steam: not applicable (no steam_api.dll), skipped");
            return false;
        }

        // Hook the import now, from the install pass. SteamAPI_Init loads
        // libraries and starts threads, so it has to run outside the loader lock
        // (the worker below); installing the hook first means an achievement
        // written during that window is still observed and queued instead of
        // silently missed.
        if (!ReplaceImportOrdinal(GetModuleHandleW(nullptr), "xlive.dll", 5278,
                                  reinterpret_cast<void*>(&XUserWriteAchievementsHook),
                                  reinterpret_cast<void**>(&g_write_original)))
        {
            g_disabled = true;
            Log("steam: XUserWriteAchievements import not found, skipped");
            return false;
        }
        g_hook_installed = true;
        Log("steam: hooked xlive.dll ordinal 5278 (XUserWriteAchievements)");

        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(1000);
            (void)InstallSteamNow();
            return 0;
        }, nullptr, 0, nullptr);
        if (thread == nullptr)
        {
            Log("steam: could not start the Steam init worker");
            return false;
        }
        CloseHandle(thread);
        g_thread_started = true;
        return true;
    }

    bool InstallSteamNow() noexcept
    {
        if (g_ready)
            return true;
        if (g_disabled)
            return false;

        HMODULE steam = LoadLibraryA("steam_api.dll");
        if (steam == nullptr)
        {
            g_disabled = true;
            Log("steam: steam_api.dll could not be loaded, skipped");
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
        const auto is_running = reinterpret_cast<bool(__cdecl*)()>(
            GetProcAddress(steam, "SteamAPI_IsSteamRunning"));
        if (init == nullptr || get_user == nullptr || find_interface == nullptr)
        {
            g_disabled = true;
            Log("steam: required exports missing, skipped");
            return false;
        }
        if (is_running != nullptr && !is_running())
        {
            g_disabled = true;
            Log("steam: Steam client not running, skipped");
            return false;
        }
        if (!init())
        {
            g_disabled = true;
            Log("steam: SteamAPI_Init failed, skipped");
            return false;
        }

        g_user_stats =
            find_interface(get_user(), "STEAMUSERSTATS_INTERFACE_VERSION012");
        if (g_user_stats == nullptr)
        {
            g_disabled = true;
            Log("steam: ISteamUserStats v012 unavailable, skipped");
            return false;
        }
        auto* vtable = *reinterpret_cast<void***>(g_user_stats);
        if (vtable == nullptr)
        {
            g_disabled = true;
            Log("steam: ISteamUserStats vtable unavailable, skipped");
            return false;
        }
        // ISteamUserStats v012: RequestCurrentStats at 0, SetAchievement at 7
        // (28 bytes) and StoreStats at 10 (40 bytes).
        g_request_stats = reinterpret_cast<RequestStatsFn>(vtable[0 / 4]);
        g_set_achievement = reinterpret_cast<SetAchievementFn>(vtable[28 / 4]);
        g_store_stats = reinterpret_cast<StoreStatsFn>(vtable[40 / 4]);
        g_run_callbacks = run_callbacks;
        if (g_set_achievement == nullptr || g_store_stats == nullptr)
        {
            g_disabled = true;
            Log("steam: unexpected ISteamUserStats layout, skipped");
            return false;
        }

        // Load the user's stats, then let the first callbacks arrive before the
        // mirror accepts writes. Achievements written meanwhile stay queued.
        if (g_request_stats != nullptr)
            (void)g_request_stats(g_user_stats);
        if (g_run_callbacks != nullptr)
        {
            for (int i = 0; i < 10; ++i)
            {
                g_run_callbacks();
                Sleep(50);
            }
        }

        PublishReady();
        Log("steam: achievements bridge ready (GFWL preserved)");
        return true;
    }
} // namespace opp
