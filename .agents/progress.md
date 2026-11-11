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

## 2026-11-11 — remaining changed implementations

- `sco_anchor`: branch-anchored SCOSignatureFix scan (`89 3D … 85 ED 74 1E`, +8),
  fallback to the recovered `-32` arm; same byte on 1.0.8.0.
- `garage_entry`: prologue hook at 0xC68B90 (match - 0x20), xmm0 reloaded, resume
  at +8; 5-byte E9 moves exactly -0x20.
- `cutscene_entry`: one hook at the shared callee 0xC9A640 with a relocated copy
  (13 calls re-pointed, branch fix baked); 3 sites -> 1; falls back on shape or
  allocation miss.
- `caches`: direct-mapped shared model-lookup memo (positive results only);
  `zp_caches` 8 checks. Popup/model-name in-body precompute remains out (asm).
- `dlc_path`: 0x104-byte module-path bound; `zp_dlc_path` 4 checks.
- `import`: first-run `ZolikaPatch.grouped.ini` creation, grouped preference,
  flat file untouched; `zp_import` 11 checks.
- `task_dialogs`: `TaskDialogIndirect` with the ASI's common-controls 6.0
  manifest + `CreateActCtx`; `zp_pe_check` finds the manifest. UI unproven.

## 2026-11-11 — release packaging and the remaining refactors

- Root `IV.EFLC.OpenPreCEPatch.ini` is the shipped file; the flat parity INI
  moved to `tests/parity/ZolikaPatch.ini` and the tools default there.
- `cmake --build --preset release --target package` writes
  `dist/IV.EFLC.OpenPreCEPatch/` and the zip.
- The Episode NOP windows and the simple stability scan/register/jump arms are
  data tables; `FchsCheckCpp` replaces the FCHS asm with null object/lookup
  guards (`zp_fchs`); VRAMFix/FixPCIDKickbug/NoPhysicsSleep/DoNotPauseOnMinimize
  code is deleted.
- 149 bodies, 35 tests, Release/Trace green; parity 80 closed / 5 deltas / 0
  open. The remaining unreferenced graphics/loading arms and the duplicate
  publish/install scan cost (#8) are still open.

## 2026-11-11 — OpenPreCEPatch surface, integrated improvements, audits

- The runtime `OPCEP_IMPROVEMENTS` switch is gone. Failsafes/P13 live in
  `patch.ixx` and are unconditional; config parsing never throws; the improved
  paths are the only paths.
- Catalogue 99 -> 85: the 14 keys oPCEP removed or moved to XLivelessAddon are
  out; `menu.ixx`, `launcher.ixx`, `useless_checks.ixx` and `tests/dfa_close.cpp`
  deleted; the parity INI, applicability table and oracle rows regenerated.
- Ported this round: UAL >= 6.9.0 version-resource threshold (`QueryLoaderVersion`,
  `zp_loader_info`); full C++ DLC discovery on `std::filesystem`
  (`DlcDiscoveryCpp`, `zp_dlc_full`); C++ popup check with resolve/angle memos
  (`zp_popup_cache`); sorted 38,455-name model index (`zp_model_index`);
  GFWL module-image guard (`zp_module_guard`).
- Audit fixes: Steam init outside the state mutex and Steam calls outside it;
  model memo is one 64-bit atomic; NMS page bitmap read under the registry lock;
  NMS allocation arithmetic is 64-bit with an alignment bound; OOC first build
  serialised; `PrepareArenaRegistry` uses `call_once`.
- `parity_report.py 9`: 80 closed, 5 expected deltas, 0 open. 34/34 Release.
- Docs: AGENTS identity/surface, README status, improvements sections 1-6 and
  the parity distribution banner are current again.

## 2026-11-11 — distribution identity

- Artifact/project: `IV.EFLC.OpenPreCEPatch.asi`; version resource follows the
  target name. The shipped INI is OpenPreCEPatch's grouped
  `data/plugins/IV.EFLC.OpenPreCEPatch.ini`, copied beside the build artifact.
- `Config::Load` resolves the INI next to the ASI first, then the working
  directory; the flat `ZolikaPatch.ini` remains the legacy/parity file.
- `Feather::LoadGrouped` normalises the shipped file before the original
  grammar: strips trailing `//` comments, drops `;` header lines (a `;` line
  containing `=` would be a key before the first section and fault), trims
  `key = value`, and therefore keeps every line under the 256-byte buffer.
  `Feather::Load` (flat) keeps the original grammar and its 256-byte fault.
- `zp_shipped_ini` proves the grouped file parses (8 checks); `zp_tests` loads
  the renamed ASI with the shipped INI; 34/34 Release.

## 2026-11-11 — matrix fault capture

- The 1.0.0.0 full cell's preserved NoImpostor fault is an out-of-range
  `pattern.get(1)` (Debug: vector subscript out of range). In Release it is
  undefined: it can fault or spin depending on the heap layout, and the dump
  used to lose the 443-byte partial when the fault happened during install.
- `tests/dump_main.cpp` now runs `Loader::Run` under `__try`, dumps the image as
  it stands on the fault, and exits with the exception code. The full cell
  writes 443 rows again (diff 0). `dump_matrix.py` caps a spinning cell at 120 s
  and reports TIMEOUT, so one UB child cannot stall the matrix.
- Re-ran the 18-cell matrix; see `docs/matrix.md`.

## Open items

- 18-cell `dump_matrix.py` full run; record `differing`.
- Live xlive/Steam validation for `opcep`; a live game run for `nms`.
- Candidates not ported, with reasons in `docs/improvements.md` §4: NewSaves
  hardening, garage/cutscene/NMS-thunk entry rewrites, asm-body caches,
  grouped-INI creation/import, SCOSignatureFix anchor, DLC path buffer.
