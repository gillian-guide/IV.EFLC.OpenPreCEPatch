# Improvements log



One entry per behaviour-changing checkpoint. The improvements are folded into

the original INI options; the test switches are documented in

`docs/testing.md`.



## 2026-10-25 — safety net and folded improvements



**Baseline:** `baseline-openprece-20261025` (commit `175cb56`). Harness

checkpoint: `good-matrix-20261025`. This checkpoint: tag

`good-folded-20261025`.



### Model change (supersedes the per-key pass)



The first implementation added eleven opt-in keys. That surface is gone. Each

improvement now rides on the original option:



| original option | folded improvements |

| --- | --- |

| `NewSavesCompatibility` | guard header/chunk reads, `size < 9`, `malloc`, payload reads; `fseek` over discarded payloads |

| `OutOfCommissionFix` | fail closed on decode/allocation; cache the archive instead of re-decoding and leaking per call |

| `MovingSteeringWheelSupport` / `ShakingEngineBlock` / `MovingPlaneParts` | component/bone-chain guards, script-resolver guard, wheelangle tilt cache |

| `PopupHeadlightsFlag` / `ForceCarHeadlightShadows` | lookup/parameter guards, hash→index cache |

| `BadPlayerModelCrashFix` / `DebugPrintSpawns` | sorted model-name hash index |

| `SkipIntro` | FusionFix-style 300 ms body |

| installer pass | arm logging |



The INI is back to **101 keys / 40 FF**, the parity-era surface. The original

branch remains compiled and is selected by:



- `OPCEP_IMPROVEMENTS` (environment, no INI change):

  `none`, `all`, `only:newsaves,ooc`, `except:trace`, bare `skipintro`, ...

- `OPCEP_PARITY_BASELINE` (CMake option): a separate legacy dump/ASI.

- `tools/opcep_matrix.py --env OPCEP_IMPROVEMENTS=...` for automated cells.



### Safety net



- 114 matrix cells: defaults, all-off, all-on, each option, each section and the

  frozen legacy import.

- Per-cell write sets and per-byte arm attribution in `tests/parity/`.

- Module-relative operands are canonicalised to site-derived markers; an

  unrelated 4 KB global was added, the dump rebuilt, and the defaults write set

  stayed byte-identical.

- `tools/gen_ini_template.py --check` in CI.



### Expected deltas



| comparison | observed |

| --- | --- |

| defaults: improvements on vs off (`OPCEP_IMPROVEMENTS=none`) | **4 bytes**, all `InstallSkipIntro` (the 300 ms body rel32) |

| defaults: `OPCEP_PARITY_BASELINE=ON` vs env-off | **0 bytes** |

| every other option/cell with improvements on vs off | **0 bytes** (runtime branches only) |

| `all-off` | 0 writes |



The four-byte SkipIntro delta is the only write-set-visible improvement. The

guards/caches are runtime branches verified by the native tests and by live

targeted runs; they cannot be proved by a write diff because they do not change

any installed site.



### Tests run



- `cmake --build build --config Release` (x86) — pass.

- `ctest --test-dir build -C Release` — pass. New tests cover the

  `OPCEP_IMPROVEMENTS` parser (all/none/only/except/bare), the environment

  resolution, header-only and malformed save chunks, and the model-name index.

- `cmake --build build64 --config Release --target opcep_parity_dump` — pass.

- `cmake -S . -B build-parity -A x64 -DOPCEP_PARITY_BASELINE=ON` and build the

  parity dump — pass; its defaults output is byte-identical to the env-off run.

- `python tools/opcep_matrix.py --update` then `--check` — pass, 114 cells,

  40,257 writes.

- `python tools/opcep_matrix.py --env OPCEP_IMPROVEMENTS=none --report ...` —

  only `defaults`, `all-on`, `legacy`, `option:SkipIntro` and

  `section:COMPATIBILITY` differ, each by the expected SkipIntro rel32.

- premake `vs2022` + MSBuild Win32 in a scratch copy — pass.

- `llvm-objdump`: the original `SkipIntro` body is still present; the 300 ms

  body is separate at the same resume slot.

- No live game test in this pass; the INI defaults now select the folded

  improvements, so a live run should use `OPCEP_IMPROVEMENTS=none` for the

  parity A/B if needed.


## 2026-10-26 - live-fix pass: windowed port, GFWL disabler, Steam bridge

