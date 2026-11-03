// SPDX-License-Identifier: GPL-3.0-or-later
//
// Steam achievement mirror, implemented independently of the game-code detours the
// standalone SteamAchievements.asi used.
//
// It hooks xlive.dll!XUserWriteAchievements (ordinal 5278, a 3-argument __stdcall) in the
// game's import table as soon as the install pass runs, calls the original first (so GFWL
// achievements and xliveless's local store keep working) and mirrors each achievement id
// to Steam's ISteamUserStats. Because the hook sits at the GFWL API boundary, game-code
// patches (TryToSkipAllErrors, VSyncFix) cannot move or overwrite the site.
//
// SteamAPI_Init runs on a worker thread after the loader lock is released; ids written
// before it finishes are queued and replayed, so no write is lost.

module;

#include "common.hxx"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

export module opp.steam;

import opp.common;
import opp.log;

export namespace opp
{
    // Installs the XUserWriteAchievements import hook and starts the Steam
    // initialisation worker. Returns false when steam_api.dll/Steam is
    // unavailable or the import is absent; the game then keeps its normal
    // GFWL-only behaviour.
    bool InstallSteamAchievementsBridge(GameImage image) noexcept;
} // namespace opp

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

namespace opp
{
    bool InstallSteamNow(int attempt) noexcept;

    namespace
    {
        bool IsEpisodesExecutable() noexcept
        {
            char module[MAX_PATH] = {};
            if (GetModuleFileNameA(nullptr, module, MAX_PATH) == 0)
                return false;
            const char* name = std::strrchr(module, '\\');
            name = name != nullptr ? name + 1 : module;
            return _strnicmp(name, "EFLC", 4) == 0;  // EFLC.exe vs GTAIV.exe
        }
        // GTA IV / EFLC achievement ids and their Steam API names. Ids 1..40 are
        // the base-game single-player achievements, 41..50 the multiplayer ones
        // (AWARD_ACHIEVEMENT indices; the script ACHIEVEMENT_IDS enum agrees),
        // 51..55 TLAD and 56..65 TBoGT. The same names appear in Complete
        // Edition's achievement blob and in the standalone SteamAchievements.asi.
        struct Achievement
        {
            uint32_t id;
            std::string_view name;
        };

