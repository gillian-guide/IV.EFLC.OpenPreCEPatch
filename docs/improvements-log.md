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

## 2026-10-30 - INI categories: EPISODIC and MISC

`[INTERFACE]` is abandoned. Two new sections group the content/mod-support
options:

- `[EPISODIC]`: `LoadDLCs`, `EpisodicVehicleSupport`, `EpisodicWeaponSupport`,
  `PoliceEpisodicWeaponSupport`, `TBoGTHeliHeightLimit`, `SCOSignatureFix`,
  `PipeBombDropIconSupport`.
- `[MISC]`: `AllowHashesForIDE`, `CopSirensFlag`, `MovingPlaneParts`,
  `MovingSteeringWheelSupport`, `NoLiveryLimit`, `PopupHeadlightsFlag`,
  `ShakingEngineBlock`, `RestoreDeathMusic`.

`Config::Option` and the legacy INI importer still accept the old `[INTERFACE]`
(and `[MAIN]`/`[XLIVELESS]`) section names, so a hand-edited or old grouped file
keeps working. Generated `docs/features.md`/`docs/ini.md` now show 12 sections.

Matrix: 98 -> 99 cells (two new section cells, `section:INTERFACE` removed).
Defaults/all-on/legacy are unchanged (same keys and values). Section deltas:
`section:VEHICLES` 302 -> 23, `section:LOADING` 69 -> 35,
`section:COMPATIBILITY` 57 -> 52, new `section:EPISODIC` 272 and
`section:MISC` 67.

`XLivelessAddon.ini` now uses the original flat `[Options]` style
(`;` comment, `Key=Value`, no aligned spaces); the embedded first-run template
(`kDefaultIni`) is byte-for-byte the same content, so recreating a missing INI
no longer produces different defaults from the shipped one (it previously set
SkipWebConnect/SkipIntro/SkipMenu/DoNotPause/Borderless to 0 while the shipped
file set them to 1).


## 2026-10-30 - adopted INI surface, gate audit and fork ownership

**Adopted from the user's INI:** categories `[STABILITY]`, `[FIXES]`,
`[PERFORMANCE]`, `[GRAPHICS]`, `[EPISODIC]`, `[COMPATIBILITY]`, `[MULTIPLAYER]`,
`[VEHICLES]`, `[MISC]` (MULTIPLAYER after COMPATIBILITY), the user's defaults,
the populated `[HeadlightConeOffsets]` table and the FF tags (32 options; see
`source/fusionfix_compat.cpp` for the authoritative list).

### Gate audit

The binary was re-examined instead of trusting the generated JSON:

- Only 14 handlers read `dword_1015BE04` at all. The rest are genuinely ungated;
  the "missing" gates are successor policy, not lost recoveries.
- `LaunchWithPlayGTAIV`: the JSON's `!=9` is a mis-extraction. Handler
  `0x100441B0` is a switch with a **1080 case** and its own RVA pair; 1080 is
  restored and tagged `[1000,1040,1060,1070,1080]`.
- `RemoveRGSCLoginRequirement`: no option gate in the binary. The `build == 9`
  branch in `0x10045490` is the missing-signature fallback; the tag is `[all]`
  and the 1080 RVA is restored.
- `BenchmarkFix`: `!=9 && !=13` (tag `[all-1080,1130]`).
- `VSyncFix`: `!=1..5,9,13` (tag includes `12043`).
- `Episodic*`, `TBoGTHeliHeightLimit`, `PipeBombDropIconSupport`: mask
  `0x4bfc0` = 1004 + 1060..1130 + 12043. PipeBomb was already gated in code;
  tags are now explicit `[1004,1060-1130,12043; FF]`.
- Semantic gates added per request: `BuildingAlphaFix` 1060+,
  `NewSavesCompatibility` 1060-1080, `OutOfCommissionFix` 1060-1130,
  `RemoveModChecks` 1004/1060/1070, `RemoveGFWLUpdatePopup` 1000-1040.
- `RemoveModChecks` unifies the old `Patch4RemoveModChecks`,
  `Patch6and7RemoveModChecks` and `Patch7RemoveModChecks`; the old keys remain
  readable as aliases. `RemoveGFWLUpdatePopup` shares the same stub and is gated
  to pre-patch-6.
- `LocalSettingsPath` moved to XLivelessAddon. The main arms/registrations are
  gone; the fork implements it the FusionFix way (redirect
  `SHGetFolderPathA/W`), and gates it off when FusionFix is present.

### Matrix / tests

