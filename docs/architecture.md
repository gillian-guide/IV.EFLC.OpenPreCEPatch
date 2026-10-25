# Architecture

## Entry point

`source/dllmain.cpp` exports `InitializeASI` and calls it from `DllMain`. The
work is synchronous: the DRM has already rewritten the game's import-time code by
the time a deferred worker would get to scan, so signatures must be matched now.

```
DllMain / InitializeASI
  create IV.EFLC.OpenPreCEPatch.ini from ZolikaPatch.ini or the template
  read the game's VS_FIXEDFILEINFO -> compact version -> GameBuild
  old-loader warning + unsupported-version guard
  capture xlive.dll / XLivelessAddon.asi / CitizenIV.exe
  detect a FusionFix ASI and force its overlapping options off
  read the grouped INI, vehicle-light sections and option globals
  ZMenuIV version guard
  XLivelessAddon feature set's one-shot DFA pass, then SkipLauncher
  IVMenuAPI registration, LocalSettingsPath capture
  unconditional preamble (game pointers + hash salts)
  PatchGame (all INI-gated installs)
  LaunchWithPlayGTAIV
```

## Files

| file | responsibility |
| --- | --- |
| `source/dllmain.cpp` | entry point, config resolution, module capture, loader stages |
| `source/common.hxx` | shared types, third-party headers, `GameBuild`, `PatchContext` |
| `source/config.*` | INI access, grouped/legacy section lookup |
| `source/ini_import.*` | first-run INI creation and ZolikaPatch.ini port |
| `source/fusionfix_compat.*` | FusionFix detection and the auto-gated option table |
| `source/game.*` | version classification, joaat hash, process probe |
| `source/pattern.*` | signature normalisation and scans |
| `source/rebased.*` | the globals the hook bodies address, plus the original-name tables |
| `source/hooks.*` | machine-level bodies and the reference-VA registry |
| `source/patches.*` | the ordered option pass |
| `source/preamble.cpp` | the seven unconditional pointer scans and salt fallback |
| `source/stability.ixx` | crash/freeze arms |
| `source/performance.ixx` | loading, streaming, limits, memory |
| `source/graphics.ixx` | shadows, lights, LODs, reflections |
| `source/vehicles.ixx` | vehicle behaviour and model flags |
| `source/input.ixx` | mouse, camera, window, VSync |
| `source/multiplayer.ixx` | GFWL/CitizenIV fixes |
| `source/loading.ixx` | files, saves, DLC |
| `source/compatibility.ixx` | other mods, game builds, packaging |
| `source/interface.ixx` | menu, HUD and audio toggles |
| `source/xliveless_features.ixx` | the five XLivelessAddon features |
| `source/skip_launcher.*` | SkipLauncher/DFA/ROM stages and file-API hooks |
| `source/compat/xliveless_proxy.cpp` | inert `XLivelessAddon.asi` marker shim |
| `source/data/` | the 38,455-entry model catalogue and the RemoveUselessChecks NOP list |

The per-subsystem files are C++20 modules (`.ixx`), in the same style as
FusionFix: a `module;` global fragment includes the shared headers, then
`export module stability;` and the group class with its static instance.

## Configuration

Options are grouped into `[STABILITY]`, `[PERFORMANCE]`, `[GRAPHICS]`,
`[VEHICLES]`, `[INPUT]`, `[MULTIPLAYER]`, `[LOADING]`, `[COMPATIBILITY]`,
`[XLIVELESS]` and `[INTERFACE]`. Keys are unique across sections, so
`Config::Option` looks in `[Options]` first (old flat files) and then walks the
grouped sections.

`ini_import.cpp` owns the first-run path: it writes the embedded template when
`IV.EFLC.OpenPreCEPatch.ini` is absent, or builds that file from an old
`ZolikaPatch.ini` (values, the `MiscFixes` split and the light tables). There is
no external converter.

## FusionFix gating

`fusionfix_compat.cpp` looks for a `*FusionFix*.asi` in the module directory.
When one is present, `Config::Option` returns false for every key in its conflict
table. The table names the FusionFix module/setting behind each gate; the INI
marks the same options with `FF`.

## XLivelessAddon

`XLivelessAddon.asi` is a marker shim: its `DllMain` and `InitializeASI` are
empty, and it exports `OpcepCompatShim`. Its only job is to satisfy a plugin
that checks whether `XLivelessAddon.asi` is loaded. OpenPreCEPatch recognises the
export and does not treat the shim as an external XLivelessAddon.

## Install order

Each feature group is a class with a static instance; its constructor registers
its arms with an explicit order number. `source/patches.cpp` walks the sorted
registry. This keeps the original loader sequence independent of static-init
order across translation units. It matters because some arms publish resume
addresses before another arm overwrites the site that owns them.
`RemoveUselessChecks` runs first; all slot publishes run before the install arms.
The five XLivelessAddon arms occupy the position the old `MiscFixes` bundle had.

## Hooks

`source/hooks.cpp` registers every body in `kAbiBodies`, keyed by the original
destination VA. Installers take a body address through `Abi("name")`, a small
name table generated from the same definitions. A row whose body is missing is
skipped, matching the original handler's behaviour.
