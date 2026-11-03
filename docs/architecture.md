# Architecture

The tree follows the original mod's own source layout: the loader and the shared headers live under `src/`, the module units (one logical piece per `.ixx`) under `source/`, and the two vendored libraries are git submodules under `external/` (`external/injector/`, `external/Hooking.Patterns/`). `ZolikaPatch.sln` / `CMakeLists.txt` build the ASI from there; both add `external/injector/include` and `external/Hooking.Patterns` to the include path.

## Entry point

`src/dllmain.cpp` is the **only plain translation unit** and exports `DllMain`. Everything happens synchronously on `DLL_PROCESS_ATTACH`; the DRM has already modified the game's code by the time a deferred thread would run, so signatures must be matched now. It also holds the loader pipeline (`SetStuff`, whose phases are named below) and its helpers; the install arms themselves live in the feature modules, and `PatchGame` walks the two arm tables over them.

```
DllMain
  resolve ZolikaPatch.ini next to the ASI
  read the game's VS_FIXEDFILEINFO -> compact version -> GameBuild
  old-loader warning + unsupported-version guard
  capture xlive.dll / XLivelessAddon.asi / CitizenIV.exe
SetStuff
  phase 1  read INI, vehicle-light sections, option globals
  phase 2  ZMenuIV version guard (unconditional)
  phase 3  SkipLauncher (+ DFA + ROM stages), FixPCIDKickbug
  phase 4  IVMenuAPI registration, LocalSettingsPath capture
  phase 5  the probe: game pointers + hash salts, double-load guard
  phase 6  PatchGame (every INI-gated install, in this tree's arm order: derived
           from the recovered loader, not identical to it; see "Option blocks")
  phase 7  LaunchWithPlayGTAIV
```

The support code is split into modules under `source/`: `common`, `game`, `patch`, `log`, `config`, `catalogue`, `vehicle_lights`, `menu`, `launcher`, `useless_checks`, `hook_bodies` (the rebased globals, the 107 hook bodies and the resolver, plus the plain-TU helpers the bodies call), `xliveless` (SkipLauncher/DFA/ROM) and one module per feature group (`vehicles`, `graphics`, `stability`, `audio`, `input`, `loading`, `mission`, `world`, `multiplayer`, `misc`). Types the plain TU and the headers name stay in `src/*.h`; the modules move implementations and export the declarations.

## Files

| file | responsibility |
| --- | --- |
| `src/dllmain.cpp` | the plain TU: `DllMain`, the loader pipeline (`SetStuff` and its phases), `PatchGame`'s two arm tables, `FindGamePointers`, the INI/config helpers, the SkipLauncher/DFA/launcher stages |
| `source/hook_bodies.ixx`, `source/hook_bodies.cpp` | the rebased globals, the hook bodies (interface declares, implementation unit defines: MSVC rejects naked bodies in an imported interface) and the resolver (`BodyId`/`kAbiBodies`) |
| `source/ooc.cpp`, `source/nms_allocator.cpp`, `source/new_saves.cpp`, `source/script_calls.cpp`, `source/vehicle_steer.cpp` | the compiled helpers the bodies call: the OutOfCommission decoder/archive, the NewMemorySystem allocator, the NewSaves converter, the native-script call layer and the steer/engine/plane consumer (plain TUs, ASI/test-side only) |
| `source/xliveless.ixx`, `source/xliveless.cpp` | SkipLauncher/DFA/ROM stages, file-API hooks and the ROM window |
| `source/<feature>.ixx` | one module per feature group: that group's `Publish*`/`Apply*` arms, patterns, constants and config reads, in this tree's arm order |
| `source/patch.ixx` | signature normalisation and scans, all-hit collection, the mapped-image write and the arms' scan/write helpers |
| `source/game.ixx`, `source/common.ixx`, `source/log.ixx`, `source/config.ixx` | build identity, shared bounds/text/byte helpers, the boot/fault trace, the config document |
| `source/useless_checks.ixx` | RemoveUselessChecks module: the original handler's three write shapes over its five RVA tables and 23 singles |
| `source/catalogue.ixx` | model-catalogue module: count, index and name-hash lookups over the generated table |
| `source/launcher.ixx` | the launch-slot rewrite bytes and the `LaunchGTAIV.exe\0` scan signature |
| `source/vehicle_lights.ixx`, `source/menu.ixx` | the two 512-slot light tables; IVMenuAPI/ZMenuIV |
| `src/useless_checks_data.inc` | generated: the five original RVA arrays and the single-write rows (`tools/extract_useless_checks.py`) |
| `src/catalogue_data.cpp` | the 38,455-entry model catalogue (generated data) |
| `external/Hooking.Patterns/Hooking.Patterns.cpp` | vendored signature scanner (submodule) |
| `src/ooc_payload.h` | original compressed OutOfCommission resource (static data) |
| `data/finale2.sco` | decoded OutOfCommission script table for research |
| `src/common.h`, `src/hooks.h`, `src/patches.h`, `src/rebased.h`, `src/hook_bodies.h`, `src/INI.h`, `src/xlive/xliveless.h` | declarations shared by the loader, the modules and the tests |
| `external/injector/include/injector/` | LINK/2012's memory-write library (submodule) |
| `tests/test_main.cpp` | native assertions; no game files needed |

