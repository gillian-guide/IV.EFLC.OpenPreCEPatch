# OpenPreCEPatch

[![Build](https://github.com/OpenZDecomp/OpenPreCEPatch/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/OpenZDecomp/OpenPreCEPatch/actions/workflows/msvc_x86.yml)

OpenPreCEPatch is the community successor to **ZolikaPatch**, the GTA IV / Episodes
from Liberty City quality-of-life and crash-fix plugin. It keeps the original
mod's fixes and features, rewrites them as readable, maintainable C++23, and
modernises the loader, hooking and configuration layers around the same
signature data.

The project follows the structure of
[GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix):
`source/` holds one file per subsystem, `includes/` holds the vendored injector
and pattern libraries as submodules, and `data/plugins/` holds the shipped INI.
The repository builds on its own; no game files are needed to compile.

## What it does

Eighty-five independent options. The highlights:

- **Stability** — aim-projectile, bad-player-model, benchmark, blackscreen,
  collision-deadlock, combined-radio, cutscene, episode-switch, freeze-car,
  freeze-gun, garage-model, group-hack, MP-Niko, path-node, plane,
  wanted-level and weapon-protection crash fixes.
- **Performance / streaming** — fast loading, improved shader streaming,
  increased PtrNode and vehicle limits, the new memory system, no loading
  sleep, and the high-FPS speedup.
- **Graphics** — building and vehicle dynamic shadows, emissive-lerp,
  high-quality reflections, LOD/impostor fixes, popup headlights, reversing
  lights, and the static-shadow controls.
- **Input / camera** — mouse deadzone and sniper acceleration, drive-by FOV,
  cutscene camera fixes, and moving steering wheels.
- **Multiplayer** — better sync, model/weapon support across episodes,
  teleport protection, livery limits, spawn diagnostics and the GFWL CD-key
  kick fix.
- **Loading / compatibility** — local settings, DLC loading, new save/file
  compatibility and the ZMenuIV guard.

Every option is documented in [docs/features.md](docs/features.md), and the full
key table lives in [docs/ini.md](docs/ini.md).

The intro/menu, windowed, launcher/DRM and VRAM features belong to the separate
**XLivelessAddon** companion repository. They are not part of this repository and
are not built by it; OpenPreCEPatch only detects an installed companion module.

## Improvements

Safety and performance improvements are part of the shipped code; there are no
per-fix INI keys and no switch that selects an older branch.
`NewSavesCompatibility` uses the validated converter, `OutOfCommissionFix`
caches the archive, the moving-parts options guard their vehicle walk, and the
installer scans stay inside the executable sections and stop at the first match
(which is what the original loader did; the startup pass is about 3x faster).
`GarageModelFix`, `CutsceneFixes` and `BuildingAlphaFix` hook the better entry
point (prologue, shared callee, the branch alone).

The write-set effect of every option is recorded in
[docs/improvements-log.md](docs/improvements-log.md) and checked by
`tools/opcep_matrix.py` against the committed `tests/parity/matrix.json`.

The mod also carries the standalone `GFWLProtectionDisabler2019` patch
(`GFWLProtectionDisabler=1`) and a Steam achievement mirror
(`SteamAchievements=1`). The mirror hooks `XUserWriteAchievements` and calls the
original GFWL function first, so GFWL achievements stay intact alongside Steam.

## Compatibility and the INI

OpenPreCEPatch owns `IV.EFLC.OpenPreCEPatch.ini`. On first run the ASI writes the
shipped template if the file is absent. If an old `ZolikaPatch.ini` is next to
it, the ASI ports its values into the grouped file first; no external converter
is involved.

Options are grouped into `[STABILITY]`, `[FIXES]`, `[PERFORMANCE]`, `[GRAPHICS]`,
`[EPISODIC]`, `[COMPATIBILITY]`, `[MULTIPLAYER]`, `[VEHICLES]` and `[MISC]`.

## Installation

1. Install an ASI loader (`dinput8.dll` or `xlive.dll`).
2. Copy `IV.EFLC.OpenPreCEPatch.asi` next to `GTAIV.exe` and `EFLC.exe`.
3. Copy `IV.EFLC.OpenPreCEPatch.ini` next to it, or let the ASI create it on
   first launch.
4. Start the game through `PlayGTAIV.exe`.

If an old `ZolikaPatch.ini` is present and `IV.EFLC.OpenPreCEPatch.ini` is not,
the ASI imports the old values on first run and writes the grouped file. The old
file is left untouched.

## Supported game versions

| compact | version | status |
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
| 12043 | 1.2.0.43 | recognised, unverified |

1.2.0.59 and newer are deliberately unrecognised: the loader installs nothing.
See [docs/builds.md](docs/builds.md).

## Building

Requirements: Windows 10/11, Visual Studio 2022 17.10+ (or newer) with the
**x86** C++ toolchain, CMake 3.31+, and the submodules checked out.

```bat
git clone --recursive https://github.com/OpenZDecomp/OpenPreCEPatch
cd OpenPreCEPatch
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

The output is `bin/IV.EFLC.OpenPreCEPatch.asi` and its INI.

The 64-bit parity dump is a separate configure:

```bat
cmake --preset x64
cmake --build --preset x64-release
```

`build.bat` wraps the x86 configure/build/test sequence. See
[docs/building.md](docs/building.md) for the target list, the MSVC settings that
matter and the submodule layout.

## INI

Options are grouped by subsystem:

```ini
[STABILITY]
AimProjectileCrashFix = 1

[GRAPHICS]
CarDynamicShadowFix = 1

[COMPATIBILITY]
SteamAchievements = 1
```

The old flat `[Options]` layout is still accepted. A key that is absent uses the
original default; a value must parse to exactly `1` to enable the option.

## Accuracy

The shipped binary and the proven reconstruction are the specification. The
behaviour follows the original, including its documented rough edges. Known
upstream instabilities and the improvements planned for later releases are
listed in [docs/improvements.md](docs/improvements.md); the preserved behaviour
is recorded in [docs/origins.md](docs/origins.md).

## Credits and license

Original mod by **Zolika1351**. The loader and modernisation follow
**GTAIV.EFLC.FusionFix** by ThirteenAG and contributors. `injector` by LINK/2012
and ThirteenAG, `Hooking.Patterns` by CitizenFX and ThirteenAG, `IniReader` by
ThirteenAG. The separate XLivelessAddon companion descends from
**GTAmodding/XLivelessAddon**. See [CREDITS.md](CREDITS.md) and [NOTICE](NOTICE).

The project is licensed under the **GNU General Public License v3.0 or later**,
the same license as FusionFix. GTA IV and EFLC are property of Rockstar Games /
Take-Two. This project ships no game code.
