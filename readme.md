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

- Launch patch 7 or 8 through Steam with a non-SecuROM launcher renamed to `PlayGTAIV.exe`.
- Fix the 15+ second wait before the game window even pops up.
- Cut loading times in half or more.
- Get to the main menu faster.
- Improve a badly coded shader-streaming function, reducing stutter somewhat.
- Reimplement memory allocation, reducing memory usage and loading times and eliminating SMPA* errors.
- Fix the crash after the benchmark ends.
- Fix the freeze after Concrete Jungle with the three EFLC-exclusive radio stations installed.
- Fix the crash when switching episodes mid-game.
- Fix the crash when firing the ROCKET weapon.
- Fix the rare crash when getting a wanted level.
- Fix SMPA60 when the game is abused with car mods.
- Ignore all error messages, which is possibly useful for debugging.
- Fix the climbing quick-time event in Out of Commission at high FPS.
- Fix the crash when SuperLOD is spawned.
- Fix the crash when starting cutscenes as a ped.
- Fix random crashes while using actual planes.
- Fix the crash when another player changes their model to Niko and then back.
- Fix the crash when another player switches to an invalid player model.
- Fix the crash when another player spawns a car with the wrong vehicle type.
- Fix the blackscreen, crash-message and group-hack crash methods used by malicious trainers.
- Fix the aim-projectile, path-node and collision-deadlock crashes and freezes that usually happen in GTAC.
- Increase the PtrNode limit, fixing random crashes in big Cunning Stunts maps.
- Fix zoomed-in cutscenes and jittery cutscene camera motion at high framerates.
- Fix bikes sliding out easily above 60 FPS.
- Fix the foot position on chopper bikes and the standing-still animation during a phone call.
- Fix the game speeding up above 300 FPS.
- Fix the game being permanently sped up slightly in some cases.
- Fix VSync locking below 60 FPS.
- Fix the mouse deadzone and remove mouse acceleration from snipers.
- Fix driveby shots not landing in the center of the crosshair when high above ground level.
- Increase the sync interval when players are far away.
- Stop other players teleporting you or removing all your weapons with malicious trainers.
- Remove the modded-file checks from 1.0.4.0, 1.0.6.0 and 1.0.7.0.
- Remove the Social Club login requirement for multiplayer.
- Remove the "update or sign out of LIVE" popup on patch 5 and below.
- Log entities as they sync into the GTAC console.
- Allow `script.img` and `radiohud` from newer versions to work (`script.img` is still 1060+ only).
- Make newer savefiles load properly without downgrading them first.
- Remove the GFWL savegame CRC check, allowing other people's savegames to load.
- Disable the GFWL `xlive.dll` protection check.
- Mirror GFWL achievements to Steam.
- Fix emissive lights such as lampposts slowly fading in and out with the time of day.
- Fix reverse lights being cast in the middle of the car's hood.
- Fix the missing fade-in for LODs.
- Enable night shadows cast from buildings.
- Cast dynamic shadows for cars and detached car parts from the Booth Tunnel, lampposts and helicopter spotlights.
- Make all objects cast shadows (e.g. bollards, Hove Beach train tracks).
- Cast two separate lights for car headlights, each with its own shadows.
- Re-enable headlight shadows while CarDynamicShadowFix is active (buggy; never use with dual headlights).
- Remove the arbitrary height cutoff from vehicle imposters, making them behave more like V.
- Remove the arbitrary height cutoff from distant lights.
- Remove the static shadow textures under cars, in the world and in cutscenes.
- Remove the draw distance from static vehicle shadows.
- Load TBoGT and TLAD from EFLC or CE folders, and further add-on episodes in the same format.
- Allow loading any `.sco` file regardless of which episode or platform it came from.
- Add APC and Buzzard support to all episodes.
- Add explosive shotgun, sticky bomb and DSR1 sniper support to all episodes.
- Make cops use P90s and assault shotguns.
- Unlock the aircraft height limit to TBoGT's level for all episodes.
- Use `droppipebomb_icon` for the pipe bomb in base IV.
- Fix cars being randomly changed in parking spots when add-on models are added.
- Increase the number of vehicles with different models that can be loaded at one time.
- Increase the number of possible `vehicles.ide` entries to 1024.
- Fix bullet holes not appearing on some modded cars outside TBoGT.
- Remove the 4-livery limit, allowing up to 255 unique livery textures for any car.
- Add the `+popuplights` `vehicles.ide` flag for the Hakumai's popup headlights.
- Add the `+siren` `vehicles.ide` flag for police sirens.
- Make propellers, ailerons, elevators and rudders move on planes that support them.
- Make the engine block part of cars shake like V.
- Make the `steeringwheel` car bone rotate properly, with a configurable `+wheelangle` flag.
- Restore the unused music on death.
- Load `gta.dat` from another path.
- Allow raw hashes in `.ide` entries by prefixing the unsigned decimal hash with `hash:`.
- Fix Niko peds' clothes never changing properly in the trainer.

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
