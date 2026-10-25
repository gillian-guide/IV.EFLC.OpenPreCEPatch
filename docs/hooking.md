# Hooking

The installers write code the way the original loader does: a signature scan
finds a site, then `injector::` writes bytes, installs a five-byte branch, or
points a call at a rebuilt body. Resumes are tail-jumped (`jmp`), never called.

## Bodies

`source/hooks.cpp` contains the machine-level bodies and the
`{reference_va, address}` registry. Every body keeps the calling convention and
register contract of the original destination. A body that is not in the
registry resolves to 0 and the installer skips its row, exactly like a handler
whose destination is missing.

Installers refer to a body by name:

```cpp
injector::MakeJMP(match, Abi("abi_path_node_shim"), true);
```

`AbiByName` is a table generated from the same definitions, so an installer and
the registry can never disagree about a body's address.

## Example: a resume body

```cpp
extern "C" __declspec(naked) void abi_path_node_shim()
{
    __asm
    {
        // ... original body ...
        jmp dword ptr [zpatch_path_node_resume_ok]
    }
}
```

The resume slot is published by the installer before the site is overwritten.
That order is why `PatchGame` runs all publishes before the install arms.

## Writer helpers

- `Pattern(image, "AA BB ? CC")` — the original's whitespace-separated
  signature syntax, normalised for Hooking.Patterns.
- `MatchAddress`, `MatchTarget`, `MatchPointer` — the site inside a match.
- `WriteBranchNoRead` — a five-byte branch that does not first dereference a
  `FF 15 <disp32>` operand.
- `Abi("name")` — a body address, or null in the dump harness.

## Contract rules

1. Preserve the original register, flag and stack contract. The decompiled C
   hides these; the disassembly is the reference.
2. Resume with `jmp`, not `call; ret`.
3. Publish a resume before overwriting the site that owns it.
4. Keep the original's faults: a missing null check is part of the behaviour
   until a later, documented improvement changes it.
5. Gate every install on its option and build.
