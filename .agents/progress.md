# Progress — readable-fusion-opcep

## 2026-11-10 — fork and baseline

- Copied `readable-fusion` to `readable-fusion-opcep` (excluding `build/`, `.git/`,
  `.scratch/`), initialized a new repository. Tag `opcep-base`.
- Verified the fork builds and the inherited suite passes: 22/22 ctest,
  `parity_report.py 9` = 98 closed / 0 open / 1 oracle-verified.

## 2026-11-10 — improvement switch and failsafe primitives

- `source/improvements.ixx`: `OPCEP_IMPROVEMENTS` parser (`none`/`all`/`only:`/
  `except:`/bare list), one-time resolver, test seams, and the failsafe
  primitives `Missing`, `Site`, `Body`.
- `patch.ixx`: `ExpectMatchCount` (P13), gated by the failsafe group.
- `tests/failsafe.cpp`: parser/resolver/containment/match-count and the same
  null dereference in a child under both configurations. 41 checks.
- Decision: unset means the 1:1 path; `all` is the folded set (failsafe only), so
  `except:failsafe` is the 1:1 fault configuration. This is the one deliberate
  inversion of oPCEP's default, required by the "1:1 is the default" rule.
- Tag `opcep-failsafe-switch`.

## 2026-11-10 — failsafe guards and fault parity

- Install-time guards for the oPCEP list: SuperLODFix, PlayerAsPedComponentsFix,
  BadPlayerModelCrashFix, GarageModelFix, PedModelsInCutscenesFix, the
  moving-parts trio, CopSirensFlag, NoLiveryLimit, RemoveGFWLUpdatePopup,
  LoadDLCs; `ExpectMatchCount` also on BuildingDynamicShadows, BenchmarkFix,
  EpisodicVehicleSupport's two rows and LaunchWithPlayGTAIV.
- `tools/check_failsafe_parity.py`: healthy 1.0.8.0 writes identical; forced
  missing removes exactly the guarded E9 run; isolated 1.0.0.0 NoImpostor exits
  0xC0000005 with `except:failsafe` and 0 with `all`. 27 checks.
- `docs/parity.md`: failsafe table. Tag `opcep-deltas` follows the next batch.

## 2026-11-10 — source audit of OpenPreCEPatch

Audited `openprecepatch/source/*` and its shipped INI, not just its docs:

- Option surface: 86 oPCEP keys vs 99 here. oPCEP-only: `GFWLProtectionDisabler`,
  `SteamAchievements`, `RemoveModChecks`. Ours-only: 15 keys oPCEP removed
  (`AddOptionsInMenu`, `FixPCIDKickbug`, `ForceDynamicShadowsEverywhere`,
  `HighQualityReflections`, `NoPhysicsSleep`, `RemoveBoundingBoxCulling`,
  `RemoveUselessChecks`, `MiscFixes`) or moved to XLivelessAddon
  (`BorderlessWindowed`, `DoNotPauseOnMinimize`, `LocalSettingsPath`,
  `SkipIntro`, `SkipLauncher`, `SkipMenu`, `VRAMFix`).
- 15 shipped defaults differ; documented in `docs/improvements.md` §3.
- Implementation deltas catalogued in §4; the not-ported ones have reasons
  (NewSaves rewrite, entry-point rewrites, asm-body caches, grouped INI import).

## 2026-11-10 — changed implementations as groups

- `shader`: two LookupShader calls only (LCP signature intact).
- `alpha_nop`: BuildingAlphaFix `jnz` only.
- `opcep`: GFWLProtectionDisabler + unified RemoveModChecks + SteamAchievements.
- `nms`: private heap, overflow refusal, flat registry, pre-swap vtable capture,
  foreign-pointer fallback.
- `config`: no-throw reads, guarded vehicle table, FusionFix gate.
- `ooc_cache`: one decode/build/mount.
- PopupHeadlightsFlag if/else restored.
- Tests: `zp_config_guard`, `zp_steam`, `zp_warnings`, `zp_applicability`,
  extended `zp_ooc_init`/`zp_nms_arena`, `tools/check_opcep_deltas.py`.
- 30/30 ctest. Tag `opcep-deltas`.

## 2026-11-10 — build matrix and docs

- `data/applicability_evidence.json`, `tools/gen_applicability.py` generate
  `docs/version-matrix.md` and `tests/parity/applicability.tsv`; the drift guard
  is `zp_applicability_drift`. `zp_applicability` proves the refusal in code.
- `docs/improvements.md` written from the source audit; `AGENTS.md` updated.
- 18-cell dump matrix completed: all nine builds `diff 0`; 1.0.0.0 full cell
  faults with `0xC0000005` after 443 bytes (the preserved NoImpostor fault), and
  the per-option cell faults at that key. The only `extra` bytes are the two
  oracle-verified baseline omissions (`SkipLauncher`, `CarDynamicShadowFix` on
  1.0.0.0/1.0.4.0). `docs/matrix.md` regenerated.
- Release and Trace suites: 30/30 each.

## 2026-11-11 — MMA10 investigation and the NewSaves walk

- Read the four crash reports and the 2026-11-04 minidump. Three are unrelated
  plugins (SISCO/overlay stack overflow, CloudFixIV access violations). The
  memory run has `NewMemorySystem=1` and `GTA IV Fatal Error: MMA10` on the
  crashing thread's stack.
- IDA: `0x00456D16` is the deliberate null store in `lmsgboxExit`; the caller is
  `sysMemMultiAllocator::free` (`0x00455FF0`) at `0x00456021`, which calls
  `lmsgboxExit("MMA10")` when none of its children claims the pointer. The
  minidump gives `a2 = 0x979F1B10` and 127,454 live NMS registry entries.
- Root cause: the original `sysMemSimpleAllocator::isThisPointerMine`
  (`0x00454FB0`) is a **range** test; oPCEP's NMS registry is an **exact-start**
  set. An interior or double-freed pointer makes the multi allocator fatal.
- Fix in the `nms` group: a 128 KiB page bitmap answers the range question; the
  exact registry still decides the real free; a non-exact free is a logged no-op.
  `zp_nms_arena` now proves interior containment, interior no-op, page-stays-owned
  and double-release (255 checks). `docs/mma10.md` records the evidence.
- Also shipped the NewSavesCompatibility `hardening` group: validated header and
  chunk reads, `size < 9` rejection, payload `fseek`, and a rejected walk leaves
  the save unpatched. `zp_save_walk` proves the same corrupt files throw under
  1:1 and are `rejected` under the group (264 checks).

## Open items

- 18-cell `dump_matrix.py` full run; record `differing`.
- Live xlive/Steam validation for `opcep`; a live game run for `nms`.
- Candidates not ported, with reasons in `docs/improvements.md` §4: NewSaves
  hardening, garage/cutscene/NMS-thunk entry rewrites, asm-body caches,
  grouped-INI creation/import, SCOSignatureFix anchor, DLC path buffer.
