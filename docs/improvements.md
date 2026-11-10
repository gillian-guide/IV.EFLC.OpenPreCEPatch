# OpenPreCEPatch improvements in this tree

This tree is a fork of `readable-fusion` (the proved 1:1 ZolikaPatch 7.65
replacement) with the OpenPreCEPatch improvements added as **named, revertable
overlays**. The source-level audit below was made against
`openprecepatch/source/*` and `openprecepatch/data/plugins/IV.EFLC.OpenPreCEPatch.ini`,
not against its prose docs; where the prose and the code disagree, the code is
quoted here.

The rule that makes the overlays safe: nothing in this file changes the 1:1
configuration. `OPCEP_IMPROVEMENTS` unset means no group; `all` is the folded
set, which today is only `failsafe`; every other group is named explicitly.
`OPCEP_IMPROVEMENTS=except:failsafe` is therefore the 1:1 configuration the
fault-parity tests use.

## 1. Options OpenPreCEPatch adds (implemented here, opt-in)

| option | oPCEP default | where it lives here | how it is proved |
| --- | ---: | --- | --- |
| `GFWLProtectionDisabler` | 1 | `source/misc.ixx` (`InstallGFWLProtectionDisabler`), `opcep` group | writes xlive.dll+0xF1605 from `jz`/`jmp` to `jmp`, only after reading the pre-image byte; the RVA and opcode are the anchor. No xlive image is available, so the live effect is **unproven**; the option read and the pre-image guard are source-audited. |
| `SteamAchievements` | 1 | `source/steam.ixx`, `opcep` group | hooks xlive.dll ordinal 5278, calls the original first, mirrors ids 1..65 to `ISteamUserStats` v012; `zp_steam` pins the recovered id/name table and the refusal path. Live Steam unlock is **unproven** (no Steam client in the harness). |
| `RemoveModChecks` | 1 | `source/misc.ixx` (`ModChecksEnabled`, `OpenOptionEnabled`), `opcep` group | a unified alias over the three recovered keys: patch-4 on 1.0.4.0, patch-6/7 on 1.0.6.0/1.0.7.0. No new writes; the existing write-set oracles cover the arms. |

These are read outside the recovered 99-key catalogue (so the scoreboard and
the shipped INI keep their 1:1 shape) and only inside the `opcep` group.

## 2. Options OpenPreCEPatch removes or moves (kept here deliberately)

The task is a superset of behaviour, so every key oPCEP dropped is still
implemented and documented here. A migrated oPCEP INI that still carries one of
these keys keeps working.

| option | oPCEP disposition | here |
| --- | --- | --- |
| `AddOptionsInMenu` | removed (IVMenuAPI surface) | kept, `source/menu.ixx`, 1:1 |
| `FixPCIDKickbug` | removed | kept, `source/misc.ixx`, 1:1 registry write |
| `ForceDynamicShadowsEverywhere` | removed | kept, `source/graphics.ixx` |
| `HighQualityReflections` | removed | kept, `source/graphics.ixx` |
| `NoPhysicsSleep` | removed | kept, `source/audio.ixx` |
| `RemoveBoundingBoxCulling` | removed | kept, `source/graphics.ixx` |
| `RemoveUselessChecks` | removed | kept, `source/useless_checks.ixx` |
| `MiscFixes` | folded away (not in the oPCEP INI) | kept as the original bundle, `source/misc.ixx` |
| `BorderlessWindowed` | moved to XLivelessAddon | kept, `source/graphics.ixx` |
| `DoNotPauseOnMinimize` | moved to XLivelessAddon | kept, `source/audio.ixx` |
| `LocalSettingsPath` | moved to XLivelessAddon | kept, `source/loading.ixx` |
| `SkipIntro` | moved to XLivelessAddon | kept, `source/loading.ixx` |
| `SkipLauncher` | moved to XLivelessAddon | kept with the 25-row launcher + DFA/ROM stages, `source/launcher.ixx` |
| `SkipMenu` | moved to XLivelessAddon | kept, `source/loading.ixx` |
| `VRAMFix` | moved to XLivelessAddon | kept, `source/graphics.ixx` |

