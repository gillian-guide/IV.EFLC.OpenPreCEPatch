# Building

## Requirements

- Windows 10/11 with the MSVC toolchain. The `win-x86` preset names the **Visual Studio 18 2026** generator; the project rejects a non-MSVC compiler and a 64-bit target, because the ASI is 32-bit (PE32) and must be built as such.
- CMake 3.28 or newer, with C++23 modules support in the installed toolset.
- The two vendored libraries, checked out as submodules. In an existing checkout:

```
git submodule update --init --recursive
```

- Python 3 for the offline tests and drift checks. CMake looks for it with `find_package(Python3)` and registers the Python-driven tests only when it is found.
- No game files are needed to compile. The measurement tests want the game images in `../game_refs/`, and the save-converter test wants `../game_refs/saves/`.

## Presets

`CMakePresets.json` defines one configure preset, three build presets and two test presets:

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

`win-x86` configures `build/win-x86` for Win32 with `Debug;Release;Trace`. The build presets are `debug`, `release` and `trace`; the test presets are `release` and `trace` and both print the failing output. A trace test run is the same suite against the traced binary:

```
cmake --build --preset trace
ctest --preset trace
```

## Configurations

`Release` is the shipped binary and is silent, as the original is. `Trace` compiles with the same flags and defines `ZP_TRACE`, which turns on `ZolikaPatch.trace.log` next to the ASI; the write set and the install decisions are identical. `Debug` is a normal debug build and is not shipped.

All configurations compile C++23 with `MultiThreaded` (static CRT), `/utf-8`, `/Zc:__cplusplus` and `/W3`, and link with:

```
/MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO
```

`ZP_LINK_MAP` is on by default and adds `/MAP`, because the body proof resolves each body's candidate address from `ZolikaPatch.map`. The map is a development aid; turning the option off changes no byte of the image.

## Artifact

```
build/win-x86/Release/ZolikaPatch.asi    <- copy next to GTAIV.exe or EFLC.exe
build/win-x86/Release/ZolikaPatch.map    <- the proof's symbol table, not shipped
```

The trace build writes the same two files under `build/win-x86/Trace/`. Copy the ASI and `ZolikaPatch.ini` into the game folder. If the game is running, the ASI is locked; close it first.

## Offline checks and fixtures

`ctest --preset release` runs the 22 registered tests. Several of them are registered only when their inputs exist, and the conditions are in `CMakeLists.txt`:

| input | tests it enables |
| --- | --- |
| `../game_refs/GTAIV.exe` | `zp_dump_empty`, `zp_relocated_literals` |
| `../game_refs/GTAIV.exe`, `../game_refs/GTAIV_1000.exe`, `../game_refs/GTAIV_1040.exe` | `zp_salt_probe` |
| `../game_refs/ZolikaPatch_original.asi.off` | `zp_body_proof`, `zp_body_proof_selftest`, `zp_signature_parity` |
| `../game_refs/EFLC_1110.exe` | `zp_launcher_slots` |
| `../game_refs/saves/` | `zp_save_walk` |

The rest are self-contained: the contract suite, the model tables, the native guard, the arena, the DFA close frame, the speedup clock, the OOC init, the fault parity suite, the PE check and the five Python checks that ride the same `ctest` run. `ctest --preset release -R zp_tests` runs one of them by name, and every test executable can be run directly from `build/win-x86/Release/`.

## Reproducing the generated files

Three generated artefacts are checked for drift rather than rebuilt by hand:

```
python tools/gen_options.py --check        # source/options.inc
python tools/gen_ini.py --check            # ZolikaPatch.ini
python tools/gen_body_targets.py --check   # tests/body_targets.tsv
```

`data/useless_checks.inc` is committed generated data: its header records the oracle ASI's SHA-256 and each table's own hash, so a mismatch is detectable. The extraction script the header names is not part of this tree.

## Reproducing the case base

The bodies' proofs and the dump comparisons use `../game_refs/`, which is not part of the repository. The original ASI is `../game_refs/ZolikaPatch_original.asi.off` (kept `.off` so the game never loads it), and the nine images are `GTAIV_1000.exe`, `GTAIV_1040.exe`, `GTAIV_1004.exe`, `GTAIV_1060.exe`, `GTAIV_1070.exe`, `GTAIV.exe`, `EFLC_1110.exe`, `EFLC_1120.exe` and `EFLC_1130.exe`. See [testing.md](testing.md) for what each measurement does with them.
