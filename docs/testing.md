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
- vehicle-light parsing: the empty/missing-name skip, the duplicate-angle
  last-wins rule and the missing/malformed-value throw (`std::stof`, as the
  original does). The assertions name the original's two messages, and the
  behaviour is the original's `sub_100523C0`: only an empty or missing
  `Car<N>Name` is skipped (std::string compare `call 0x1005AF10`, skip branch
  `jne 0x1005289D` at `0x10052638`); the value is converted with `_errno`
  (`0x10052745`) + `strtof` (`0x10052763`), and the endptr/errno checks at
  `0x10052772` / `0x1005277E` call `_Xinvalid_argument` (IAT `0x10063120`,
  "invalid stof argument" at `0x10063A24`) and `_Xout_of_range` (IAT
  `0x10063124`, "stof argument out of range" at `0x10063A80`), uncaught;
- the signature parser, including the original's three-digit-token quirk;
- IVMenuAPI probing and registration rows;
- the ZMenuIV version threshold and message constants;
- SkipLauncher table shapes (25/2/6/2/1).

The ASI also has two offline proofs that need the built binary or a game image:

- `tools/verify_bodies.py` — the hook-body proof. With the ASI built with
  `ZP_LINK_MAP=ON`, it disassembles every `@exact` body from the linker map and
  compares it instruction-for-instruction with the original ASI, binding module
  operands through the documented slots; compiled C++ replacements are reported
  as INFO with their documented instruction range. Registered as
  `zp_body_proof`; also the `verify_bodies` target. Exit codes: 0 proved,
  1 a body fails, 2 inputs/capstone missing.
- `tools/verify_launcher_slots.py` — maps every on-disk game exe and checks that
  the launcher scan resolves exactly the slots the original's per-build table
  named (the launcher only runs with PlayGTAIV.exe present, so the write-set
  dump cannot exercise it).

## Running the tests

From Visual Studio, build and run `ZolikaPatchTests` (`Release | x86`). With
CMake:

```bat
cmake -S . -B build -A Win32 -DZP_LINK_MAP=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

That runs `zp_tests`, `zp_useless_checks_data` (the generated data file's
hashes) and, with `ZP_LINK_MAP=ON` and the original ASI present,
`zp_body_proof`. A green run also proves the module compiles and links.

## Write-set dump

The installers can be applied to a mapped copy of a game image and the resulting
bytes diffed against a baseline; this is the oracle for every installer change:

```bat
cmake -S . -B build-dump -A x64 -DZP_BUILD_ASI=OFF -DZP_BUILD_TESTS=OFF -DZP_BUILD_DUMP=ON
cmake --build build-dump --config Release
build-dump\Release\zp_dump.exe out.txt ZolikaPatch.ini
build-dump\Release\zp_dump.exe --per-option out.txt ZolikaPatch.ini
build-dump\Release\zp_dump.exe --exe ..\game_refs\GTAIV_1070.exe --build 8 out.txt ZolikaPatch.ini
ctest --test-dir build-dump -C Release --output-on-failure
```

`ctest` runs `zp_dump_full` (the 1.0.8.0 full write set) and
`zp_dump_ordering` (RemoveUselessChecks must stay ahead of MiscFixes and
RestoreDeathMusic). `--exe/--build` maps any supported executable at its
preferred base, which is how the per-build baselines are produced.

## Style check

`tools/style_lint.py` enforces `CONTRIBUTING.md` across the hand-written sources:

```bat
python tools\style_lint.py --fail-on error
```

Vendored code and generated data are out of scope; the linter lists them.

`tools/magic_numbers.py --check` enforces the no-bare-hex-literal rule: every
literal in the hand-written sources must be a named declaration, carry an
adjacent `why:`/`opaque:`/`orig:`/`provenance:` reason, or be a transcribed
assembly operand of a hook body (proved by the body verifier).