Triggered by the user's FusionFix install: the real `XLivelessAddon.asi` boots,
the inert shim does not. The shim therefore must not replace a functional
XLivelessAddon; the features it stood in for are being folded into this mod.

### New features

- `BorderlessWindowed` / `DoNotPauseOnMinimize`: ported FusionFix `windowed.ixx`
  logic (GPL-3.0, ThirteenAG). IAT hooks for `CreateWindowExA/W`, `MoveWindow`,
  `AdjustWindowRect`, `SetWindowPos`, `SetWindowLongA`, `SetRect`; style sync,
  centring, resize handling; focus-loss input suppression via the game's
  per-frame input call. The original ZolikaPatch byte patches still run, so the
  parity write set is unchanged.
- `GFWLProtectionDisabler`: same one-byte patch as
  `GFWLProtectionDisabler2019.asi` (`xlive.dll+0xF1605`: 0x74/0xEB -> 0xEB),
  guarded by the module and pre-image.
- `SteamAchievements`: XUserWriteAchievements (xlive ordinal 5278) IAT hook.
  Calls the original first (GFWL achievements stay active), then
  `ISteamUserStats_v012` `SetAchievement`/`StoreStats` for the 1..40 and 51..65
  id table used by the standalone SteamAchievements.asi.

### Why this is independent of TryToSkipAllErrors / VSyncFix

The standalone SteamAchievements.asi detours a game-code function
(`GTAIV.exe+0x2CC2D0` on 1080) and depends on the surrounding code. Any other
patch that changes that region/fingerprint can disturb it. The bridge here sits
at the `xlive.dll!XUserWriteAchievements` import boundary instead: the game's
achievement ids arrive through the documented API, so `TryToSkipAllErrors`
(`lmsgboxExit` first-byte `ret`) and `VSyncFix` (presentation-call operands)
cannot move or overwrite the mirror site. No option gating is needed.

### Diagnostics from the user's log

- The modal "Addresses could not be determined" dialog is from
  `LibertyCityPlates.asi` (ASCII string at its RVA 0x453C0, followed by
  "Make sure you are using the latest version of GTA IV: Complete Edition").
  It is not emitted by OpenPreCEPatch.
- The log's install pass completes; the failure happens afterwards/outside this
  mod. The shim vs real XLivelessAddon difference is being treated as a
  compatibility bug: do not deploy the shim next to a functional
  XLivelessAddon.

### Tests / deltas

- `ctest` PASS (feature-count assertion updated for the two new arms).
- `opcep_matrix --check` PASS, 116 cells / 40,257 writes; the two new options
  have zero-write cells (external module / IAT only).
- No live test yet; the Steam bridge needs the Steam build and `steam_api.dll`.


## 2026-10-26 - XLivelessAddon split

All XLivelessAddon-only features (SkipLauncher, SkipIntro, SkipMenu, BorderlessWindowed, DoNotPauseOnMinimize, VRAMFix, the five GFWL fixes, custom paths/registry) were removed from the main ASI and now live in the separate xlivelessaddon subproject (XLivelessAddon.asi, XLivelessAddon.ini). The main matrix lost those cells (104 cells / 39,807 writes); all-off is 0 again.

## 2026-10-26 - GFWL disconnect regression guard

The split exposed two code paths that were not in the original XLivelessAddon/OpenPreCEPatch combination and are not yet live-validated against a GFWL session:

- the recovered SkipLauncher main stage (the fork now defaults `SkipLauncher=0`, so the original DFA block is the default again);
- the `xlive.dll+0xF1605` protection patch (main now defaults `GFWLProtectionDisabler=0`).

Both remain available as explicit opt-ins. Existing installed INIs keep their old values; set both to 0 to return to the pre-split GFWL behaviour.

## 2026-10-26 - signature scans: executable range, first match

**Baseline:** `41167df` (tag `good-launcher-stage-20261026`). Tags:
`good-fastscan-20261026` (this checkpoint).

### Finding

`helpers.hxx` handed Hooking.Patterns `base..base + SizeOfImage` and every arm
calls `pattern.size()`, which scans the whole range and collects every match.
The original uses `hook::pattern(module)`, i.e. module base to the end of the
**last executable section**, and the arms read match 0. On 1.0.8.0 that is
25.9 MB vs 9.9 MB per scan, times 264 scans; measured 5.82 s for the defaults
installer pass (6.43 s all-on).

### Change

