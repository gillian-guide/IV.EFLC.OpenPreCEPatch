# XLivelessAddon (successor)

A maintained fork of [GTAmodding/XLivelessAddon](https://github.com/GTAmodding/XLivelessAddon)
for GTA IV / EFLC. It is a **separate ASI** from OpenPreCEPatch: the main
OpenPreCEPatch module no longer carries the XLivelessAddon feature set.

## Features (from the original addon)

- Custom save path and custom settings path
- Remove registry path dependency
- Skip web connect / GFWL gating checks
- Skip intro and skip menu
- Do not pause on minimize
- Borderless windowed
- VRAM detection fix
- Data-integrity/token/first-run/health checks from the same code base

The original 2019 source is the baseline. The successor also carries the recovered SkipLauncher/DFA/ROM implementation from OpenPreCEPatch, the 300 ms intro body, and the modern INI port. The successor adds the modern config/INI port (reads its own `XLivelessAddon.ini` first, then falls back to `ZolikaPatch.ini` and finally the grouped sections), the FusionFix-style 300 ms intro body, and the same build/runtime conventions as OpenPreCEPatch. This fork keeps its pattern-based
installs and version gates, while using the same modern submodules as
OpenPreCEPatch/FusionFix:

- `external/injector` (ThirteenAG)
- `external/hooking` (Hooking.Patterns)
- `external/inireader`

## Build

From the OpenPreCEPatch repository root:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Outputs `build/Release/XLivelessAddon.asi` and `XLivelessAddon.ini`.

Premake also defines the `XLivelessAddon` project; build it with the same
Win32 Release configuration.

## Install

Copy `XLivelessAddon.asi` and `XLivelessAddon.ini` next to the game executable.
Do **not** also install `IV.EFLC.OpenPreCEPatch.asi` with the same feature
options enabled; choose one module as the owner of the XLivelessAddon fixes.

## Modernisation status

The code compiles against the modern submodules and uses CMake/premake. The
inline hooks that required safetyhook/C++23 (`CustomSettingsPath`, the intro
duration hook) were rewritten or deferred; the rest is the original pattern
set. Improvements are tracked in the repository history and should be applied
here first, then mirrored into OpenPreCEPatch only when they are not
XLivelessAddon-specific.
