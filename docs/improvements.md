# Improvement backlog

The first release keeps the original behaviour. The grouped INI, the five-way
MiscFixes split and the FusionFix auto-gate are documented config-surface
changes; the items below are
known upstream rough edges or future improvements. They are **not shipped** in
the parity release; each needs an INI escape hatch, a preserved original path,
and a test that proves the old behaviour is still reachable.

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

| site | cost today | candidate |
| --- | --- | --- |
| moving steering wheel / shaking engine / moving plane parts | a 512-entry `wheelangle` walk and a model lookup per consumer call | one-time `{hash -> entry}` cache keyed off the game's lookup |
| popup headlights | 512-slot hash scan plus a list walk per call | same cache; check the flag last |
| force car headlight shadows | full table walk per call | cache the resolved entry |
| spawn diagnostics | linear 31,000-entry hash scan per spawn | build a static hash index once |
| plane script resolver | re-resolves the script-pointer chain per float read | cache the last `(script, index)` pair per frame |
| new-saves converter | reads and discards every chunk payload | `fseek` past payloads; only headers are needed |
| OutOfCommission wrapper | decode + RPF build + mount per call; archive leaked | set the intended one-shot guard and cache the mounted archive |

## Stability and safety

- Null-check the vehicle-component walk (`ped -> +8 -> +0xB4 -> +0xC`) and the
  entries pointer; document the original fault first.
- Null-check the popup model object before reading the bone/position.
- Treat resolver 0 as a clean skip in every body that calls through a slot
  (the original dereferences unconditionally).
- NewSaves converter: validate `fread`, the `size < 9` underflow and the
  `malloc` result.
- The unsupported-version guard opens a web page and terminates; make the
  launch path configurable without weakening the default.
- The double-load guard shows a modal box; prefer a log line with a documented
  opt-out.
- In-place operand patches (`p-5`/`p-11` in NewSaves) should re-scan and assert
  the pre-image before writing.

## Features that need a real decision

- `RemoveBoundingBoxCulling` is inverted relative to its name and unstable in
  the original. Keep the original code and default `0`; a corrected variant is
  a separate, clearly named option if it is ever added.
- `SkipIntro` and `SkipMenu` have archived FusionFix-style variants (brief
  intro, savegame check). They change behaviour; ship only behind new keys.
- `NewMemorySystem` replaces the game's allocator wholesale and fills
  allocations with `0xCD`. Keep the original behaviour; a guarded variant is a
  later opt-in.

## Tooling

- Derive the hard-coded test counts from the tables instead of repeating them.
- Compare hook bodies against the true function end (map/PDB), not the first
  `RET`.
- Add a per-option write-set diff to CI once a redistributable game image is
  available; today the dump runs locally against the user's own executable.