96 cells / 3,909 writes; defaults 493, all-on 855, legacy 810. The removed
cells are `LocalSettingsPath`, `Patch4RemoveModChecks`,
`Patch6and7RemoveModChecks` and the `[INPUT]`/`[LOADING]` section cells;
`RemoveModChecks` and `[FIXES]` are new. `build.bat --test` will be re-run after
the docs commit.

### Reconstruction mirror

`recovered_build_gates.inc` now carries the relational gates the first
extraction dropped: `BenchmarkFix` `0x49de2`, `VSyncFix` `0x49dc0` (15 rows,
test updated). `option_build_gates.json` records the Launch switch and the
RemoveRGSC fallback note. A standalone harness verified the masks; the full
reconstruction suite could not link because its Makefile omits `src/log.cpp`
(pre-existing).

---

## 2026-10-30 — audit implementation round 1

The audit (`docs/entry-points-audit.md`) is now also the work list; this round
applied everything that did not need a live run.

Behaviour fixes, no opt-in (they restore the original's own mechanism):

- `PopupHeadlightsFlag` installs one of the two bodies, like the handler's
  `if`/`else`, instead of both against shared resume slots.
- `LoadDLCs` writes each site once (the recovered rows list one).
- `SCOSignatureFix` anchors its signature on the branch it flips instead of
  writing at `match-32`.

Internal changes with no write-set or behaviour delta:

- `NewMemorySystem`'s registry is a flat open-addressing table over
  `VirtualAlloc` (no per-entry allocation, no throw), with a self-test.
- The garage and popup model-table scans are memoised (negative results are
  never cached; the popup memo keeps the original's last-match rule under the
  folded `popup` group).

Opt-in entry-point rewrites (each outside `all`, original install untouched):

- `garage_entry` — GarageModelFix site A at the function prologue.
- `cutscene_entry` — cutscene camera callee hook, replacing three sites.
- `nms_thunk` — the allocator init thunk, restoring the game's own ctor and
  `atexit`; memory behaviour changes, so it must not be promoted without a live
  test.
- `alpha_nop` — BuildingAlphaFix NOPs only the `jnz`, leaving the `test` flags.

Rejected with evidence: garage site B (loop), the multi-pattern scan pass and
duplicate-scan cache (write-aware invalidation unproven), the LoadDLCs flow
rewrite, the moving-parts/plane vtable rewrites, the popup IDE relocation (needs
a new body and a live test), and the OOC prologue. Deferred for the same reason:
EmissiveLerpFix boundary, NoImpostorHeightCutoff constants, PlaneCrashFix vtable,
CarDynamicShadowFix entry. `AbiByName` was measured unnecessary.

Every step left `build.bat --test`, `opcep_matrix.py --check` (96 cells,
3,909 writes with the opt-in groups off), `gen_ini_template.py --check` and
`gen_version_matrix.py --check` green.

## 2026-10-30 — audit dispositions closed

The three rows still marked "deferred" were closed with evidence rather than
left open, because each would cost more than it saves:

- **NoImpostorHeightCutoff data constants.** The arm writes two 2-byte NOPs
  (0x8A058D, 0x8A118E); shifting the shared height constants would write 4 bytes
  per constant and change every other reader of them.
- **PlaneCrashFix vtable wrapper.** The current fix is one 5-byte JMP
  (0x5E0D0B); a vtable-slot wrapper is the same byte count plus an unverified
  body on a slot shared by the whole class.
- **CarDynamicShadowFix function entry.** 0x40C9F9 is written by *two* options
  (CarDynamicShadowFix and CarPartsShadowFix each install a body there), so an
  entry hook would have to merge both behaviours; the 0x40C9F0 entry has 13
  callers and the body would replay the prologue for no byte saved.

The only open item is a live A/B of the four opt-in groups
(`garage_entry`, `cutscene_entry`, `nms_thunk`, `alpha_nop`), which the boot log
and `tools/opcep_optin.py` were built to make checkable.

## 2026-10-30 — first live run (1.0.8.0, garage_entry on)

The first run on the user's install produced a clean install pass
(`126 writes applied, 11 skipped`) and named every active group in the boot log.
It also exposed two installs that were inert at runtime and one wrong build gate:

- **FreezeCarFix's ten spawn-case hooks never installed.** The bodies existed and
  were in `kAbiBodies` but missing from the `AbiByName` name table, so all ten
  `CallToBody("abi_spawns_case_0x..")` calls skipped ("no body" ×10 in the log).
  The write-set dump already had them, which is why the matrix never noticed:
  the live ASI and the dump disagreed. Names added.
- **GFWLProtectionDisabler's xlive.dll write was refused** by the failsafe
  ("site outside the game image", 0x683D1605). The guard was right to reject a
  stray pointer but wrong to reject a module the option deliberately patches.
  Added `ModuleSiteWritable`/`WriteModuleValue`: a foreign site is accepted only
  inside the target module's own allocation. The strict image check is unchanged
  for every other arm.
- **RemoveModChecks' patch-4 gate targeted 1.0.0.4 instead of 1.0.4.0.** "Patch
  4" in GTA IV's numbering is 1.0.4.0 (id 5); the port had mapped it to 1.0.0.4
  (id 6). The original's patch-4 modded-file check is pattern-only (its recovered
  rows carry no build id), so the narrowed gate stays, now on the right build;
  `docs/version-matrix.json` marks 1.0.4.0 as the semantic build.