## Build classification

`QueryGameVersion` reads the executable's file version, composes the four fields as a decimal integer (`1.0.7.0 -> 1070`) and classifies it through the original's switch. The enum value is the original build id (1.0.0.0 = 1, 1.0.4.0 = 5, 1.0.0.4 = 6, 1.0.6.0 = 7, 1.0.7.0 = 8, 1.0.8.0 = 9, 1.1.0.0/1.1.1.0/1.1.2.0/1.1.3.0 = 10/11/12/13, 1.0.6.1 = 15, 1.2.0.43 = 18), and the blocks compare it directly. Unknown installs nothing.

## Build-gated options

The original compares the build id at `dword_1015BE04` before it reads the INI for a handful of options. Those checks stay where the original had them: inline in the option block, with no shared gate table.

| option | how the block gates |
| --- | --- |
| `BenchmarkFix` | `!BuildIs(build, {v1080, v1130})` in the publish block and the recovered list in the install block (the handler's `cmp 9` / `cmp 0Dh`) |
| `VSyncFix` | `BuildIs(build, {v1004, v1060, v1070, v1061, v1100, v1110, v1120, v12043})` — the handler excludes 1.0.0.0, 1.0.4.0, 1.0.8.0 and 1.1.3.0 |
| `LaunchWithPlayGTAIV` | an explicit five-build switch (the handler's switch cases 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0); the scan for `LaunchGTAIV.exe\0` also hits 1.0.0.4/1.0.1.0 and EFLC, so the gate is load-bearing |
| `LoadDLCs`, `Patch6and7RemoveModChecks`, `Patch7RemoveModChecks`, `RemoveUselessChecks`, `SuperLODFix` | per-build `if (build == ...)` blocks (rows 7/8/9, 7/8, 7/8, 5/8/9 and 8/9 respectively), matching the handler's per-build RVA rows |
| `EpisodicVehicleSupport`, `EpisodicWeaponSupport`, `PoliceEpisodicWeaponSupport`, `TBoGTHeliHeightLimit`, `PipeBombDropIconSupport` | `BuildIs(build, {v1004, v1060, v1070, v1080, v1061, v1100, v1110, v1120, v1130, v12043})` — the handler's `cmp [build],6; jl`, which includes 1.0.6.1 |

`DualVehicleHeadlights` has no install gate: its build check only selects which of the two bodies to write, so the option installs everywhere. `CarDynamicShadowFix`, `Patch4RemoveModChecks` and `RemoveRGSCLoginRequirement` have no build compare in the original either (pattern-gated, or the missing-signature fallback), so they stay unconditional. `LaunchWithPlayGTAIV`'s gate was previously recorded as `!=9`; the handler is a switch whose case 9 is the 1.0.8.0 pair, so it installs there.

## Option blocks

`PatchGame` is the reconstruction's own arm schedule. Each INI option's arm is a function in its feature module (`source/<feature>.ixx`: `Publish*` for the slot phase, `Apply*` for the install), and `PatchGame` walks two `constexpr` tables of arm pointers (`kPublishArms`, `kInstallArms`) in the order this tree has always used: derived from the recovered loader but **not** the original loader's order (measured first-reference rank displacements reach 89: `NoLODLightHeightCutoff` original 3 → here 90, `CarDecalsFix` 0 → 79, `NoPhysicsSleep` 1 → 85, `LocalSettingsPath` 93 → 12). Each arm gates on its key (and its build), scans the site and calls `injector::` directly; the slot publishes all run before the first install, because a site write destroys the signature a later publish scans for, and because the original reads a site's original bytes before overwriting them.

Two ordering relations are load-bearing and pinned by `zp_dump --ordering`: every publish runs before any install, and `RemoveUselessChecks` runs before `MiscFixes` and `RestoreDeathMusic`. Re-ordering the rest of the arms to the recovered loader order is a separate change that would need a per-build dump proof (the 1.0.8.0 hash alone is not sufficient) and is not part of this tree yet.

`RemoveUselessChecks` is applied once, after the `BuildingDynamicShadows` arm, as the original applies it once (the loader's 27th option read). The position is load-bearing in one respect: its tables are already written when `MiscFixes` and `RestoreDeathMusic` scan, so their signatures are destroyed and those two installs are suppressed exactly as in the original. Moving it after either arm re-enables that install, and the `zp_dump --ordering` CTest (`zp_dump_ordering`) fails if that happens.

The arms were recovered from the shipped ASI as row tables and emitted once by `tools/gen_installers.py`; they are ordinary source now (moved verbatim into the feature modules by the module split) and no longer depend on those tables.

The cross-option rules are ordinary `if` conditions in the blocks that need them:

- `CarPartsShadowFix` and `CarDynamicShadowFix` share one block.
- `MiscFixes` is skipped when `XLivelessAddon.asi` is loaded.
- `SkipIntro` / `SkipMenu` / `BorderlessWindowed` are skipped when the FusionFix compatibility probe closes its gate.
- `NewFilesCompatibility` is skipped when `GTAC.dll` or `JacksIVFixes.asi` is loaded.
- `RestoreDeathMusic` also runs when IVMenuAPI integration is available.

## Hooks

`source/hook_bodies.ixx` declares every body and `source/hook_bodies.cpp` registers it in `kAbiBodies`, keyed by a `BodyId`: the enum's value is the original destination VA (the resolver's key), and `OriginalHandlerVa` is the only conversion. Each enum entry names the original function and what the body does. `ResolveHookBody` maps a reference VA to the live body. The install blocks never point at a missing body: a row with an unresolved companion is skipped as a unit, matching the handler's behaviour.

`tools/verify_bodies.py` proves the bodies offline. A body carries `@exact 0xVA` when it is a byte-level transcription; the tool finds it in the linker map (build the ASI with `ZP_LINK_MAP=ON`), disassembles it and compares it instruction-for-instruction with the original ASI at the reference VA. Module addresses are not blanked out blindly: internal branch targets are compared as body-relative deltas, every other module operand is bound original↔port with a required bijection, and the bindings `src/rebased.h` documents must agree by symbol name. Compiled C++ replacements are reported as INFO with the original function's disassembled extent and the source comment that documents them, never as byte-equal. It runs as the `zp_body_proof` CTest and as the `verify_bodies` target.

Recovered game-image offsets that no safe signature covers are typed as `GameRva` (`value` plus `site()`), with the build, original function and instruction in the constant's provenance comment. `tools/magic_numbers.py` classifies every hex literal in the hand-written sources (named / documented / transcribed-asm) and fails on anything unclassified.

## Diagnostics

The release ASI is silent: `ZP_TRACE_INSTALLS` and `ZP_TRACE_WRITES` are opt-in
compile definitions (CMake options of the same names, both default OFF), so
`Trace`/`TraceBoot` compile to no-ops and no `ZolikaPatch.trace.log` is written.
Configure with `-DZP_TRACE_INSTALLS=ON` to log every installer arm, and with
`-DZP_TRACE_WRITES=ON` to also log every game write through the trace injector
(`tests/trace_injector`). Both are diagnostic builds; neither changes the write
set. The manual `ZolikaPatch.vcxproj` build is the other way to get the trace
build (it defines the same macros when configured to).

## Hash salts

Seven signatures populate the original's loader globals. A final probe looks for the salt-site pattern; when it is absent the loader writes the fallback salt constants (the values the original uses on the probe-miss builds).

## Style

`CONTRIBUTING.md` documents the source style and the linter:

```bat
python tools\style_lint.py --fail-on error
```
