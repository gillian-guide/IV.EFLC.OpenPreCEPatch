# Improvements

The improvements are **part of the shipped code**, not a mode: every option's
implementation is the improved one, and there is no environment variable, build
option or INI key that selects an older branch. `NewSavesCompatibility=1` runs
the guarded/faster converter, `OutOfCommissionFix=1` the cached/safe archive
setup, and so on. Options that moved to the XLivelessAddon companion (intro/menu,
window, launcher, VRAM) are no longer in this module's INI; their improvements
live in that separate repository.

What used to be test switches (`OPCEP_IMPROVEMENTS`, `OPCEP_PARITY_BASELINE`)
is gone. The evidence for a change is the write-set matrix and per-arm
attribution (`tests/parity/`), which is what caught the live-run divergences.

## What each option does now

| original option | what its implementation does |
| --- | --- |
| `NewSavesCompatibility` | validated header/chunk reads, `size < 9`, `malloc`, payload reads; `fseek` over discarded payloads |
| `OutOfCommissionFix` | fail-closed decode/allocation; one-time archive cache instead of per-call decode/leak |
| `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `MovingPlaneParts` | component-registry/bone-chain null guards; script-resolver guard; script-slot and lock-owner guards; wheelangle model→tilt cache |
| `PopupHeadlightsFlag`, `ForceCarHeadlightShadows` | model-lookup/parameter guards; hash→index cache |
| `BadPlayerModelCrashFix`, `DebugPrintSpawns` | one-time model-name hash index |
| `SteamAchievements` | GFWL achievement writes mirrored to Steam at the `XUserWriteAchievements` boundary (import ordinal 5278, the exact 3-argument `__stdcall`); the original GFWL/xliveless call runs first, and writes made before `SteamAPI_Init` finishes are queued and replayed |
| installer pass | every arm logs its name and the pass logs `install: N writes applied, M skipped`; signature scans stay inside the executable sections and stop at the first match |
| every option | every arm write is checked before it lands (site inside the image or inside the target module, destination body resolved) and the runtime bodies skip a missing preamble pointer or game-published lookup instead of dereferencing null |
| `GarageModelFix`, `CutsceneFixes`, `BuildingAlphaFix` | hooked at the better entry point proved on the 1.0.8.0 live run: the garage prologue, one shared camera callee instead of three sites, and the `jnz` alone instead of the `test` + `jnz` pair |

`SkipIntro`, `SkipMenu`, `BorderlessWindowed`, `DoNotPauseOnMinimize` and
`VRAMFix` are XLivelessAddon features now: their improved bodies live in the
separate companion repository and the main module no longer registers those arms.

There is currently **no** write-set-visible improvement in this module: a pure
implementation change must keep the 96-cell matrix byte-identical. The formerly
visible `SkipIntro` rel32 moved out with the option. The scan-range change is
also a pure implementation fix: the original `hook::pattern(module)` never
scanned `.rdata`/`.data`/`.rsrc`, so the write set is identical.

## Cross-build coverage (audit 2026-10-30)

`docs/version-matrix.md` is the table this audit produced: per option, per build,
whether the original handler's own gate admits it (R), whether the original
admits a successor build with no per-build data (S), whether the original is
pattern-gated only (A), or whether the port narrowed an ungated install (G).

Findings, against `docs/version-matrix.json` (IDA) and the arms:

- **Every install is build-gated.** Twelve options have a recovered gate
  (`head_compare`, `switch` or `rva_rows`) and the arm mirrors it exactly;
  `LaunchWithPlayGTAIV` is gated in `source/dllmain.cpp` by the per-build
  launcher table. The other 64 shipped options have no build compare in the
  original either: their install is a pattern scan, so they apply on any build
  whose bytes match. That is a property of the view, not a missing gate, and the
  failsafe layer now makes a non-matching view a logged skip.
- **No arm installs on a build its recovered gate excludes.** The twelve were
  compared instruction-by-instruction; the only mismatches the tool reports are
  the shared arms, where one arm serves two options (RemoveModChecks installs the
  GFWL stub that `RemoveGFWLUpdatePopup` owns on the pre-patch-6 builds).
- **Six arms narrow an otherwise ungated install**: `OutOfCommissionFix`
  (1060-1130), `BuildingAlphaFix` (1060+), `NewSavesCompatibility` (1060-1080),
  `RemoveGFWLUpdatePopup` (1000-1040), `AllowHashesForIDE` (1000-1040) and
  `RemoveRGSCLoginRequirement`'s 1.0.8.0 fallback branch. Each narrowing is
  deliberate: the feature or its payload only exists on those builds. Widening
  one needs per-build evidence (a verified pattern or an RVA row), not a guess;
  none was widened here, and the matrix lists them so the gap stays visible.
- **Untested.** Only the 1.0.8.0 executable is committed, so every non-1080 cell
  is evidence-based, not a live result. A build is "supported" here when its
  gate and pattern are recovered; it is "verified" only on 1080. Testing another
  build means running the dump against that build's executable
  (`python tools\opcep_matrix.py --exe ...`) and, for runtime behaviour, a live
  run on that install.

## Compatibility

- **XLivelessAddon.** Its feature set lives in the separate companion
  repository; there is no marker shim and no duplicate option in the main ASI.
- **FusionFix overlap audit.** The automatic gate list is in
  `source/fusionfix_compat.cpp`; re-check it when FusionFix adds a subsystem that
  collides with a still-active option.
- **Old INI import.** Done: the ASI ports `ZolikaPatch.ini` into the grouped file
  on first run and the option lookup still accepts the flat layout.
- **Build 18 (1.2.0.43).** No verified per-build data exists; pattern rows only.
  A CE executable would need its own evidence before RVA gates are added.

## Performance

The hot caches are now part of the options in the table above. The installer pass
itself was the largest startup cost: 264 signature scans over the whole
`SizeOfImage` (25.9 MB on 1.0.8.0) collected every match even though the arms
read match 0. The scans now stop at the first match and stay inside the
executable sections (9.9 MB), which is what the original `hook::pattern(module)`
did. Measured on the 1080 dump: defaults 5.82 s → 1.87 s, all-on 6.43 s → 1.96 s,
write set unchanged. Remaining candidates:

| site | cost today | candidate |
| --- | --- | --- |
| plane script resolver | re-resolves the script-pointer chain per float read | resolve once per frame / cache the last `(script, index)` pair |
| `OutOfCommissionFix` wrapper | decode + RPF build + mount per call | cached; the archive is intentionally retained |
| duplicate scans | publish and install arms scan the same signature twice | a scan cache is unsafe across arms (earlier arms patch bytes a later scan can match); keep the first-match hint only |

## Stability and safety

### The failsafe layer

Two failures used to be fatal on a build the port only half-recognises, and both
are now a logged clean skip:

- **Install time (always on).** Every `Install*`/`Publish*` arm writes through
  `helpers.hxx` (`JumpToBody`, `CallToBody`, `NopAt`, `WriteRaw`,
  `WriteValue`). Before touching the image the helper checks the site and its
  bytes are inside the game image, and that the body name resolved to a live
  body. A withheld body used to install a jump to 0; an RVA row from the wrong
  build used to write into whatever followed. Both now log one line naming the
  arm (and the missing signature, for a pattern miss) and leave the image
  untouched, which is what the original loader does when a signature does not
  match. On a healthy image every check passes, so the write set is unchanged:
  the matrix reports the same 3,909 writes over 96 cells.
- **Run time.** The bodies call their preamble pointers and game-published
  lookups without checking them, exactly like the original: a missing pointer
  faults. The body now continues with the value the game would have had without
  the fix and logs one line.
  This covers PlayerAsPedComponentsFix, PedModelsInCutscenesFix, SuperLODFix,
  BadPlayerModelCrashFix, GarageModelFix, the moving-parts trio, CopSirensFlag,
  DrivebyFOVFix, RemoveGFWLUpdatePopup and LoadDLCs' path buffer. The guards sit
  at the top of the body, before any register or stack state the resume
  consumes.

The installer pass ends with one `install: N writes applied, M skipped` line,
so a partially-matching image is visible without reading the whole log.

- **Shipped:** NewSaves guards, OOC safe init/cache, component guards, model
  lookup guards, model-name index (table above), and the script-slot/lock-owner
  guards in the moving-parts body.
- **Still candidates (documented, not shipped):**
  - the popup model object itself (not just the lookup result) is dereferenced
    to read the bone/position without a class check;
  - the version guard opens a web page and terminates; make the launch path
    configurable without weakening the default;
  - the double-load guard shows a modal box; prefer a log line with a documented
    opt-out;
  - in-place operand patches (`p-5`/`p-11` in NewSaves) should re-scan and assert
    the pre-image before writing;
  - `Config::Option` keeps `std::stoi`, so a non-numeric value (e.g. `Key = true`
    or an empty value) throws inside `DllMain` and the game exits before any
    install. This is the original's own behaviour (its source uses `std::stoi`
    too), so it is preserved; a guarded variant would be a separate change;
  - the ZMenuIV version guard is skipped when the INI is missing or unreadable,
    although the comment says it is unconditional. The original applies it
    unconditionally; changing this alters startup for a broken install, so it
    needs a decision;
  - `dlc_discovery_post` allocates its 0x104 path buffer with a throwing `new`
    and ignores `GetModuleFileNameA`'s result; `strcat_s` failure leaves the
    module path in `path` (a directory that exists), which registers the wrong
    content path for an over-long install path;
  - `OutOfCommissionFix`'s archive growth uses throwing `operator new` from a
    `noexcept` caller; a failed allocation terminates instead of faulting at the
    original store.

## Entry-point audit (2026-10-30)

`docs/entry-points-audit.md` is an audit-only list: for the options that patch
the game, whether a cleaner *game-side* entry point exists at the same spot
(function prologue instead of a mid-body byte, a shared callee instead of two
call sites, a vtable slot instead of a virtual body, a data value instead of
NOPs), plus the mod-side mechanisms that could be simpler. Findings worth a
decision: one hook at `sub_C9A640` for CutsceneFixes (3 sites → 1), the
NewMemorySystem init thunk instead of the ctor-call/atexit pair, the garage
prologues, a flat open-addressing allocator registry, and a real fragility in
PopupHeadlightsFlag whose site clobbers an unrelated `mov ecx,7` ten bytes before
the instruction it wants. Nothing there is implemented; each row needs approval
and, for the write-set-visible ones, a matrix cell.

## Features that need a real decision
- `SkipMenu` has an archived FusionFix-style savegame check. It changes
  behaviour and needs a savegame-path implementation; not shipped.
- `NewMemorySystem` replaces the game's allocator wholesale and fills
  allocations with `0xCD`. Keep the original behaviour; a guarded variant is a
  separate decision.

## Tooling

- `tools/opcep_matrix.py` produces the per-cell write set, per-arm attribution
  and a committed golden (`tests/parity/`), with `--env` and `--exe` for
  improved-vs-baseline runs.
- `tools/gen_ini_template.py` regenerates the embedded template; the raw string
  is split because MSVC truncates literals above 16,380 bytes. CI runs its
  `--check` mode.
- Derive the hard-coded test counts from the tables instead of repeating them.
- Compare hook bodies against the true function end (map/PDB), not the first
  `RET`.

## Audit 2026-10-26 — candidates and outcome

Evidence from the tree at `41167df` (before this pass): the x86 build, `ctest`
(1 test) and the 104-cell matrix (39,807 writes) were green.

### Implemented

| finding | evidence | outcome |
| --- | --- | --- |
| every installer scan walked the whole 25.9 MB image and collected all matches | `source/helpers.hxx` built `make_range_pattern(base, base+SizeOfImage)` and every arm calls `pattern.size()`; the original scans to the end of the last executable section (9.9 MB) | `ExecutableView` + `count_hint(1)`; five multi-hit arms use `PatternAll`; 3.1-3.3x faster installer pass, zero write-set delta |
| five multi-hit arms silently depended on all matches | `graphics.ixx` (BuildingDynamicShadows hash pair, NoImpostorHeightCutoff), `stability.ixx` (BenchmarkFix), `vehicles.ixx` (EpisodicVehicleSupport), `performance.ixx` (StartupTimeFix literal loop) | explicit `PatternAll`; the matrix first caught StartupTimeFix as a missed loop consumer |
| the fork captured the CreateFile IAT slot but never published it | `skip_launcher.cpp` filled `report.createFileSlot` and `skip_launcher.h` documented forwarding, but `SetCreateFileSlot` had no caller; the body always called `CreateFileA` directly | published inside `ApplySkipLauncher`; `xla_tests` (mutation-checked) |
| the moving-parts guards still dereferenced three runtime pointers | `hooks.cpp` `script_float`, the unconditional tail and the steering lock owner only checked the script context | guarded inside the existing folded path; original path still reachable with `OPCEP_IMPROVEMENTS=none` |
| the fork logged no build id or per-patch counts | the companion XLivelessAddon repository's `source/dllmain.cpp` logged only the config line and SkipLauncher counts | build, window/borderless/focus, skip-intro/menu sites, VRAM hooks, registry IAT replacements and the legacy ROM flag are logged |

### Documented, left unchanged (fault preserved)

The XLivelessAddon paths in this section refer to the separate companion
repository, not this tree.

- `XLivelessAddon/source/dllmain.cpp:885` — the legacy ROM block calls
  `hook::pattern(...).get(0)` for two secondary signatures with no size check;
  a missing signature is undefined behaviour, exactly like the original addon.
- `XLivelessAddon/source/dllmain.cpp:993-996` — the borderless
  `DefWindowProc` shims use `pattern.count(2).get(0/1)` unguarded.
- `XLivelessAddon/source/dllmain.cpp:404` — the delayed-init path busy-waits
  (`while (!(pattern.size() > 0))`) with no sleep; on a build where the probe
  never matches, that worker thread never exits.
- `XLivelessAddon/source/dllmain.cpp:67` — `szCustomSavePath.copy(pBuffer, 256)`
  followed by `pBuffer[len] = 0` writes past a 256-byte buffer when the custom
  path is longer than 255 characters (original addon code).
- `source/config.cpp:50` — `std::stoi` throws on a non-numeric value; the
  original mod's source uses `std::stoi` in the same way.
- `source/dllmain.cpp:290-309` — the ZMenuIV guard is skipped when the config is
  missing, despite the "unconditional" comment.

### Fork decision pending a live test

With the shipped `SkipLauncher = 1`, the recovered DFA/ROM stage
(`skip_launcher.cpp`) and the legacy `CreateROMWindow`/`WM_ROM` block
(`XLivelessAddon/source/dllmain.cpp:894-902`) both run. The legacy DFA file
hooks are already gated by `!bSkipLauncher`; the ROM block is not. Gating it is a
launcher/GFWL change, so it is only made observable (`legacyRom=` in the log)
until a live A/B on the user's install confirms which ROM service the game
actually uses.

## 2026-10-30 — option removals, regrouping and direct fork fixes

**Defaults are chosen, not inherited.** Four template values differ from the
original distribution INI and are kept deliberately as the preferred defaults:
`BetterMPSync=1`, `Patch4RemoveModChecks=1`, `RemoveGFWLUpdatePopup=1`,
`TryToSkipAllErrors=0`. They are no longer treated as accidental divergence.
`FixPCIDKickbug` remains unimplemented; `RestoreDeathMusic=1` matches the user's
copy of the original INI.

### Removed options

| key | reason |
| --- | --- |
| `RemoveUselessChecks` | 11,824 bytes over 2,735 rows on 1080; 2,230 rows are whole `call 0x74A710` and `0x74A710` is a bare `ret`. It skips calls to an empty stub, not expensive checks, and is highly incompatible. |
| `NoPhysicsSleep` | zeroes the `.data` float at RVA `0xC90B18` (ships `0.005f`); that stops physics objects sleeping, a realism change at a likely CPU cost, not a performance win. |
| `HighQualityReflections` | heavy eye candy, default 0, no QoL/perf value in a stability-focused build. |
| `ForceDynamicShadowsEverywhere` | same; a broad graphics toggle that only costs frames. |
| `RemoveBoundingBoxCulling` | niche/unstable escape hatch tied to HQR; it did force the non-culling branch, but both the feature it served and the need for it are gone. |
| `AddOptionsInMenu` | the IVMenuAPI registration surface (two PREF toggles and their callbacks) was removed with the option; only the ZMenuIV version guard remains. |

`ForceCarHeadlightShadows` and `ForceShadowsOnObjects` are kept (FCHS is still
default 0, FF-gated, and the CarDynamicShadowFix no-FCHS writes are now
unconditional because that option is the only survivor of the pair).

### Regrouping and gates

- The multiplayer stability fixes moved from `[STABILITY]` to `[MULTIPLAYER]`:
  `AimProjectileCrashFix`, `BadPlayerModelCrashFix`, `BlackscreenFix`,
  `CollisionDeadlockFix`, `CrashMsgFix`, `GroupHackFix`, `MPNikoCrashFix`,
  `PathNodeCrashFix`. `FreezeGunFix` moved the other way (it is a single-player
  rocket crash, not a multiplayer fix). `SMPA60Fix` stays in STABILITY because it
  also fires in single player with car mods.
- `BuildingAlphaFix` is now FusionFix-gated (`[all; FF]`; `fixes.ixx` LOD
  fade-in).
- Duplicate writes removed: `RemoveRGSCLoginRequirement` wrote the same v1080
  JMP twice, and `CarPartsShadowFix` scanned the `485 DB …` signature that
  normalises to the same `85 DB …` bytes as its first scan.

### Fork: direct fixes (no fault preservation)

- `getSavefilePath` builds into a bounded `snprintf` buffer, so a custom save
  path over 255 chars is truncated and logged instead of writing past the
  caller's 256-byte buffer.
- The delayed-init probe waits with `Sleep(1)` and gives up after 10 s with a log
  line instead of spinning a core forever.
- An unrecognised build (`gv() == UINT_MAX`) now logs and installs nothing,
  matching the main module instead of dereferencing unsupported patterns.
- The unguarded `pattern.get(0)` / `get_first` sites (savegames path, DFA
  secondary jump targets, legacy ROM window, SendMessage) and the borderless
  `count(2)` shim now check the match count and log a miss.
- The legacy `CreateROMWindow`/`WM_ROM` service is gated behind
  `!bSkipLauncher`, so the recovered ROM stage is the only owner when
  `SkipLauncher=1`; the `legacyRom` log field reports which path ran.
- The import-descriptor count no longer underflows when the import directory is
  empty.

The current matrix is **96 cells / 3,874 writes** (the 2026-10-30 six option
cells removed; defaults refreshed 2026-10-31); `--check` passes with the new
golden.