- `pattern::ExecutableView()` returns the PE32 executable range (whole image
  fallback for a non-PE/synthetic buffer or headers that point outside it).
- `helpers.hxx`: `Pattern()` uses that range and `count_hint(1)`; new
  `PatternAll()` keeps every match for the arms that patch match 1 or loop.
- `PatternAll` call sites: `BuildingDynamicShadows` (hash pair),
  `NoImpostorHeightCutoff`, `BenchmarkFix`, `EpisodicVehicleSupport` (two
  NOP pairs) and `StartupTimeFix` (literal loop).
- `preamble.cpp`, `hooks.cpp` `scan_game`, the death-music and new-files scans,
  and the CarDecals double-load probe use the executable range too.

The matrix caught the one non-obvious loop consumer before the commit:
`StartupTimeFix` patched all three matches; with the first-match hint alone it
patched one (12 -> 4 bytes in `option:StartupTimeFix`). It uses `PatternAll`.

### Deltas / measurements

| comparison | result |
| --- | --- |
| `opcep_matrix --check` | **PASS**, 104 cells / 39,807 writes, identical sha256 per cell |
| defaults installer pass (dump, median) | **5.82 s -> 1.87 s** (3.1x) |
| all-on installer pass | **6.43 s -> 1.96 s** (3.3x) |

Zero write-set delta: every pattern that the arms use matches inside `.text`,
and the dropped non-`.text` matches were never part of the original's scan.

### Tests

- `TestPatternScan`: two signature copies in `.text`, one in `.data`;
  `Pattern()` returns only the first `.text` hit and `PatternAll()` both, never
  the `.data` copy.
- `TestExecutableView`: synthetic PE32, non-PE fallback, out-of-image section
  fallback, no-executable-section fallback.
- `ctest` PASS (opcep_tests + xla_tests).
- `python tools/gen_ini_template.py --check` PASS (no INI change).

## 2026-10-26 - XLivelessAddon: CreateFile slot publication

**Tag:** `good-createslot-20261026`.

`ApplySkipLauncher` captured the `FF 15 <slot>` operand into
`SkipLauncherReport::createFileSlot` and `skip_launcher.h` said the create_file
body forwards through that slot, but `SetCreateFileSlot` had no caller. The body
therefore always called `CreateFileA` directly, bypassing whatever the game's
import slot resolved to (including a detour another ASI installed there); the
original trampoline at `0x10004820` reads the slot.

`ApplySkipLauncher` now publishes (or clears) the slot as part of the pass,
before the hook it installs can run. `XLivelessAddon.log` gains
`createFileSlot=XXXXXXXX (forwarding|direct)`.

`xla_tests` (new, Win32, ctest) covers the forwarding body, the
clear-on-missing-capture path, missing signatures installing nothing, and an
empty image as invalid input. A mutation check (removing the publish call) fails
three assertions. `ctest` PASS.

## 2026-10-26 - moving-parts guards complete

**Tag:** `good-guards-20261026`.

The folded component guards selected by `MovingSteeringWheelSupport`,
`ShakingEngineBlock` and `MovingPlaneParts` still dereferenced three runtime
pointers: the script-float slot value (in `script_float()` and in the
unconditional tail) and the vehicle's lock owner before the steering block. A
null value faulted exactly like the original, defeating the guard.

All three checks are inside the existing `guarded` path; the unguarded reads run
unchanged with `OPCEP_IMPROVEMENTS=none` / `OPCEP_PARITY_BASELINE`.
`opcep_matrix --check` PASS, 104 cells / 39,807 writes (zero delta).

## 2026-10-26 - fork diagnostics and pending ROM-overlap decision

`XLivelessAddon.log` now records the detected build id, the borderless and
focus-loss install results, whether the SkipIntro/SkipMenu sites were found, the
VRAM hook count, the ADVAPI32 IAT replacement count, and whether the legacy ROM
window block ran.

That last flag exposes a split leftover: with the shipped `SkipLauncher=1`, the
recovered DFA/ROM stage and the legacy `CreateROMWindow`/`WM_ROM` block both run.
The legacy DFA file hooks are gated by `!bSkipLauncher`; the ROM block is not.
Gating it changes the launcher path, so it is documented in
`docs/improvements.md` and left for a live GFWL A/B rather than changed here.
The fork's unguarded `pattern.get(0)` sites, its busy-wait init loop and the
custom-save-path length overflow are likewise preserved faults and documented.

