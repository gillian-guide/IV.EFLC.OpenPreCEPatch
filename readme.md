# OpenPreCEPatch

[![Build](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml)

OpenPreCEPatch is an open-source successor to ZolikaPatch, the GTA IV / Episodes from Liberty City quality-of-life and crash-fix plugin. It keeps the original mod's options, rewrites them using modern conventions and modernises the loader, hooking and configuration layers around the same signature data, while adding improvements to some unreliable code.

It targets improvements to the pre-CE experience to bring it closer to the Complete Edition, and to
general stability and the multiplayer experience. Options FusionFix already owns are gated off
automatically when a FusionFix ASI is next to the game.

It is meant to be used with [this fork of XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon) as a companion, because some features belong there. It also carries the standalone
`GFWLProtectionDisabler2019` patch and a reimplementation of the Steam Achievements mod that works differently from the original: GFWL achievements now work alongside Steam's.

## Features

- Skip the launcher/DRM checks (XLivelessAddon).
- Set custom savegame and settings folders, or move both into a saves folder inside the game folder (XLivelessAddon).
- Remove the Rockstar Games registry path dependency and skip the GFWL web-connect and health checks (XLivelessAddon).
- Shorten the intro screens and skip the main menu (XLivelessAddon).
- Add borderless windowed mode, keep rendering while unfocused, and fix the VRAM detection (XLivelessAddon).
- Use a (non-SecuROM) launcher renamed to `PlayGTAIV.exe`.
- Fix the 15+ second startup times before the game window even pops up, cut loading times in half or more, and get to the main menu faster.
- Fix benchmark, Concrete Jungle, episode-switch, ROCKET, wanted-level, SuperLOD and SMPA60 crashes.
- Fix the multiplayer crashes and freezes, including the modmenu crash methods.
- Fix some high-FPS problems: cutscenes, bike physics, the 300 FPS speedup, VSync, driveby aim and the mouse deadzone.
- Raise the PtrNode and vehicle limits, remove the livery limit and reimplement memory allocation.
- Fix the emissive-light, reversing-light and LOD fade-in bugs, and add car, object and building shadows.
- Add experimental night shadow features.
- Add compatibility for Complete Edition's folder structure, files and savefiles to minimize downgrade requirements.
- Remove GFWL checks.
- Add the APC/Buzzard, explosive weapons, police weapons and TBoGT aircraft height limit.
- Add the popup headlights, sirens, plane parts, engine block, steering wheel and garage fixes.
- Death music restoration
- .ide hash support
- Mirror GFWL achievements to Steam and disable the GFWL `xlive.dll` protection check, and add the pipe-bomb icon, `gta.dat` override and Niko ped clothes.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   (it can be installed as `xlive.dll` for xliveless).
2. Extract the archive next to `GTAIV.exe` and `EFLC.exe`.
3. Start the game and edit `IV.EFLC.OpenPreCEPatch.ini`; `IV.EFLC.OpenPreCEPatch.log` records what
   was applied.

If an old `ZolikaPatch.ini` is present and `IV.EFLC.OpenPreCEPatch.ini` is not, the ASI imports
the old values on first run and writes the grouped file. The old file is left untouched.

`BorderlessWindowed`, `DoNotPauseOnMinimize`, `LocalSettingsPath`, `SkipIntro`, `SkipLauncher`,
`SkipMenu` and `VRAMFix` belong to the
[XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon) companion, which reads the same
legacy keys from its own INI.

## Building

Requirements: Windows 10/11, Visual Studio 2022 17.10+ (or newer) with the **x86** C++ toolchain,
CMake 3.31+, and the submodules checked out.

```bat
git clone --recursive https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch
cd OpenPreCEPatch
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

The output is `bin/IV.EFLC.OpenPreCEPatch.asi` and its INI. The 64-bit parity dump is a separate
configure:

```bat
cmake --preset x64
cmake --build --preset x64-release
```

`build.bat` wraps the x86 configure/build/test sequence; `CMakePresets.json` has the presets CI
uses (`x86`, `x64`, `x86-release`, `x64-release`). The MSVC settings that matter (C++23 modules,
static CRT, `/BASE:0x10000000`, the `external/` submodules) are in `CMakeLists.txt`, and
`.github/workflows/msvc_x86.yml` is the exact CI invocation.

## Credits

- **Zolika1351** — author of ZolikaPatch, the original mod whose options, defaults, signatures and behaviour this project preserves.
- **ThirteenAG and the FusionFix contributors** — the project layout, build and hook conventions this repository follows
- **LINK/2012** — the `injector` memory-write library.
- **CitizenFX and ThirteenAG** — `Hooking.Patterns`.

Licensed under the GNU General Public License, version 3 or later; see `LICENSE` and `NOTICE`.
