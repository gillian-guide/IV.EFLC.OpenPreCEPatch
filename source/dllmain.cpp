// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenPreCEPatch entry point. Everything runs synchronously at process attach:
// the DRM has already rewritten the game's import-time code by the time a
// deferred worker would get to scan, so signatures have to be matched now.

#include "common.hxx"
#include "hooks.h"

#include "rebased.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

import opp.common;
import opp.game;
import opp.log;
import opp.patch;
import opp.config;
import opp.vehicles;
import opp.warnings;

namespace
{
    std::filesystem::path modulePath;
    std::filesystem::path moduleFile;
    std::filesystem::path configPath;
    HMODULE hSelf = nullptr;
    std::once_flag g_once;

    std::wstring Widen(const std::string& text)
    {
        return std::wstring(text.begin(), text.end());
    }

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

    // The old dsound.dll and any other non-UAL loader get the same warning: the
    // loader cannot be identified as a modern Ultimate ASI Loader. A fatal
    // version warning stops the start-up here; its dialog is posted to a worker
    // thread and exits the process when it is closed.
    bool CheckGameVersion(uint32_t compactVersion)
    {
        const opp::AsiLoaderInfo loader = opp::QueryAsiLoader();
        if (loader.detected)
        {
            opp::Log("loader: %s %s", loader.name.c_str(),
                     loader.version.empty() ? "(version unknown)" : loader.version.c_str());
        }
        else
        {
            opp::Log("loader: no Ultimate ASI Loader found in this process");
        }

        const bool modernLoader =
            loader.detected &&
            (!loader.versionKnown ||
             opp::AsiLoaderVersionAccepted(loader.versionMs, loader.versionLs));
        if (!modernLoader)
        {
            opp::ShowWarningAsync(
                opp::OldAsiLoaderDialog(loader.name, loader.version, loader.versionKnown),
                std::nullopt);
        }

        if (opp::IsUnsupportedGameVersion(compactVersion))
        {
            // E_UNEXPECTED, the exit code the original passes to TerminateProcess.
            opp::Log("version: %u is outside the supported range", compactVersion);
            opp::ShowWarningAsync(opp::UnsupportedGameVersionDialog(), 0x8000FFFFu);
            return false;
        }
        return true;
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

    void EnforceZMenuVersion()
    {
        uint32_t out = 0;
        if (opp::ZMenuVersionGuard(out) != opp::ZMenuGuardResult::version_rejected)
            return;

        MessageBoxA(nullptr,
                    "Incompatible/outdated version of ZMenu detected! The game will "
                    "now exit.",
                    "", MB_ICONERROR);
        TerminateProcess(GetCurrentProcess(), 0u);
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
        if (config.Option("RemoveModChecks", "0") ||
            config.Option("Patch4RemoveModChecks", "0"))
            zpatch_flag_patch4_modchecks = 1u;
        if (config.Option("RemoveGFWLUpdatePopup", "0"))
            zpatch_flag_gfwl_popup = 1u;
        if (config.Option("FreezeCarFix", "1"))
        {
            if (config.Option("DebugPrintSpawns", "0"))
                zpatch_flag_debug_spawns = 1u;
        }
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
    // ignored the INI. The set is written to a .txt next to the ASI on every
    // launch; the dialog is shown only while that file does not exist yet, so it
    // stays a one-time notice. Deleting the file brings the dialog back.
    void NotifyFusionFixGatedOptions(const opp::Config& config)
    {
        if (!opp::FusionFixPresent())
            return;

        std::vector<std::wstring> gated;
        std::string names;
        size_t count = 0;
        const opp::FusionFixConflict* conflicts = opp::FusionFixConflicts(count);
        for (size_t i = 0; i < count; ++i)
        {
            // RawOption is noexcept (an unparseable value is logged and falls
            // back to the default), so there is no exception boundary here.
            if (!config.RawOption(conflicts[i].option, "0"))
                continue;

            if (!names.empty())
                names += ", ";
            names += conflicts[i].option;
            gated.push_back(Widen(conflicts[i].option) + L" - " + Widen(conflicts[i].reason));
        }

        if (gated.empty())
            return;

        opp::Log("compat: enabled but disabled by FusionFix: %s", names.c_str());

        const std::filesystem::path notice =
            configPath.parent_path() / L"IV.EFLC.OpenPreCEPatch.fusionfix-notice.txt";
        const bool existed = std::filesystem::exists(notice);
        if (!opp::WriteFusionFixNotice(notice, L"IV.EFLC.OpenPreCEPatch.ini", gated))
            opp::Log("compat: cannot write %s", notice.string().c_str());

        if (existed)
            return;

        opp::ShowWarningAsync(opp::FusionFixNoticeDialog(L"IV.EFLC.OpenPreCEPatch.ini",
                                                         notice.filename().wstring(),
                                                         notice.wstring(), gated),
                              std::nullopt);
    }

    void SetStuff(HMODULE module)
    {
        // The INI and log live next to the ASI; the legacy import and the
        // FusionFix probe also look in the game folder, because either file or
        // ASI may sit there when this module is installed in plugins\.
        const std::filesystem::path gameDirectory = ModuleDirectory(nullptr);
        configPath = opp::EnsureConfigFile(ModuleDirectory(module), nullptr, gameDirectory);
        if (configPath.empty())
        {
            opp::Log("config: no IV.EFLC.OpenPreCEPatch.ini or ZolikaPatch.ini next to the ASI");
            return;
        }

        opp::Config config(configPath);
        if (!config.present())
        {
            opp::Log("config: cannot read %s", configPath.string().c_str());
            return;
        }

        opp::Log("config: %s", configPath.string().c_str());
        NotifyFusionFixGatedOptions(config);
        LoadFlags(config);
        if (config.InvalidValues() != 0)
        {
            // Each of these used the option's own default instead of throwing
            // std::invalid_argument out of DllMain (the original's fault).
            opp::Log("config: %zu value(s) did not parse and used the option default",
                     config.InvalidValues());
        }

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

        const opp::PreambleReport preamble = opp::FindGamePointers(gameImage);
        if (preamble.missing != 0)
            opp::Log("preamble: %zu resolved, %zu missing", preamble.resolved,
                     preamble.missing);
        else
            opp::Log("preamble: %zu pointers resolved", preamble.resolved);

        // CarDecalsFix's site: a miss is no longer read as "another copy of this
        // ASI already patched it" (that is the single-instance marker's job); say
        // what was observed instead, so a genuinely different build is visible.
        if (config.Option("CarDecalsFix", "1"))
        {
            opp::Match decals;
            const opp::ModuleView view =
                opp::ExecutableView({gameImage.base, gameImage.size});
            if (!opp::FindPattern(
                    view,
                    "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00", decals))
            {
                opp::Log("CarDecalsFix: site not found (already patched, or a different "
                         "game image)");
            }
        }

        opp::PatchGame(build, gameImage, config);
        opp::Log("install: option pass complete");
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

        // Two copies of this ASI (a renamed duplicate, or two ASI loaders each
        // loading their own copy) both run the entry point. The process-wide
        // marker is the direct test; a scan-based one false-positives whenever a
        // pattern is simply missing.
        if (!opp::ClaimSingleInstance(L"IV.EFLC.OpenPreCEPatch"))
        {
            opp::Log("init: another copy of OpenPreCEPatch already initialized in this process");
            opp::ShowWarningAsync(opp::LoadedTwiceDialog(), std::optional<UINT>{0u});
            return;
        }

        if (!CheckGameVersion(compactVersion))
            return;

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
