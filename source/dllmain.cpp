// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenPreCEPatch entry point. Everything runs synchronously at process attach:
// the DRM has already rewritten the game's import-time code by the time a
// deferred worker would get to scan, so signatures have to be matched now.

#include "common.hxx"
#include "game.hxx"
#include "hooks.h"
#include "fusionfix_compat.hxx"
#include "ini_import.hxx"
#include "log.hxx"
#include "menu.hxx"
#include "patches.hxx"
#include "pattern.hxx"
#include "rebased.h"
#include "vehicle_lights.hxx"
#include "skip_launcher.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

import common;
import settings;

namespace
{
    constexpr uint64_t kOldDsoundLoaderSize = 91648ull;
    constexpr uint32_t kUnsupportedVersionMin = 1201;
    constexpr uint32_t kUnsupportedVersionMax = 12999;
    constexpr DWORD kUnsupportedSleepMs = 500;
    constexpr UINT kUnsupportedExitCode = 0x8000FFFFu;

    struct LauncherPatch
    {
        opp::GameBuild build;
        uint32_t first;
        uint32_t second;
    };

    constexpr LauncherPatch kLauncherPatches[] = {
        {opp::GameBuild::v1000, 10421308u, 10025484u},
        {opp::GameBuild::v1040, 10396036u, 10344608u},
        {opp::GameBuild::v1060, 10456896u, 11129048u},
        {opp::GameBuild::v1070, 10461040u, 11133208u},
        {opp::GameBuild::v1080, 10815736u, 10855908u},
    };

