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
