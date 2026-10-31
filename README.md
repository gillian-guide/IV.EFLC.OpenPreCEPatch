# Open-source reimplementation of ZolikaPatch v7.65

This project is an open-source reconstruction of **ZolikaPatch 7.65** for Grand Theft Auto IV. This project has been done primarily by LLM agents. The compiled version can be used as a replacement for the original binary.

The code is recovered from the shipped 32-bit ASI by disassembly and decompilation, then written back as ordinary C++17: the loader, the per-option install blocks, the hook bodies and the launcher bypass.

It is not Zolika1351's original source. It is a study of what the shipped plugin does and how, with the addresses, signatures, defaults and behaviour taken from the 7.65 binary.

## Supported game versions

| compact | version | status |
| ---: | --- | --- |
| 1000 | 1.0.0.0 | supported |
| 1004 | 1.0.0.4 | supported |
| 1040 | 1.0.4.0 | supported |
| 1060 | 1.0.6.0 | supported |
| 1061 | 1.0.6.1 | recognised, unverified |
| 1070 | 1.0.7.0 | supported |
| 1080 | 1.0.8.0 | supported |
| 1100 | 1.1.0.0 (EFLC) | recognised, unverified |
| 1110 | 1.1.1.0 (TLAD) | supported |
| 1120 | 1.1.2.0 (TBoGT) | supported |
| 1130 | 1.1.3.0 (EFLC) | supported |

The launcher bypass is optional. Use it only with a legitimate game copy.

## Building

`injector` and `Hooking.Patterns` are git submodules under `include/`, so clone recursively (or run `git submodule update --init --recursive` in an existing checkout):

```bat
git clone --recursive https://github.com/gillian-guide/ZolikaPatch.git
```

You need Visual Studio 2022/2026 with the x86 C++ toolchain. Open `ZolikaPatch.sln` and build `Release | x86`, or:

```bat
msbuild ZolikaPatch.sln /p:Configuration=Release /p:Platform=x86
```

The output is `build\msvc-x86\ZolikaPatch.asi` plus the native tests and a linker `.map`. `build\build_msvc.cmd` and CMake are also supported:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

A GitHub Actions workflow builds the ASI and runs the native tests on every push. See `docs/building.md` for details and MinGW notes.

## Accuracy

The 7.65 binary is the specification. The recovery keeps what the original does, including its rough edges: missing null checks, uninitialised reads, out-of-bounds walks and the crashes they can cause are reproduced. `docs/origins.md` lists the preserved behaviour and the few places where the source could not be made byte-identical. If a behaviour differs from the shipped plugin, that is a bug in this reconstruction.

# Credits

- **Zolika1351** - author of ZolikaPatch, the mod this code reconstructs.
- **XLivelessAddon** - the launcher/DRM bypass and the xliveless design that ZolikaPatch's loader descends from.
- **LINK/2012** - the `injector` header library.
- **CitizenFX contributors** - `Hooking.Patterns`, and the `Hooking.h` / `Hooking.cpp` hook framework carried in the tree.
- **Takashi Tosaka** - `jitasm.h`, the runtime assembler the original project built its hooks on.
- **Turbine** - the Feather INI parser, whose `INI<>` call shape this project's reader keeps.
- **Rockstar Games / Take-Two** - GTA IV and EFLC. This project is not affiliated with or endorsed by them.