# The 18-cell dump matrix

Nine supported images x two cells -- the **full** cell (the shipped INI) and the
**per-option** cell (every key off, one on) -- measured against the reference
tree's stored dumps in `readable/build/p3-final-dumps3`. Generated, not
hand-written:

```
python tools/dump_matrix.py --markdown docs/matrix.md
```

Each cell is its own subprocess. Every number is the scoreboard's comparison
(`tools/parity_report.py`): a branch displacement -- the four bytes after an
`E8`/`E9`, found in either cell or in the pristine image -- is excluded, because
the baseline recorded one stand-in address for every body target and those bytes
cannot be compared. `differing`, `missing` and `extra` count real bytes only.

**Retained-surface note.** The baseline is the original ZolikaPatch dump. This
distribution drops the 14 keys OpenPreCEPatch removed or moved to XLivelessAddon
and has five documented improved deltas. The full cell's `missing` therefore
contains those expected bytes (the residual names them), while `differing` is
`0` on every build; the per-option `closed / 85` excludes the five deltas. This is
the retained-arm check, not a 1:1 distribution score.

## Full cell

`bytes` is what our cell writes; the rest is against the baseline. The residual
column names the keys whose per-key cell writes those bytes (a byte can be named
by more than one key, so the counts need not add up to the byte count), each with
its scoreboard state: `closed`, `open`, or `oracle-verified baseline omission`.

| build | version | image | bytes | differing | missing | extra | residual |
|---:|---|---|---:|---:|---:|---:|---|
| 1 | 1.0.0.0 | `GTAIV_1000.exe` | 368 | 0 | 27 | 6 | missing 27: `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `GarageModelFix` 2 (open), `ImprovedShaderStreaming` 2 (open), `NoImpostorHeightCutoff` 2 (open), `SkipLauncher` 2 (closed), `BadPlayerModelCrashFix` 1 (open), `MovingPlaneParts` 1 (open), `MovingSteeringWheelSupport` 1 (open), `SCOSignatureFix` 1 (open), `ShakingEngineBlock` 1 (open), `SkipIntro` 1 (closed), partly from open keys; extra 6: `CarDynamicShadowFix` 6 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 5 | 1.0.4.0 | `GTAIV_1040.exe` | 427 | 0 | 29 | 7 | missing 29: `SkipLauncher` 9 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 7: `CarDynamicShadowFix` 6 (oracle-verified baseline omission), `GarageModelFix` 1 (open), partly from open keys |
| 6 | 1.0.0.4 | `GTAIV_1004.exe` | 453 | 0 | 30 | 2 | missing 30: `SkipLauncher` 9 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 7 | 1.0.6.0 | `GTAIV_1060.exe` | 819 | 0 | 29 | 2 | missing 29: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 8 | 1.0.7.0 | `GTAIV_1070.exe` | 842 | 0 | 28 | 2 | missing 28: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `NoPhysicsSleep` 4 (closed), `BorderlessWindowed` 3 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 9 | 1.0.8.0 | `GTAIV.exe` | 797 | 0 | 28 | 2 | missing 28: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 11 | 1.1.1.0 | `EFLC_1110.exe` | 755 | 0 | 29 | 2 | missing 29: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 12 | 1.1.2.0 | `EFLC_1120.exe` | 752 | 0 | 29 | 2 | missing 29: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), `VRAMFix` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |
| 13 | 1.1.3.0 | `EFLC_1130.exe` | 739 | 0 | 28 | 2 | missing 28: `SkipLauncher` 8 (closed), `BuildingAlphaFix` 7 (open), `BorderlessWindowed` 4 (closed), `NoPhysicsSleep` 4 (closed), `ImprovedShaderStreaming` 2 (open), `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), `SkipIntro` 1 (closed), partly from open keys; extra 2: `CutsceneFixes` 1 (open), `GarageModelFix` 1 (open), all from open keys |

A **faulted** full cell's partial rows are not a write-set scoreboard (the
preserved 1.0.0.0 fault stops the install pass); its diff/missing/extra cells
are `-`, the fault is reported below, and the per-option/isolated cells are the
scoreboard for that build.

## Per-option cell

`closed / 99` is the key scoreboard: a key is closed when its cell has the same
rows as the baseline's, branch displacements aside. `open` names the keys that
are not; `oracle` names the keys whose difference is a measured omission in the
stored baseline (`tests/oracle_verified.tsv`, per key and build) and is therefore
reported apart and never counted as closed; `fault` names the cell that died.

| build | version | closed / 99 | open | oracle | fault |
|---:|---|---:|---|---|---|
| 1 | 1.0.0.0 | 74 | `BadPlayerModelCrashFix`, `SCOSignatureFix`, `GarageModelFix`, `MovingPlaneParts`, `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `BuildingAlphaFix`, `ImprovedShaderStreaming`, `NoImpostorHeightCutoff`, `RemoveGFWLUpdatePopup` | `CarDynamicShadowFix` | - |
| 5 | 1.0.4.0 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | `CarDynamicShadowFix` | - |
| 6 | 1.0.0.4 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | - | - |
| 7 | 1.0.6.0 | 78 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup`, `Patch4RemoveModChecks` | `Patch7RemoveModChecks` | - |
| 8 | 1.0.7.0 | 78 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup`, `Patch4RemoveModChecks` | `Patch7RemoveModChecks` | - |
| 9 | 1.0.8.0 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | - | - |
| 11 | 1.1.1.0 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | - | - |
| 12 | 1.1.2.0 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | - | - |
| 13 | 1.1.3.0 | 80 | `GarageModelFix`, `BuildingAlphaFix`, `CutsceneFixes`, `ImprovedShaderStreaming`, `RemoveGFWLUpdatePopup` | - | - |

## The fault

No cell faulted.

## What the matrix proves

18 cell(s) ran for 9 build(s). A per-option cell that
completes does so over all 99 keys, so any fault inside it would have shown up
as an abort; the one fault recorded is the reproduced original fault, not a
harness failure. Every residual full-cell byte has a key named behind it, and
the per-option column is the scoreboard that says whether that key is done.
