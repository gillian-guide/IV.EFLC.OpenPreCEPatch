# Testing

The suite has two halves. The 22 ctest targets prove contracts and behaviour offline: no game launch, and only the tests that map a game image need one on disk. The measurement tools produce the numbers the project quotes, and they are run by hand or by the docs that record their output. Both halves are deliberately falsifiable: a check is trusted only after it has been shown to fail on a case it should reject.

## The 22 tests

`ctest --preset release` runs these. A target that needs an input is registered only when the input exists, and the conditions are listed in [building.md](building.md).

| test | what it proves |
| --- | --- |
| `zp_tests` | the contract suite: INI grammar, build classification, the signature normaliser, the scanner, the event tiers, the option gate (including `AddOptionsInMenu`'s stored-answer rule), and the built ASI's startup path |
| `zp_model_tables` | the two per-model light tables: the `Car<N>Name`/`Angle`/`Offset` grammar, the section gate, the hash-before-value order, the degrees-to-radians conversion and the `std::stof` faults |
| `zp_save_walk` | the save converter's eighteen-chunk walk and the three patch values it selects, on the real saves and on synthetic new/old layouts, against a parser written independently in the test |
| `zp_native_guard` | the two guarded native entries: the invoke calls and answers 1, the resolve answers without calling, a fault inside either region answers 0, and a null resolver's zero is the caller's fault in a child process |
| `zp_nms_arena` | the `NewMemorySystem` arena: the alignment scheme, the distance word, the `0xCD` fill, membership, free and the null-returning failed request |
| `zp_dfa_close` | the DFA close-handle body and its four transcribed siblings on a real file, the closed/null/double answers, and the frame's retry of a faulting call in a child process |
| `zp_speedup_clock` | the `SpeedupBugFix` arithmetic: the four derived floats at five frequencies, the eight published slots, the store order and the missing-scan fault |
| `zp_ooc_init` | the `OutOfCommissionFix` init: three calls build three archives and free none, the cursor and mount count, and the two damaged-resource runs that still register and build |
| `zp_fault_parity` | the INI loader's preserved faults by behaviour: both conversion throws by exact type, the key-before-section and BOM null stores in children, and the 255/256-byte line boundary (the 256-byte child must still be spinning when the test ends it) |
| `zp_salt_probe` | the loader preamble's salt rule on the real images: the probe hits on 1.0.8.0 and misses on 1.0.0.0 and 1.0.4.0 |
| `zp_dump_empty` | with no arm installed the pipeline changes nothing on all nine images |
| `zp_pe_check` | the built ASI is 32-bit PE32, image base `0x10000000`, windowed subsystem, with a static CRT |
| `zp_body_proof_selftest` | the body comparator against the original itself (must pass) and against a one-byte corruption (must fail), plus the registered-table path both ways |
| `zp_body_proof` | all 175 transcribed bodies, whole, instruction for instruction against the original ASI |
| `zp_catalogue_drift` | `source/options.inc` matches its generator |
| `zp_body_targets_drift` | `tests/body_targets.tsv` matches its generator |
| `zp_ini_drift` | `ZolikaPatch.ini` matches the catalogue |
| `zp_oracle_verified` | every row of `tests/oracle_verified.tsv` names a real key, a build its mask allows, an existing docs anchor, and a measured omission no wider than the evidence |
| `zp_fault_dialogs` | every test that faults on purpose suppresses the Windows fault dialog |
| `zp_relocated_literals` | no recovered signature literal's hit set depends on the image's relocation delta |
| `zp_signature_parity` | every arm's scan resolves the same first hit, per image, as the original handler's own signature string |
| `zp_launcher_slots` | the launcher scan resolves exactly the slots each image carries, and the build gate is load-bearing where the scan alone would fire |

## The measurement tools

These produce the tables in [matrix.md](matrix.md) and the scoreboard the project quotes. They write to a file or stdout; the suite does not depend on them.

| tool | what it measures |
| --- | --- |
| `tools/parity_report.py <build>` | the per-key scoreboard for one build: closed, open, and oracle-verified keys |
| `tools/parity_matrix.py` | the per-key scoreboard across every supported build, one row per build |
| `tools/dump_matrix.py` | the 18 cells: nine images in full and per-option mode, every residual attributed to a key |
| `tools/full_diff.py <build>` | attributes a full-cell difference to the keys whose per-key cells touch the differing RVAs |
| `tools/compare_dumps.py <baseline> <ours>` | diffs two dumps, optionally for one key |
| `tools/single_key_matrix.py` | every key alone on every image, one process per cell; the matrix that attributes a fault to a key |
| `tools/check_empty_dump.py` | the empty baseline: no arm, no bytes written |
| `tools/verify_bodies.py` | the whole-body proof, and `--selftest` for the comparator |
| `tools/verify_launcher_slots.py` | the launcher scan's slot count and gate per image |
| `tools/check_relocated_literals.py` | signature literals whose fixed bytes cover a relocation fixup |
| `tools/check_signature_parity.py` | each arm's per-image hit set against the original handler's own signature |
| `tools/check_oracle_verified.py` | the oracle-verified rows' keys, build scope, anchors and measurements |
| `tools/check_fault_dialogs.py` | the fault-dialog suppression in every faulting test |
| `tools/crosscheck_reconstruction.py` | the write set against the third, independently produced reconstruction tree |
| `tools/gen_options.py`, `tools/gen_ini.py`, `tools/gen_body_targets.py` | regeneration and `--check` for the generated catalogue, INI and body attribution |
| `tools/read_dump.py` | names the faulting site in a game crash minidump |

## Running them

The suite:

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

One target by name, with the build's own output:

```
ctest --preset release -R zp_body_proof --output-on-failure
```

The measurements behind the headline numbers:

```
python tools/parity_report.py 9
python tools/verify_bodies.py --original ../game_refs/ZolikaPatch_original.asi.off \
    --candidate build/win-x86/Release/ZolikaPatch.asi \
    --bodies tests/bodies.tsv --map build/win-x86/Release/ZolikaPatch.map
python tools/dump_matrix.py --markdown docs/matrix.md
python tools/full_diff.py 9
python tools/check_oracle_verified.py
```

`parity_report.py 9` prints `98 key(s) closed, 0 open, 1 oracle-verified`; the body proof prints `175 body(ies), 0 failure(s)`; `check_oracle_verified.py` prints `3 oracle-verified row(s), 0 failure(s)`. The matrix is generated into [matrix.md](matrix.md), where `differing` is 0 in every full cell.

## Falsification

The habit is that a guard is shown to fail on a case it should reject before it is trusted. The recorded cases:

- `verify_bodies.py --selftest` requires the original to pass against itself and a one-byte corruption to fail, and it runs the registered-table path both ways (a content change fails as `table contents differ`, an unregistered displacement still compares literally).
- `check_oracle_verified.py` pins each omission's build scope. Widening `CarDynamicShadowFix` to `*` makes it fail with `excuses build(s) [6, 7, 8, 9, 11, 12, 13]; the measured omission covers only [1, 5]`; the row is then reverted. The build-9 scoreboard alone cannot catch that, because the key is closed there.
- `check_relocated_literals.py` was falsified with a synthetic base-dependent literal: it is classified "ours to fix", while the real `MiscFixes` literal is classified as the original's preserved fault.
- the option-gate check was falsified by running the old `== 1` rule through the same requirement: it reports one failure (`TestOptionGates`), and the working rule reports none.
- `zp_fault_parity` pins both sides of the line-buffer boundary, so a re-introduced line split fails the child check instead of hanging the suite.
- `check_fault_dialogs.py` exists because the mistake it catches happened twice: a deliberately faulting test without `SetErrorMode` put an Application Error dialog in front of the person waiting for the result.

## What the suite does not establish

Nothing here has run against a live game. The native-call layer, the rebuilt C++ consumers and the in-game paths are pinned by disassembly, instruction proofs, write-set equality and behavioural tests with stand-in callers - not by observation. The rebuilt pieces' residual risks are listed per piece in [rebuilt.md](rebuilt.md), the preserved faults in [parity.md](parity.md), and the one-page summary of what is and is not established is [review.md](review.md).
