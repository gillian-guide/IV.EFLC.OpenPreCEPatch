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

- Skip the launcher/DRM checks (recovered DFA and ROM stages).
- Use a (non-SecuROM) launcher renamed to `PlayGTAIV.exe`.
- Fix the 15+ second startup times before the game window even pops up, cut loading times in half or more, and get to the main menu faster.
- Fix the benchmark, Concrete Jungle, episode-switch, ROCKET, wanted-level, SuperLOD and SMPA60 crashes.
- Fix the multiplayer crashes and freezes, including the modmenu crash methods.
- Fix the high-FPS problems: cutscenes, bike physics, the 300 FPS speedup, VSync, driveby aim and the mouse deadzone.
- Raise the PtrNode and vehicle limits, remove the livery limit and reimplement memory allocation.
- Fix the emissive-light, reversing-light and LOD fade-in bugs, and add car, object and building shadows.
- Add dual headlights and night shadows.
- Add compatibility for newer `script.img`/`radiohud` files, newer savefiles, any `.sco` file and the Complete Edition folder structure and files.
- Remove the GFWL savegame CRC check, Social Club login, GFWL update popup and modded-file checks.
- Add the APC/Buzzard, explosive weapons, police weapons and TBoGT aircraft height limit.
- Add the popup headlights, sirens, plane parts, engine block and steering wheel support.
- Add the pipe-bomb icon, raw `.ide` hashes, `gta.dat` override, death music, Niko ped clothes and the GTAC spawn log.
- Shorten the intro screens, skip the main menu, move savegames and settings into the game folder, add borderless windowed mode, keep rendering while unfocused, and fix the VRAM detection.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   (it can be installed as `xlive.dll` for xliveless).
2. Extract the archive next to `GTAIV.exe` and `EFLC.exe`.
3. Start the game and edit `ZolikaPatch.ini`; `ZolikaPatch.log` records what was applied.

The launcher bypass is optional. Use it only with a legitimate copy of the game.

## Supported game versions

The id is the compact game version (`1080` = 1.0.8.0). The status reflects what has been verified
on this reconstruction.

| id | version | status |
| ---: | --- | --- |
| 1000 | 1.0.0.0 | supported |
| 1004 | 1.0.0.4 | supported |
| 1040 | 1.0.4.0 | supported |
| 1060 | 1.0.6.0 | supported |
| 1061 | 1.0.6.1 | recognised, unverified |
| 1070 | 1.0.7.0 | supported |
| 1080 | 1.0.8.0 | supported |
| 1100 | 1.1.0.0 (EFLC) | recognised, unverified |
| 1110 | 1.1.1.0 (TLAD) | supported |
| 1120 | 1.1.2.0 (TBoGT) | supported |
| 1130 | 1.1.3.0 (EFLC) | supported |

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
