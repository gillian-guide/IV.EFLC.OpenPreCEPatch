module;

#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>

export module steam;

import common;

// The Steam achievement mirror, implemented at the GFWL API boundary.
//
// GTA IV imports xlive.dll!XUserWriteAchievements by ordinal 5278 (a
// three-argument __stdcall). Replacing that import slot and calling the original
// first keeps the GFWL/xliveless achievement store working, while each id is
// mirrored to Steam's ISteamUserStats. Because the hook is at the import, no
// game-code patch (TryToSkipAllErrors, VSyncFix) can move or overwrite it.
//
// The hook is installed synchronously in the install pass. SteamAPI_Init loads
// libraries and starts threads, so it runs lazily on the first achievement write
// (a game thread), never in DllMain; ids observed before it succeeds stay queued
// and are replayed when it does.
//
// The achievement ids/names and the ISteamUserStats v012 method indices are
// recovered data, used with a provenance comment; every executable line here is
// written for this tree.

export namespace Steam
{
    // Installs the import hook. Answers false when the module is not the game
    // executable, steam_api.dll is absent, xlive is the packed system build, or
    // the ordinal is not imported; the game then keeps its GFWL-only behaviour.
    bool Install() noexcept;

    // The id -> Steam name mapping, exposed for the native test.
    const char* AchievementNameForTest(uint32_t id) noexcept;
}

namespace
{
    struct Achievement
    {
        uint32_t id;
        const char* name;
    };

    // GTA IV / EFLC achievement ids and their Steam API names: ids 1..40 are the
    // base game's single-player achievements, 41..50 the multiplayer ones,
    // 51..55 TLAD and 56..65 TBoGT (the script ACHIEVEMENT_IDS enum and the
    // standalone SteamAchievements.asi agree).
    constexpr Achievement kAchievements[] = {
        { 1, "THETOUR" }, { 2, "SCOREDARTGAME" }, { 3, "POOLWIN" }, { 4, "QUBBEAT" },
        { 5, "MELEECOUNTER" }, { 6, "STREETRACE" }, { 7, "BIKEWHEELIE" }, { 8, "TURKEY" },
        { 9, "SATAXI" }, { 10, "CARROLLS" }, { 11, "LOSERATING" }, { 12, "DRUGDONE" },
        { 13, "SAGUNCAR" }, { 14, "CHAINREACTION" }, { 15, "WANTEDLEVEL6STARSURVIVAL" },
        { 16, "KEYMISSION1" }, { 17, "IMPORTDONE" }, { 18, "WANTEDDONE" },
        { 19, "VIGILDONE" }, { 20, "KEYMISSION0" }, { 21, "TAXIPASSANGER" },
        { 22, "ALLHELITOURTAKEN" }, { 23, "DATING" }, { 24, "SAHELI" },
        { 25, "HALFMILLION" }, { 26, "KEYMISSION2" }, { 27, "OPENEDALDERNEY" },
        { 28, "SMSTHEFTDONE" }, { 29, "USJDONE" }, { 30, "ASSASSIN" },
        { 31, "HIDDENPACKAGES" }, { 32, "UNDERALLBRIDGES" }, { 33, "SABOMBCAR" },
        { 34, "KEYMISSION3" }, { 35, "FRIENDSLIKEYOU" }, { 36, "METRANDOM" },
        { 37, "KEYMISSION4" }, { 38, "STORYDONE" }, { 39, "STREETLIGHTSDOWN" },
        { 40, "GAMECOMPLETION100" }, { 41, "MPTEAMWIN1" }, { 42, "MPCOMPWIN1" },
        { 43, "MPRACEWIN1" }, { 44, "MPCOOPWIN1" }, { 45, "MPTEAMWINALL" },
        { 46, "MPCOMPWINALL" }, { 47, "MPRACEWINALL" }, { 48, "MPSTARRATING" },
        { 49, "MPCOOPWINALL" }, { 50, "VIRALKILLME" }, { 51, "E1INTRO" },
        { 52, "E1BECOMELEADER" }, { 53, "E1FINISHSTORY" }, { 54, "E1KNOCKOFF" },
        { 55, "E1BUDDYSTAT" }, { 56, "E2BASEJUMP" }, { 57, "E2TRINITY" },
        { 58, "E2GOLF" }, { 59, "E2FIGHTC" }, { 60, "E2DANCE" }, { 61, "E2DRUGS" },
        { 62, "E2FREEFALL" }, { 63, "E2STORY" }, { 64, "E2FAIRFLAIR" },
        { 65, "E2ALLFLAIR" },
    };

    constexpr uint32_t kMaxAchievementId = 65;
    constexpr uint32_t kMaxMirrorBatch = 16;
    constexpr WORD kWriteAchievementsOrdinal = 5278;

    struct XUserAchievement
    {
        uint32_t userIndex;
        uint32_t achievementId;
    };

