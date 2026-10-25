# Origins, provenance and preserved behaviour

## What this project is

OpenPreCEPatch is a community successor to ZolikaPatch. ZolikaPatch was
distributed as a compiled ASI without source. The repository `ZolikaPatch/`
contains an older source mirror that is documentation only; the shipped binary
and the proven reconstruction are the specification.

The installer arms and hook bodies here were ported from the **readable
reconstruction** of the 7.65 binary, which was itself verified against the
shipped ASI. The surrounding loader, configuration and build system
are new work modelled on GTAIV.EFLC.FusionFix.

Two config-surface changes are intentional and are not behavioural:
the INI is grouped by subsystem, and the `MiscFixes` bundle is split into five
keys. An old INI keeps the original bundle semantics and the old flat
`[Options]` layout is still read; the per-fix keys only take effect when the old
master key is absent.

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

These are documented, not fixed. The improvement backlog lives in
[improvements.md](improvements.md).

## The XLivelessAddon split

The original shipped eleven signature writes under one `MiscFixes` key. This
project keeps the same writes but gives each logical fix its own key:

| key | rows |
| --- | --- |
| `RemoveDataIntegrityChecks` | VDS100/VDS102 rows |
| `SkipTokenActivation` | token activation rows |
| `SkipFirstTimePlayAsk` | first-run prompt |
| `SkipInternetConnectionCheck` | three `InternetGetConnectedState` rows |
| `SkipHealthCheck` | health-check early return |

An old INI that still has `MiscFixes` keeps the bundle semantics: the master
value gates all five. An INI without it uses the five keys, all defaulting on.
An external `XLivelessAddon.asi` still suppresses the set, as in the original.

## Extracted data

`source/data/model_catalogue.cpp` is the 38,455-entry model-name catalogue.
`source/data/ooc_payload.h` is the compressed OutOfCommission script payload.
Both come from the user's own game/mod files. `tools/` documents how to
regenerate them; the repository does not ship game executables or archives.

## License

GPL-3.0-or-later, the same license as FusionFix, so fixes borrowed from
FusionFix remain compatible. See `license`, `NOTICE` and `CREDITS.md`.
