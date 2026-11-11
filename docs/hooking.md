# Hook bodies

The original mod installs jumps and calls from the game into its own module, so the port has to present the same code at the same entry points. Most bodies are **transcribed**: the original's instruction stream, written as naked assembly with its recovered VA in the comment above it. The rest are **rebuilt** in C++, and [rebuilt.md](rebuilt.md) is their catalogue: the instruction that blocks the transcription, what the rebuild reproduces, how it was verified and what stays unproven.

## Transcription rules

1. **One body, one recovered VA.** The comment names the original function and its VA, `tests/bodies.tsv` carries a row with the body's exact instruction count, and the arm calls `Bodies::Register(body, recovered VA)` before it writes the site.
2. **Resumes are tail-jumped, never called.** `return resume();` compiles to `jmp`; a `call; ret` leaves a return address that a `ret N` resume then eats. The body ends in `jmp DWORD PTR [zpatch_..._resume]`.
3. **Keep the register, flag and stack contract.** The game's surrounding code may depend on EAX, EDX, ECX, EFLAGS or a callee-cleaned stack, so the body replays the instructions the jump overwrites and leaves everything else as the original left it.
4. **Publish before install.** The resume slots are read from the site in the publish tier, before the install tier overwrites it. Publishing after an install makes the body tail-jump to itself.
5. **No new guards.** A body the original dereferences without a check keeps that dereference. A port that does not fault where the original faults is a fidelity bug, not a safety win.
6. **No invented entry point.** A body that cannot be recovered is left out of the registry, which makes the destination a clean skip. Guessing at its code is not an option.
7. **A body is proved whole.** The manifest row's count is the body's true instruction count, not a cap; a short decode on either side fails the row.

## The C2488 rule

MSVC rejects `__declspec(naked)` on a declaration (`C2488: 'naked' can only be applied to non-member function definitions`). In a module tree the rule bites twice: a naked definition with external linkage in an interface unit is written to the BMI, and every importer then fails with C2488. The tree's conventions follow from that:

- a body used above its definition gets a plain `static` prototype in the readable section; only the definition in the marked tail keeps the attribute;
- a body that another unit must reach is either defined in an implementation-side unit or exported by address;
- `source/shared.ixx` keeps the `PlayerAsPedComponentsFix` bodies because they `call` the resolver by name, and `source/stability.ixx` reaches bodies in other modules through `Bodies::All()` instead of naming them.

The tail convention is the other half of the same rule: every module reads header, published state, C++ helpers, `Publish<Key>`, `Install<Key>`, `Register<Key>`, then a marked *Transcribed bodies* section. The readable code is what an installer change touches; the tail is what the proof compares.

## The body registry

`source/bodies.ixx` holds three registries, each with one job:

| registry | what it records | why |
| --- | --- | --- |
| `Bodies::Register(address, reference)` | where a body lives here, and the VA it has in the original ASI | the offline dump rewrites a branch to a registered body back to the recovered VA, so its bytes do not depend on where the tool placed the body; it is also how a module reaches a body it cannot export |
| `Bodies::RegisterTable(address, reference, bytes)` | a table a body reads through an indexed absolute operand | the proof compares the table's *contents* (`table:<content hash>`) instead of a displacement the module cannot reproduce |
| `Bodies::RegisterPublished(slot)` / `RegisterPublishedImage(slot)` | the 4-byte slots an arm writes a module or game-image address into | the dump records the baseline's placeholder for those words; only the arm knows which words they are |

A table is registered only if a `table` row in `tests/bodies.tsv` mirrors the call. A displacement no row names keeps the literal comparison, so the proof is never relaxed by accident. The registered tables are the 512-slot model tables and the 38,455-entry name table; the proof currently carries 175 body rows and 9 table rows.

## The whole-body proof

`tools/verify_bodies.py` disassembles exactly the manifest's instruction count on both sides - the original ASI at the recovered VA and the built ASI at the symbol the linker map resolves - and compares each instruction as:

```
mnemonic  reg:<name>...  imm  [base+index*scale+disp]  abs  [index*scale+table:<hash>]
```

An absolute operand is a *slot*: the module may point it at its own global or its own function. A register, a frame displacement and a memory shape are compared literally, because the transcription writes the same instructions. A `call`/`jmp`/`push` immediate is a slot as well. A table operand is a slot only when the manifest names it, and then the comparison is the table's content.

The sweep is linear and does not stop at the first terminal instruction: a body that returns in the middle still has its tail compared. Registered as the `zp_body_proof` ctest and runnable directly:

```
python tools/verify_bodies.py --original ../game_refs/ZolikaPatch_original.asi.off \
    --candidate build/win-x86/Release/ZolikaPatch.asi \
    --bodies tests/bodies.tsv \
    --map build/win-x86/Release/ZolikaPatch.map
```

