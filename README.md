# IV.EFLC.OpenPreCEPatch

This repository ships as **IV.EFLC.OpenPreCEPatch**: an improvement/successor fork of ZolikaPatch 7.65 with the OpenPreCEPatch changes compiled in. The ASI is `IV.EFLC.OpenPreCEPatch.asi`; its shipped configuration is OpenPreCEPatch's grouped `IV.EFLC.OpenPreCEPatch.ini`, read from next to the ASI. There is no runtime improvement switch and no separate parity build. `tests/parity/ZolikaPatch.ini` is an internal regression fixture, not a shipped file.

The implementation is written with C++23 module interface units, one plain translation unit for the startup driver, `Event<Args...>` tiers and a `Game` install context, with the vendored libraries pinned under `external/`. It is not Zolika1351's source. The shipped 32-bit ASI was the specification, and its loader, per-key install arms and hook bodies are written back as ordinary C++ with the recovered addresses, signatures, defaults and behaviour.

The compiled ASI supports every game version from 1.0.0.0 to 1.1.3.0, including EFLC, and the 1.2.0.43 product version the catalogue also masks. It is Win32 only. Each catalogue key carries its shipped default, its absent-key default and the builds it applies to; `IV.EFLC.OpenPreCEPatch.ini` supplies the shipped values.

## Status

The retained arms are measured against the recovered binary; the successor changes are the product.

|measurement|result|produced by|
|-|-|-|
|retained keys on 1.0.8.0|80 closed, 5 documented improved deltas, 0 open (85-key surface)|`python tools/parity\_report.py 9`|
|hook bodies proven whole|149 transcribed bodies, 0 failures|`python tools/verify\_bodies.py --original ../game\_refs/ZolikaPatch\_original.asi.off --candidate build/win-x86/Release/IV.EFLC.OpenPreCEPatch.asi --bodies tests/bodies.tsv --map build/win-x86/Release/IV.EFLC.OpenPreCEPatch.map`|
|write set|retained-arm dump matrix; the full-cell diff also contains the bytes the 14 removed keys used to write (expected missing)|`python tools/dump_matrix.py --markdown <out>`|
|tests|35 registered ctest targets|`ctest --preset release`|

The five improved deltas are deliberate: GarageModelFix's prologue, BuildingAlphaFix's `jnz`-only removal, CutsceneFixes' callee, ImprovedShaderStreaming's two calls, and RemoveGFWLUpdatePopup's shipped default. They are reported apart from `open`. The 14 keys OpenPreCEPatch removed or moved to XLivelessAddon are out of the catalogue (SkipLauncher, SkipIntro, SkipMenu, BorderlessWindowed, DoNotPauseOnMinimize, LocalSettingsPath, VRAMFix and the seven dropped fixes).

## Features

* Fix the benchmark, Concrete Jungle, episode-switch, ROCKET, wanted-level, SuperLOD and SMPA60 crashes.
* Fix the multiplayer crash methods, and block weapon-stripping and teleport trainers.
* Fix the high-FPS problems: cutscenes, bike physics, the 300 FPS speedup, VSync, drive-by aim and the mouse deadzone.
* Raise the PtrNode and vehicle limits, remove the livery limit, and reimplement memory allocation.
* Fix the emissive, reversing-light and LOD fade-in bugs, and add car, object and building shadows.
* Load TBoGT and TLAD content from the EFLC folder layout, and add the APC/Buzzard, explosive and police weapons and the TBoGT aircraft height limit.
* Add the popup-headlights, siren, plane-part, engine-block and steering-wheel support, and .ide hash support.
* Restore the death music, and add the OpenPreCEPatch failsafes, caches, Steam achievement mirror and GFWL protection disabler.

Launcher/menu/intro/borderless/VRAM/settings-path features moved to the
XLivelessAddon companion and are not part of this ASI.

## Building

Requirements: Windows, Visual Studio with the x86 C++ toolchain (the preset names the `Visual Studio 18 2026` generator), CMake 3.28 or newer, and the two submodules checked out. The shipped ASI is 32-bit.

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

The artifact is `build/win-x86/Release/IV.EFLC.OpenPreCEPatch.asi`; copy it next to `GTAIV.exe` or `EFLC.exe` with `IV.EFLC.OpenPreCEPatch.ini`. The release folder and zip are produced by:

```
cmake --build --preset release --target package
```

They land in `dist/IV.EFLC.OpenPreCEPatch/` and `dist/IV.EFLC.OpenPreCEPatch.zip`. The `Trace` configuration builds the same ASI with diagnostics enabled.

## Documentation

|document|what it answers|
|-|-|
|[architecture.md](docs/architecture.md)|the module layout, the three event tiers, the shape of an arm, and why startup is synchronous|
|[building.md](docs/building.md)|the presets, the toolchain and link settings, Release versus Trace, and where the artifact lands|
|[features.md](docs/features.md)|every catalogue key, grouped by module, with its shipped and absent-key default and its build scope|
|[hooking.md](docs/hooking.md)|how a body is transcribed, registered and proved, the C++ rebuilds and why, and the preserved-fault policy|
|[testing.md](docs/testing.md)|every test and measurement tool, what each proves, and how to run them|
|[parity.md](docs/parity.md)|the retained arms' original behavior and the remaining fault evidence|

## Credits

* **Zolika1351** - author of ZolikaPatch, the mod this code reimplements.
* **ThirteenAG and the FusionFix contributors** - the project layout, the `Event<Args...>` tiers and the hook conventions this tree follows.
* **LINK/2012, ThirteenAG** - the `injector` memory-write library under `external/injector/`.
* **CitizenFX, ThirteenAG** - the Hooking.Patterns scanner under `external/hooking/`.
* **Turbine1991** - feather-ini-parser 1.41, whose grammar `source/config.ixx` reproduces.
* **Rockstar Games / Take-Two** - GTA IV and EFLC. This project is not affiliated with or endorsed by them.

Licensed under the GPL-3.0; see `LICENSE` and `NOTICE`. The vendored libraries keep their own licences.

## Licence and attribution

This tree is licensed under the **GNU General Public License v3.0** (see LICENSE), because
its architecture is GTAIV.EFLC.FusionFix's and that project is GPL-3.0:

- GTAIV.EFLC.FusionFix (https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) — the module
  layout, the Event<Args...> tiers, the Game install context, the plain-translation-unit
  startup driver and the ind_pattern house idiom are its design, adapted here. Where a file
  is a direct adaptation rather than an independent implementation it says so in its header.
- injector (https://github.com/thelink2012/injector), Hooking.Patterns
  (https://github.com/Hooking-Patterns/Hooking.Patterns) and IniReader
  (https://github.com/Rookfighter/inifile-cpp) are vendored under external/ at the pinned
  commits recorded there, each under its own licence.
- ZolikaPatch itself (Zolika1351) is the specification this reimplements; no source was
  copied from it, and the recovered addresses, signatures, defaults and behaviour are
  documented with provenance throughout docs/.