    // "PlayGTAIV.exe" followed by the two zero bytes the original leaves in place.
    constexpr char kLauncherNamePatch[16] = {'p', 'l', 'a', 'y', 'G', 'T', 'A', 'I',
                                             'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};

    std::filesystem::path modulePath;
    std::filesystem::path moduleFile;
    std::filesystem::path configPath;
    HMODULE hSelf = nullptr;
    bool bDFAInstalled = false;
    std::once_flag g_once;

    bool IsSameModule(HMODULE module, HMODULE self) noexcept
    {
        return module != nullptr && module == self;
    }

    bool IsCompatShim(HMODULE module) noexcept
    {
        return module != nullptr && GetProcAddress(module, "OpcepCompatShim") != nullptr;
    }

    std::filesystem::path ModuleDirectory(HMODULE module)
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};
        return std::filesystem::path(buffer).parent_path();
    }

    void CheckGameVersion(uint32_t compactVersion)
    {
        if (const HMODULE dsound = GetModuleHandleW(L"dsound.dll"))
        {
            wchar_t path[MAX_PATH]{};
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetModuleFileNameW(dsound, path, MAX_PATH) &&
                GetFileAttributesExW(path, GetFileExInfoStandard, &info))
            {
                const uint64_t size =
                    (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
                if (size == kOldDsoundLoaderSize)
                {
                    MessageBoxA(0,
                                "Old dsound.dll detected! It's strongly recommended to use Ultimate ASI "
                                "Loader as either dinput8.dll or xlive.dll to load this mod.",
                                "OpenPreCEPatch Warning", 0);
                }
            }
        }

        if (compactVersion >= kUnsupportedVersionMin &&
            compactVersion <= kUnsupportedVersionMax)
        {
            std::system("start https://zolika1351.pages.dev/mods/ivpatch/downgrading");
            Sleep(kUnsupportedSleepMs);
            TerminateProcess(GetCurrentProcess(), kUnsupportedExitCode);
        }
    }

    // The three modules the option arms consult. A module named XLivelessAddon.asi
    // that is this DLL, or our compatibility shim, is self, not an external addon.
    void CaptureModules(HMODULE self)
    {
        zpatch_xlive_base = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(GetModuleHandleA("xlive.dll")));

        const HMODULE xliveless = GetModuleHandleA("XLivelessAddon.asi");
        zpatch_xliveless_present =
            (xliveless != nullptr && !IsSameModule(xliveless, self) && !IsCompatShim(xliveless))
                ? 1u
                : 0u;
        zpatch_citizen_present = GetModuleHandleA("CitizenIV.exe") ? 1u : 0u;
    }

    bool IsDFAAllowed(opp::GameBuild build)
    {
        if (zpatch_xliveless_present)
            return false;
        if (GetModuleHandleA("GTAC.dll"))
            return false;

        switch (build)
        {
        case opp::GameBuild::v1100:
        case opp::GameBuild::v1110:
        case opp::GameBuild::v1120:
        case opp::GameBuild::v1130:
            return false;
        default:
            return true;
        }
    }

    bool XlivelessBundleEnabled(const opp::Config& config)
    {
        // Legacy master switch: an existing ZolikaPatch.ini keeps its behaviour.
        if (config.Has("Options", "MiscFixes"))
            return config.Option("MiscFixes", "1");

        return config.Option("RemoveDataIntegrityChecks", "1") ||
               config.Option("SkipTokenActivation", "1") ||
               config.Option("SkipFirstTimePlayAsk", "1") ||
               config.Option("SkipInternetConnectionCheck", "1") ||
               config.Option("SkipHealthCheck", "1");
    }

    void ApplyDFA(opp::GameBuild build, const opp::GameImage& image)
    {
        if (!IsDFAAllowed(build) || bDFAInstalled)
            return;

        bDFAInstalled = true;
        SkipLauncherReport report;
        (void)ApplySkipLauncher(image, &ResolveSkipHook, nullptr, report,
                                      SkipLauncherStage::dfa);
        SetCreateFileSlot(report.createFileSlot);
    }

    SkipLauncherReport ApplySkipLauncherIfEnabled(opp::GameBuild build,
                                                        const opp::Config& config)
    {
        SkipLauncherReport report;
        if (!config.Option("SkipLauncher", "1"))
            return report;

        auto* module = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
        if (!module)
            return report;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return report;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return report;

        SkipLauncherStage stages = SkipLauncherStage::rom;
        if (!zpatch_xliveless_present)
            stages = stages | SkipLauncherStage::main;
        if (IsDFAAllowed(build) && !bDFAInstalled)
        {
            stages = stages | SkipLauncherStage::dfa;
            bDFAInstalled = true;
        }

        const opp::GameImage image{module, nt->OptionalHeader.SizeOfImage};
        (void)ApplySkipLauncher(image, &ResolveSkipHook, nullptr, report, stages);

        if (report.createFileSlot)
            SetCreateFileSlot(report.createFileSlot);
        return report;
    }

    bool IsMenuIntegrationAvailable(const opp::Config& config)
    {
        if (!config.Option("AddOptionsInMenu", "1"))
            return false;
        if (GetModuleHandleA("IVMenuAPI.asi"))
            return true;

        static bool loadFailed = false;
        if (loadFailed)
            return false;
        if (LoadLibraryA("IVMenuAPI.asi"))
            return true;

        loadFailed = true;
        return false;
    }

    void EnforceZMenuVersion()
    {
        uint32_t out = 0;
        if (opp::ZMenuVersionGuard(out) != opp::ZMenuGuardResult::version_rejected)
            return;

        MessageBoxA(nullptr, opp::kZMenuRejectText, opp::kZMenuRejectCaption,
                    opp::kZMenuRejectFlags);
        TerminateProcess(GetCurrentProcess(), opp::kZMenuTerminateExitCode);
    }

    void ApplyLaunchWithPlayGTAIV(opp::GameBuild build, uint8_t* moduleBase,
                                  const opp::Config& config)
    {
        if (!config.Option("LaunchWithPlayGTAIV", "0"))
            return;
        if (opp::FindGameProcess() == 0)
            return;

        uint32_t sites[2]{};
        for (const LauncherPatch& patch : kLauncherPatches)
        {
            if (patch.build == build)
            {
                sites[0] = patch.first;
                sites[1] = patch.second;
                break;
            }
        }
        if (!sites[0])
            return;

        for (const uint32_t site : sites)
        {
            injector::WriteMemoryRaw(moduleBase + site,
                                     const_cast<char*>(kLauncherNamePatch),
                                     sizeof(kLauncherNamePatch), true);
        }
    }

    void LoadVehicleTables(const opp::Config& config)
    {
        opp::VehicleLightTables tables;
        if (!opp::LoadVehicleLightTables(config, tables))
        {
            opp::Log("config: vehicle light tables rejected");
            return;
        }

        for (size_t i = 0; i < tables.popupAngles.size(); ++i)
        {
            const opp::VehicleLightValue& popup = tables.popupAngles[i];
            zpatch_popup_hashes[i] = popup.populated ? popup.modelHash : 0u;
            zpatch_popup_angles[i] = popup.populated ? popup.value : 0.0f;

            const opp::VehicleLightValue& offset = tables.headlightOffsets[i];
            zpatch_fchs_hashes[i] = offset.populated ? offset.modelHash : 0u;
            zpatch_fchs_offsets[i] = offset.populated ? offset.value : 0.0f;
        }
    }

    void LoadFlags(const opp::Config& config)
    {
        LoadVehicleTables(config);

        if (config.Option("RestoreDeathMusic", "0"))
            zpatch_flag_death_music = 1u;
        if (config.Option("Patch4RemoveModChecks", "0"))
            zpatch_flag_patch4_modchecks = 1u;
        if (config.Option("RemoveGFWLUpdatePopup", "0"))
            zpatch_flag_gfwl_popup = 1u;
        if (config.Option("FreezeCarFix", "1"))
        {
            if (config.Option("DebugPrintSpawns", "0"))
                zpatch_flag_debug_spawns = 1u;
        }
        if (config.Option("DoNotPauseOnMinimize", "0"))
            zpatch_flag_donotpause = 1u;
        if (config.Option("MovingSteeringWheelSupport", "1"))
            zpatch_flag_steering = 1u;
        if (config.Option("MovingPlaneParts", "1"))
            zpatch_flag_plane = 1u;
        if (config.Option("ShakingEngineBlock", "1"))
            zpatch_flag_engine = 1u;

        const std::string dat = config.String("", "OverrideGTADatPathTo",
                                              "common:/data/gta.dat");
        SetOverrideGtaDatPath(dat);
    }

    // FusionFix gates overlapping options silently, which can look like the mod
    // ignored the INI. Show the enabled-but-gated set once per install (a marker
    // file next to the ASI) and log it on every run.
    void NotifyFusionFixGatedOptions(const opp::Config& config)
    {
        if (!opp::FusionFixPresent())
            return;

        size_t count = 0;
        std::string names;
        const opp::FusionFixConflict* conflicts = opp::FusionFixConflicts(count);
        for (size_t i = 0; i < count; ++i)
        {
            bool enabled = false;
            try
            {
                enabled = config.RawOption(conflicts[i].option, "0");
            }
            catch (...)
            {
                continue;
            }

            if (!enabled)
                continue;

            if (!names.empty())
                names += ", ";
            names += conflicts[i].option;
        }

        if (names.empty())
            return;

        opp::Log("compat: enabled but disabled by FusionFix: %s", names.c_str());

        const std::filesystem::path marker =
            configPath.parent_path() / L"IV.EFLC.OpenPreCEPatch.fusionfix-notice";
        if (std::filesystem::exists(marker))
            return;

        const std::string text =
            "FusionFix is installed. These OpenPreCEPatch options are enabled in "
            "IV.EFLC.OpenPreCEPatch.ini but will not be active:\n\n" +
            names +
            "\n\nThis notice is shown once. Delete "
            "IV.EFLC.OpenPreCEPatch.fusionfix-notice to show it again.";
        MessageBoxA(nullptr, text.c_str(), "OpenPreCEPatch - FusionFix compatibility",
                    MB_OK | MB_ICONINFORMATION);

        std::ofstream notice(marker, std::ios::binary | std::ios::trunc);
        if (notice)
            notice << "FusionFix compatibility notice shown.\r\n";
    }

    void SetStuff(HMODULE module)
    {
        configPath = opp::EnsureConfigFile(ModuleDirectory(module));
        if (configPath.empty())
        {
            opp::Log("config: no IV.EFLC.OpenPreCEPatch.ini or ZolikaPatch.ini next to the ASI");
            return;
        }

        opp::Config config(configPath);
        if (!config.loaded())
        {
            opp::Log("config: cannot read %s", configPath.string().c_str());
            return;
        }

        opp::Log("config: %s", configPath.string().c_str());
        NotifyFusionFixGatedOptions(config);
        LoadFlags(config);

        // The ZMenuIV guard follows config-object construction and is
        // unconditional, so a failed load must not suppress it.
        EnforceZMenuVersion();

        uint32_t compact = 0;
        opp::GameBuild build = opp::GameBuild::unknown;
        (void)opp::QueryGameVersion(compact, build);
        opp::Log("build: %s", opp::GameBuildName(build));

        auto* moduleBase = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                             ? reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew)
                             : nullptr;

        // The XLivelessAddon loader/DFA stages belong to the separate
        // XLivelessAddon successor module; this module does not install them.

        if (build == opp::GameBuild::unknown)
        {
            opp::Log("build: unknown, installs skipped");
            return;
        }
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
            return;

        const opp::GameImage gameImage{moduleBase, nt->OptionalHeader.SizeOfImage};

        const bool menu = IsMenuIntegrationAvailable(config);
        opp::SetMenuIntegration(menu);
        if (menu)
            opp::RegisterMenuOptions();
        if (menu || config.Option("DoNotPauseOnMinimize", "0"))
            zpatch_menu_prime_focus_loss();

        if (config.Option("LocalSettingsPath", "0"))
            zpatch_local_settings_capture_cwd();

        (void)opp::FindGamePointers(gameImage);

        // CarDecalsFix's double-load guard: when its scan finds nothing, another
        // ASI loader already patched the site.
        if (config.Option("CarDecalsFix", "1"))
        {
            opp::Match decals;
            const opp::ModuleView view{gameImage.base, gameImage.size};
            if (!opp::FindPattern(
                    view,
                    "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00", decals))
            {
                MessageBoxA(0,
                            "OpenPreCEPatch was loaded twice! Make sure that you don't have multiple "
                            "ASI loaders installed! (e.g. xliveless + dsound, xliveless + dinput8, "
                            "dsound + dinput8, etc)",
                            "OpenPreCEPatch Error", 0);
            }
        }

        opp::PatchGame(build, gameImage, config);
        opp::Log("install: option pass complete");

        ApplyLaunchWithPlayGTAIV(build, moduleBase, config);
    }

    void Initialize(HMODULE module)
    {
        DisableThreadLibraryCalls(module);
        wchar_t moduleName[MAX_PATH]{};
        if (GetModuleFileNameW(module, moduleName, MAX_PATH))
            moduleFile = moduleName;
        modulePath = moduleFile.parent_path();
        if (!modulePath.empty())
            opp::OpenLog(modulePath / L"IV.EFLC.OpenPreCEPatch.log");

        uint32_t compactVersion = 0;
        opp::GameBuild build = opp::GameBuild::unknown;
        (void)opp::QueryGameVersion(compactVersion, build);

        opp::Log("module: %s", modulePath.string().c_str());
        CheckGameVersion(compactVersion);
        CaptureModules(module);
        if (opp::DetectFusionFix(modulePath))
        {
            size_t count = 0;
            (void)opp::FusionFixConflicts(count);
            opp::Log("compat: FusionFix present, %zu overlapping options disabled", count);
        }
        opp::Log("modules: xliveless=%u citizen=%u xlive=%08X", zpatch_xliveless_present,
                 zpatch_citizen_present, zpatch_xlive_base);
        SetStuff(module);
    }
} // namespace

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(g_once, []() {
        HMODULE module = hSelf;
        if (!module)
        {
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&InitializeASI), &module);
        }
        Initialize(module);
    });
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        hSelf = module;
        InitializeASI();
    }
    return TRUE;
}