    using WriteAchievementsFn = uint32_t(__stdcall*)(uint32_t, const XUserAchievement*, void*);
    using SetAchievementFn = bool(__thiscall*)(void*, const char*);
    using StoreStatsFn = bool(__thiscall*)(void*);
    using RequestStatsFn = bool(__thiscall*)(void*);
    using RunCallbacksFn = void(__cdecl*)();

    WriteAchievementsFn g_original = nullptr;
    SetAchievementFn g_set_achievement = nullptr;
    StoreStatsFn g_store_stats = nullptr;
    RequestStatsFn g_request_stats = nullptr;
    RunCallbacksFn g_run_callbacks = nullptr;
    void* g_user_stats = nullptr;

    std::mutex g_mutex;
    std::mutex g_init_mutex;
    bool g_hook_installed = false;
    std::atomic<bool> g_disabled{ false };
    std::atomic<bool> g_ready{ false };
    bool g_pending[kMaxAchievementId + 1] = {};
    bool g_sent[kMaxAchievementId + 1] = {};

    const Achievement* AchievementFor(uint32_t id) noexcept
    {
        for (const auto& achievement : kAchievements)
        {
            if (achievement.id == id)
                return &achievement;
        }
        return nullptr;
    }

    bool EpisodesExecutable() noexcept
    {
        char module[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, module, MAX_PATH) == 0)
            return false;
        const char* name = std::strrchr(module, '\\');
        name = name != nullptr ? name + 1 : module;
        return _strnicmp(name, "EFLC", 4) == 0;
    }

    bool SteamApiPresent() noexcept
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

    // The packed system xlive's ordinal-5278 export uses the caller's return
    // address in its anti-tamper check; an IAT hook changes it. Only the plain
    // local xliveless build is a supported target.
    bool ProtectedSystemXlive() noexcept
    {
        const HMODULE xlive = GetModuleHandleA("xlive.dll");
        if (xlive == nullptr)
            return false;
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(xlive, path, MAX_PATH) == 0)
            return false;
        char windows[MAX_PATH] = {};
        const UINT length = GetWindowsDirectoryA(windows, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;
        if (_strnicmp(path, windows, length) != 0)
            return false;
        return path[length] == '\\' || path[length] == '\0';
    }

    void PrepareAppId() noexcept
    {
        char inherited[32] = {};
        const DWORD length = GetEnvironmentVariableA("SteamAppId", inherited, sizeof(inherited));
        if (length != 0)
            return;
        const char* app = EpisodesExecutable() ? "12220" : "12210";
        SetEnvironmentVariableA("SteamAppId", app);
        SetEnvironmentVariableA("SteamGameId", app);
    }

    bool ReplaceImportOrdinal(HMODULE module, const char* library, WORD ordinal,
        void* replacement, void** original) noexcept
    {
        const auto* base = reinterpret_cast<const uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0)
            return false;

        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            const_cast<uint8_t*>(base) + directory.VirtualAddress);
        for (; descriptor->Name != 0; ++descriptor)
        {
            if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), library) != 0)
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

                DWORD previous = 0;
                if (!VirtualProtect(&slot->u1.Function, sizeof(void*), PAGE_READWRITE, &previous))
                    return false;
                if (original != nullptr)
                    *original = reinterpret_cast<void*>(slot->u1.Function);
                slot->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                VirtualProtect(&slot->u1.Function, sizeof(void*), previous, &previous);
                return true;
            }
        }
        return false;
    }

    bool TryInitialise() noexcept
    {
        std::lock_guard initLock(g_init_mutex);
        if (g_ready.load(std::memory_order_acquire))
            return true;
        if (g_disabled.load(std::memory_order_relaxed))
            return false;

        PrepareAppId();

        const HMODULE api = LoadLibraryA("steam_api.dll");
        if (api == nullptr)
        {
            g_disabled = true;
            return false;
        }

        using InitFn = bool(__cdecl*)();
        using GetUserFn = int(__cdecl*)();
        using FindInterfaceFn = void*(__cdecl*)(int, const char*);
        using IsRunningFn = bool(__cdecl*)();

        const auto init = reinterpret_cast<InitFn>(GetProcAddress(api, "SteamAPI_Init"));
        const auto get_user = reinterpret_cast<GetUserFn>(GetProcAddress(api, "SteamAPI_GetHSteamUser"));
        const auto find_interface = reinterpret_cast<FindInterfaceFn>(
            GetProcAddress(api, "SteamInternal_FindOrCreateUserInterface"));
        const auto is_running = reinterpret_cast<IsRunningFn>(GetProcAddress(api, "SteamAPI_IsSteamRunning"));
        g_run_callbacks = reinterpret_cast<RunCallbacksFn>(GetProcAddress(api, "SteamAPI_RunCallbacks"));
        if (init == nullptr || get_user == nullptr || find_interface == nullptr)
        {
            g_disabled = true;
            return false;
        }
        if (is_running != nullptr && !is_running())
            return false;
        if (!init())
            return false;

        g_user_stats = find_interface(get_user(), "STEAMUSERSTATS_INTERFACE_VERSION012");
        if (g_user_stats == nullptr)
        {
            g_disabled = true;
            return false;
        }
        const void* const* vtable = *reinterpret_cast<const void* const* const*>(g_user_stats);
        if (vtable == nullptr)
        {
            g_disabled = true;
            return false;
        }
        g_request_stats = reinterpret_cast<RequestStatsFn>(vtable[0]);
        g_set_achievement = reinterpret_cast<SetAchievementFn>(vtable[7]);
        g_store_stats = reinterpret_cast<StoreStatsFn>(vtable[10]);
        if (g_set_achievement == nullptr || g_store_stats == nullptr)
        {
            g_disabled = true;
            return false;
        }

        if (g_request_stats != nullptr)
            (void)g_request_stats(g_user_stats);
        for (int i = 0; i < 10 && g_run_callbacks != nullptr; ++i)
        {
            g_run_callbacks();
            Sleep(50);
        }
        g_ready.store(true, std::memory_order_release);
        return true;
    }

    void Apply(uint32_t id, const char* name) noexcept
    {
        if (g_set_achievement == nullptr)
            return;
        const bool set = g_set_achievement(g_user_stats, name);
        if (set && g_store_stats != nullptr)
            g_store_stats(g_user_stats);
        if (g_run_callbacks != nullptr)
            g_run_callbacks();
        Log::Print("steam: achievement {} ({}) {}", id, name,
            set ? "unlocked" : "rejected by Steam");
    }

    void Observe(uint32_t id) noexcept
    {
        const Achievement* achievement = AchievementFor(id);
        if (achievement == nullptr)
        {
            Log::Print("steam: achievement id {} has no Steam name, ignored", id);
            return;
        }

        bool need_init = false;
        {
            std::lock_guard lock(g_mutex);
            if (g_sent[id])
                return;
            g_pending[id] = true;
            need_init = !g_ready.load(std::memory_order_acquire);
        }

        if (need_init)
        {
            // Initialisation is deliberately outside the state mutex: it loads
            // steam_api.dll and pumps callbacks, and a callback can reach this
            // hook on the same thread.
            (void)TryInitialise();
            std::lock_guard lock(g_mutex);
            if (!g_ready.load(std::memory_order_acquire))
            {
                Log::Print("steam: achievement {} ({}) queued until Steam is ready", id,
                    achievement->name);
                return;
            }
        }

        uint32_t send[kMaxAchievementId + 1] = {};
        size_t send_count = 0;
        {
            std::lock_guard lock(g_mutex);
            if (!g_ready.load(std::memory_order_acquire))
                return;
            for (uint32_t queued = 1; queued <= kMaxAchievementId; ++queued)
            {
                if (g_pending[queued] && !g_sent[queued])
                {
                    g_sent[queued] = true;
                    g_pending[queued] = false;
                    send[send_count++] = queued;
                }
            }
        }

        // The Steam calls run without the state mutex; the function pointers are
        // fixed once g_ready was published.
        for (size_t index = 0; index < send_count; ++index)
        {
            const Achievement* queued = AchievementFor(send[index]);
            if (queued != nullptr)
                Apply(send[index], queued->name);
        }
    }

    extern "C" uint32_t __stdcall WriteAchievementsHook(
        uint32_t count, const XUserAchievement* achievements, void* overlapped) noexcept
    {
        const uint32_t result =
            g_original != nullptr ? g_original(count, achievements, overlapped) : 0;

        if (achievements != nullptr && count != 0)
        {
            const uint32_t mirror = count > kMaxMirrorBatch ? kMaxMirrorBatch : count;
            for (uint32_t index = 0; index < mirror; ++index)
                Observe(achievements[index].achievementId);
        }
        return result;
    }
}

