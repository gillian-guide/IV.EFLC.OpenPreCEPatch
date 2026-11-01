# OpenPreCEPatch

[![Build](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml)

OpenPreCEPatch is an open-source successor to ZolikaPatch, the GTA IV / Episodes from Liberty City
quality-of-life and crash-fix plugin. It keeps the original mod's options, rewrites them as
readable, maintainable C++23, and modernises the loader, hooking and configuration layers around
the same signature data, while adding improvements to some unreliable code.

It targets improvements to the pre-CE experience to bring it closer to the Complete Edition, and to
general stability and the multiplayer experience. Options FusionFix already owns are gated off
automatically when a FusionFix ASI is next to the game. Every entry below is an option in
`data/plugins/IV.EFLC.OpenPreCEPatch.ini`, which records the default and the game versions each
option applies to.

It is meant to be used with [this fork of XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon)
as a companion, because some features belong there. It also carries the standalone
`GFWLProtectionDisabler2019` patch and a reimplementation of the Steam Achievements mod that works
differently from the original: GFWL achievements now work alongside Steam's.

## Features

- Skip the launcher/DRM checks (XLivelessAddon).
- Set custom savegame and settings folders, or move both into a saves folder inside the game folder (XLivelessAddon).
- Remove the Rockstar Games registry path dependency and skip the GFWL web-connect and health checks (XLivelessAddon).
- Shorten the intro screens and skip the main menu (XLivelessAddon).
- Add borderless windowed mode, keep rendering while unfocused, and fix the VRAM detection (XLivelessAddon).
- Use a (non-SecuROM) launcher renamed to `PlayGTAIV.exe`.
- Fix the 15+ second startup times before the game window even pops up, cut loading times in half or more, and get to the main menu faster.
- Fix the benchmark, Concrete Jungle, episode-switch, ROCKET, wanted-level, SuperLOD and SMPA60 crashes.
- Fix the multiplayer crashes and freezes, including the modmenu crash methods.
- Fix the high-FPS problems: cutscenes, bike physics, the 300 FPS speedup, VSync, driveby aim and the mouse deadzone.
- Raise the PtrNode and vehicle limits, remove the livery limit and reimplement memory allocation.
- Fix the emissive-light, reversing-light and LOD fade-in bugs, and add car, object and building shadows.
- Add compatibility for newer `script.img`/`radiohud` files, newer savefiles, any `.sco` file and the Complete Edition folder structure and files.
- Remove the GFWL savegame CRC check, Social Club login, GFWL update popup and modded-file checks.
- Add the APC/Buzzard, explosive weapons, police weapons and TBoGT aircraft height limit.
- Add the popup headlights, sirens, plane parts, engine block, steering wheel and garage fixes.
- Mirror GFWL achievements to Steam, disable the GFWL `xlive.dll` protection check, and add the pipe-bomb icon, raw `.ide` hashes, `gta.dat` override, death music and Niko ped clothes.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   (it can be installed as `xlive.dll` for xliveless).
2. Extract the archive next to `GTAIV.exe` and `EFLC.exe`.
3. Start the game and edit `IV.EFLC.OpenPreCEPatch.ini`; `IV.EFLC.OpenPreCEPatch.log` records what
   was applied.

If an old `ZolikaPatch.ini` is present and `IV.EFLC.OpenPreCEPatch.ini` is not, the ASI imports
the old values on first run and writes the grouped file. The old file is left untouched.

## Supported game versions

The id is the compact game version (`1080` = 1.0.8.0).

| id | version | status |
| ---: | --- | --- |
| 1000 | 1.0.0.0 | supported |
| 1004 | 1.0.0.4 | supported |
| 1040 | 1.0.4.0 | supported |
| 1060 | 1.0.6.0 | supported |
| 1070 | 1.0.7.0 | supported |
| 1080 | 1.0.8.0 | supported |
| 1110 | 1.1.1.0 (TLAD) | supported |
| 1120 | 1.1.2.0 (TBoGT) | supported |
| 1130 | 1.1.3.0 (EFLC) | supported |

## Original options that are not implemented

An old `ZolikaPatch.ini` is imported key by key into the grouped file. These original options have
no row there on purpose; the import writes one `legacy key ... not imported` line per dropped key
to `IV.EFLC.OpenPreCEPatch.log` and lists them in the report the loader keeps
(`LegacyImportReport`):

| option | why |
| --- | --- |
| `AddOptionsInMenu` | the menu integration is not implemented |
| `FixPCIDKickbug` | not implemented in this tree |
| `ForceDynamicShadowsEverywhere` | not implemented in this tree |
| `HighQualityReflections` | not implemented in this tree |
| `NoPhysicsSleep` | not implemented in this tree |
| `RemoveBoundingBoxCulling` | not implemented in this tree |
| `RemoveUselessChecks` | the build-gated RVA table is not carried by this tree |

`BorderlessWindowed`, `DoNotPauseOnMinimize`, `LocalSettingsPath`, `SkipIntro`, `SkipLauncher`,
`SkipMenu` and `VRAMFix` belong to the
[XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon) companion, which reads the same
legacy keys from its own INI; the import lists them separately so they are not mistaken for
missing features.

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

- **Zolika1351** — author of ZolikaPatch, the original mod whose options, defaults, signatures and
  behaviour this project preserves.
- **GTAmodding / XLivelessAddon contributors** — the launcher/DRM bypass and the xliveless file
  layer the original loader descends from.
- **ThirteenAG and the FusionFix contributors** — the project layout, build and hook conventions
  this repository follows, the reference for compatibility work, and the intro/windowed logic used
  by the separate XLivelessAddon companion (GPL-3.0).
- **LINK/2012** — the `injector` memory-write library.
- **CitizenFX and ThirteenAG** — `Hooking.Patterns`.
- **ThirteenAG** — `IniReader`, the injector fork and the packaging conventions.
- **The readable reconstruction contributors** — the verified reimplementation of the original ASI
  from which the installer arms and hook bodies were ported.

Licensed under the GNU General Public License, version 3 or later; see `LICENSE` and `NOTICE`.
