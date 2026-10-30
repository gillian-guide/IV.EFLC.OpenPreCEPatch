# Parity report — 2026-10-25

> **Superseded (2026-10-30):** the improvement switches this report was written
> against (`OPCEP_IMPROVEMENTS`, `OPCEP_PARITY_BASELINE`) have been removed. The
> improved implementations are the shipped code; the numbers below are the record
> of the checkpoint that established them.
>
> **Superseded (2026-10-26).** The split moved `SkipIntro`, `SkipMenu`,
> `BorderlessWindowed`, `DoNotPauseOnMinimize` and `VRAMFix` to
> XLivelessAddon, so the 4-byte `SkipIntro` delta below no longer exists in this
> module. The 2026-10-30 cleanup removed six option cells (`RemoveUselessChecks`,
> `NoPhysicsSleep`, `HighQualityReflections`, `ForceDynamicShadowsEverywhere`,
> `RemoveBoundingBoxCulling`, `AddOptionsInMenu`). Current matrix: **98 cells /
> 4,294 writes**, defaults 819, all-off 0, all-on 873, legacy 810; improved and
> `OPCEP_PARITY_BASELINE=ON` dumps are byte-identical on every cell. The
> historical numbers below are kept for the pre-split checkpoint.

Baseline: `baseline-openprece-20261025` (commit `175cb56`). Harness checkpoint:
`good-matrix-20261025`. Folded-improvement checkpoint: `good-folded-20261025`.

## Method

`tools/opcep_matrix.py` runs `opcep_parity_dump` for 104 cells against the same
`game_refs/GTAIV.exe` image (hash pinned in `tests/parity/matrix.json`):

- shipped defaults, all-off, all-on;
- each boolean option alone (against all-off);
- each section alone (against all-off);
- the frozen legacy `ZolikaPatch.ini` through the real first-run importer.

Each cell records the full `RVA byte` write set, its SHA-256, and the arm that
left each byte changed. Module-relative operands are canonicalised to
site-derived markers; adding a 4 KB unrelated global and rebuilding produced a
byte-identical defaults write set, so layout shifts cannot masquerade as
behaviour changes.

The improvements are folded into the original options. Their on/off comparison
uses the test-only switches documented in `docs/testing.md`, not an INI key.

## Results

| check | result |
| --- | --- |
| `python tools/opcep_matrix.py --check` | **PASS** — 104 cells, 39,807 writes |
| defaults | 823 writes, hash in `matrix.json` |
| all-off | 0 writes |
| all-on | 12,700 writes |
| legacy fixture | 900 writes |
| defaults, `OPCEP_IMPROVEMENTS=none` | 823 writes; **byte-identical** (the pre-split `SkipIntro` rel32 delta moved to the fork) |
| `OPCEP_PARITY_BASELINE=ON` dump vs improved dump | byte-identical, 104/104 cells |
| every option/section, improved vs off | byte-identical |
| `ctest --test-dir build -C Release` | **PASS** |
| x86 CMake Release build | **PASS** |
| x64 `opcep_parity_dump` build | **PASS** |
| premake `vs2022` + MSBuild Win32 (scratch copy) | **PASS** |

## Folded mapping and observable effect

| original option | folded improvement | write-set effect |
| --- | --- | --- |
| `NewSavesCompatibility` | guards + payload seek | none (runtime branch) |
| `OutOfCommissionFix` | safe init + archive cache | none (runtime branch) |
| `MovingSteeringWheelSupport` / `ShakingEngineBlock` / `MovingPlaneParts` | component guards + tilt cache | none (runtime branch) |
| `PopupHeadlightsFlag` / `ForceCarHeadlightShadows` | lookup guards + hash cache | none (runtime branch) |
| `BadPlayerModelCrashFix` / `DebugPrintSpawns` | model-name index | none (runtime branch) |
| installer pass | arm logging + executable-range first-match scans | none (log/cost only) |

The guards/caches cannot be proved by a write diff because they do not change
an installed site. They are covered by the native tests (`NewSavesCompatibility`
valid/truncated/`size < 9`/header-only chunks, environment parsing, model index)
and by targeted live runs with `OPCEP_IMPROVEMENTS`.

## Not covered

No in-game launch was performed in this pass. For the runtime-only groups, use a
targeted `OPCEP_IMPROVEMENTS` value (`none`, `only:...`, `except:...`) and the
log. See `docs/testing.md`.

## Known-good history

```
175cb56 baseline: OpenPreCEPatch before fixes and improvements   (baseline-openprece-20261025)
d40c2d7 test: add INI write-set matrix and per-arm attribution   (good-matrix-20261025)
c2ad62a feat: add opt-in stability, performance and FusionFix intro improvements
79fd582 docs: record opt-in improvements, parity tooling and FusionFix attribution
e794437 docs: record disassembly check and opt-in config surface
b95decc fix: accept header-only save chunks and cover them in tests
96e2862 ci: verify the embedded INI template is regenerated
911d1ef docs: add parity report for the opt-in pass
f6f5fd1 fix: keep the original malloc path for zero-payload chunks when guards are off
98a1531 fix: build the model-name index without CRT/TLS-backed statics
da1ad14 docs: note the fixed-array model index
9039ba9 refactor: fold improvements into original options and add test-only switches (good-folded-20261025)
```
