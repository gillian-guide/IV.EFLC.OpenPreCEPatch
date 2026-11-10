# The MMA10 crash with NewMemorySystem

Investigated from the four crash reports in
`D:\Games\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\CrashDumps` and the
2026-11-04 minidump. The three 2026-11-03 crashes are **not** MMA10:

| report | exception | faulting module | relevance |
| --- | --- | --- | --- |
| `20261103_190540` | `EXCEPTION_STACK_OVERFLOW` | `gameoverlayrenderer.dll` <-> `SISCO.asi` recursion | unrelated plugin/overlay loop |
| `20261103_190552` | access violation, read | `CloudFixIV.asi+0x19B8` | unrelated plugin |
| `20261103_190622` | access violation, read | `CloudFixIV.asi+0x19B8` | unrelated plugin |

The memory run is `20261104_190205`: GTA IV 1.0.8.0, `NewMemorySystem=1`,
`ImprovedShaderStreaming=1`, FusionFix present with 31 overlapping options gated,
2.1 GB private, 2.2 GB working set, 726 MB address space free, 11,061 handles,
uptime 2:39. The exception is a null write at `GTAIV.exe+0x56D16`
(`0x00456D16`). The stack holds the UTF-16 text `GTA IV Fatal Error: MMA10`.

## What MMA10 is

`0x00456D16` is inside `lmsgboxExit` (`0x00456680`):

```
mov large ds:0, ebx      ; 0x00456D16 - deliberate null store after the box
```

This is the game's "show the fatal box, then crash" path; it is a symptom, not
the cause. The cause is one frame up:

```
0x00455FF0  sysMemMultiAllocator::free
  ...
  0x00456008  mov ecx, [edi]              ; child allocator
  0x0045600A  mov eax, [ecx]
  0x0045600C  mov edx, [eax+48h]          ; child->isThisPointerMine
  0x0045600F  push ebp                    ; the pointer being freed
  0x00456010  call edx
  0x00456012  test al, al
  0x00456014  jnz loc_456039              ; found an owner
  ...
  0x00456021  push 0 / push 0 / push "MMA10"
  0x0045602A  call lmsgboxExit
```

So **MMA10 means "no allocator child claims this pointer"** — a mis-owned free,
not a heap-exhaustion error code. The game then shows the fatal box and null-writes
on purpose.

The minidump gives the arguments: the stack frame holds
`a2 = 0x979F1B10` (also the saved ESI), and the NMS registry at
`IV.EFLC.OpenPreCEPatch.asi+0x1BEBF0` has 127,454 live entries in a 262,144-slot
table. The pointer is 16-byte aligned and sits in the same `0x979F....` region as
other live game objects (`0x979F5290`, `0x979FB190`, ...), so it is an
NMS-private-heap-shaped address.

## Why NMS rejects it

The original simple allocator answers ownership by **range**
(`sysMemSimpleAllocator::isThisPointerMine`, `0x00454FB0`):

```c
BOOL isThisPointerMine(this, ptr) {
    int base = this[1];              // the 100 MiB arena
    return ptr >= base + 16 && ptr < base + this[19];
}
```

Any pointer inside the arena is "mine", including an interior pointer and a
pointer that was already freed. NMS replaced that with an **exact allocation
start** set (`sub_10019D80` in oPCEP: open-addressing table, key == pointer).
The two questions differ:

* an interior pointer into a live NMS block is inside the block but not its start;
* a double-freed pointer was erased from the set on the first free;
* in both cases the original would still route the free to the simple allocator,
  while NMS says "not mine", the multi allocator finds no owner, and the game
  fatals with MMA10.

The fix is therefore not "more memory": it is to restore the range answer and to
stop a non-exact free from reaching `HeapFree` a second time.

## What this tree does now

The `nms` group keeps the exact registry for the real free and adds a 128 KiB
page bitmap (one bit per 4 KiB page of the 32-bit address space, allocated once):

* every NMS block marks its pages, and a page stays marked like the original
  arena's reserved space;
* `isThisPointerMine` answers exact-or-page, so an interior or freed pointer is
  still "mine" and the multi allocator does not fatal;
* `free` uses the exact registry: an exact live pointer is `HeapFree`d; a pointer
  that is only inside a marked page is a logged no-op; a pointer outside every
  page goes to the captured original allocator (or is left alone). It is never
  freed twice.

Measured by `zp_nms_arena` (255 checks): interior containment, interior release
no-op, page-stays-owned after free, double release ignored, foreign pointer
untouched, overflow refused.

## What this does not fix

The 2.1 GB private working set is separate. NMS removes the game's fixed 100 MiB
arena and allocates through a private heap; per-allocation overhead and heap
fragmentation can be worse than the arena, and the run also carried DXVK/DLSS,
FusionFix, OpenPreCEPatch, XLivelessAddon and several plugins. The MMA10 fatal is
now prevented, but if the process still reaches the 2 GB/3 GB 32-bit limit the
game's own allocation failure will surface elsewhere. Reproduce with
`OPCEP_IMPROVEMENTS=nms` in this tree (or report the range-set fix upstream to
OpenPreCEPatch) and watch private bytes/address space before the next run.

## Provenance

Game addresses and the two ownership tests were read from the annotated
1.0.8.0 IDB (`IDA Files\GTAIV 1.0.8.0_Human.i64`, base `0x400000`); the oPCEP
side from the shipped `IV.EFLC.OpenPreCEPatch.asi` and its PDB-less symbols
(`sub_100189F0` / `sub_10019D80`). The minidump was read with the `minidump`
Python package.
