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
- IVMenuAPI probes and the ZMenuIV threshold;
- the hook registry and the OutOfCommission payload self-test;
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
set OPCEP_IMPROVEMENTS=except:skipintro
set OPCEP_IMPROVEMENTS=except:trace
```

`all`/unset enables every group; `none` disables every group; a bare list is an
`only:` list. The groups are `new_saves`, `ooc`, `vehicle`, `popup`,
`model_index`, `skip_intro`, `trace` and `windowed`.

```bat
:: separate legacy dump/ASI, for automated A/B
cmake -S . -B build-parity -A x64 -DOPCEP_PARITY_BASELINE=ON
cmake --build build-parity --config Release --target opcep_parity_dump
python tools\opcep_matrix.py --exe build-parity\Release\opcep_parity_dump.exe --report parity.json
```

The normal and parity dumps are byte-identical on every cell except
`SkipIntro` (4-byte body rel32). `tools/opcep_matrix.py --env` applies the
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

On the emulated-original 1080 cell (the user's 59-option configuration), the
dump produced 598 changed bytes against the original's 669. Of those, 576 final
bytes match exactly; 73 are resolver-dependent SkipLauncher/DFA hook installs
that the dump intentionally does not execute; 20 are rebased global-address
operands (the original stores its own globals' addresses); and 2 are the known
lazy `DoNotPauseOnMinimize` site that the emulator never reaches. This is a
diff of the *final image*, not a count of write rows.

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
