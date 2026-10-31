# Building

## Requirements

- Windows 10/11.
- Visual Studio 2022 17.10 or newer with the **Desktop development with C++**
  workload and the **x86** toolchain. Visual Studio 2026 is supported. The
  shipped ASI must be 32-bit.
- CMake 3.31 or newer (the repository uses presets and C++23 module file sets).
- The submodules: `git submodule update --init --recursive` after cloning, or
  clone with `--recursive`.

No game files are needed to compile. The feature files under `source/` are C++23
modules (`.ixx`); both CMake and the compiler scan them.

## Quick start

```bat
git clone --recursive https://github.com/OpenZDecomp/OpenPreCEPatch
cd OpenPreCEPatch
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

The presets do not pin a Visual Studio generator, so they use whichever Visual
Studio CMake finds. `x86` sets the Win32 platform; `x64` builds only the parity
dump.

`build.bat` wraps the same sequence:

```bat
build.bat          :: configure + build the ASI into bin\
build.bat --test   :: also run the native tests
```

## Outputs

```
bin/IV.EFLC.OpenPreCEPatch.asi
bin/IV.EFLC.OpenPreCEPatch.ini
build/x86/Release/opcep_tests.exe
build/x86/Release/opcep_dump.exe
build/x64/Release/opcep_parity_dump.exe
```

The `.map` file is a development aid; `release.bat` packages the ASI and INI.

## Targets

| target | platform | purpose |
| --- | --- | --- |
| `OpenPreCEPatch` | x86 | the shipped ASI |
| `opcep_tests` | x86 | native test suite (`ctest`) |
| `opcep_dump` | x86 | write-set dump against a mapped copy of the game |
| `opcep_parity_dump` | x64 | the same dump without the 32-bit body registry |

`OPCEP_BUILD_TESTS`, `OPCEP_BUILD_DUMP` and `OPCEP_BUILD_PARITY_DUMP` can be
turned off on the configure line.

## Install

```bat
cmake --install build/x86 --config Release --prefix "C:\path\to\game"
```

This copies `IV.EFLC.OpenPreCEPatch.asi` and its INI. Close the game first.

## Link settings that matter

- `/MACHINE:X86`
- `/SUBSYSTEM:WINDOWS`
- `/BASE:0x10000000` (the mod's preferred base)
- `/INCREMENTAL:NO`
- `/MAP`
- static CRT (`/MT`)
- `version.lib user32.lib shlwapi.lib`

The library is linked with `$<LINK_LIBRARY:WHOLE_ARCHIVE,opcep_core>`; dropping
whole-archive silently removes every feature arm that is only reachable through
the static registry.

## Submodules

| path | upstream |
| --- | --- |
| `includes/injector` | https://github.com/ThirteenAG/injector |
| `includes/hooking` | https://github.com/ThirteenAG/Hooking.Patterns |
| `includes/inireader` | https://github.com/ThirteenAG/IniReader |

`includes/injector` has its own nested submodules (safetyhook, bddisasm,
kananlib, minhook) plus a vendored Zydis copy. `--recursive` fetches them for
users who build the injector targets; the mod itself uses the header-only
injector API and Hooking.Patterns.

To update a submodule, check out the new upstream commit inside
`includes/<name>`, then commit the new gitlink and keep `.gitmodules` on the
`includes/` path.

## Continuous integration

`.github/workflows/msvc_x86.yml` checks out the submodules recursively, verifies
the generated INI template, builds and tests the x86 targets, builds the x64
parity dump, and uploads the ASI, its map and the dump.

## Troubleshooting

- `Cannot find source file: includes/...` — the submodules are not checked out;
  run `git submodule update --init --recursive`.
- `CMAKE_CXX_COMPILER not found` with the Win32 platform — the x86 C++ toolchain
  is not installed in Visual Studio.
- A clean reconfigure is always safe: delete `build/x86` and rerun
  `cmake --preset x86`.
