#include <common.hxx>

import common;
import config;
import game;
import improvements;
import loader;
import options;
import shared;
import native;
import warnings;
import stability;
import vehicles;
import useless_checks;
import graphics;
import launcher;
import loading;
import audio;
import misc;
import menu;
import ooc;

extern "C" __declspec(dllexport) void InitializeASI();

// The startup driver. Like the original, everything happens synchronously on
// DLL_PROCESS_ATTACH: the DRM has already rewritten the game's code by the time
// a deferred thread would run, so the signatures have to be matched now. The arm
// pipeline is the next module in; this driver stops after the identity and the
// configuration it has to read.
namespace
{
    std::once_flag startupFlag;
    std::atomic<bool> started{ false };

    void StartUp(HMODULE module)
    {
        auto& game = Game::Get();
        game.Initialise(module);

        if (ImprovementEnabled(Improvement::warnings))
        {
            if (!Warnings::ClaimSingleInstance(L"ZolikaPatch-opcep"))
            {
                Warnings::Show(L"ZolikaPatch",
                    L"Another copy of ZolikaPatch is already initialized in this process. "
                    L"This copy does nothing.");
                return;
            }
            if (Warnings::IsUnsupportedVersion(game.FileVersion()))
            {
                Warnings::Show(L"ZolikaPatch",
                    L"This game version is not supported by this build. Supported builds are "
                    L"1.0.0.0 through 1.2.0.43 (1.2.0.43 is the only Complete Edition build).",
                    0x8000FFFFu);
                return;
            }
            if (!IsUALPresent())
            {
                Warnings::Show(L"ZolikaPatch",
                    L"Old ASI Loader detected! Install Ultimate ASI Loader 6.9.0 or newer, or "
                    L"the module may not load next to other ASIs.");
            }
        }

#ifdef ZP_TRACE
        Log::Open(game.LogPath());
        Log::Print("{} {}", rsc_ProductName, rsc_ProductVersion);
        Log::Print("module: {}", game.ModulePath().string());
#endif

        const bool read = Config::Load();
        if (ImprovementEnabled(Improvement::import))
        {
            // First-run creation: render the flat document once into the grouped
            // file. The current run keeps reading the flat document; the next
            // run prefers the grouped one, and deleting it returns to the flat
            // INI. The flat file is never modified.
            std::error_code ignored;
            if (!std::filesystem::exists(Config::GroupedFileName, ignored) &&
                std::filesystem::exists(Config::FileName, ignored))
            {
                const auto resolve = [](std::string_view key) -> std::string_view
                {
                    const auto* option = Catalogue::Find(key);
                    if (option == nullptr)
                        return "Options";
                    // The section names follow OpenPreCEPatch's shipped INI
                    // where a key's owning module is not the oPCEP section.
                    if (key == "BenchmarkFix" || option->module == "stability")
                        return "STABILITY";
                    if (key == "OutOfCommissionFix" || key == "CutsceneFixes")
                        return "FIXES";
                    if (option->module == "vehicles") return "VEHICLES";
                    if (option->module == "graphics") return "GRAPHICS";
                    if (option->module == "loading") return "COMPATIBILITY";
                    return "MISC";
                };
                (void)Config::WriteGrouped(Config::Ini(), resolve);
            }
        }
        if (ImprovementEnabled(Improvement::config) ||
            ImprovementEnabled(Improvement::warnings))
        {
            // The OpenPreCEPatch overlap gate: one probe, recorded once, and
            // every overlapping option reads as off through Catalogue::Read.
            // The warnings group needs the same answer for its notice.
            Config::SetFusionFixPresent(Config::DetectFusionFix(game.ModuleDirectory()));
        }

        if (ImprovementEnabled(Improvement::warnings) && Config::FusionFixPresent())
        {
            std::vector<std::string_view> gated;
            for (const auto& conflict : Config::kFusionFixConflicts)
            {
                const auto* value = Config::Ini().Find("Options", conflict.option);
                if (value == nullptr)
                    continue;
                const auto parsed = Config::ParseInt(*value);
                if (parsed.ok && parsed.value == 1)
                    gated.push_back(conflict.option);
            }
            if (!gated.empty())
            {
                bool existed = false;
                const auto notice = Warnings::WriteFusionFixNotice(
                    game.ModuleDirectory(), gated, existed);
                if (!existed && !notice.empty())
                {
                    std::wstring text =
                        L"GTAIV.EFLC.FusionFix.asi is installed. These enabled options are "
                        L"disabled to avoid overlapping fixes:\r\n\r\n";
                    for (const auto option : gated)
                        text += L"  " + std::wstring(option.begin(), option.end()) + L"\r\n";
                    text += L"\r\nSee " + notice.filename().wstring() + L" next to the ASI.";
                    Warnings::Show(L"ZolikaPatch", text);
                }
            }
        }
        Loader::Current() = Classify(game.FileVersion());
        const auto build = Loader::Current();

#ifdef ZP_TRACE
        Log::Print("game: {} {} (image {:#010x}+{:#x})", game.ExecutablePath().string(),
            game.FileVersionString(), game.ImageBase(), game.ImageSize());
        Log::Print("ini: {} ({})", std::string(Config::FileName), read ? "read" : "absent");
        Log::Print("build: {} ({})", BuildName(build), CompactVersion(game.FileVersion()));

        // Which keys the arms will actually act on. A crash report is only useful
        // alongside the configuration that produced it.
        std::string enabled;
        for (const auto& option : Catalogue::All)
            if (Catalogue::Read(option.key) != 0)
                enabled += std::string(option.key) + " ";
        Log::Line("enabled: " + enabled);
#endif

        FusionFix::onInitEvent().executeAll();
        Shared::Register();
        // The native-call layer resolves the game's native-handle pointer and
        // publishes its entries before any arm can install a body that uses them.
        Native::Register();
        RegisterStability();
        RegisterVehicles();
        RegisterUselessChecks();
        RegisterGraphics();
        RegisterLauncher();
        RegisterLoading();
        RegisterAudio();
        RegisterMisc();
        // The IVMenuAPI integration runs after the audio arm: AddOptionsInMenu's
        // death-music installs are the second half of the original's
        // `RestoreDeathMusic == 1 || sub_10052380()` OR.
        RegisterMenu();
        RegisterOoc();
        Loader::Run();

#ifdef ZP_TRACE
        Log::Print("options: {}", Catalogue::Count());
#endif
    }

    void ShutDown()
    {
        if (!started)
            return;
        started = false;
        FusionFix::onShutdownEvent().executeAll();
        Log::Close();
    }
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(startupFlag, []
    {
        started = true;
        StartUp(GetThisModuleHandle());
    });
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        InitializeASI();
        break;
    case DLL_PROCESS_DETACH:
        ShutDown();
        break;
    }
    return TRUE;
}
