# OpenPreCEPatch

[![Build](https://github.com/OpenZDecomp/OpenPreCEPatch/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/OpenZDecomp/OpenPreCEPatch/actions/workflows/msvc_x86.yml)

OpenPreCEPatch is the community successor to **ZolikaPatch**, the GTA IV / Episodes
from Liberty City quality-of-life and crash-fix plugin. It keeps the original
mod's fixes and features, rewrites them as readable, maintainable C++20, and
modernises the loader, hooking and configuration layers around the same
signature data.

The project is built on the same foundation as
[GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix):
`source/` holds one file per subsystem, `external/` holds the injector and
pattern libraries as submodules, and `data/plugins/` holds the shipped INI.

## What it does

Around a hundred and ten independent options. The highlights:

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
- **Loading / compatibility** — SkipLauncher, SkipIntro, SkipMenu, local
  settings, DLC loading, new save/file compatibility, ZMenuIV guard and
  IVMenuAPI registration.

Every option is documented in [docs/features.md](docs/features.md), and the full
key table lives in [docs/ini.md](docs/ini.md).

## Opt-in improvements

The parity baseline still keeps the original behaviour. Safer and faster
variants are opt-in keys whose default is `0`, so an existing INI behaves
exactly as before:

- stability: `NewSavesGuards`, `OutOfCommissionSafeInit`,
  `VehicleComponentGuards`, `ModelLookupGuards`, `TraceInstallArms`;
- performance: `NewSavesSeekPayloads`, `OutOfCommissionCache`,
  `SteerLookupCache`, `PopupLookupCache`, `ModelNameIndex`;
- compatibility: `SkipIntroShowScreens` (FusionFix-style 300 ms intro).

The original code path stays compiled and selected. The exact write-set effect
of every key is recorded in [docs/improvements-log.md](docs/improvements-log.md)
and checked by `tools/opcep_matrix.py` against the committed
`tests/parity/matrix.json`.

## Compatibility and the INI

OpenPreCEPatch owns `IV.EFLC.OpenPreCEPatch.ini`. On first run the ASI writes the
shipped template if the file is absent. If an old `ZolikaPatch.ini` is next to
it, the ASI ports its values into the grouped file first; no external converter
is involved.

Options are grouped into `[STABILITY]`, `[PERFORMANCE]`, `[GRAPHICS]`,
`[VEHICLES]`, `[INPUT]`, `[MULTIPLAYER]`, `[LOADING]`, `[COMPATIBILITY]`,
`[XLIVELESS]` and `[INTERFACE]`. Each line ends with a compact build tag
(`[all]`, `[1060+]`, ...) and, when applicable, `FF`, meaning the option is
automatically disabled when a FusionFix ASI is present. See
[docs/fusionfix.md](docs/fusionfix.md) for the full overlap list. If an FF
option is enabled, a one-time notice lists the options that will not be active.

FusionFix and other plugins sometimes check for a loaded `XLivelessAddon.asi`.
The optional `XLivelessAddon.asi` in this repository is a marker shim: it does
nothing on its own, it only satisfies that presence check. The real mod always
stays `IV.EFLC.OpenPreCEPatch.asi`.

## Installation

1. Install an ASI loader (`dinput8.dll` or `xlive.dll`).
2. Copy `IV.EFLC.OpenPreCEPatch.asi` next to `GTAIV.exe` and `EFLC.exe`.
3. Copy `IV.EFLC.OpenPreCEPatch.ini` next to it, or let the ASI create it on
   first launch.
4. Optional: copy `XLivelessAddon.asi` next to it if FusionFix needs to see that
   module name.
5. Start the game through `PlayGTAIV.exe`, or enable `SkipLauncher=1` and start
   `GTAIV.exe` directly.

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

Requirements: Windows 10/11, Visual Studio 2022 or newer with the **x86** C++
toolchain, and the submodules checked out.

```bat
git submodule update --init --recursive
premake5 vs2022
msbuild build\OpenPreCEPatch.sln /p:Configuration=Release /p:Platform=Win32
```

The output is `bin/IV.EFLC.OpenPreCEPatch.asi`, the optional
`bin/XLivelessAddon.asi` shim, and a linker `.map`. CMake is also supported:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

See [docs/building.md](docs/building.md) for details.

## INI

Options are grouped by subsystem:

```ini
[STABILITY]
AimProjectileCrashFix = 1

[GRAPHICS]
CarDynamicShadowFix = 1

[XLIVELESS]
RemoveDataIntegrityChecks = 1
SkipTokenActivation = 1
```

The old flat `[Options]` layout is still accepted. A key that is absent uses the
original default; a value must parse to exactly `1` to enable the option.

## Accuracy

The shipped binary and the proven reconstruction are the specification. The
first release keeps the original behaviour, including its rough edges. Known
upstream instabilities and the improvements planned for later releases are
listed in [docs/improvements.md](docs/improvements.md); the preserved behaviour
is recorded in [docs/origins.md](docs/origins.md).

## Credits and license

Original mod by **Zolika1351**; launcher bypass descended from
**XLivelessAddon**. The loader and modernisation follow
**GTAIV.EFLC.FusionFix** by ThirteenAG and contributors. `injector` by LINK/2012
and ThirteenAG, `Hooking.Patterns` by CitizenFX and ThirteenAG, `IniReader` by
ThirteenAG. See [CREDITS.md](CREDITS.md) and [NOTICE](NOTICE).

The project is licensed under the **GNU General Public License v3.0 or later**,
the same license as FusionFix. GTA IV and EFLC are property of Rockstar Games /
Take-Two. This project ships no game code.
