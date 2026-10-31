# MinGW notes

The supported release build is MSVC x86 (`ZolikaPatch.sln` or
`build\build_msvc.cmd`). MinGW-w64 can build the non-ASI core and the native
tests, but not the release ASI:

- several hook bodies use MSVC `__asm` syntax;
- the CloseHandle body uses MSVC SEH (`__try`/`__except`);
- the exact register/stack contracts are verified on the MSVC object code.

## Building the core / tests

Use a 32-bit MinGW-w64 toolchain (`i686-w64-mingw32-g++`) if you need the
inline assembly to compile:

```sh
i686-w64-mingw32-g++ -std=c++17 -O2 \
  -Isrc -Iexternal/injector/include -Iexternal/Hooking.Patterns \
  external/Hooking.Patterns/Hooking.Patterns.cpp src/catalogue_data.cpp \
  src/patch_data.cpp src/xlive/xliveless.cpp src/dllmain.cpp \
  tests/test_main.cpp \
  -o build/zp_tests.exe -lversion -luser32 -lshlwapi
```

The vendored libraries are git submodules under `external/`; run
`git submodule update --init --recursive` first.

## Packaging

Do not distribute a MinGW-built ASI. The bodies are transcribed at the
instruction level and must be built with the same toolchain/ABI assumptions
used by the verification step. If you only need to read or modify the mod,
MinGW is fine for the portable modules.