namespace Steam
{
    bool Install() noexcept
    {
        if (g_hook_installed)
            return true;
        if (g_disabled)
            return false;

        // The offline dump maps a copy of the image; only the real game process
        // owns the import table this hook rewrites.
        if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != Game::Get().ImageBase())
            return false;
        if (!SteamApiPresent())
        {
            g_disabled = true;
            Log::Print("steam: no steam_api.dll, skipped");
            return false;
        }
        if (ProtectedSystemXlive())
        {
            g_disabled = true;
            Log::Print("steam: packed system xlive.dll, IAT hook skipped");
            return false;
        }

        void* original = nullptr;
        if (!ReplaceImportOrdinal(GetModuleHandleW(nullptr), "xlive.dll",
                kWriteAchievementsOrdinal, reinterpret_cast<void*>(&WriteAchievementsHook),
                &original))
        {
            g_disabled = true;
            Log::Print("steam: XUserWriteAchievements ordinal {} not imported, skipped",
                kWriteAchievementsOrdinal);
            return false;
        }
        g_original = reinterpret_cast<WriteAchievementsFn>(original);
        g_hook_installed = true;
        Log::Print("steam: hooked xlive.dll ordinal {}", kWriteAchievementsOrdinal);
        return true;
    }

    const char* AchievementNameForTest(uint32_t id) noexcept
    {
        const Achievement* achievement = AchievementFor(id);
        return achievement != nullptr ? achievement->name : nullptr;
    }
}
