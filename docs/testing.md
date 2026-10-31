# Testing

The native suite needs no game files. It covers the parts of the loader that can
be checked offline:

- RemoveUselessChecks' write set (the compact tables + singles reproduce the
  expanded 7,489-row list: 12,406 / 11,516 / 11,839 byte positions on builds
  5 / 8 / 9, nothing on any other build) and the generated data file's hashes;
- the model hash (including `hash:` input, case folding and slash folding);
- build classification and the per-block build gates (the original's head
  compares, its LaunchWithPlayGTAIV switch and the per-build RVA rows);
- INI parsing, comments, BOM, duplicate keys, absent-key defaults and the "exactly 1" rule;
- the 38,455-entry model catalogue (first/last/unknown);
- vehicle-light parsing, atomic rejection and duplicate-angle semantics;
- the signature parser, including the original's three-digit-token quirk;
- IVMenuAPI probing and registration rows;
- the ZMenuIV version threshold and message constants;
- SkipLauncher table shapes (25/2/6/2/1).

## Running the tests

From Visual Studio, build and run `ZolikaPatchTests` (`Release | x86`). With
CMake:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
build\Release\zp_tests.exe
```

A green run also proves the module compiles and links.

## Style check

`tools/style_lint.py` enforces `CONTRIBUTING.md` across the hand-written sources:

```bat
python tools\style_lint.py --fail-on error
```

Vendored code and generated data are out of scope; the linter lists them.
