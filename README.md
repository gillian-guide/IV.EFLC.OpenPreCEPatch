# IV.EFLC.OpenPreCEPatch

[![Build](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/build.yml/badge.svg)](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/build.yml)

IV.EFLC.OpenPreCEPatch is an open-source successor to ZolikaPatch. It keeps most of the original mod's options, rewrites them as ordinary C++ around the same recovered signature data, and modernises the loader, hooking and configuration layers, while adding improvements to some unreliable code.

It targets improvements to the pre-CE experience to bring it closer to the Complete Edition, and to
general stability and the multiplayer experience. Options FusionFix already owns are gated off
automatically when a FusionFix ASI is next to the game. It is meant to be used with
[this fork of XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon) as a companion,
because some features belong there.

## Features

- Use a (non-SecuROM) launcher renamed to `PlayGTAIV.exe`.
- Fix the 15+ second startup times before the game window even pops up, cut loading times in half or more, and get to the main menu faster.
- Fix benchmark, Concrete Jungle, episode-switch, ROCKET, wanted-level, SuperLOD and SMPA60 crashes.
- Fix the multiplayer crashes and freezes, including the modmenu crash methods.
- Fix some high-FPS problems: cutscenes, bike physics, the 300 FPS speedup, VSync, driveby aim and the mouse deadzone.
- Raise the PtrNode and vehicle limits, remove the livery limit and reimplement memory allocation.
- Fix the emissive-light, reversing-light and LOD fade-in bugs, and add car, object and building shadows.
- Add experimental night shadow features.
- Add compatibility for Complete Edition's folder structure, files and savefiles to minimize downgrade requirements.
- Add the APC/Buzzard, explosive weapons, police weapons and TBoGT aircraft height limit.
- Add the popup headlights, sirens, plane parts, engine block, steering wheel and garage fixes.
- Death music restoration
- .ide hash support
- Mirror GFWL achievements to Steam
- Disable the GFWL `xlive.dll` protection and mod checks.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   (it can be installed as `xlive.dll` for xliveless).
2. Extract the archive anywhere the ASI loader loads from: the game folder, `plugins\`,
   `scripts\` or `update\`.
3. Start the game and edit `IV.EFLC.OpenPreCEPatch.ini`; the shipped ASI writes no log, so a
   `Trace` build is what records what was applied.

The ASI resolves its own directory, so its INI, FusionFix notice and trace log are
resolved next to it. A FusionFix ASI is detected by its loaded module name, or next to this ASI.

If an old `ZolikaPatch.ini` is present (next to the ASI, or in the working directory) and
`IV.EFLC.OpenPreCEPatch.ini` is not, the ASI imports the old values on first run and writes the
grouped file next to the ASI. The old file is left untouched.

## Building

Requirements: Windows 10/11, Visual Studio with the **x86** C++ toolchain (the `win-x86` preset
names the Visual Studio 18 2026 generator) and CMake 3.28 or newer. The vendor libraries are
committed under `external/`; there are no submodules to initialise.

```bat
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

The output is `build/win-x86/Release/IV.EFLC.OpenPreCEPatch.asi` and its INI. The release folder
and zip are a separate target:

```bat
cmake --build --preset release --target package
```

They land in `dist/IV.EFLC.OpenPreCEPatch/` and `dist/IV.EFLC.OpenPreCEPatch.zip`. The `Trace`
configuration builds the same ASI with diagnostics enabled (`cmake --build --preset trace`,
`ctest --preset trace`).

`CMakePresets.json` has the presets (`win-x86`, `debug`, `release`, `trace`). The MSVC settings
that matter (C++23 modules, static CRT, `/BASE:0x10000000`, the committed `external/` trees) are in
`CMakeLists.txt`, and `.github/workflows/build.yml` is the CI invocation.

## Credits

- **Zolika1351** — author of ZolikaPatch, the original mod whose options, defaults, signatures and behaviour this project preserves.
- **ThirteenAG and the FusionFix contributors** — the project layout, build and hook conventions this repository follows.
- **LINK/2012** — the `injector` memory-write library.
- **CitizenFX and ThirteenAG** — `Hooking.Patterns`.
- **Turbine1991** — feather-ini-parser 1.41, whose grammar `source/config.ixx` reproduces.

Licensed under the GNU General Public License, version 3; see `LICENSE` and `NOTICE`.
