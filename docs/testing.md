# Testing

No game files are needed for the native tests.

## Native tests

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release --target opcep_tests
build\Release\opcep_tests.exe
```

or, with premake, build the solution and run `bin\opcep_tests.exe` if the test
project is enabled.

The suite covers:

- grouped and legacy INI lookup, the `1`-only rule and defaults;
- game hash corner cases and build classification;
- the 38,455-entry model catalogue's first/last/unknown entries;
- the two vehicle-light tables and their atomic rejection;
- first-run INI creation from `ZolikaPatch.ini`;
- FusionFix presence gating;
- the ZMenuIV version threshold;
- the hook registry and the OutOfCommission payload self-test;
- the arm and body failsafes: a synthetic image where the site check accepts an
  in-image write, refuses one that would leave the image or a branch with no
  body, and the accepted write is byte-identical to the unchecked injector call;
  then the whole 122-arm pass over a 4 KiB image, which must install nothing and
  report its skips (no crash, no stray write);
- `ExecutableView` and the scan semantics: `Pattern()` returns only the first
  match inside the executable sections, `PatternAll()` returns every match in
  that range.

`xla_tests` (also run by `ctest`, 32-bit like the fork) covers the
XLivelessAddon successor's recovered SkipLauncher path: CreateFile slot capture
and forwarding, clearing a stale slot, missing signatures installing nothing, and
invalid input. The fork has no write-set dump; these tests plus the
`XLivelessAddon.log` counters are its offline check.

## INI write-set matrix

`tools/opcep_matrix.py` is the behaviour safety net. It generates a temporary
INI for the shipped defaults, all options off, all options on, each option
alone, each section alone and the frozen legacy `ZolikaPatch.ini`, runs
`opcep_parity_dump` for each cell and records the write set plus the installer
arm that produced every byte.

The dump runs the same installer pass as the ASI, so it also reflects the scan
semantics: signatures are matched only inside the executable sections and stop
at the first hit (unless the arm uses the multi-match helper). The matrix is the
check that those semantics still produce the committed writes.

```bat
python tools\opcep_matrix.py --check
python tools\opcep_matrix.py --update
```

The committed baseline is `tests/parity/matrix.json` with per-cell files under
`tests/parity/writesets/` and per-option attribution under
`tests/parity/deltas/`. The dump canonicalises module-relative operands
(`AddressOf`/`FindGlobalValue`/`FindGlobalSymbol`) to site-derived markers, so a
pure refactor that only moves globals still produces a zero diff. See
`tests/parity/README.md` for the cell list and the zero-diff policy. `--check`
fails on any change, including an unattributed byte or a changed game image.

## Testing the folded improvements on/off

The improvements are part of the original INI options, so there is no per-fix
INI key. Use one of these switches:

```bat
:: one process / one live run, no INI edit
set OPCEP_IMPROVEMENTS=none
set OPCEP_IMPROVEMENTS=only:newsaves,ooc
set OPCEP_IMPROVEMENTS=except:ooc
set OPCEP_IMPROVEMENTS=except:trace
```

`all`/unset enables every group; `none` disables every group; a bare list is an
`only:` list. The groups are `new_saves`, `ooc`, `vehicle`, `popup`,
`model_index`, `skip_intro`, `trace`, `windowed` and `failsafe`. `skip_intro` and
`windowed` are vestigial in this module now: the `SkipIntro`/window arms moved to
XLivelessAddon with the options themselves. `except:failsafe` restores the
original null-dereference faults in the bodies for a fault-parity run.

Opt-in entry-point rewrites are separate bits **outside** `all`, so a default run
never installs them; they exist to be A/B tested and promoted later:

| group | what it moves |
| --- | --- |
| `garage_entry` | `GarageModelFix` site A from the mid-body anchor (0x868BB0) to the function prologue (0x868B90), reusing the store body and reloading the `xmm0` the prologue instruction sets |
| `cutscene_entry` | `CutsceneFixes` from its three sites (two camera call rewrites + the branch byte) to one hook at the shared camera callee (0x89A640), by relocating the callee into a private copy whose 13 `call rel32`s are re-pointed and whose branch fix is baked in |
| `nms_thunk` | `NewMemorySystem` from the allocator-ctor call rewrite + `atexit` skip to one hook at the static-init thunk 0xD56060: the game's own allocator construction and its `atexit` registration run, then the reimplemented vtable is stored. This restores the game's ~100 MiB simple-allocator block, so it changes memory behaviour and must not be promoted without a live test |
| `alpha_nop` | `BuildingAlphaFix` site 2 NOPs only the six-byte `jnz` (0x559CDA) instead of the 13-byte `test` + `jnz`, so the flags the `test` sets stay live for the following code |

Run one with `OPCEP_IMPROVEMENTS=only:<group>`. Because `all` excludes the
opt-in bits, a list may name `all` to mean the folded groups:
`OPCEP_IMPROVEMENTS=all,cutscene_entry` keeps every folded improvement (including
the failsafes) on and adds one entry-point rewrite. `entry` expands to all four
opt-in rewrites at once, so `all,entry` is the full A/B. The default
`opcep_matrix.py --check` must stay green with every opt-in group off; the delta
for a group is attributed with `--env OPCEP_IMPROVEMENTS=all,<group>`.

## Build applicability

`tools/gen_version_matrix.py` renders `docs/version-matrix.md` from
`docs/version-matrix.json` (the recovered per-option handler and gate evidence),
the arms in `source/*.ixx` and the shipped INI:

```bat
python tools\gen_version_matrix.py            :: regenerate
python tools\gen_version_matrix.py --check    :: verify the committed file
```

`docs/version-matrix.json` is the IDA-derived evidence (handler VA, gate
instruction, per-build RVA rows); it is not regenerated by the tool. `--check`
also covers the INI template (`gen_ini_template.py --check`) and the write-set
matrix, so a tag change that is not reflected in the matrix is a failure.

```bat
:: separate legacy dump/ASI, for automated A/B
cmake -S . -B build-parity -A x64 -DOPCEP_PARITY_BASELINE=ON
cmake --build build-parity --config Release --target opcep_parity_dump
python tools\opcep_matrix.py --exe build-parity\Release\opcep_parity_dump.exe --report parity.json
```

The normal and parity dumps are byte-identical on every cell (0 differing
cells in the last 96-cell run: the formerly visible `SkipIntro` rel32 moved to
the fork with the option). `tools/opcep_matrix.py --env` applies the
environment switch per cell without changing any file.

## Write-set dump

`opcep_parity_dump` maps a copy of a PE32 executable and prints every byte the
installer pass changes, as `RVA byte` lines:

```bat
build64\Release\opcep_parity_dump.exe GTAIV.exe IV.EFLC.OpenPreCEPatch.ini out.txt
```

The 64-bit build uses a small injector shim and resolves a body name to the
original destination VA, so the installer's rel32 is comparable with the
reference build. The 32-bit `opcep_dump` is provided for the same purpose when
the image can be mapped at its preferred base.

On the emulated-original 1080 cell (the user's 59-option configuration; a
pre-split measurement), the dump produced 598 changed bytes against the
original's 669. Of those, 576 final bytes match exactly; 73 are
resolver-dependent SkipLauncher/DFA hook installs that the dump intentionally
does not execute; 20 are rebased global-address operands (the original stores its
own globals' addresses); and 2 are the known lazy `DoNotPauseOnMinimize` site
that the emulator never reaches. This is a diff of the *final image*, not a
count of write rows.

## Installer parity

The generated arms were compared against the readable reimplementation's loader
blocks after normalising whitespace and the `Abi("name")` to `&name` rewrite.
141 of 144 arms are byte-identical in source; the three differences are the
intentional `MiscFixes` split, the `ApplyUselessChecks` preamble wrapper and the
`Patch7RemoveModChecks` alias.

## Live testing

The game must be launched through `PlayGTAIV.exe` (it bypasses the DRM). Read
`IV.EFLC.OpenPreCEPatch.log` next to the ASI afterwards; it names the config,
build and stages. Ask the user to launch; do not edit an installed game
directory automatically.

## Live validation of the opt-in groups

The four entry-point rewrites are off unless asked for, so a normal run is the
reference. To validate one on the user's install:

1. `build.bat`, copy `bin\IV.EFLC.OpenPreCEPatch.asi` (and its INI) next to the
   game. Launch through `PlayGTAIV.exe`.
2. Run once with no `OPCEP_IMPROVEMENTS` and keep `IV.EFLC.OpenPreCEPatch.log`
   (the baseline: `install: N writes applied, M skipped`).
3. Run again with the group, e.g. `set OPCEP_IMPROVEMENTS=all,garage_entry`, and
   compare the same log line and the per-arm lines. The expected write-set
   change is the one listed in `docs/entry-points-audit.md`.
4. What to watch, per group:
   - `garage_entry` - enter/leave a safehouse garage with a modded car; the car
     must keep its model, no new log skip lines.
   - `cutscene_entry` - the first story cutscene (the cutscene camera fix) at
     >= 120 FPS: no crash on entry, zoom/jitter still fixed.
   - `nms_thunk` - boot to the main menu, then load a save (allocator active):
     watch for SMPA*/out-of-memory and the process's memory use, which is higher
     by the game's ~100 MiB simple-allocator block by design.
   - `alpha_nop` - look at LOD fade-in on buildings; the flags after the test are
     the only difference from the default 13-byte NOP.
5. `OPCEP_IMPROVEMENTS=all,entry` runs all four together. A group that misbehaves
   is disabled by dropping it from the list; no rebuild, no INI edit.