Also added: the single `Improvements` INI key (below), because the four opt-in
entry-point rewrites were only reachable through `OPCEP_IMPROVEMENTS`.

Decision on "gate out what does not work on 1.0.8.0": nothing is inert by
design. Every arm the log skipped was one of the two bugs above (now fixed) or a
build-gated option that is *supposed* to skip on 1.0.8.0 (VSyncFix,
RemoveModChecks, RemoveGFWLUpdatePopup). No gate was added.

## 2026-10-30 — second live run (1.0.8.0, `Improvements = 1`, NewMemorySystem on)

`install: 134 writes applied, 0 skipped` — both fixes hold: the ten FreezeCarFix
spawn-case calls install, the xlive byte is written (no "site outside the game
image"), and the three entry rewrites that replace sites account for the count
(126 + 11 fixed - 3 replaced).

The run also reported **more stutters** and an **out-of-memory error on Alt+F4**.
The only new variable against the previous run is `nms_thunk` (it is in
`Improvements = 1`), and the reported failure matches its known risk exactly: the
game's own allocator constructor runs (100 MiB arena) and registers
`atexit(sub_D65C50)`, which on exit resets the vtable to the original allocator
while objects allocated by our `HeapAlloc` allocator are still live — the original
allocator then frees foreign pointers. That is an allocator-shape fault, and it is
why the audit said the thunk must not be promoted without a live test.

Action taken: the convenience toggle (`Improvements = 1`/`all`/`on`) no longer
includes `nms_thunk`; the group stays reachable by name
(`Improvements = all,entry,nms_thunk`) for a dedicated experiment. The stutters
still need isolating: `NewMemorySystem = 1` is the original mod's own
"potentially unstable" option and was already on in the previous run, so the next
A/B is (a) `Improvements = 1` with the allocator rewrite out, then (b)
`NewMemorySystem = 0` as well.

## 2026-10-30 — entry-point rewrites promoted to the default set

The user's 1.0.8.0 run with all four rewrites on was smooth, so the three that do
not touch the allocator are now part of the default set instead of opt-in bits:

| rewrite | effect on the default write set |
| --- | --- |
| `garage_entry` | site moves 0x868BB0 -> 0x868B90 (2 sites / 10 bytes either way) |
| `cutscene_entry` | 3 sites / 9 bytes -> 1 site / 5 bytes at 0x89A640 |
| `alpha_nop` | 13 bytes at 0x559CD3 -> 6 bytes at 0x559CDA (the `test` keeps setting the flags) |

`nms_thunk` stays out of the default set: its live run OOM'd on exit (the game's
own allocator `atexit` reset frees through the original allocator while our
allocations are live). It remains reachable by name (`all,entry,nms_thunk`).

`Improvement::all` keeps meaning the folded groups; the new
`DefaultImprovements()` = folded + the three rewrites is what `all`/absent/`1`
resolve to, and `except:<group>` turns one rewrite off (the parity baseline is
`Improvements = 0`).

Matrix: defaults 493 -> 482 writes, all-on 855 -> 844, legacy 810 -> 799; the
only changed cells are the three options and the sections that own them
(FIXES 120 -> 116, GRAPHICS 72 -> 65, VEHICLES 73 -> 73 with the garage site
moved). Every other cell is byte-identical. `tools/opcep_optin.py` now pins each
rewrite's contribution (`except:<group>`, and `all,nms_thunk` for the allocator
one).

## 2026-10-30 — improvement switches removed; the improved code is the code

The switch layer is gone: `source/improvements.*` deleted, the `zpatch_opt_*`
globals and every branch that selected an original implementation removed, the
`OPCEP_IMPROVEMENTS` environment variable and `OPCEP_PARITY_BASELINE` build
option dropped, and the `Improvements` INI key plus `tools/opcep_optin.py` and
its fixtures removed. Behaviour is unchanged from the promoted default set:
`garage_entry`, `cutscene_entry` and `alpha_nop` are the code; the allocator
thunk stays out (deleted after its live OOM). The write set is byte-identical
apart from the arm name (`InstallCutsceneFixesAtCallee` folded back into
`InstallCutsceneFixes`) and the INI template hash: defaults 482, all-on 844,
legacy 799 writes.

