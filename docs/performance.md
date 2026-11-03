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

The first lookup of each family is replaced with a scan of the same shader group:

* the group's vector is at `group + 8`, its `uint16` count at `group + 0xC`;
* each non-null entry's `vtable[+0x10]()` returns its name;
* the HPE filter accepts name byte 4 `'h'` (`gta_hair`) or byte 4 `'p'` with
  byte 5 `'e'` (`gta_ped`); the V filter accepts byte 4 `'v'`
  (`gta_vehicle`);
* a match returns that entry, a miss returns null and the game's original name
  chain continues.

The two call sites are replaced with the same `E8 rel32` form as the original
calls, so the calling convention is unchanged: `__fastcall(self, edx, query)`
reproduces `__thiscall(this, query)` (ECX = this, one stack argument, `ret 4`).

The earlier implementation also overwrote the two `mov ecx, [esi+8]`
instructions after those calls with jumps that skipped the rest of each name
chain. Those jumps are only a miss-path speed-up, and the bytes they overwrite
are part of LibertyCityPlates' address signature: that mod scans the vehicle
block and aborts its address setup — so its shader code never loads — when the
signature no longer matches. This implementation leaves those bytes untouched.
A miss now falls through the game's own chain, which is the original semantics;
a hit still skips the rest through the game's existing `test eax, eax; jnz`.

The scan also checks the group pointer and the entry array before dereferencing
them. The game's code assumes both are valid; the check only turns a malformed
call into a null result.

## NewMemorySystem

### The game

`initialize_memoryAllocators` (`0x401060`) constructs the global
`rage::sysMemSimpleAllocator` at `0x19CF9F0`:

* one 100 MiB `HeapAlloc` block (`0x6400000`) is carved into fixed-size free
  lists (8, 16, 32 and 64-byte buckets with 2044/1022/511/255 entries) plus the
  descriptor tables;
* the buddy and multi allocators are initialised afterwards, and the multi
  allocator is stored in the TEB.

The arena never returns memory to the OS. Every allocation is carved from the
fixed block, so unused bucket space is lost for the session and the 100 MiB
reservation is visible in the address space even when the game needs less.

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
```

`free` recovers the block base from `[aligned - 4]`, frees it, and removes the
ownership entry. Only `vtable[18]` ("is this pointer mine?") consults the set.

Deliberate differences from the recovered allocator:

* **private heap** — `HeapCreate` instead of the process heap. The game's
  allocation traffic no longer contends with the CRT heap used by FusionFix,
  LibertyCityPlates and the rest of the process.
* **overflow guard** — a 32-bit size/alignment overflow is rejected before
  `HeapAlloc`. The game never asks for such a block; a corrupt size would
  otherwise under-allocate and then be written past.
* **published-singleton guard** — the install arm refuses to run when the publish
  scan did not resolve `g_systemAllocator`. Replacing the constructor call
  without a vtable to install would leave the game with an uninitialised
  allocator.
* **foreign-free fallback** — a pointer that is not in the ownership set is
  handed back to the original allocator when its vtable was captured, instead of
  reading NMS's base-offset dword from a pointer NMS never allocated. The game
  and other ASIs can hold objects that predate the swap. If no original vtable
  was captured, the pointer is left alone rather than freed through the wrong
  heap.

The vtable shape, entry order and calling conventions are the game's; the small
methods (constructor, thunks, ownership query) keep their exact prologues.
