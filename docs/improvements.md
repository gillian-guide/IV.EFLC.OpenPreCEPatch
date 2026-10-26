# Improvement model and backlog

The improvements are **folded into the original INI options**: there are no
per-fix keys. `NewSavesCompatibility=1` means the guarded/faster converter,
`OutOfCommissionFix=1` means the cached/safe archive setup, `SkipIntro=1` means
the 300 ms intro body, and so on. An INI from the parity era still enables the
improved path for the options it already has.

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
| `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `MovingPlaneParts` | component-registry/bone-chain null guards; script-resolver guard; wheelangle model→tilt cache |
| `PopupHeadlightsFlag`, `ForceCarHeadlightShadows` | model-lookup/parameter guards; hash→index cache |
| `BadPlayerModelCrashFix`, `DebugPrintSpawns` | one-time model-name hash index |
| `SkipIntro` | FusionFix-style 300 ms intro body instead of skipping outright (original body still available via the test switches) |
| `BorderlessWindowed`, `DoNotPauseOnMinimize` | FusionFix-style IAT window manager (style sync, centering, resize) and focus-loss input suppression on top of the original patches |
| `SteamAchievements` | GFWL achievement writes mirrored to Steam at the `XUserWriteAchievements` boundary; the original GFWL call still runs |
| installer pass | arm logging is part of the normal trace (`OPCEP_IMPROVEMENTS=except:trace` silences it) |

Every improved body keeps the original branch first when its switch is off. The
only write-set-visible difference with improvements on is the `SkipIntro`
rel32: 4 bytes at the same site.

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

The hot caches are now part of the options in the table above. Remaining
candidates:

| site | cost today | candidate |
| --- | --- | --- |
| plane script resolver | re-resolves the script-pointer chain per float read | resolve once per frame / cache the last `(script, index)` pair |
| `OutOfCommissionFix` wrapper | decode + RPF build + mount per call | folded cache shipped; archive is intentionally retained |

## Stability and safety

- **Folded:** NewSaves guards, OOC safe init/cache, component guards, model
  lookup guards, model-name index (table above).
- **Still candidates:**
  - the popup model object itself (not just the lookup result) is dereferenced
    to read the bone/position without a class check;
  - the version guard opens a web page and terminates; make the launch path
    configurable without weakening the default;
  - the double-load guard shows a modal box; prefer a log line with a documented
    opt-out;
  - in-place operand patches (`p-5`/`p-11` in NewSaves) should re-scan and assert
    the pre-image before writing.

## Features that need a real decision

- `RemoveBoundingBoxCulling` is inverted relative to its name and unstable in
  the original. Keep the original code and default `0`; a corrected variant is a
  separate, clearly named option if it is ever added.
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