## 2026-10-30 - DualVehicleHeadlights: install the build-correct body

**Tag:** `good-dualheadlights-20261030`.

The original handler `0x10006770` patches the game match **once** and picks body
`0x10006630` for build ids 1..6 (`v1000`/`v1040`/`v1004`) or `0x100064F0` for 7+
(`v1060` and later, including `v1080` and EFLC). The port registered three arms:
an empty `InstallDualVehicleHeadlights`, plus `...Old` and `...New`, all ungated
and scanning the same five bytes. The old arm ran first; its `E9` overwrote the
signature, so the new arm's scan missed and 1.0.8.0 silently installed the **old**
body. The matrix's attribution showed exactly that (`InstallDualVehicleHeadlightsOld`).

The INI tag `[1000,1004,1040]` therefore described where the accidental old-body
install happened to be correct, not where the feature works. One arm now chooses
the body from the build and installs once; the tag is `[all]`.

Write-set delta:

| cell | before | after |
| --- | --- | --- |
| `option:DualVehicleHeadlights` | 5 bytes, rel32 -> `0x10006630` | 5 bytes, rel32 -> `0x100064F0` |
| `section:GRAPHICS` | same 5 bytes | same 5 bytes, new target |
| `all-on` | same 5 bytes | same 5 bytes, new target |
| defaults / all-off / legacy | unchanged | unchanged (option default `0`) |

Verified with a targeted dump: bytes at RVA `0x4F7CE5` are `E9 06 E8 70 0F`,
which resolve to `0x100064F0` at the dump's `0x400000` image base. The native
feature count dropped from 137 to 135 (two arms collapsed into one).

## 2026-10-30 - option cleanup, regrouping and direct fork fixes

**Defaults are preferred, not inherited.** `BetterMPSync=1`,
`Patch4RemoveModChecks=1`, `RemoveGFWLUpdatePopup=1` and
`TryToSkipAllErrors=0` stay as the chosen defaults (they differ from the
original distribution INI by design).

### Removed

`RemoveUselessChecks`, `NoPhysicsSleep`, `HighQualityReflections`,
`ForceDynamicShadowsEverywhere`, `RemoveBoundingBoxCulling` and
`AddOptionsInMenu`, with their arms, hook bodies (HQR), globals, table data,
menu registration callbacks and INI keys. `ForceCarHeadlightShadows` and
`ForceShadowsOnObjects` are kept. The CarDynamicShadowFix no-FCHS writes are now
unconditional (FCHS remains an opt-in, default `0`).

The IVMenuAPI registration surface (`ProbeMenuApi`, `MenuRegistrations`,
`RegisterMenuOptions`, the PREF_DEATHMUSIC/PREF_FOCUSLOSS callbacks and
`bMenuIntegration`) is gone; only the ZMenuIV version guard remains.

### Regrouped / gated

- MP stability keys moved to `[MULTIPLAYER]`: `AimProjectileCrashFix`,
  `BadPlayerModelCrashFix`, `BlackscreenFix`, `CollisionDeadlockFix`,
  `CrashMsgFix`, `GroupHackFix`, `MPNikoCrashFix`, `PathNodeCrashFix`.
  `FreezeGunFix` moved to `[STABILITY]` (single-player rocket crash).
- `BuildingAlphaFix` is FusionFix-gated (`[all; FF]`).
- Duplicate writes removed: the second v1080 JMP in
  `RemoveRGSCLoginRequirement`, and the `485 DB …` CarPartsShadowFix branch that
  normalises to the same bytes as its first scan.

### Fork (direct, no fault preservation)

Bounded save-path build, `Sleep(1)` + 10 s deadline in the delayed init,
unknown-build skip, match-count guards on the former unguarded pattern sites,
the legacy ROM block gated behind `!SkipLauncher`, and an import-descriptor
count that cannot underflow. See `xlivelessaddon/README.md`.

### Matrix / tests

98 cells / 4,294 writes (six option cells removed); defaults 823 -> 819 (the
four-byte NoPhysicsSleep write), legacy 814 -> 810, all-on 12,700 -> 873
(RemoveUselessChecks alone was 11,824). The per-option union still equals
`all-on` plus the documented CarDynamic/FCHS interaction. `ctest` 2/2 PASS;
`gen_ini_template.py --check` PASS; `opcep_dump` rebuild + matrix `--update`
then `--check` PASS.