The three companion-owned implementations are not ported *into this ASI*: they
need the companion's window/savegame/launcher ownership, and duplicating them
would install the same hooks twice. The keys stay here because this tree's 1:1
catalogue has them.

## 3. Shipped defaults OpenPreCEPatch changes (ours stay 1:1)

`gen_ini.py` still generates the original values. The table is the whole delta;
switching a row means editing the INI, not building a different ASI. No code
group changes a default, because defaults are the INI's text.

| key | original / this tree | OpenPreCEPatch |
| --- | ---: | ---: |
| `BetterMPSync` | 0 | 1 |
| `CopSirensFlag` | 1 | 0 |
| `EpisodicVehicleSupport` | 1 | 0 |
| `EpisodicWeaponSupport` | 1 | 0 |
| `ImprovedShaderStreaming` | 1 | 0 |
| `InfiniteCarBottomDistance` | 1 | 0 |
| `MovingPlaneParts` | 1 | 0 |
| `MovingSteeringWheelSupport` | 1 | 0 |
| `PipeBombDropIconSupport` | 1 | 0 |
| `PlaneCrashFix` | 1 | 0 |
| `PopupHeadlightsFlag` | 1 | 0 |
| `RemoveGFWLUpdatePopup` | 0 | 1 |
| `RestoreDeathMusic` | 1 | 0 |
| `ShakingEngineBlock` | 1 | 0 |
| `TryToSkipAllErrors` | 1 | 0 |

One arm-level fallback also differs: `CarDynamicShadowFix`'s absent-key default
is 0 here and 1 in oPCEP (`Game::Option("CarDynamicShadowFix", "1")`). The arm's
own default is the 1:1 one; a present key is identical in both trees.

## 4. Implementation changes, and their disposition

