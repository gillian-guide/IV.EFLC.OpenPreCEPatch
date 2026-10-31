# OpenPreCEPatch

[![Build](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/gillian-guide/IV.EFLC.OpenPreCEPatch/actions/workflows/msvc_x86.yml)

OpenPreCEPatch is an open-source successor to [ZolikaPatch](https://github.com/gillian-guide/ZolikaPatch), the GTA IV / Episodes from Liberty City quality-of-life and crash-fix plugin. It keeps the original mod's fixes and features, rewrites them as readable, maintainable C++23, and modernises the loader, hooking and configuration layers around the same signature data, as well as introduces improvements to some unreliable code.

The mod targets improvements to the PreCE experience to bring closer parity to CE, as well as improve general stability and improve the multiplayer experience. Features that exist in FusionFix are already gated and are intentionally not reimplemented in this mod.

This mod is meant to be used with [this fork of XLivelessAddon](https://github.com/gillian-guide/XLivelessAddon) as a companion, as some features belong there.

The mod also carries the standalone `GFWLProtectionDisabler2019` patch and a reimplementation of the Steam Achievements mod that works differently from the original one - now GFWL achievements work alongside Steam's.

## Installation

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (can be installed as `xlive.dll` for xliveless)
2. Extract the archive next to `GTAIV.exe` and `EFLC.exe`.

If an old `ZolikaPatch.ini` is present and `IV.EFLC.OpenPreCEPatch.ini` is not, the ASI imports the old values on first run and writes the grouped file. The old file is left untouched.

## Supported game versions

| compact | version | status |
| ---: | --- | --- |
| 1000 | 1.0.0.0 | supported |
| 1004 | 1.0.0.4 | supported |
| 1040 | 1.0.4.0 | supported |
| 1060 | 1.0.6.0 | supported |
| 1070 | 1.0.7.0 | supported |
| 1080 | 1.0.8.0 | supported |
| 1110 | 1.1.1.0 (TLAD) | supported |
| 1120 | 1.1.2.0 (TBoGT) | supported |
| 1130 | 1.1.3.0 (EFLC) | supported |

## Building

Requirements: Windows 10/11, Visual Studio 2022 17.10+ (or newer) with the **x86** C++ toolchain, CMake 3.31+, and the submodules checked out.

```bat
git clone --recursive https://github.com/SandeMC/IV.EFLC.OpenPreCEPatch
cd OpenPreCEPatch
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

The output is `bin/IV.EFLC.OpenPreCEPatch.asi` and its INI.

The 64-bit parity dump is a separate configure:

```bat
cmake --preset x64
cmake --build --preset x64-release
```

`build.bat` wraps the x86 configure/build/test sequence. See [docs/building.md](docs/building.md) for the target list, the MSVC settings that matter and the submodule layout.


## Credits

- Original mod by **Zolika1351**.
- **GTAIV.EFLC.FusionFix** by ThirteenAG and contributors
- `injector` by LINK/2012 and ThirteenAG
- `Hooking.Patterns` by CitizenFX and ThirteenAG
- `IniReader` by ThirteenAG.
