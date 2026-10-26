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

## Recovered SkipLauncher path

`SkipLauncher = 1` (the shipped default) applies the recovered DFA and ROM
stages only — the main signature table stays off, matching the ZolikaPatch
handler this was recovered from. The DFA stage captures the game's CreateFileA
IAT slot and the create_file body forwards through it, so a detour another ASI
installed in that slot stays in the chain; a missing capture is logged and falls
back to `CreateFileA`. `ApplySkipLauncher` publishes the slot as part of the
pass, before the hook can run.

`XLivelessAddon.log` records the detected build id and what actually applied:
the window/borderless/focus-loss install results, whether the SkipIntro/SkipMenu
sites were found, the VRAM hook count, the ADVAPI32 IAT replacement count and
`legacyRom`. With `SkipLauncher = 1` the legacy `CreateROMWindow`/`WM_ROM` block
still runs next to the recovered ROM stage (`legacyRom=1`); gating it off is a
launcher/GFWL decision that needs a live A/B, so it is only observable today.
The fork's unguarded `pattern.get(0)` sites are the original addon's fault paths
and are deliberately preserved; see `docs/improvements.md`.

## Tests

`xla_tests` (ctest, 32-bit) exercises the recovered path with synthetic images:
slot capture and forwarding, stale-slot clearing, missing signatures installing
nothing, and invalid input. There is no write-set matrix for the fork yet; the
fork's patches are inline in `Init()` rather than a table, so a dump would need
that refactor first.

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