| OpenPreCEPatch change | source evidence | here |
| --- | --- | --- |
| **No-throw config read.** A present but unparseable value falls back to the option default and is counted once. | `config.ixx` `Config::Resolve`/`ParseIntPrefix` | **ported**, `config` group. `Config::Integer`/`Float` use a prefix parse and the default on failure; `Config::ReadModelTables` skips a bad row. `zp_config_guard` proves both sides (1:1 throws, group falls back). |
| **FusionFix overlap gate.** ~30 options are disabled when a FusionFix ASI is present, with the reason recorded. | `config.ixx` `kConflicts`, `Config::Resolve` | **ported**, `config` group. `Config::kFusionFixConflicts`, `Catalogue::Read` returns 0 for a gated key. `zp_config_guard` proves gated/non-gated/absent. |
| **First-run INI creation + legacy import.** A grouped INI is written next to the ASI, old flat keys imported, unsupported/companion keys reported. | `config.ixx` `EnsureConfigFile`, `UnsupportedLegacyKeys`, `CompanionLegacyKeys` | **not ported.** This tree's 1:1 contract is the original flat `ZolikaPatch.ini` with the original keys and sections; writing a grouped file and re-mapping sections would change the INI surface the parity tests pin. The `config` group keeps the *read* hardening, which is the part every arm depends on. |
| **Startup warnings.** Loader probe, unsupported version (1.2.0.1–1.2.9.99, 1.2.0.43 excepted), loaded-twice mutex, FusionFix notice. | `dllmain.cpp`, `warnings.ixx` | **ported**, `warnings` group, synchronous `MessageBoxW` instead of posted task dialogs (the non-negotiable keeps startup synchronous). The unsupported range, the 1.2.0.43 exception, the named mutex and the one-time notice file are `zp_warnings`; the task-dialog presentation and the loader *version-resource* threshold are not ported (the marker probe `IsUALPresent` is). |
| **Arm-write failsafe.** A site outside the image or an unresolved body is a logged skip. | `patch.ixx`, `body_helpers.inc` | **ported**, `failsafe` group. `Failsafe::Site`/`Body`; LoadDLCs applies them per row. The shared decision is `zp_failsafe`. |
| **Runtime-pointer failsafe.** A missing preamble pointer continues with the un-patched value. | `body_helpers.inc` `zpatch_failsafe_skip`; guards in stability/vehicles/loading/compatibility | **ported at the install boundary**, `failsafe` group. A transcribed body is never rewritten: the arm refuses the install, which is the same un-patched outcome. Guarded keys are in `docs/parity.md`'s failsafe table; `zp_failsafe_parity` measures the real images. |
| **P13 multi-hit count.** A fixed-site arm refuses a partial install. | `patch.ixx` `ExpectMatchCount` | **ported**, `failsafe` group, for BuildingDynamicShadows (2), NoImpostorHeightCutoff (2), BenchmarkFix (2), EpisodicVehicleSupport (2+2) and LaunchWithPlayGTAIV (2). The 1.0.0.0 NoImpostor fault is the measured both-sides case. |
| **Launcher: exactly two name slots.** | `config`/launcher arms | **ported**, `failsafe` group, `ExpectMatchCount(pattern, 2, "LaunchWithPlayGTAIV name slots")`; `zp_launcher_slots` still pins the 1:1 two-slot write. |
| **ImprovedShaderStreaming keeps LCP's signature.** Only the two LookupShader calls are replaced; the two chain jumps are dropped. | `performance.ixx` `kHpeFilterCall`/`kVFilterCall` | **ported**, `shader` group, with the call-opcode pre-check. `tools/check_opcep_deltas.py` measures the 10-byte delta. |
| **NewMemorySystem hardening.** Private heap, request-overflow refusal, published-singleton guard, flat registry, foreign-pointer free fallback. | `performance.ixx` `NmsRegistry`, `zpatch_nms_alloc/free` | **ported**, `nms` group. The registry is unconditional (same membership, no per-insert allocation); the heap/overflow/foreign-pointer/capture are grouped. `zp_nms_arena` adds the overflow and foreign-pointer cases. |
| **OutOfCommissionFix cache.** Decode/build/mount once; fail closed on allocation/decode failure. | `stability.ixx` `zpatch_ooc_lookup` | **cache ported**, `ooc_cache` group, with the allocation/decode checks on that path; the 1:1 path keeps the re-run and leak. `zp_ooc_init` proves the cached archive/state. |
| **NewSavesCompatibility hardening.** Header/chunk validation, `size < 9`, malloc checks, seek over payloads. | `loading.ixx` `FileCursor`/`ScanChunks` | **not ported.** The save walk is a proved transcription/rebuild whose fault shapes are in `docs/parity.md`; replacing it would invalidate the instruction proof and the `zp_save_walk` oracle. Documented as a candidate. |
| **GarageModelFix prologue / CutsceneFixes callee / NewMemorySystem thunk.** Opt-in entry-point rewrites. | `entry-points-audit.md`; oPCEP later shipped garage/cutscene, reverted the thunk | **not ported.** Each needs a new body and resume contract; the audit itself calls the callee rewrite a relocation of a 0x2d2-byte function. The 1:1 sites remain. |
| **BuildingAlphaFix `jnz`-only site 2.** | `graphics.ixx` | **ported**, `alpha_nop` group; `tools/check_opcep_deltas.py` measures the 7-byte delta. |
| **PopupHeadlightsFlag if/else.** The EDX twin must not clobber the EAX resume. | `misc.ixx` | **ported unconditionally** (it restores the original handler's own if/else; no write change on a view with one twin). |
| **Garage/popup/model-name caches.** Memoise garage lookups, precompute the popup angle, index the model-name table. | `stability.ixx`, `vehicles.ixx`, `misc.ixx` | **not ported.** The bodies are transcribed asm; adding a cache means rewriting the body and losing the instruction-for-instruction proof. The NMS registry is the one cache that is already C++. |
| **ForceCarHeadlightShadows lookup cache + null guards.** | `graphics.ixx` `abi_fchs_check` | **not ported.** The 46-instruction checker is transcribed asm with a proof row; its cache and `lookup == nullptr`/`base == nullptr` checks would be a body rewrite. The 1:1 unguarded lookup is preserved. |
| **DrivebyFOV resolver SEH.** | `driveby_seh.h` | **already present one layer down.** This tree's `source/native.ixx` rebuilds `sub_10003980` with the original's catch-all `__try/__except`, and the driveby body calls that entry; no second wrapper is needed. `zp_native_guard` proves the swallow. |
| **NewFilesCompatibility scan guards.** | `loading.ixx` update bodies | **not ported.** The four update/registrar bodies are transcribed and their null-scan faults are preserved in `docs/parity.md`; the guards would change the body contract. |
| **MiscFixes per-fix split.** | oPCEP INI | **not ported.** The bundle is the original's own option; splitting it would add keys the 99-key catalogue does not have. Kept 1:1. |
| **LoadDLCs duplicate same-site writes.** | `loading.ixx` | **not applicable.** This tree's per-build row table issues each write once; `full_diff`/the matrix would have caught a duplicate. |
| **LoadDLCs duplicate same-site writes.** | `loading.ixx` | **not applicable.** This tree's per-build row table issues each write once; `full_diff`/the matrix would have caught a duplicate. |
| **SCOSignatureFix anchor on the branch.** | `stability.ixx` | **not ported.** This tree's arm uses the recovered `-32` anchor, and the signature-parity tool pins it; the extended signature would need a nine-image re-verification. |
| **Multi-pattern scan pass / duplicate-scan cache / `AbiByName` hash.** | `entry-points-audit.md` Part B | **not ported.** oPCEP itself rejected the scan cache (an earlier write can change a later scan) and measured the name hash as unnecessary; this tree has no name table with that shape. |
| **DLC discovery path buffer.** | `loading.ixx` | **not ported.** It is a transcribed body; a `std::filesystem` rewrite would change the register/stack contract and the body proof. |
| **Steam achievement mirror.** | `steam.ixx` | **ported**, `opcep` group, `source/steam.ixx`; lazy `SteamAPI_Init` on the first achievement write, queue/replay, packed-xlive refusal. `zp_steam` covers the table and refusal. |
| **GFWL protection disabler.** | `compatibility.ixx` | **ported**, `opcep` group, pre-image byte guard. |
| **Option removals/regrouping/one `RemoveModChecks` key.** | oPCEP INI | **superset instead:** the 99-key catalogue and the per-patch keys stay; `RemoveModChecks` is an alias inside `opcep`. |
| **Default changes.** | oPCEP INI | **documented, not applied** (section 3). |

## 5. Measurement

```
ctest --preset release                       # 30 targets, all green
python tools/parity_report.py 9              # 98 closed, 0 open, 1 oracle-verified
python tools/check_failsafe_parity.py --dump build/win-x86/Release/zp_dump.exe --game ../game_refs
python tools/check_opcep_deltas.py --dump build/win-x86/Release/zp_dump.exe --game ../game_refs
python tools/gen_applicability.py --check
python tools/check_signature_parity.py --game ../game_refs
python tools/check_relocated_literals.py --game ../game_refs
python tools/check_oracle_verified.py
```

The 18-cell dump matrix is `python tools/dump_matrix.py`; its failing 1.0.0.0
cell is the preserved original fault with the failsafe group off. With the group
on that cell completes and the NoImpostor write is absent — the same input, two
outcomes, which is the fault-parity contract.

Measured on this tree (2026-11-10): all nine builds report `diff 0`; the 1.0.0.0
full cell faults with `0xC0000005` after 443 bytes (the preserved
NoImpostorHeightCutoff write through `hits[1]`), and the per-option cell faults
at that key. The `extra` column is the two oracle-verified baseline omissions
(`SkipLauncher`, and `CarDynamicShadowFix` on 1.0.0.0/1.0.4.0), exactly as in
`parity_report.py`. There are no unexcused extra bytes and no differing byte on
any cell.

## 6. What is deliberately still unproven

- `GFWLProtectionDisabler` and `SteamAchievements` need a live xlive.dll and a
  Steam client; only their decisions, tables and refusal paths are measured
  offline.
- The `warnings` dialogs are exercised by their predicates, not by a UI run.
- The `nms` group's private heap and original-vtable capture are exercised on
  the process heap in `zp_nms_arena`; a live game run is still the only proof of
  the allocator swap.
- The `ooc_cache` archive is compared by pointer/state reuse, not by a live
  mount.
- The 18-cell matrix is written by the 1:1 configuration; no cell runs with a
  group on except the dedicated fault/delta cells above.
