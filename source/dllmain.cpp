#include <common.hxx>

import common;
import config;
import game;
import improvements;
import loader;
import options;
import shared;
import native;
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

#ifdef ZP_TRACE
        Log::Open(game.LogPath());
        Log::Print("{} {}", rsc_ProductName, rsc_ProductVersion);
        Log::Print("module: {}", game.ModulePath().string());
#endif

        const bool read = Config::Load();
        if (ImprovementEnabled(Improvement::config))
        {
            // The OpenPreCEPatch overlap gate: one probe, recorded once, and
            // every overlapping option reads as off through Catalogue::Read.
            Config::SetFusionFixPresent(Config::DetectFusionFix(game.ModuleDirectory()));
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
