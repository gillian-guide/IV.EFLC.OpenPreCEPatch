# Parity report — 2026-10-25

Baseline: `baseline-openprece-20261025` (commit `175cb56`). Harness checkpoint:
`good-matrix-20261025`. Improvement checkpoint: `good-optin-20261025`.

## Method

`tools/opcep_matrix.py` runs `opcep_parity_dump` for 126 cells against the same
`game_refs/GTAIV.exe` image (hash pinned in `tests/parity/matrix.json`):

- shipped defaults, all-off, all-on;
- each boolean option alone (against all-off);
- each section alone (against all-off);
- `combo:SkipIntroShowScreens` (against `option:SkipIntro`);
- the frozen legacy `ZolikaPatch.ini` through the real first-run importer.

Each cell records the full `RVA byte` write set, its SHA-256, and the arm that
left each byte changed. Module-relative operands are canonicalised to
site-derived markers; adding a 4 KB unrelated global and rebuilding produced a
byte-identical defaults write set, so layout shifts cannot masquerade as
behaviour changes.

## Results

| check | result |
| --- | --- |
| `python tools/opcep_matrix.py --check` | **PASS** — 126 cells, 40,272 writes |
| defaults | 909 writes, hash in `matrix.json` |
| all-off | 0 writes |
| all-on | 12,802 writes |
| legacy fixture | 900 writes |
| each new option alone | 0 writes (runtime branch only) |
| `combo:SkipIntroShowScreens` | 4 bytes vs `option:SkipIntro`, all `InstallSkipIntro` |
| `ctest --test-dir build -C Release` | **PASS** |
| x86 CMake Release build | **PASS** |
| x64 `opcep_parity_dump` build | **PASS** |
| premake `vs2022` + MSBuild Win32 (scratch copy) | **PASS** |

The defaults write count changed from the pre-harness 908 to 909 only because
the canonical representation always emits all four bytes of a module pointer;
no game site changed with the shipped INI. `all-off` remains exactly zero.

## Per-key expectations

| key | expected cells | observed |
| --- | --- | --- |
| `NewSavesGuards` | none (runtime branch) | 0 writes |
| `NewSavesSeekPayloads` | none (runtime branch) | 0 writes |
| `OutOfCommissionSafeInit` | none (runtime branch) | 0 writes |
| `OutOfCommissionCache` | none (runtime branch) | 0 writes |
| `VehicleComponentGuards` | none (runtime branch) | 0 writes |
| `ModelLookupGuards` | none (runtime branch) | 0 writes |
| `SteerLookupCache` | none (runtime branch) | 0 writes |
| `PopupLookupCache` | none (runtime branch) | 0 writes |
| `ModelNameIndex` | none (runtime branch) | 0 writes |
| `TraceInstallArms` | none (runtime branch) | 0 writes |
| `SkipIntroShowScreens` | none alone; 4 bytes with `SkipIntro=1` | as expected |

The shipped INI has `SkipIntro=1` and every new key at `0`; therefore the
default cell still contains the original `SkipIntro` body and all original
fault paths.

## Not covered

No in-game launch was performed in this pass. The opt-in paths that need the
game are the runtime guards/caches (they do not change the write set) and the
300 ms intro body. Live validation should use a targeted INI with one key at a
time and the log enabled; see `docs/testing.md`.

## Known-good history

```
175cb56 baseline: OpenPreCEPatch before fixes and improvements   (baseline-openprece-20261025)
d40c2d7 test: add INI write-set matrix and per-arm attribution   (good-matrix-20261025)
c2ad62a feat: add opt-in stability, performance and FusionFix intro improvements
79fd582 docs: record opt-in improvements, parity tooling and FusionFix attribution
e794437 docs: record disassembly check and opt-in config surface
b95decc fix: accept header-only save chunks and cover them in tests (good-optin-20261025)
```
