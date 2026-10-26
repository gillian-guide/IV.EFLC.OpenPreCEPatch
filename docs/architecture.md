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
  read the grouped INI, vehicle-light sections and option globals
  ZMenuIV version guard
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
| `source/pattern.*` | signature normalisation, the executable-section range and the scans |
| `source/rebased.*` | the globals the hook bodies address, plus the original-name tables |
| `source/hooks.cpp` | machine-level bodies and the reference-VA registry |
| `source/patches.*` | the ordered option pass, the folded-improvement publish and the optional dump observer |
| `source/improvements.*` | the `OPCEP_IMPROVEMENTS` / `OPCEP_PARITY_BASELINE` test switches |
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
| `xlivelessaddon/` | separate XLivelessAddon successor ASI (own source/INI/CMake) |
| `source/data/` | the 38,455-entry model catalogue and the RemoveUselessChecks NOP list |

The per-subsystem files are C++20 modules (`.ixx`), in the same style as
FusionFix: a `module;` global fragment includes the shared headers, then
`export module stability;` and the group class with its static instance.

## Configuration

Options are grouped into `[STABILITY]`, `[PERFORMANCE]`, `[GRAPHICS]`,
`[VEHICLES]`, `[INPUT]`, `[MULTIPLAYER]`, `[LOADING]`, `[COMPATIBILITY]` and
`[INTERFACE]`. Keys are unique across sections, so
`Config::Option` looks in `[Options]` first (old flat files) and then walks the
grouped sections.

`ini_import.cpp` owns the first-run path: it writes the embedded template when
`IV.EFLC.OpenPreCEPatch.ini` is absent, or builds that file from an old
`ZolikaPatch.ini` (values and the light tables). There is no external converter. `tools/gen_ini_template.py` regenerates
`source/ini_template.hxx`; the raw string is emitted as adjacent literals because
MSVC truncates a single literal above 16,380 bytes.

`PatchGame` also publishes the folded improvement switches into the
`zpatch_opt_*` globals once, before any arm runs. They are resolved from the
original options (`NewSavesCompatibility`, `OutOfCommissionFix`, the moving-parts
trio, `PopupHeadlightsFlag`/FCHS, `BadPlayerModelCrashFix`/`DebugPrintSpawns`)
plus the test-only `OPCEP_IMPROVEMENTS` environment variable or the
`OPCEP_PARITY_BASELINE` build definition. There are no per-fix INI keys; see
`docs/improvements.md` and `docs/testing.md`.

## FusionFix gating

`fusionfix_compat.cpp` looks for a `*FusionFix*.asi` in the module directory.
When one is present, `Config::Option` returns false for every key in its conflict
table. The table names the FusionFix module/setting behind each gate; the INI
marks the same options with `FF`.

## XLivelessAddon

The XLivelessAddon feature set is a **separate subproject** in
`xlivelessaddon/`. It builds `XLivelessAddon.asi` and reads
`XLivelessAddon.ini`; it is not linked into the OpenPreCEPatch module. Do not
load both modules as the owner of the same patches.

## Install order

Each feature group is a class with a static instance; its constructor registers
its arms with an explicit order number. `source/patches.cpp` walks the sorted
registry. This keeps the original loader sequence independent of static-init
order across translation units. It matters because some arms publish resume
addresses before another arm overwrites the site that owns them.
`RemoveUselessChecks` runs first; all slot publishes run before the install arms.

## Hooks

`source/hooks.cpp` registers every body in `kAbiBodies`, keyed by the original
destination VA. Installers take a body address through `Abi("name")`, a small
name table generated from the same definitions. A row whose body is missing is
skipped, matching the original handler's behaviour.

`helpers.hxx` wraps the installers' scans. `Pattern()` scans the executable
sections (the range the original `hook::pattern(module)` used) and stops at the
first match; `PatternAll()` returns every match for the arms that patch match 1
or loop over their hits. The reduction is pure cost: the write set is identical
to scanning the whole image and collecting every match.
