# ZolikaPatch

[![Build](https://github.com/gillian-guide/ZolikaPatch/actions/workflows/build.yml/badge.svg)](https://github.com/gillian-guide/ZolikaPatch/actions/workflows/build.yml)

ZolikaPatch is an open-source reconstruction of ZolikaPatch 7.65, the GTA IV / Episodes from
Liberty City quality-of-life and crash-fix plugin. It is not Zolika1351's original source: the
shipped 32-bit ASI was disassembled and decompiled, and its loader, per-option install blocks,
hook bodies and launcher bypass are written back as ordinary C++23 with the recovered addresses,
signatures, defaults and behaviour.

The compiled ASI is a drop-in replacement for the original binary on every game version from
1.0.0.0 to 1.1.3.0. Every entry below is an option in `ZolikaPatch.ini`, which records the
default and the game versions each option applies to.

## Features

- Skip the launcher and DRM checks, launching the game directly from its own executable or from
  any launcher renamed to `PlayGTAIV.exe`.
- Launch patch 7 or 8 through Steam with a non-SecuROM launcher renamed to `PlayGTAIV.exe`.
- Remove the extra file dependencies the original loader needs, so 1.0.8.0 and below start without DFA, `gta4Browser` or `binkw32`.
- Fix the crash after the benchmark ends.
- Fix the freeze after Concrete Jungle with the three EFLC-exclusive radio stations installed.
- Fix the crash when switching episodes mid-game.
- Fix the crash when firing the ROCKET weapon.
- Fix the rare crash when getting a wanted level.
- Fix SMPA60 when the game is abused with car mods and when another player does it in multiplayer.
- Fix the climbing quick-time event in Out of Commission at high FPS.
- Fix the crash when SuperLOD is spawned.
- Fix the crash when starting cutscenes as a ped.
- Fix random crashes while using actual planes.
- Fix the crashes caused by another player switching to an invalid model, changing to Niko and back, or spawning a car with the wrong vehicle type.
- Fix the blackscreen, crash-message and group-hack crash methods used by malicious trainers.
- Fix the aim-projectile, path-node and collision-deadlock crashes and freezes that usually happen in GTAC.
- Fix keygen users kicking each other on join in GFWL.
- Replace IV's error dialogs with real crash dumps, or keep the game running after a non-fatal error.
- Fix the 15+ second wait before the game window even pops up.
- Cut loading times in half or more.
- Get to the main menu faster.
- Improve a badly coded shader-streaming function, reducing stutter somewhat.
- Increase the PtrNode limit, fixing random crashes in big Cunning Stunts maps.
- Increase the number of possible `vehicles.ide` entries to 1024.
- Increase the number of vehicles with different models that can be loaded at one time.
- Remove the 4-livery limit, allowing up to 255 unique livery textures for any car.
- Reimplement memory allocation, reducing memory usage and loading times and eliminating SMPA* errors.
- Remove leftover nonfunctional debug code for a small performance boost (highly incompatible with other mods).
- Fix bikes sliding out easily above 60 FPS.
- Fix the game speeding up above 300 FPS.
- Fix the game being permanently sped up slightly in some cases.
- Fix VSync locking below 60 FPS on patch 7.
- Fix zoomed-in cutscenes and jittery cutscene camera motion at high framerates.
- Fix driveby shots not landing in the centre of the crosshair when high above ground level.
- Fix the mouse deadzone and remove mouse acceleration from snipers.
- Fix the foot position on chopper bikes and the standing-still animation during a phone call
  (patch 5 and up).
- Increase the sync interval when players are far away (GFWL and GTAC).
- Stop other players teleporting you or removing all your weapons with malicious trainers.
- Allow `script.img` and `radiohud` from newer versions to work (`script.img` is still 1060+ only).
- Make newer savefiles load properly without downgrading them first (1060+ only).
- Remove the GFWL savegame CRC check, allowing other people's savegames to load.
- Allow loading any `.sco` file regardless of which episode or platform it came from.
- Load TBoGT and TLAD, and further add-on episodes, in base IV.
- Remove the Social Club login requirement for multiplayer.
- Remove the "update or sign out of LIVE" popup on patch 5 and below.
- Remove the modded-file checks from 1.0.4.0, 1.0.6.0 and 1.0.7.0 so multiplayer works across patches.
- Cast dynamic shadows for cars and detached car parts from the Booth Tunnel, lampposts and helicopter spotlights.
- Make all objects cast shadows (e.g. bollards, Hove Beach train tracks).
- Fix the missing fade-in for LODs on patch 6+.
- Fix emissive lights such as lampposts slowly fading in and out with the time of day.
- Fix reverse lights being cast in the middle of the car's hood.
- Enable night shadows cast from buildings.
- Cast two separate lights for car headlights, each with its own shadows.
- Make all light sources cast dynamic shadows.
- Make everything reflect the entire world except vehicles and peds.
- Remove the static shadow textures under cars, in the world and in cutscenes.
- Remove the draw distance from static vehicle shadows.
- Remove the arbitrary height cutoff from distant lights.
- Fix bullet holes not appearing on some modded cars outside TBoGT.
- Remove the arbitrary height cutoff from vehicle imposters, making them behave more like V.
- Remove bounding-box culling (unstable).
- Add APC and Buzzard support to all episodes.
- Add explosive shotgun, sticky bomb and DSR1 sniper support to all episodes.
- Make cops use P90s and assault shotguns.
- Use `droppipebomb_icon` for the pipe bomb in base IV.
- Add the `+popuplights` `vehicles.ide` flag for the Hakumai's popup headlights.
- Add the `+siren` `vehicles.ide` flag for police sirens.
- Make propellers, ailerons, elevators and rudders move on planes that support them.
- Make the engine block part of cars shake like V.
- Make the `steeringwheel` car bone rotate properly, with a configurable `+wheelangle` flag.
- Unlock the aircraft height limit to TBoGT's level for all episodes.
- Allow raw hashes in `.ide` entries by prefixing the unsigned decimal hash with `hash:`.
- Load `gta.dat` from another path.
- Restore the unused music on death.
- Skip the intro screens and, optionally, the main menu (launching straight into the last savegame, or IV if there is none).
- Move savegames and settings to a saves folder inside the game folder.
- Add borderless windowed mode and keep the game running while it is unfocused.
- Add extra menu options when IVMenuAPI is present.
- Reduce the threshold velocity below which physics objects go to sleep, so vehicles stop more realistically.
- Log entities as they sync into the GTAC console.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   (it can be installed as `xlive.dll` for xliveless).
2. Extract the archive next to `GTAIV.exe` and `EFLC.exe`.
3. Start the game and edit `ZolikaPatch.ini`; `ZolikaPatch.log` records what was applied.

## Building

Requirements: Windows 10/11, Visual Studio 2022/2026 with the **x86** C++ toolchain, and the
submodules checked out.

```bat
git clone --recursive https://github.com/gillian-guide/ZolikaPatch.git
```

Open `ZolikaPatch.sln` and build `Release | x86`, or:

```bat
msbuild ZolikaPatch.sln /p:Configuration=Release /p:Platform=x86
```

CMake is also supported:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The output is `build\msvc-x86\ZolikaPatch.asi` plus the native tests; the release artefact is a
zip holding only the ASI and `ZolikaPatch.ini`. See `docs/building.md` for the target list and
MinGW notes.

## Accuracy

The 7.65 binary is the specification. The recovery keeps what the original does, including its
rough edges: missing null checks, uninitialised reads, out-of-bounds walks and the crashes they
can cause are reproduced. `docs/features.md` records the preserved behaviour and the per-option
build scope; if a behaviour differs from the shipped plugin, that is a bug in this reconstruction.

## Credits

- **Zolika1351** — author of ZolikaPatch, the mod this code reconstructs.
- **XLivelessAddon** — the launcher/DRM bypass and the xliveless design that ZolikaPatch's loader descends from.
- **LINK/2012** — the `injector` header library.
- **CitizenFX contributors** — `Hooking.Patterns`, and the `Hooking.h` / `Hooking.cpp` hook framework carried in the tree.
- **Takashi Tosaka** — `jitasm.h`, the runtime assembler the original project built its hooks on.
- **Turbine** — the Feather INI parser, whose `INI<>` call shape this project's reader keeps.
- **Rockstar Games / Take-Two** — GTA IV and EFLC. This project is not affiliated with or endorsed by them.

Licensed under the MIT License; see `LICENSE` and `NOTICE`.
