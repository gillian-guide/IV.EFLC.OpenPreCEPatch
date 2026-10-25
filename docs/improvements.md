# Improvement backlog

The parity baseline keeps the original behaviour, including its rough edges.
Everything below is either **shipped opt-in** (default `0`, original path still
compiled and selected) or a documented candidate. The key list and the exact
write-set effect of every shipped key live in `docs/improvements-log.md`.

## Shipped opt-in improvements

Each key defaults to `0`; an INI without it behaves exactly like the parity
baseline. The original body/path stays reachable and is what the default selects.

| key | section | what it does |
| --- | --- | --- |
| `NewSavesGuards` | STABILITY | validates the save converter's header/chunk reads, `size < 9` and `malloc`; a malformed save is skipped instead of faulting or under-allocating |
| `OutOfCommissionSafeInit` | STABILITY | skips the archive setup cleanly when the embedded decode or either allocation fails |
| `VehicleComponentGuards` | STABILITY | null-checks the component registry walk (`ped -> +8 -> +0xB4 -> +0xC`), the bone-matrix chain and the script-resolver result in the moving-parts consumer |
| `ModelLookupGuards` | STABILITY | null-checks the shared model lookup, its FCHS parameter and popup results |
| `TraceInstallArms` | STABILITY | logs each installer arm before it runs, so the last arm in a crash log is known |
| `NewSavesSeekPayloads` | PERFORMANCE | seeks over discarded chunk payloads instead of reading them |
| `OutOfCommissionCache` | PERFORMANCE | runs the OOC decode/RPF build/mount once and reuses the archive instead of re-decoding and leaking per call |
| `SteerLookupCache` | PERFORMANCE | caches the `wheelangle` model resolution for the steering-wheel/engine/plane consumers |
| `PopupLookupCache` | PERFORMANCE | caches popup-headlight and FCHS model-hash resolution |
| `ModelNameIndex` | PERFORMANCE | builds a one-time sorted hash index for the 38,455-name catalogue used by MP diagnostics |
| `SkipIntroShowScreens` | COMPATIBILITY | FusionFix-style 300 ms intro screens instead of skipping them outright; requires `SkipIntro=1` |

`OutOfCommissionSafeInit` and `OutOfCommissionCache` are independent: safe init
decides what happens on failure, cache decides whether the init runs again.
`NewSavesGuards` and `NewSavesSeekPayloads` are independent for the same reason.

## Compatibility

- **FusionFix marker shim.** Keep `XLivelessAddon.asi` inert; it exists only so
  FusionFix's presence check succeeds.
- **FusionFix overlap audit.** The automatic gate list is in
  `source/fusionfix_compat.cpp`; re-check it when FusionFix adds a subsystem that
  collides with a still-active option.
- **Old INI import.** Done: the ASI ports `ZolikaPatch.ini` into the grouped file
  on first run and the option lookup still accepts the flat layout.
- **Build 18 (1.2.0.43).** No verified per-build data exists; pattern rows only.
  A CE executable would need its own evidence before RVA gates are added.

## Performance

The four hot caches are shipped behind keys (table above). Remaining candidates:

| site | cost today | candidate |
| --- | --- | --- |
| `ForceCarHeadlightShadows` | full table walk per call | covered by `PopupLookupCache` for the hash resolution; the walk itself is already bounded |
| plane script resolver | re-resolves the script-pointer chain per float read | resolve once per frame / cache the last `(script, index)` pair |
| `OutOfCommissionFix` wrapper | decode + RPF build + mount per call | shipped as `OutOfCommissionCache`; archive is intentionally retained |
| `NewSavesCompatibility` converter | reads and discards every chunk payload | shipped as `NewSavesSeekPayloads` |

## Stability and safety

- **Shipped opt-in:** NewSaves guards, OOC safe init, component guards, model
  lookup guards, install-arm tracing (table above).
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
  later opt-in.

## Tooling

- `tools/opcep_matrix.py` now produces the per-cell write set, per-arm
  attribution and a committed golden (`tests/parity/`).
- `tools/gen_ini_template.py` regenerates the embedded template; the raw string
  is split because MSVC truncates literals above 16,380 bytes.
- Derive the hard-coded test counts from the tables instead of repeating them.
- Compare hook bodies against the true function end (map/PDB), not the first
  `RET`.