It exits 0 when every row is proved, 1 when a body differs, and 2 when an input or capstone is missing. `--selftest` compares the original with itself (must pass) and with a one-byte corruption (must fail), and exercises the table path both ways.

## What is rebuilt, and why

Four mechanical conditions decide whether a stream can be transcribed. Class A, an indexed absolute table operand, is now empty: the registry compares such a table by content, so the body is transcribed and proved. The remaining three classes are rebuilt:

| class | why the stream cannot be compared | pieces |
| --- | --- | --- |
| B - exception frame | `__try/__except` and C++ static-init frames split a function into fragments: the registration prologue writes `fs:[0]`, the handler is a separate funclet outside the linear stream, and a scope table or throw-info record in `.data` decides what the frame does. For the guarded native entries the frame *is* the feature. | guarded resolve-and-invoke `0x100038E0`, guarded resolve-only `0x10003980`, DFA close-handle `0x10004790`, `SpeedupBugFix` `0x10050540`, the audio install `0x10050AD0`, the save converter `0x10051B30`, save prepare `0x10051FD0`, OOC init `0x1004B750`, OOC handler `0x1004C3F0` |
| C - TLS static-init guard | a function-local `static` is initialised through `_Init_thread_header` / `_Init_thread_footer`, which reach the CRT's per-thread block through `fs:[2Ch]` and the image's `TlsIndex`; that data is image-internal state a rebuilt module cannot name. | the three NewFiles scans (`0x10051730`, `0x10051610`, `0x100517E0`), and the ROM message service |
| D - call graph of rebuilds | the body itself is comparable, but every function it calls is a rebuild, so a transcription would prove the branch shape and none of the behaviour. | the two NewFiles updates (`0x10051890`, `0x100518D0`) and the three NewMemorySystem arena methods (`0x10038360`, `0x10038430`, `0x100385A0`) |

A `/GS` cookie is **not** a reason to rebuild: `mov eax,[__security_cookie] / xor eax,ebp` and the epilogue's check are ordinary instructions with a slot operand, and the tree transcribes such bodies. The same goes for SSE conversions, `cmov` and the `lahf`/`test ah,44h` float comparison idiom.

Each rebuild's evidence differs by class. Where a piece has observable behaviour offline, a test drives it (`zp_native_guard`, `zp_dfa_close`, `zp_speedup_clock`, `zp_save_walk`, `zp_ooc_init`, `zp_nms_arena`); the rest are pinned by their install rows and by the writes their arms make. [rebuilt.md](rebuilt.md) records each piece's recovered VA, size and instruction count, the instruction that blocks it, the call sequence the rebuild reproduces, and the residual risk.

## Preserved faults

The policy is one sentence: the shipped mod behaves like the original for every input, so a guard that changes a fault/no-fault outcome is a bug. Every preserved fault is recorded in [behavior.md](behavior.md) with its condition, VA and instruction, and many are measured rather than described:

- `NoImpostorHeightCutoff` reproduces the original's 1.0.0.0 access violation (`0x100536F1`), so that build's per-option cell dies there by design.
- the NewFiles texture helpers fault on a scan miss because the original reads or calls through the null the scan left;
- the DFA close-handle frame's filter is `EXCEPTION_CONTINUE_EXECUTION`, so a faulting `CloseHandle` is retried, never answered;
- a non-numeric or out-of-range INI value throws out of the loader exactly as the original does, and a 256-byte line re-parses for ever.

The sanctioned divergences are a config-level escape hatch that keeps the original code in place (the existing example is `RemoveBoundingBoxCulling=0`, whose `0` is the original's own default) and a divergence that is strictly unavoidable, such as an artefact of rebuilding a body rather than transcribing it. Each is recorded with its evidence in [behavior.md](behavior.md), which names all of them.

## A transcribed body

```cpp
// abi_aim_projectile_crash_fix, recovered VA 0x10005590 (sub_10005590): a
// __thiscall that stores the caller's two dwords into the object in ECX, derives a
// flag from the second and returns with `ret 12`.
static __declspec(naked) void AimProjectileShim()
{
    __asm
    {
        cmp ecx, 0
        je aim_done
        mov eax, DWORD PTR [esp + 8]
        mov DWORD PTR [ecx + 0x20], eax
        mov eax, DWORD PTR [esp + 0xc]
        mov DWORD PTR [ecx + 0x24], eax
        neg eax
        sbb eax, eax
        and eax, DWORD PTR [esp + 4]
        mov DWORD PTR [ecx + 0x1c], eax
    aim_done:
        ret 12
    }
}
```

The comment above it states the calling convention, the recovered VA and what the body does, and the manifest row proves all 11 instructions against the oracle. A body whose tail ends in a tail jump names the published slot instead of a `ret`, because the resume belongs to the site, not to the body.