`docs/improvements.md`, `docs/testing.md`, `docs/architecture.md`,
`docs/entry-points-audit.md`, `docs/installing.md`, `readme.md` and
`tests/parity/README.md` were rewritten to describe the code rather than a
switch. `docs/parity-report.md` keeps its historical numbers with a header note.

## 2026-10-30 — the pre-split XLivelessAddon leftovers are deleted

The XLivelessAddon split (`486b8cd`, 2026-10-26) moved its INI keys, arms and
bodies to `xlivelessaddon/`, but six arm functions stayed in the main module
without a `RegisterFeature` row - they could never run, so they were dead code
rather than a double install: `InstallSkipLauncher`, `PublishSkipIntro`,
`InstallSkipIntro`, `PublishSkipMenu`, `InstallSkipMenu` and `InstallVRAMFix`
(VRAMFix's key left the shipped INI in that same split commit, which is why it
"disappeared" from the main INI then while its body stayed here).

Deleted with them: the `abi_skip_intro_body`, `abi_skip_intro_screens`,
`abi_skip_menu_body`, `abi_vram_fix_shim` and `abi_vram_fix_helper` bodies, their
`kAbiBodies`/`AbiByName` rows and dump stub, the `zpatch_skip_intro_match`,
`zpatch_skip_menu_resume`, `zpatch_skip_menu_flag`, `zpatch_vram_arg_a/arg_b/flag`
globals and their `FindGlobalSlot` entries. The write set is unchanged (96 cells,
3,854 writes), which is the proof they were unreachable; `dump_abi.inc` was
regenerated (103 rows). The fork keeps its own copies in `xlivelessaddon/`.

## 2026-10-30 — Steam achievements: the import hook ABI is fixed

`SteamAchievements` never unlocked a Steam achievement and could fault the game
on the first achievement write. `XUserWriteAchievements` is
`DWORD __stdcall(DWORD count, const XUSER_ACHIEVEMENT*, XOVERLAPPED*)` — twelve
stack bytes, `ret 0xC` in both xlive.dll (ordinal 5278; 5277 is
`XUserSetContext`, verified against the export table and the game's own
`XUserSetContext(0, id, value)` call site) and xliveless. The hook was declared
with a leading `user_index` (four arguments, `ret 0x10`), so the game's call
arrived shifted: the count landed in `user_index`, the array pointer became the
count and the overlapped pointer became the array. The original GFWL/xliveless
function was then invoked with a multi-megabyte count over a stack pointer, and
the mirror loop walked the same bogus range; the achievement path corrupted the
stack and faulted instead of unlocking (and GFWL achievements broke with it).

The hook is now the exact three-argument ABI. The install pass hooks the IAT
before `SteamAPI_Init` starts on the worker thread, and ids written before Steam
is ready are queued and replayed, so a write in the first second is no longer
lost. `RequestCurrentStats` runs and the initial callbacks are pumped before the
mirror accepts writes. Ids 41..50 (the multiplayer achievements, matching the
`AWARD_ACHIEVEMENT` index list and the script `ACHIEVEMENT_IDS` enum) are mapped
now too; the mirror logs every id with its API name and says when Steam rejects
one instead of failing silently.

Static check: the ASI map symbol is `_XUserWriteAchievementsHook@12` and the body
ends in `ret 0xC`; a four-argument body is the regression to watch for. The
option remains IAT-only (zero-write matrix cell), so the write set is unchanged.

The first live run after the ABI fix (21:23) showed the other half of the
failure: the hook saw `XUserWriteAchievements count=1 first id=1`, but
`SteamAPI_Init failed, skipped` - a direct `PlayGTAIV.exe` start with the Steam
client running does not inherit the `SteamAppId` the client passes to the
process it launches, so the SDK cannot resolve the game and init fails; the
achievement stayed queued and never reached Steam. The worker now logs the
inherited `SteamAppId` (or supplies `12210` for GTA IV and `12220` for the EFLC
executables when it is absent) and retries init for about 40 s, so a slow client
start is also covered while the ids queue. A dedicated probe (temporary
`ZSteamProbe.asi`, removed after the run) called the game's own
`XUserWriteAchievements` import with id 1; the 21:34 live run then logged
`inherited SteamAppId=12210`, `achievements bridge ready`,
`XUserWriteAchievements count=1 first id=1` and
`achievement 1 (THETOUR): unlocked`, with the GFWL side (xlln) recording the
same write. That is the end-to-end confirmation: hook ABI, GFWL passthrough and
Steam mirror all on the real game.

