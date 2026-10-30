# Improvement model and backlog

The improvements are **folded into the original INI options**: there are no
per-fix keys. `NewSavesCompatibility=1` means the guarded/faster converter,
`OutOfCommissionFix=1` means the cached/safe archive setup, and so on. An INI
from the parity era still enables the improved path for the options it already
has. Options that moved to the XLivelessAddon module (intro/menu, window,
launcher, VRAM) are no longer in this module's INI; their improvements live in
`xlivelessaddon/`.

The original code path is still compiled. It is selected by the test-only
switches in `docs/testing.md`:

- `OPCEP_IMPROVEMENTS` environment variable (per process / live A/B);
- `OPCEP_PARITY_BASELINE` CMake build option (a separate legacy ASI or dump);
- the parity matrix and per-arm attribution (`tests/parity/`).

Neither switch is an INI key; a normal user never has to toggle a fix
individually.

## Folded mapping

| original option | improvements folded into it |
| --- | --- |
| `NewSavesCompatibility` | validated header/chunk reads, `size < 9`, `malloc`, payload reads; `fseek` over discarded payloads |
| `OutOfCommissionFix` | fail-closed decode/allocation; one-time archive cache instead of per-call decode/leak |
| `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `MovingPlaneParts` | component-registry/bone-chain null guards; script-resolver guard; script-slot and lock-owner guards; wheelangle model→tilt cache |
| `PopupHeadlightsFlag`, `ForceCarHeadlightShadows` | model-lookup/parameter guards; hash→index cache |
| `BadPlayerModelCrashFix`, `DebugPrintSpawns` | one-time model-name hash index |
| `SteamAchievements` | GFWL achievement writes mirrored to Steam at the `XUserWriteAchievements` boundary; the original GFWL call still runs |
| installer pass | arm logging is part of the normal trace (`OPCEP_IMPROVEMENTS=except:trace` silences it); signature scans stay inside the executable sections and stop at the first match |
| every option (the `failsafe` group) | every arm write is checked before it lands (site inside the image, destination body resolved) and the runtime bodies skip a missing preamble pointer or game-published lookup instead of dereferencing null |

`SkipIntro`, `SkipMenu`, `BorderlessWindowed`, `DoNotPauseOnMinimize` and
`VRAMFix` are XLivelessAddon features now: their improved bodies live in
`xlivelessaddon/` and the main module no longer registers those arms.

Every improved body keeps the original branch first when its switch is off.
There is currently **no** write-set-visible improvement: the improved and the
parity-baseline dumps are byte-identical on all 104 matrix cells (the formerly
visible `SkipIntro` rel32 moved to the fork with the option). The scan-range
change is also a pure implementation fix: the original `hook::pattern(module)`
never scanned `.rdata`/`.data`/`.rsrc`, so the write set is identical.

## Compatibility

- **XLivelessAddon.** Its feature set lives in the separate `xlivelessaddon/`
  successor module; there is no marker shim and no duplicate option in the main
  ASI.
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
| `OutOfCommissionFix` wrapper | decode + RPF build + mount per call | folded cache shipped; archive is intentionally retained |
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
- **Run time (`failsafe` group, on by default).** The bodies call their preamble
  pointers and game-published lookups without checking them, exactly like the
  original: a missing pointer faults. With the group on, the body continues
  with the value the game would have had without the fix and logs one line.
  This covers PlayerAsPedComponentsFix, PedModelsInCutscenesFix, SuperLODFix,
  BadPlayerModelCrashFix, GarageModelFix, the moving-parts trio, CopSirensFlag,
  DrivebyFOVFix, RemoveGFWLUpdatePopup and LoadDLCs' path buffer. The guards sit
  at the top of the body, before any register or stack state the resume
  consumes. `OPCEP_IMPROVEMENTS=except:failsafe` (or `none`) restores the
  original fault for a fault-parity run, and the `OPCEP_PARITY_BASELINE` build
  disables the group with every other one.

The installer pass ends with one `install: N writes applied, M skipped` line,
so a partially-matching image is visible without reading the whole log.

- **Folded:** NewSaves guards, OOC safe init/cache, component guards, model
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
    too), so it is preserved; a guarded variant is a separate opt-in key;
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
| the folded moving-parts guards still dereferenced three runtime pointers | `hooks.cpp` `script_float`, the unconditional tail and the steering lock owner only checked the script context | guarded inside the existing folded path; original path still reachable with `OPCEP_IMPROVEMENTS=none` |
| the fork logged no build id or per-patch counts | `xlivelessaddon/source/dllmain.cpp` logged only the config line and SkipLauncher counts | build, window/borderless/focus, skip-intro/menu sites, VRAM hooks, registry IAT replacements and the legacy ROM flag are logged |

### Documented, left unchanged (fault preserved)

- `xlivelessaddon/source/dllmain.cpp:885` — the legacy ROM block calls
  `hook::pattern(...).get(0)` for two secondary signatures with no size check;
  a missing signature is undefined behaviour, exactly like the original addon.
- `xlivelessaddon/source/dllmain.cpp:993-996` — the borderless
  `DefWindowProc` shims use `pattern.count(2).get(0/1)` unguarded.
- `xlivelessaddon/source/dllmain.cpp:404` — the delayed-init path busy-waits
  (`while (!(pattern.size() > 0))`) with no sleep; on a build where the probe
  never matches, that worker thread never exits.
- `xlivelessaddon/source/dllmain.cpp:67` — `szCustomSavePath.copy(pBuffer, 256)`
  followed by `pBuffer[len] = 0` writes past a 256-byte buffer when the custom
  path is longer than 255 characters (original addon code).
- `source/config.cpp:50` — `std::stoi` throws on a non-numeric value; the
  original mod's source uses `std::stoi` in the same way.
- `source/dllmain.cpp:290-309` — the ZMenuIV guard is skipped when the config is
  missing, despite the "unconditional" comment.

### Fork decision pending a live test

With the shipped `SkipLauncher = 1`, the recovered DFA/ROM stage
(`skip_launcher.cpp`) and the legacy `CreateROMWindow`/`WM_ROM` block
(`xlivelessaddon/source/dllmain.cpp:894-902`) both run. The legacy DFA file
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

The matrix is now **98 cells / 4,294 writes** (six option cells removed);
`--check` passes with the new golden.

