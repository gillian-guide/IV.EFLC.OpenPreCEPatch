# ImprovedShaderStreaming and NewMemorySystem

Both options replace a hot path in the game rather than patching a value. The
notes below describe what the game does, what the option does, and which parts
are deliberate compatibility or robustness changes. Game addresses are for
GTA IV 1.0.8.0; the installers locate each site by signature, never by a bare
address.

## ImprovedShaderStreaming

### The game

`CBaseModelInfo::addCustomShaderEffect` (`0xCB21D0`) chooses the custom shader
effect for a drawable:

1. `a5 == 3` → `CCustomShaderEffectPedProp`.
2. Otherwise `CCustomShaderEffectAnimUVFX::CreateExtraAnimationUVvariables` may
   create `CCustomShaderEffectAnimUVFX`.
3. Otherwise it asks the drawable's `grmShaderGroup` whether it contains any of
   the known shader names, one `grmShaderGroup::LookupShader` call per name:
   8 ped/hair names (`gta_ped`, `gta_ped_alpha`, `gta_ped_reflect`,
   `gta_ped_skin`, `gta_ped_skin_blendshape`, `gta_ped_hair`,
   `gta_hair_sorted_alpha`, `gta_hair_sorted_alpha_exp`) lead to
   `CCustomShaderEffectPedBoneDamageFX` (0x370 bytes); 45 vehicle names
   (`gta_vehicle_default` … `gta_vehicle_vehglass`) lead to
   `CCustomShaderEffectVehicleFX::Create`.

The lookup result is only tested for null. Any shader of the right family makes
the game create that effect class; the exact name does not matter to the caller.

### The option

The first lookup of each family is replaced with a scan of the same shader group.
`ShaderStreamingFindHpe` (`0x10045A30`) replaces the `LookupShader("gta_ped")`
call and `ShaderStreamingFindV` (`0x10045A80`) the
`LookupShader("gta_vehicle_default")` call:

* the group's array is at `group + 8`, its `uint16` count at `group + 0xC`;
* the walk stops at the count, skips a null entry, and asks each shader's
  `vtable[+0x10]()` for its name;
* the HPE filter (`0x100459E0`) accepts name byte 4 `'h'` (`gta_hair`) or byte 4
  `'p'` with byte 5 `'e'` (`gta_ped`); the V filter (`0x10045A10`) accepts byte 4
  `'v'` (`gta_vehicle`);
* a match returns that entry, a miss returns null and the game's original name
  chain continues.

The two call sites are replaced with the same `E8 rel32` form as the original
calls (`injector::MakeCALL`), so the calling convention is unchanged: `ECX` is
the shader group and both bodies return with `ret 4`, exactly as the original's
`sub_10045A30` / `sub_10045A80` do. The arm checks that the two recovered offsets
really hold an `E8` before it writes, so a build that matches the surrounding
bytes without the two call opcodes is left alone.

**Documented delta: the chain jumps are not written.** The original's handler
wrote four sites: the two calls plus two `jmp`s that skip the rest of each name
chain. This tree writes only the two calls. The jumps are a miss-path speed-up,
and the bytes they overwrite are part of LibertyCityPlates' address signature:
that mod scans the vehicle block and aborts its address setup — so its shader
code never loads — when the signature no longer matches. A miss now falls through
the game's own chain, which is the original semantics; a hit still skips the rest
through the game's existing `test eax, eax; jnz`. The retained write set is the
one the scoreboard reports as a measured delta.

## NewMemorySystem

### The game

`initialize_memoryAllocators` (`0x401060`) constructs the global
`rage::sysMemSimpleAllocator` at `0x19CF9F0`:

* one 100 MiB `HeapAlloc` block (`0x6400000`) is carved into fixed-size free
  lists (8, 16, 32 and 64-byte buckets with 2044/1022/511/255 entries) plus the
  descriptor tables;
* the buddy and multi allocators are initialised afterwards, and the multi
  allocator is stored in the TEB.

Ownership is answered by **range** (`sysMemSimpleAllocator::isThisPointerMine`,
`sub_454FB0`): `ptr >= block + 16 && ptr < block + size`. Any pointer inside the
reserved arena is "mine", including an interior pointer and a pointer that was
already freed. The arena never returns memory to the OS, so unused bucket space
is lost for the session and the 100 MiB reservation is visible in the address
space even when the game needs less.

### The option

The allocator's vtable is replaced at construction time. The 100 MiB block and
the bucket lists are never created; allocation becomes a `HeapAlloc`-backed
allocator with the game's alignment contract:

```
unit    = max(0x10, align)
want    = max(size, unit)
block   = HeapAlloc(want + 2*unit)
aligned = ((block - 1) / unit + 2) * unit
[aligned - 4] = aligned - block
fill want bytes with 0xCD
register aligned in the ownership set
mark the pages the block covers
```

`free` recovers the block base from `[aligned - 4]`, frees it, and removes the
ownership entry. Only `vtable[18]` ("is this pointer mine?") consults the map.

The install retargets the allocator-initialisation call at the recovered site to
`NmsApplyHardenedBody`, which captures the pre-swap vtable and then writes the
tree's own 24-slot table (`0x100B5038`'s entry order) into the published
singleton; a jump over the five-slot clear the body replaces resumes at the
original's continuation. Ten of the table's methods are transcribed bodies and
three are rebuilt in C++ (allocate `0x10038360`, free `0x10038430`, membership
`0x100385A0`).

Deliberate differences from the recovered allocator:

* **private heap** — `HeapCreate` instead of the process heap, falling back to
  the process heap if the create fails. The game's allocation traffic no longer
  contends with the CRT heap used by FusionFix, LibertyCityPlates and the rest of
  the process.
* **overflow guard** — a request is refused before `HeapAlloc` when the alignment
  exceeds 1 MiB, when `want + 2*unit` does not fit in 32 bits, or when the
  aligned pointer plus `want` would pass the address space. The game never asks
  for such a block; a corrupt size would otherwise under-allocate and then be
  written past.
* **published-singleton guard** — the apply body writes the vtable only when the
  published singleton is non-null (the original's own `test eax, eax`), so a
  missed publish scan leaves the game's allocator alone instead of writing
  through zero.
* **range ownership, exact free** — "is this pointer mine?" answers from a
  128 KiB page bitmap (one bit per 4 KiB page of the 32-bit address space,
  allocated once): every block marks its pages and a page stays marked, like the
  original arena's reserved space. `free` still uses the exact registry: an exact
  live pointer is `HeapFree`d, a pointer that is only inside a marked page is a
  logged no-op, and a pointer outside every page goes to the captured original
  allocator (or is left alone). This is the `MMA10` fix: the game's
  `sysMemMultiAllocator::free` (`0x455FF0`) fatals through
  `lmsgboxExit("MMA10")` (the call at `0x456021`; the box's own exit path is the
  null write at `0x456D16`) when no allocator child claims the pointer, which is
  exactly what an exact-start set answered for an interior or already-freed
  pointer.
* **foreign-free fallback** — a pointer that is not in the ownership set is
  handed back to the original allocator when its vtable was captured, instead of
  reading NMS's base-offset dword from a pointer NMS never allocated. The game
  and other ASIs can hold objects that predate the swap. If no original vtable
  was captured, the pointer is left alone rather than freed through the wrong
  heap.

The vtable shape, entry order and calling conventions are the game's; the small
methods (constructor, thunks, ownership query) keep their exact prologues.
