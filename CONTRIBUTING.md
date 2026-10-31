# Contributing

The source follows the shape of the original mod: a signature scan, a size check, then an `injector::` call with a short comment naming the instruction. `tools/style_lint.py` checks the mechanical parts of that style:

```bat
python tools\style_lint.py --fail-on error
```

Scope: the hand-written units (`src/*.cpp` and `source/*.ixx`/`*.cpp`, their headers, and `tests/`). Vendored code (the `injector` and `Hooking.Patterns` submodules under `external/`, plus `INI.h`) and generated data (`catalogue_data.cpp`, `ooc_payload.h`) keep their own style.

## Signature scans

```cpp
if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
{
    injector::MakeJMP(pattern.get(0).get<std::uintptr_t>(81),
                      reinterpret_cast<void*>(&abi_blackscreen_shim), true); //jmp
}
```

- `Pattern(image, ...)` is the only scan entry point.
- A match needed more than once may be named once beside its first use: `auto* match = pattern.get(0).get<std::uint8_t>();`. Do not alias a single use.
- Multiple matches are `pattern.size()` and `pattern.get(i)`.

## Writes

Use the injector's typed store, with the width the original wrote:

```cpp
injector::WriteMemory<std::uint8_t>(site, 0xEB, true);   //jmp
injector::WriteMemory<std::uint16_t>(site, 0xC9EB, true); //jmp +3
injector::MakeNOP(site, 6, true);                         //nop the check
injector::MakeJMP(site, reinterpret_cast<void*>(&abi_body), true);
injector::MakeCALL(site, reinterpret_cast<void*>(&abi_body), true);
```

- No bespoke write helpers: `WriteHex`, `WriteValue`, `Nop`, `WritePointer`, `PointerAt`, `SiteAddress`, `RelativeTarget` are not used.
- Slot assignments are direct stores to the re-created globals, as the original handler wrote `dword_1015AD58 = ...`; there is no `Publish()` wrapper.
- `WriteBranchNoRead`/`WriteCallNoRead` are the documented exception where the vendored injector would dereference a corrupted operand before writing.
- Every write gets a trailing comment naming what it does (`//jmp`, `//nop`, `//ret`, `// call <handler>`). A run of writes gets one section comment.

## Locals and numbers

- Declare a local where it is used; do not split a one-use expression into a named temporary unless the name carries information the expression does not.
- Structural numbers are named constants (`kDwordLength`, `kBranchLength`, `kCallOpcode`). Site offsets, build RVAs and patch values are recovered data: keep them literal, and let the comment say what they are.
- No `match + 0`, `+ -12`, or 32-bit-wrap spellings such as `+ 4294967280`.
- No unused locals, duplicate adjacent statements, or empty arms.

## Gates and comments

- Gate with `ctx.Option("Name", "default")` and a build comparison (`ctx.build == GameBuild::v1080`) or `BuildIs(ctx.build, {...})`, exactly as the recovered rows do; never invent a gate.
- Keep the arm's comment and add the build range when it is known: `// data integrity checks VDS102 [v1006 - v1008]`.
- Keep the preserved-fault notes; they document the original, not a bug in this tree.

## Layout

- Tabs for indentation, braces on their own line, one statement per line.
- Keep lines under ~110 columns where the expression allows it.
- The option blocks in `PatchGame` follow the original loader's order.

## Lint baseline

`style_lint.py --fail-on error` is the gate; warnings are reviewed, not auto-fixed. The baseline is 0 errors and a small number of W001 single-use locals, each documented at its declaration (out-parameters, values whose address is taken, deliberately hoisted reads, calling-convention typedefs, and the deferred `initialized` flag that keeps `LoadConfigAndFlags` before the ZMenuIV
guard). Do not inline one without checking its comment.
