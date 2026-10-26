# Building

## Requirements

- Windows 10/11.
- Visual Studio 2022 or newer with the **Desktop development with C++** workload
  and the **x86** toolchain. The ASI must be 32-bit.
- The submodules: `git submodule update --init --recursive`.

No game files are needed to compile. The feature files under source/ are C++20 modules (.ixx); both build paths scan them.

## One-shot build script

```bat
build.bat          :: configure + build both ASIs into bin\
build.bat --test   :: also run the native tests
```

## Premake (the repository build)

```bat
git submodule update --init --recursive
premake5 vs2022
msbuild build\OpenPreCEPatch.sln /p:Configuration=Release /p:Platform=Win32
```

Both modules are emitted into `bin/` by one build:

```
bin/IV.EFLC.OpenPreCEPatch.asi
bin/IV.EFLC.OpenPreCEPatch.ini
bin/XLivelessAddon.asi
bin/XLivelessAddon.ini
bin/*.map                         (development only)
```

CMake uses the same folder through `OPCEP_DIST_DIR` (default `bin/`).

`premake5.lua` reads an optional `.env` next to it:

```
GTAIV_DIR="C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV"
```

When `GTAIV_DIR` is set, a build copies the ASI over an already-installed
`IV.EFLC.OpenPreCEPatch.asi` in that folder. It never installs into a folder
that does not already have the mod.

## CMake (alternative)

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Targets: `OpenPreCEPatch` (the main ASI), `XLivelessAddon` (the separate successor),
`opcep_tests` (native tests) and `opcep_dump` (write-set dump).

## Link settings that matter

- `/MACHINE:X86`
- `/SUBSYSTEM:WINDOWS`
- `/BASE:0x10000000` (the mod's preferred base)
- `/INCREMENTAL:NO`
- `/MAP`
- static CRT (`/MT`)
- `version.lib user32.lib shlwapi.lib`

## Continuous integration

`.github/workflows/msvc_x86.yml` downloads premake5, generates the solution,
builds Release|x86, runs the native tests and uploads the ASI and map.

## Submodules

| path | upstream |
| --- | --- |
| `external/injector` | https://github.com/ThirteenAG/injector |
| `external/hooking` | https://github.com/ThirteenAG/Hooking.Patterns |
| `external/inireader` | https://github.com/ThirteenAG/IniReader |

`external/injector` has its own nested submodules (safetyhook, zydis, bddisasm,
kananlib, minhook); `--recursive` fetches them for users who build the injector
targets. The mod itself uses the header-only injector API and Hooking.Patterns.
