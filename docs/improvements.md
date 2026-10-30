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

- `RemoveBoundingBoxCulling` forces the non-culling side of the
  `cmp [esi+0x3c],1; jne` branch at game RVA `0x557463` (the `jne` becomes an
  unconditional `jmp`), so it does what the name says; the earlier "inverted"
  note was wrong. It is still niche and unstable (only for HighQualityReflections
  pop-in) and stays default `0`.
- `SkipMenu` has an archived FusionFix-style savegame check. It changes
  behaviour and needs a savegame-path implementation that respects
  `LocalSettingsPath`; not shipped.
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

## Audit 2026-10-30 — shipped defaults, tags and dead options

The original distribution's INI (`ZolikaPatch_extracted/ZolikaPatch.ini`) is the
oracle for shipped defaults. Four keys in this template differ from it, and they
are not documented as intentional divergences:

| key | original | ours | effect of the divergence |
| --- | --- | --- | --- |
| `BetterMPSync` | `0` | `1` | a fresh install applies the 5-byte MP sync hook |
| `Patch4RemoveModChecks` | `0` | `1` | a fresh install applies the shared GFWL stub |
| `RemoveGFWLUpdatePopup` | `0` | `1` | same stub, and the xlive-offset writes run |
| `TryToSkipAllErrors` | `1` | `0` | the original suppresses error dialogs by default; ours shows them |

`FixPCIDKickbug` (original default `1` in the extracted copy, `0` in the user's
copy) is not implemented in either module. `RestoreDeathMusic` differs between
the two original INI copies (`1` vs `0`); ours is `1`, matching the user's copy.

### Build tags

`DualVehicleHeadlights` was the one hard tag/build mismatch: the arms were
ungated, the old-body arm clobbered the new-body arm's signature, and 1080 got
the old body. Fixed and tagged `good-dualheadlights-20261030`; the tag is now
`[all]` and the original handler's build choice (ids 1..6 old, 7+ new) is
explicit. The remaining `[all]` tags for `Patch4RemoveModChecks` and
`RemoveGFWLUpdatePopup` are technically accurate (their stub is ungated in the
original too) but their descriptions say "patch 4" / "pre-patch 6", which is
misleading on 1.0.8.0.

### No-effect, misclassified and risky options

- `NoPhysicsSleep` writes `0.0f` over the `.data` float at RVA `0xC90B18`, whose
  shipped value is `0.005f` (the next field is `0.01f`). The executable reads it
  through five `movss` loads and has no direct writer, so the patch persists and
  really does zero the sleep threshold: physics objects stop sleeping. That is a
  realism change (the docs' "vehicles stop more realistically") at a likely CPU
  cost, not a performance feature; it sits in PERFORMANCE with default `1`.
  A live A/B is needed to measure the cost.
- `RemoveUselessChecks` writes 11,824 bytes over 2,735 rows on 1080. 2,230 of the
  rows are whole `call 0x74A710` instructions and `0x74A710` is a bare `ret`;
  the rest are other instructions or mid-instruction fragments (202 rows are
  non-NOP). It removes calls to an empty stub, not expensive checks; the gain is
  a few cycles per call and the option is documented as highly incompatible.
- `TryToSkipAllErrors` rewrites the error-handler entry with one `ret`; it
  suppresses the dialog, not the underlying error. Debug aid, not QoL.
- `Patch4RemoveModChecks` and `RemoveGFWLUpdatePopup` both install the same
  5-byte `E9` at RVA `0x3CBF70` to `abi_gfwl_stub`; the stub's two actions are
  independently flagged. The xlive half writes `0x10000E83` to four hard-coded
  xlive offsets with no module/build check (original handler `0x10005340` does
  the same).
- `NewMemorySystem`, `HighQualityReflections` and `ForceDynamicShadowsEverywhere`
  are experimental/heavy by design and already default `0`.
