# Architecture

The tree follows the original mod's own source layout: the project's sources live under `src/`, and the two vendored libraries are git submodules under `external/` (`external/injector/`, `external/Hooking.Patterns/`). `ZolikaPatch.sln` / `CMakeLists.txt` build the ASI from there; both add `external/injector/include` and `external/Hooking.Patterns` to the include path.

## Entry point

`dllmain.cpp` exports `DllMain`. Everything happens synchronously on `DLL_PROCESS_ATTACH`; the DRM has already modified the game's code by the time a deferred thread would run, so signatures must be matched now.

```
DllMain
  resolve ZolikaPatch.ini next to the ASI
  read the game's VS_FIXEDFILEINFO -> compact version -> GameBuild
  old-loader warning + unsupported-version guard
  capture xlive.dll / XLivelessAddon.asi / CitizenIV.exe
  read INI, vehicle-light sections, option globals
  ZMenuIV version guard
  SkipLauncher (+ DFA + ROM stages), FixPCIDKickbug
  IVMenuAPI registration, LocalSettingsPath capture
  unconditional preamble (game pointers + hash salts)
  PatchGame (all INI-gated installs)
  LaunchWithPlayGTAIV
```

The same file holds the small support modules the loader needs (INI reader, game identity, signature wrapper, mapped-image writes, model catalogue, vehicle-light tables, IVMenuAPI and ZMenuIV), and - as in the original's single `dllmain.cpp` - the re-created globals, the hook bodies and the inline option blocks.

## Files

| file | responsibility |
| --- | --- |
| `src/dllmain.cpp` | loader, INI feature blocks, build classification, config parser, model hash, signature scanner, map writes, catalogue, vehicle lights, IVMenuAPI/ZMenuIV, re-created globals, hook bodies and their VA table, and the inline option blocks in loader order |
| `src/patch_data.cpp` | the RemoveUselessChecks NOP list (data) |
| `src/xlive/xliveless.cpp` | SkipLauncher/DFA/ROM stages and file-API hooks |
| `src/catalogue_data.cpp` | the 38,455-entry model catalogue (data) |
| `external/Hooking.Patterns/Hooking.Patterns.cpp` | vendored signature scanner (submodule) |
| `src/ooc_payload.h` | original compressed OutOfCommission resource (static data) |
| `data/finale2.sco` | decoded OutOfCommission script table for research |
| `src/INI.h`, `src/common.h`, `src/hooks.h`, `src/patches.h`, `src/patch_data.h`, `src/rebased.h`, `src/xlive/xliveless.h` | declarations shared by those units |
| `external/injector/include/injector/` | LINK/2012's memory-write library (submodule) |
| `src/jitasm.h`, `src/Hooking.h`, `src/Hooking.cpp`, `src/Pool.h`, `src/StdInc.h` | carried from the original tree; not used by the recovered bodies |
| `tests/test_main.cpp` | native assertions; no game files needed |

## Build classification

`QueryGameVersion` reads the executable's file version, composes the four fields as a decimal integer (`1.0.7.0 -> 1070`) and classifies it through the original's switch. The enum value is the original build id (1.0.0.0 = 1, 1.0.4.0 = 5, 1.0.0.4 = 6, 1.0.6.0 = 7, 1.0.7.0 = 8, 1.0.8.0 = 9, 1.1.0.0/1.1.1.0/1.1.2.0/1.1.3.0 = 10/11/12/13, 1.0.6.1 = 15, 1.2.0.43 = 18), and the blocks compare it directly. Unknown installs nothing.

## Build-gated options

The original compares the build id at `dword_1015BE04` before it reads the INI for a handful of options. Those checks stay where the original had them: inline in the option block, with no shared gate table.

| option | how the block gates |
| --- | --- |
| `BenchmarkFix` | `!BuildIs(build, {v1080, v1130})` in the publish block and the recovered list in the install block (the handler's `cmp 9` / `cmp 0Dh`) |
| `VSyncFix` | `BuildIs(build, {v1004, v1060, v1070, v1061, v1100, v1110, v1120, v12043})` — the handler excludes 1.0.0.0, 1.0.4.0, 1.0.8.0 and 1.1.3.0 |
| `LaunchWithPlayGTAIV` | the per-build RVA table holds exactly the handler's switch cases (1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0); any other build finds no row and returns |
| `LoadDLCs`, `Patch6and7RemoveModChecks`, `Patch7RemoveModChecks`, `RemoveUselessChecks`, `SuperLODFix` | per-build `if (build == ...)` blocks (rows 7/8/9, 7/8, 7/8, 5/8/9 and 8/9 respectively), matching the handler's per-build RVA rows |
| `EpisodicVehicleSupport`, `EpisodicWeaponSupport`, `PoliceEpisodicWeaponSupport`, `TBoGTHeliHeightLimit`, `PipeBombDropIconSupport` | `BuildIs(build, {v1004, v1060, v1070, v1080, v1061, v1100, v1110, v1120, v1130, v12043})` — the handler's `cmp [build],6; jl`, which includes 1.0.6.1 |

`DualVehicleHeadlights` has no install gate: its build check only selects which of the two bodies to write, so the option installs everywhere. `CarDynamicShadowFix`, `Patch4RemoveModChecks` and `RemoveRGSCLoginRequirement` have no build compare in the original either (pattern-gated, or the missing-signature fallback), so they stay unconditional. `LaunchWithPlayGTAIV`'s gate was previously recorded as `!=9`; the handler is a switch whose case 9 is the 1.0.8.0 pair, so it installs there.

## Option blocks

`PatchGame` is the original loader's option pass: every INI option appears as an inline `if (ctx.Option(...)) { ... }` block, in the original loader's order. Each block gates on its key (and its build), scans the site and calls `injector::` directly; resume slots are assigned before the site they describe is overwritten, because the original reads them out of the site first. That is also why the slot phase runs before the write phase.

`RemoveUselessChecks` runs before all of them: its NOPs change what the later scans see.

The blocks were recovered from the shipped ASI as row tables and emitted once by `tools/gen_installers.py`; they are ordinary source now and no longer depend on those tables.

The cross-option rules are ordinary `if` conditions in the blocks that need them:

- `CarPartsShadowFix` and `CarDynamicShadowFix` share one block.
- `MiscFixes` is skipped when `XLivelessAddon.asi` is loaded.
- `SkipIntro` / `SkipMenu` / `BorderlessWindowed` are skipped when the FusionFix compatibility probe closes its gate.
- `NewFilesCompatibility` is skipped when `GTAC.dll` or `JacksIVFixes.asi` is loaded.
- `RestoreDeathMusic` also runs when IVMenuAPI integration is available.

## Hooks

`dllmain.cpp` registers every body in `kAbiBodies`, keyed by the original destination VA. `ResolveHookBody` maps a reference VA to the live body. The install blocks never point at a missing body: a row with an unresolved companion is skipped as a unit, matching the handler's behaviour.

## Hash salts

Seven signatures populate the original's loader globals. A final probe looks for the salt-site pattern; when it is absent the loader writes the fallback salt constants (the values the original uses on the probe-miss builds).

## Style

`CONTRIBUTING.md` documents the source style and the linter:

```bat
python tools\style_lint.py --fail-on error
```
