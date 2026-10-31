# Origins, provenance and preserved behaviour

## What this project is

OpenPreCEPatch is a community successor to ZolikaPatch. ZolikaPatch was
distributed as a compiled ASI without source. The repository `ZolikaPatch/`
contains an older source mirror that is documentation only; the shipped binary
and the proven reconstruction are the specification.

The installer arms and hook bodies here were ported from the **readable
reconstruction** of the 7.65 binary, which was itself verified against the
shipped ASI. The surrounding loader, configuration and build system
are new work modelled on GTAIV.EFLC.FusionFix. The FusionFix-style intro and
window bodies live in the XLivelessAddon successor, and the pre-split copies that
were left in this module (SkipIntro, SkipMenu, SkipLauncher, VRAMFix) have been
deleted: they were unregistered, so they could never run.

One config-surface change is intentional and not behavioural: the INI is
grouped by subsystem, and the old flat `[Options]` layout is still read. The
folded improvements (selected by the original options) are an additional
documented behaviour surface; see `docs/improvements.md`. XLivelessAddon's
features live in the separate companion repository, not in this ASI.

## Preserved behaviour

The first release is fault-inclusive: it keeps the original's null dereferences,
uninitialised reads, out-of-bounds walks and other rough edges. The important
ones:

- `DrivebyFOVFix` dereferences the resolver result unconditionally; a null
  result faults at `[eax+0x0E]`, as in the original.
- `ForceCarHeadlightShadows` and `GarageModelFix` call the shared model lookup
  and dereference the object without a guard.
- `NewSavesCompatibility` ignores `fread` results, lets a `size < 9` underflow
  the allocation count, and does not check the `malloc`.
- `RemoveGFWLUpdatePopup` writes through `xlive.dll` + `0x66F770` with no base
  check; with `xlive.dll` absent this faults on the low page.
- `OutOfCommissionFix` decodes and mounts its archive on every call and leaks
  it; the original's guard has no writer.
- `CutsceneFixes` chain B self-modifies the game's camera function and
  dereferences the native-script resolver result without a guard.
- `NewMemorySystem` replaces the game's allocator and fills allocations with
  `0xCD`; the game's own allocator init never runs.

These are documented, not fixed in the default path. The improvement backlog
lives in [improvements.md](improvements.md).

## Folded improvements

The faulting original body is kept for every improved site. The improvement is
selected by the original option itself, so there are no per-fix keys:

- `NewSavesCompatibility` adds the guarded/faster converter;
- `OutOfCommissionFix` adds the fail-closed/cached archive path;
- the moving-parts trio adds component/script guards and the tilt cache;
- `PopupHeadlightsFlag` / `ForceCarHeadlightShadows` add lookup guards and the
  hash cache;
- `BadPlayerModelCrashFix` / `DebugPrintSpawns` use the model-name index.

`SkipIntro`, `SkipMenu`, `BorderlessWindowed`, `DoNotPauseOnMinimize` and
`VRAMFix` moved to the XLivelessAddon companion repository; their 300 ms intro
body and FusionFix-style window manager are not part of this tree.

The original branches are gone: the improved code is the only implementation.
The shared model-name table and OOC payload are unchanged. The model index only
adds a sorted `(hash, table index)` view of the same catalogue; first-match
order is preserved.

## XLivelessAddon

The XLivelessAddon feature set is not included here. It is its own companion
repository and builds `XLivelessAddon.asi`. The main module only keeps features
that belong to ZolikaPatch itself and detects an installed companion for
compatibility reporting.

## Extracted data

`source/data/model_catalogue.cpp` is the 38,455-entry model-name catalogue.
`source/data/ooc_payload.h` is the compressed OutOfCommission script payload.
Both come from the user's own game/mod files. `tools/` documents how to
regenerate them; the repository does not ship game executables or archives.

## License

GPL-3.0-or-later, the same license as FusionFix, so fixes borrowed from
FusionFix remain compatible. See `LICENSE`, `NOTICE` and `CREDITS.md`.