        constexpr Achievement kAchievements[] = {
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
        // x64 has no __thiscall; the Steam interface methods are thiscall on x86
        // and the platform's default calling convention on x64.
#if defined(_M_IX86)
        using SetAchievementFn = bool(__thiscall*)(void*, const char*);
        using StoreStatsFn = bool(__thiscall*)(void*);
        using RequestStatsFn = bool(__thiscall*)(void*);
#else
        using SetAchievementFn = bool (*)(void*, const char*);
        using StoreStatsFn = bool (*)(void*);
        using RequestStatsFn = bool (*)(void*);
#endif
        using RunCallbacksFn = void(__cdecl*)();

        constexpr const char* kSteamAppIdVariable = "SteamAppId";
        constexpr const char* kSteamApiDll = "steam_api.dll";
        constexpr const char* kXliveDll = "xlive.dll";
        constexpr WORD kXUserWriteAchievementsOrdinal = 5278;
        constexpr size_t kAppIdCapacity = 32;

        constexpr uint32_t kAchievementSlots =
            kAchievements[std::size(kAchievements) - 1].id + 1;
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
        char g_app_id[kAppIdCapacity] = {};  // PrepareSteamAppId runs first

        bool g_hook_installed = false;
        bool g_thread_started = false;
        bool g_disabled = false;

        const Achievement* AchievementFor(uint32_t id) noexcept
        {
            for (const Achievement& achievement : kAchievements)
            {
                if (achievement.id == id)
                    return &achievement;
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
            std::snprintf(path, sizeof(path), "%s%s", module, kSteamApiDll);
            return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
        }

        // The system GFWL xlive.dll is packed; its ordinal-5278 export is a
        // protector thunk that uses the caller's return address as part of the
        // anti-tamper check. The IAT hook changes that return address (the call
        // now comes from this ASI), so the protector worker faults and takes the
        // game down. The local xliveless xlive.dll is a plain build and is the
        // supported environment for this hook; only hook that one.
        bool IsProtectedSystemXlive(HMODULE xlive) noexcept
        {
            if (xlive == nullptr)
                return false;
            char module_path[MAX_PATH] = {};
            if (GetModuleFileNameA(xlive, module_path, MAX_PATH) == 0)
                return false;
            char windows[MAX_PATH] = {};
            const UINT length = GetWindowsDirectoryA(windows, MAX_PATH);
            if (length == 0 || length >= MAX_PATH)
                return false;
            if (_strnicmp(module_path, windows, length) != 0)
                return false;
            return module_path[length] == '\\' || module_path[length] == '\0';
        }

        // SteamAPI_Init takes the AppID from the environment the Steam client
        // passes to the process it launched. When the game is started directly
        // (desktop shortcut to PlayGTAIV.exe) with the client running, that
        // variable is absent and Init fails even though Steam is up and the user
        // owns the game. Supply the product the executable belongs to.
        void PrepareSteamAppId() noexcept
        {
            static bool prepared = false;
            if (prepared)
                return;
            prepared = true;

            char inherited[kAppIdCapacity] = {};
            const DWORD length = GetEnvironmentVariableA(
                kSteamAppIdVariable, inherited, sizeof(inherited));
            if (length != 0)
            {
                std::snprintf(g_app_id, sizeof(g_app_id), "%s", inherited);
                Log("steam: inherited SteamAppId=%s", g_app_id);
                return;
            }

            // The base game's (12210) and Episodes' (12220) product ids; Steam
            // reads both the AppId and GameId names, so set both to the same id.
            std::snprintf(g_app_id, sizeof(g_app_id), "%s",
                          IsEpisodesExecutable() ? "12220" : "12210");
            SetEnvironmentVariableA(kSteamAppIdVariable, g_app_id);
            SetEnvironmentVariableA("SteamGameId", g_app_id);
            Log("steam: no inherited SteamAppId, using %s", g_app_id);
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

        // The GFWL hook may fire before Steam is initialized; this class owns
        // the queue, the "already sent" set and the one lock they share.
        class AchievementMirror final
        {
        public:
            bool Ready() const noexcept
            {
                return m_ready.load(std::memory_order_acquire);
            }

            // Runs on the game's achievement thread.
            void Observe(uint32_t id)
            {
                const Achievement* achievement = AchievementFor(id);
                if (achievement == nullptr)
                {
                    Log("steam: achievement id %u has no Steam name, ignored", id);
                    return;
                }

                bool apply_now = false;
                {
                    std::scoped_lock lock(m_mutex);
                    State& state = m_states[id];
                    if (state != State::idle)
                        return;
                    state = m_ready.load(std::memory_order_relaxed) ? State::sent
                                                                   : State::pending;
                    apply_now = state == State::sent;
                }
                if (apply_now)
                    Apply(id, achievement->name);
                else
                    Log("steam: achievement %u (%.*s) queued until Steam is ready", id,
                        static_cast<int>(achievement->name.size()),
                        achievement->name.data());
            }

            // Runs on the Steam init worker after the interface is resolved.
            void PublishReady()
            {
                std::array<uint32_t, kAchievementSlots> replay{};
                size_t count = 0;
                {
                    std::scoped_lock lock(m_mutex);
                    m_ready.store(true, std::memory_order_release);
                    for (uint32_t id = 0; id < kAchievementSlots; ++id)
                    {
                        if (m_states[id] != State::pending)
                            continue;
                        m_states[id] = State::sent;
                        replay[count++] = id;
                    }
                }
                for (size_t i = 0; i < count; ++i)
                    Apply(replay[i], AchievementFor(replay[i])->name);
            }

        private:
            enum class State : uint8_t
            {
                idle,
                pending,
                sent,
            };

            void Apply(uint32_t id, std::string_view name)
            {
                if (g_set_achievement == nullptr)
                    return;
                const bool set = g_set_achievement(g_user_stats, name.data());
                if (set && g_store_stats != nullptr)
                    g_store_stats(g_user_stats);
                if (g_run_callbacks != nullptr)
                    g_run_callbacks();
                Log("steam: achievement %u (%.*s): %s", id,
                    static_cast<int>(name.size()), name.data(),
                    set ? "unlocked" : "rejected by Steam");
            }

            std::mutex m_mutex;
            std::atomic<bool> m_ready{false};
            std::array<State, kAchievementSlots> m_states{};
        };

        AchievementMirror g_mirror;

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
                Log("steam: XUserWriteAchievements count=%u first id=%u", count,
                    achievements[0].achievementId);
                for (uint32_t i = 0; i < mirror; ++i)
                    g_mirror.Observe(achievements[i].achievementId);
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
        if (IsProtectedSystemXlive(GetModuleHandleA(kXliveDll)))
        {
            // The IAT hook is unsafe against the packed system GFWL xlive: its
            // export is a protector thunk, and the hook's return address trips
            // it. The local xliveless build is the supported environment.
            g_disabled = true;
            Log("steam: protected system xlive.dll, IAT hook skipped "
                "(local xliveless is required for the Steam mirror)");
            return false;
        }

        // Hook the import now, from the install pass. SteamAPI_Init loads
        // libraries and starts threads, so it has to run outside the loader lock
        // (the worker below); installing the hook first means an achievement
        // written during that window is still observed and queued instead of
        // silently missed.
        if (!ReplaceImportOrdinal(GetModuleHandleW(nullptr), kXliveDll,
                                  kXUserWriteAchievementsOrdinal,
                                  reinterpret_cast<void*>(&XUserWriteAchievementsHook),
                                  reinterpret_cast<void**>(&g_write_original)))
        {
            g_disabled = true;
            Log("steam: XUserWriteAchievements import not found, skipped");
            return false;
        }
        g_hook_installed = true;
        Log("steam: hooked xlive.dll ordinal %u (XUserWriteAchievements)",
            kXUserWriteAchievementsOrdinal);

        // The worker's retry policy: a first one-second settle, then two-second
        // retries until the attempt budget is spent.
        constexpr int kSteamInitAttempts = 20;
        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            // Give the client and the game a moment to settle, then retry while
            // the failure is transient (Steam still starting, the launch not yet
            // registered). Achievement ids seen meanwhile stay queued and are
            // replayed when the mirror finally comes up.
            for (int attempt = 0; attempt < kSteamInitAttempts; ++attempt)
            {
                Sleep(attempt == 0 ? 1000 : 2000);
                if (InstallSteamNow(attempt))
                    return 0;
                if (g_disabled)
                    return 0;
            }
            Log("steam: not ready after %d attempts; Steam mirror off this session "
                "(GFWL unaffected)", kSteamInitAttempts);
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

    namespace
    {
        struct SteamApi final
        {
            HMODULE module{};
            bool (__cdecl* init)(){};
            int (__cdecl* get_user)(){};
            void* (__cdecl* find_interface)(int, const char*){};
            RunCallbacksFn run_callbacks{};
            bool (__cdecl* is_running)(){};

            bool Resolve() noexcept
            {
                module = LoadLibraryA(kSteamApiDll);
                if (module == nullptr)
                {
                    g_disabled = true;
                    Log("steam: steam_api.dll could not be loaded, skipped");
                    return false;
                }
                init = reinterpret_cast<bool(__cdecl*)()>(
                    GetProcAddress(module, "SteamAPI_Init"));
                get_user = reinterpret_cast<int(__cdecl*)()>(
                    GetProcAddress(module, "SteamAPI_GetHSteamUser"));
                find_interface = reinterpret_cast<void*(__cdecl*)(int, const char*)>(
                    GetProcAddress(module, "SteamInternal_FindOrCreateUserInterface"));
                run_callbacks = reinterpret_cast<RunCallbacksFn>(
                    GetProcAddress(module, "SteamAPI_RunCallbacks"));
                is_running = reinterpret_cast<bool(__cdecl*)()>(
                    GetProcAddress(module, "SteamAPI_IsSteamRunning"));
                if (init == nullptr || get_user == nullptr || find_interface == nullptr)
                {
                    g_disabled = true;
                    Log("steam: required exports missing, skipped");
                    return false;
                }
                return true;
            }
        };

        bool OpenUserStats(const SteamApi& api) noexcept
        {
            g_user_stats = api.find_interface(
                api.get_user(), "STEAMUSERSTATS_INTERFACE_VERSION012");
            if (g_user_stats == nullptr)
            {
                g_disabled = true;
                Log("steam: ISteamUserStats v012 unavailable, skipped");
                return false;
            }
            const void* const* vtable =
                *reinterpret_cast<const void* const* const*>(g_user_stats);
            if (vtable == nullptr)
            {
                g_disabled = true;
                Log("steam: ISteamUserStats vtable unavailable, skipped");
                return false;
            }
            // ISteamUserStats v012 vtable indices (SDK order, not byte offsets).
            g_request_stats = reinterpret_cast<RequestStatsFn>(vtable[0]);
            g_set_achievement = reinterpret_cast<SetAchievementFn>(vtable[7]);
            g_store_stats = reinterpret_cast<StoreStatsFn>(vtable[10]);
            if (g_set_achievement == nullptr || g_store_stats == nullptr)
            {
                g_disabled = true;
                Log("steam: unexpected ISteamUserStats layout, skipped");
                return false;
            }
            return true;
        }

        void WarmUpUserStats() noexcept
        {
            // Request the stats, then let the first callbacks arrive before the
            // mirror accepts writes. Writes meanwhile stay queued.
            if (g_request_stats != nullptr)
                (void)g_request_stats(g_user_stats);
            if (g_run_callbacks == nullptr)
                return;
            for (int i = 0; i < 10; ++i)
            {
                g_run_callbacks();
                Sleep(50);
            }
        }
    }  // namespace

    bool InstallSteamNow(int attempt) noexcept
    {
        if (g_mirror.Ready())
            return true;
        if (g_disabled)
            return false;

        PrepareSteamAppId();

        SteamApi api;
        if (!api.Resolve())
            return false;
        if (api.is_running != nullptr && !api.is_running())
        {
            Log("steam: Steam client not running yet (attempt %d)", attempt + 1);
            return false;
        }
        if (!api.init())
        {
            Log("steam: SteamAPI_Init failed (attempt %d, appid %s)", attempt + 1,
                g_app_id);
            return false;
        }
        if (!OpenUserStats(api))
            return false;

        g_run_callbacks = api.run_callbacks;
        WarmUpUserStats();
        g_mirror.PublishReady();
        Log("steam: achievements bridge ready (GFWL preserved)");
        return true;
    }
} // namespace opp