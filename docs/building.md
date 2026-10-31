# Building

## Requirements

- Windows 10/11.
- Visual Studio 2022 or newer with the **Desktop development with C++**
  workload and the **x86** toolchain. The ASI must be 32-bit; a 64-bit build
  will not load.

The two vendored libraries are git submodules, so check them out first:

```bat
git clone --recursive https://github.com/gillian-guide/ZolikaPatch.git
```

or, in an existing checkout:

```bat
git submodule update --init --recursive
```

No game files are needed to compile.

## Solution build (the original project's build)

Open `ZolikaPatch.sln` and build `Release | x86`, or from a shell:

```bat
msbuild ZolikaPatch.sln /p:Configuration=Release /p:Platform=x86
```

It produces two artefacts:

```
build/msvc-x86/ZolikaPatch.asi        <- copy next to GTAIV.exe
build/msvc-x86/ZolikaPatchTests.exe   <- native tests, no game needed
```

`ZolikaPatch.vcxproj` is the ASI (DynamicLibrary, precompiled header `pch.h`, created by `pch.cpp`); `ZolikaPatchTests.vcxproj` builds the console tests. Both compile the sources under `src\` plus the vendored `include\Hooking.Patterns\Hooking.Patterns.cpp`, with `src\`, `include\injector\include` and `include\Hooking.Patterns` on the include path. The submodule's `.cpp` is built `NotUsing` the precompiled header, because upstream does not include `pch.h`.

Link settings that matter:

- `/MACHINE:X86`
- `/SUBSYSTEM:WINDOWS`
- `/BASE:0x10000000` (the mod's preferred base)
- `/INCREMENTAL:NO`
- `/MAP`
- `version.lib user32.lib shlwapi.lib`

`ZolikaPatch.def` sets the module name; the ASI exports nothing.

## Batch build (no solution needed)

From the project root:

```bat
build\build_msvc.cmd
```

Run it from an **x86 Developer Command Prompt** (so `cl.exe` is on `PATH`), or pass the full path to `vcvars32.bat` as the first argument. It compiles the same sources into `build\msvc-x86\ZolikaPatch.asi`, and emits `build\msvc-x86\ZolikaPatch.map`. The script uses the `src\` and `include\` include paths that match the current tree; an older copy assumed the sources sat next to `build\`.

## CMake

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

`ZP_BUILD_ASI=OFF` builds only the static core; `ZP_BUILD_TESTS=OFF` skips the tests. The ASI target uses the same base/subsystem/map options.

## Continuous integration

`.github/workflows/build.yml` builds the ASI and runs the native tests on
`windows-latest` for every push and pull request, and uploads
`ZolikaPatch.asi` and its `.map` as build artefacts. The same three commands
work locally:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
build\Release\zp_tests.exe
```

## MinGW

See `build/build_mingw.md`. MinGW-w64 can compile the core and run the native tests, but the release ASI should be built with MSVC: several bodies use MSVC's `__asm` syntax and SEH keywords. A MinGW ASI is not supported for distribution.

## Output

```
build/msvc-x86/ZolikaPatch.asi   <- copy next to GTAIV.exe
build/msvc-x86/ZolikaPatch.map   <- development only
```

If you rebuild while the game is running, the file is locked. Close the game first.

## Reproducing the data

`catalogue_data.cpp` is committed model data and `ooc_payload.h` is the original compressed OutOfCommission resource. `data/finale2.sco` is the decoded table kept for research; `tools/compress_ooc.py` can create an equivalent blob on demand. Neither script runs during a normal build. `tools/extract_data.py --help` documents regeneration from files you own.
