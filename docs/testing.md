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
- the hook registry and the OutOfCommission payload self-test.

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
