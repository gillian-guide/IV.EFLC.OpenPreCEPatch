# Entry-point and implementation audit

Status: every row was audited; this document is also the work list. **Applied**
rows are listed under "Implementation status" below and in
`docs/improvements-log.md`; the tables keep the original analysis so a rejected
row is not re-litigated.

Part A asks whether the *game* offers a cleaner entry point at the exact spot an
option patches today. Part B asks whether the mod's own mechanism at that spot
could be simpler or cheaper. A row marked `safe` would leave the write set
unchanged but still needs a matrix cell; `behaviour change` needs an opt-in;
`fidelity risk` must not ship without a live A/B.

## Implementation status

| row | status | where |
| --- | --- | --- |
| PopupHeadlightsFlag EAX/EDX twins | **fixed**: if/else like the original handler 0x10044590 | commit "fix: install only one PopupHeadlightsFlag body" |
| LoadDLCs duplicate same-site writes | **fixed**: each site written once (recovered rows list one) | commit "refactor: drop LoadDLCs' duplicate same-site writes" |
| SCOSignatureFix `-32` anchor | **fixed**: signature now ends on the branch (`89 3D ? ? ? ? 85 ED 74 1E`, unique), site = match+8 | commit "fix: anchor SCOSignatureFix on the branch it flips" |
| NewMemorySystem registry (Part B) | **done**: flat open-addressing table over VirtualAlloc, no per-entry allocation, no throw; `zpatch_nms_registry_selftest` | commit "perf: replace NewMemorySystem's allocation registry with a flat table" |
| GarageModelFix site A → prologue | **implemented, opt-in** `OPCEP_IMPROVEMENTS=only:garage_entry`: site moves 0x868BB0 → 0x868B90, 2 sites / 10 bytes unchanged; `xmm0` reloaded by the body | this document + `docs/testing.md` |
| GarageModelFix site B → prologue | **rejected**: 0x9659C3 is inside a 2-iteration loop with per-iteration ECX; a prologue hook would change lookup ordering (see `.scratch/w3_garage.md`) | analysis |
| CutsceneFixes → single callee hook | **implemented, opt-in** `OPCEP_IMPROVEMENTS=only:cutscene_entry`: one 5-byte hook at 0x89A640 replaces the 3 sites (9 → 5 bytes); the callee is relocated to a private copy with its 13 calls re-pointed and the branch fix baked in. Any shape mismatch falls back to the 3 original sites | `docs/testing.md` |
| everything else | pending | — |

Line numbers were taken while the failsafe work was in progress; the mechanisms
are unchanged but a few `hooks.cpp` line numbers may have shifted by a few lines.

Audit only: no source edits, no builds, no game launch. Game-side rows were checked in the
annotated 1.0.8.0 IDB (`IDA Files\GTAIV 1.0.8.0_Human.i64`, image base 0x400000, session 823177f2).
**VA = IDB address; RVA = writeset value in `tests/parity/writesets/`** (RVA + 0x400000 = VA).
`[IDA]` = verified in the IDB; `[inferred]` = repository evidence only. Transition targets in
writesets are the dump's own module pointers, so only the site and the overwritten instruction
are used as evidence, never the rel32 value.

Part A covers the options the brief lists; options with **no 1.0.8.0 site** are stated and not
given invented rows. Verdicts: **safe** = same effect, fewer bytes/sites; **behaviour change** =
needs an opt-in because the write set or a guard changes; **fidelity risk** = do not ship.

## Part A — cleaner game-side entry point at the patched spot

| option | current site(s) VA (RVA) + effect | enclosing function | cleaner entry point | why | risk | evidence | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| CutsceneFixes | 0xA23628 (write set 0x623629) rewrites `call sub_C9A640` → cutscene body; 0xC9A840 (0x89A840) `jz`→`jmp`; 0xC9AB1B (0x89AB1C) rewrites the second `call sub_C9A640` | 0xA23510 (0x133), 0xC9A640 (0x2d2), 0xC9A9F0 (0x24d) | 0xC9A640 function entry (prologue of the shared callee); keep the 0xC9A840 branch fix inside the body | 3 patched sites → 1; the callee has exactly 2 callers and both are the patched ones; no call-site relocation | body must reproduce 0x2d2 bytes of callee; a future third caller would inherit the fix | `[IDA]` xrefs to 0xC9A640 = 0xA23628, 0xC9AB1B only; writeset 3 sites | safe (same effect, fewer sites) |
| OutOfCommissionFix | 0xA22E97 (0x622E97) `cmp [esp+..], eax` after the finale stream call; 0xB72734 (0x372734) `mov eax,[edx+8]` 4 bytes into a 0x61-byte list helper | 0xA22DF0 (0x277), 0xB72730 (0x61) | site 2 → 0xB72730 entry (currently +4 in a 0x61-byte helper); site 1 has no narrower point (the cmp *is* the decision) | site 2 removes a mid-body 4-byte offset; site 1 is already a branch on the archive pointer | site 1 body owns ECX/EDX; prologue relocation changes the pre-image bytes the matrix checks | `[IDA]` disasm 0xA22E90-0x0xEA7, 0xB72730-0x54; writeset 2 sites | fidelity risk (do not ship) |
| VSyncFix | none on 1.0.8.0 (gated 1004/1060/1061/1070/1100/1110/1120/12043); writes are pointer operands at match+30/+13 | — | not applicable to this IDB | no site to improve | — | `source/input.ixx:207-228`, empty `option_VSyncFix` writeset | not installed on 1.0.8.0 |
| FastLoading | 0xCB1D22 (0x8B1D22) `push 0Ah`→`push 0` in a worker wait loop | 0xCB1CB0 (0x94), loop waits on 8 handles, `Sleep(10)` per miss | none — the 1-byte constant is the cleanest possible patch | one byte, no code overwrite, exactly the deciding value | none beyond the original's own spin | `[IDA]` decompile 0xCB1CB0; writeset 1 byte | safe — already optimal |
| StartupTimeFix | 0x477A48, 0x8973D9, 0x898859 (0x77A48/0x973D9/0x98859): 4-byte `push offset "*/*"` operand → address of `g_startup_time` in three WinINet request builders | 0x477960 (0x1c6), 0x8972B0 (0x207), 0x898760 (0x244) | no shared helper: three separately inlined `HttpOpenRequestA` blocks; a single IAT/flag patch is not equivalent | — | changing another operand/flag would change the HTTP request itself | `[IDA]` disasm at 0x477A42/0x8973D8/0x898858; `rebased.cpp:331-333` | fidelity risk (do not ship) |
| BuildingAlphaFix | site 1: 0x9594C4 (0x5594C4) 5-byte `E9` over a 6-byte `jz rel32`; site 2: 0x959CD3 (0x559CD3) 13-byte NOP over `test [esi+24h],40000000h` + `jnz rel32` | 0x9593E0 (0x72e), 0x959B10 (0x4d4) | site 1: write the `test` immediate (4 bytes at 0x9594C0) instead of the opcode; site 2: NOP only the `jnz` (6 bytes), keep the `test` | site 1 avoids rewriting an opcode mid-stream; site 2 halves the NOP run | site 1 leaves ZF=1 (same jump) but different SF/PF; site 2 restores/keeps flags from the test, the 13-byte NOP was presumably chosen to preserve pre-test flags | `[IDA]` disasm 0x9594BD-0x0x4CA and 0x959CD3-0x0xE0 | behaviour change (needs opt-in) |
| CarDynamicShadowFix | 0x40C9F9 (0xC9F9) JMP 9 bytes into a 0x58-byte updater; 0x8F719F (0x4F719F) `jz rel32`→`E9`; 0x95A41E (0x55A41E) `jnz`→`EB`; 0x95ADCF (0x55ADCF) JMP over a `push...call sub_959FF0`; 0x8F73EC/0x8F7422 (0x4F73EC/0x4F7422) push immediates 0x504/0x104→0x100 | 0x40C9F0 (0x58), 0x8F7080 (0x20d), 0x959FF0 (0x5c5), 0x95AC40 (0x3b6), 0x8F7290 (0x1a7) | 0x40C9F0 function entry (JMP replaces the whole 0x58-byte updater) instead of +9; the branch/immediate sites are already minimal | function-entry hook is stable under prologue shift; +9 depends on `mov edi, dword_1745F24` staying put | body must run push/push/push/mov edi then resume; 13 callers of 0x40C9F0 | `[IDA]` decompile 0x40C9F0 (13 callers); xrefs to 0x959FF0 = 11, all in 0x95AC40 | safe (same effect, fewer bytes at site 1) |
| EmissiveLerpFix | 0x7F8BA1 (0x3F8BA1) 5-byte JMP starting at `cvtsi2ss xmm0,edx` (4 bytes) and eating the `F3` prefix of the next `mulss` | 0x7F8AA0 (0x175) | move the JMP to the next instruction boundary (0x7F8BA5) or the function entry 0x7F8AA0, adjusting the body's entry contract | current site splits an instruction; boundary hook is provably safe to patch | body currently relies on the register state at 0x7F8BA1 (EDX/ECX loaded by preceding code) | `[IDA]` bytes 0x7F8B9C-0x0xBA7; writeset 1 site | safe (same effect, fewer bytes) |
| NoImpostorHeightCutoff | 0xCA058D and 0xCA118E (0x8A058D/0x8A118E): 2-byte NOP over `jbe` after `comiss xmm0,xmm1` (the height clamp) | 0xCA03B0 (0xbe6), 0xCA0FA0 (0xb9c) | patch the source height constants (`ds:dword_D9D640`, `ds:dword_DEF57C` used by all three inlined copies) instead of NOPs | data value instead of code; one or two writes covers 2-3 sites | constants are shared with other transforms; changing them is a behaviour change, not a NOP equivalent | `[IDA]` disasm 0xCA0582-0x0x59A and 0xCA1183-0x0x19B; same sequence also at 0xC9FA70 | behaviour change (needs opt-in) |
| NoLODLightHeightCutoff | 0xC9FA70 (0x89FA70): 2-byte NOP over `jbe` (same clamp, default 0) | 0xC9F880 (0x8fe) | same data-constant option as NoImpostor; alternatively the single NOP is already minimal | one 2-byte NOP is already the smallest code patch | data constant shared; option default off | `[IDA]` disasm 0xC9FA6D-0x0x7A; `graphics.ixx:691` | safe — already optimal as code |
| GarageModelFix | 0x9659C3 (0x5659C3) JMP over `movzx/push/push` before `_hasResourceLoaded` in the garage resource-request loop; 0xC68BB0 (0x868BB0) JMP mid-way through the model-info copier (second of three `mov reg,[edi+20h]`) | 0x9659A0 (0x97, 1 caller 0x967810), 0xC68B90 (0x1ac, 1 caller 0x9673C0) | hook both function prologues 0x9659A0 and 0xC68B90; each has exactly one caller, so an entry hook is equivalent to the two mid-body hooks | removes 2 mid-body overwrites; function entry is the stable anchor | store body currently runs with ESI/EDI already set; prologue version must reproduce them | `[IDA]` decompiles 0x9659A0 / 0xC68B90; xref counts = 1 each; writeset 2 sites | safe (same effect, fewer sites) |
| PopupHeadlightsFlag | 0xA53A64 (0x653A64) JMP over `movsx eax,[esi+2Eh]`+`cmp eax,[model]`; 0xA53A6E `jnz`→`jmp`; 0xA53AB6 4-byte `fld` operand → `zpatch_popup_angle`; 0xA46546 (0x646546) JMP at `mov ecx,7` (the pattern's match−10 lands in the preceding "noboot" compare, not on the model test) | 0xA536D0 (0x411), 0xA464D0 `CVehicleModelInfo::setVehicleModelOpts` (0x433) | for the IDE site, hook the actual `or dword [eax+94h],4` at 0xA46551 (or 0xA464D0 entry) instead of clobbering `mov ecx,7` 10 bytes earlier; the other three sites are already branch/operand-minimal | current JMP destroys an ECX setup belonging to a *different* string compare; the two instructions are only linked by the raw pattern | body/register contract was written for the 0xA46546 entry; moving it needs a new body and a matrix cell | `[IDA]` bytes 0xA46546 (B9 07.., F3 A6, 75 0C, OR at 0xA46551) and 0xA53A64-0x0xB6; `vehicles.ixx:753-776` | fidelity risk (do not ship) |
| ForceCarHeadlightShadows | 0x8F7590 function entry JMP to shim (0x8b0-byte function, 1 caller); 0x8F7C61/0x8F7CBA/0x8F7DD6 branch flips; 0x8F7D2E 4-byte `movss xmm2,ds:` operand → `zpatch_fchs_offset` | 0x8F7590 (0x8b0) | main hook is already at function entry; the 3 branch flips could collapse into the one global the function already reads (`byte_F3F5A2` at 0x8F75B8) but that is a shared render flag | fewer sites only if a shared flag is used | `byte_F3F5A2` is read by other render paths; the 2.0f operand patch is the intended data hook | `[IDA]` disasm 0x8F7590-0x0x5BE, 0x8F7C5B-0x0x61, 0x8F7D2E; writeset 5 sites; 1 caller 0x8FB0B0 | safe — main site already at function entry |
| PlaneCrashFix | 0x9E0D0B (0x5E0D0B) JMP over `mov [eax+26h], bl` after `call sub_960BE0`; EAX may be null | 0x9E09C0 (0x369), reached **only** through vtable slot 0xD9FDEC (no direct xrefs) | replace the vtable slot at 0xD9FDEC with a wrapper that calls the original and null-checks the `sub_960BE0` result, instead of patching mid-body | the function is a virtual; a vtable-slot patch survives body edits and moves the guard to the function boundary | vtable shared by every object of the class; wrapper must preserve `retn 14h` and the ECX `this` contract | `[IDA]` xrefs to 0x9E09C0 = data 0xD9FDEC only; site disasm 0x9E0D01-0x0x0E; `stability.ixx:700-705` | behaviour change (needs opt-in) |
| MovingPlaneParts | 0xA538A0 (0x6538A0) JMP at `mov edx,[esi]` immediately before `call [edx+128h]`; plus the 0xA46546 IDE site (shared with Popup) | 0xA536D0 (0x411) | hook the callee's vtable slot (`[edx+128h]`) or the 0xA536D0 prologue; today the JMP replaces the vtable-load sequence itself | no mid-body overwrite of a virtual-call setup | the body depends on ESI/EDX at 0xA538A0; the vtable is shared | `[IDA]` disasm 0xA538A0-0x0xAA; writeset 2 sites (same as Shaking) | fidelity risk (do not ship) |
| ShakingEngineBlock | same two sites as MovingPlaneParts (0xA46546 + 0xA538A0) | 0xA464D0 / 0xA536D0 | same as above | — | — | `[IDA]`; `vehicles.ixx:817` | fidelity risk (do not ship) |
| MovingSteeringWheelSupport | same two sites as MovingPlaneParts + steering detours; writeset identical to Shaking | 0xA464D0 / 0xA536D0 / steering sites | same as above | — | — | `[IDA]`; `vehicles.ixx:571` | fidelity risk (do not ship) |
| NewSavesCompatibility | 0x85B093 (0x45B093) JMP at the /GS cookie store, i.e. the prologue of the save-load function (after push ebp/mov ebp/and esp/sub esp/mov eax,xor) | 0x85B080 (0x3f0) | already the earliest safe whole-function point; nothing cleaner | — | — | `[IDA]` disasm 0x85B080-0x0x93; writeset 1 site | safe — already optimal |
| RemoveModChecks | **no 1.0.8.0 writes**: modded-check patches are 1060/1070 only (`compatibility.ixx:117-149`); GFWL stub is pre-6 only | — | not applicable to this IDB | — | — | `source/compatibility.ixx:96-150`, empty writeset | not installed on 1.0.8.0 |
| RemoveGFWLUpdatePopup | **no 1.0.8.0 writes**: `PrePatch6` gate only (`compatibility.ixx:81`) | — | not applicable to this IDB | — | — | same; empty writeset | not installed on 1.0.8.0 |
| RemoveRGSCLoginRequirement | 0x898460 (0x498460) whole-function replacement of a 0x22-byte getter that does `strcpy(out,"sessionid"); return 1` | 0x898460, 4 callers (0x483630, 0x48F870, 0x6CB3B0, 0xB0D2A0 `NETWORK_IS_ROCKSTART_SESSION_ID_VALID`) | function-entry hook is already ideal; the alternative is patching 4 caller checks (worse) | — | — | `[IDA]` decompile 0x898460; writeset 1 site | safe — already optimal |
| LoadDLCs | 5 code patches: 0x88F558 call→dummy; 0x89035C `call sub_AEA6C0`→jump 0x8903B8; 0x8903D4→struct-fixup body; 0x89040E branch→jump 0x89042C; 0x89043A call→call 0x88FBB0 + 12 NOPs | 0x88F440 (0x19b), 0x890320 (0xd0), 0x8903F0 (0x61) | hook 0x890320 entry (the XNotify/DLC init) and 0x8903F0 entry, one body each, instead of 5 mid-body sites | two function-entry hooks cover the two small functions | reimplements DLC listener registration (XNotify + callback structs); sequence-sensitive | `[IDA]` decompile 0x890320, disasm 0x8903F0-0x0x4E; `loading.ixx:98-108` | fidelity risk (do not ship) |
| NewMemorySystem | 0x40107A (0x107B) rewrites `call sysMemSimpleAllocator::ctor`→`abi_nms_apply`; 0x40107F (0x1080) JMP to 0x40108C skips `atexit(sub_D65C50)` | 0x401060 `initialize_memoryAllocators` (0x16e), 1 caller 0xD56060 (0x23) | hook the static-init thunk 0xD56060 at entry, let the original ctor + atexit run, then write the vtable to `dword_19CF9F0` (0x19CF9F0, seen at 0x454818) | 2 patches → 1; the game ctor and atexit are preserved; vtable write is one dword | allocator object must be constructed before the first alloc; hooking CRT init is order-sensitive | `[IDA]` decompile 0x401060 (constants 0x19cf9f0), disasm 0xD56060-0x82, 0x454818; `performance.ixx:225-231` | behaviour change (needs opt-in) |
| IncreaseVehicleModelLimit | data write at 0xF3E8E8 (0xB3E8E8): 0x400 into the model table's capacity/pointer | data (no function) | none — a data write is already the cleanest form | — | — | writeset 2 bytes; `performance.ixx:158-163` | safe — already optimal |
| IncreaseVehicleStructLimit | 0xA4A2B1 (0x64A2B1): 1-byte immediate `push 32h`→`push 64h` in `CVehicleModelInfo::initClass` (0xA4A220) | 0xA4A220 (0xaa) | none — immediate-only change | — | — | `[IDA]` bytes 0xA4A2AB-0x0xB3; `performance.ixx:181-186` | safe — already optimal |
| SCOSignatureFix | 0x626DA0 (0x226DA0): `jz short`→`jmp` (1 byte) 32 bytes before the SCM signature scan | 0x626C90 (0x18f) | patch the compared value/signature instead of the branch; but 1 byte at an instruction boundary is already minimal | — | — | `[IDA]` bytes 0x626D9C-0x0xDA6; `loading.ixx:264-269` | safe — already optimal |

Not analysed per-row (outside the brief's list): BuildingDynamicShadows, CarPartsShadow, DualVehicleHeadlights,
NoLiveryLimit, CopSirensFlag, etc. They use the same two mechanisms (pattern match base or match+N)
already visible in the rows above.

## Part B — implementation-architecture options in the mod (only where code + benefit exist)

| area | current mechanism (file:line) | proposed alternative | risk | expected win | evidence | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| pattern scanning | 243 `Pattern()` calls per pass, 171 unique signatures; every call normalises the literal into a `std::string` and rebuilds a Hooking.Patterns object (`helpers.hxx:30-39`, `pattern.cpp:110-114,170-181`) | one Aho-Corasick/multi-pattern pass over `.text` that records the **first** hit per signature, invalidated for any signature overlapping a byte written by an earlier arm | any missed invalidation changes an install; the matrix only proves the tested INIs | 2.6 GB scanned → 9.9 MB once; measured pass 1.87 s (`docs/improvements.md:64`) could fall toward ~0.1 s | scans counted with a regex over `source/*.ixx`; uniqueness 171/243; measurement in `docs/improvements.md:64` | fidelity risk (do not ship) — needs write-aware invalidation proof |
| duplicate scans | publish and install arms rescan the same literals, e.g. garage `stability.ixx:404` vs `:430` and `:409` vs `:437`; popup `vehicles.ixx:720` vs `:753`, `:726` vs `:762`, `:731` vs `:769` | reuse the publish arm's match base from a global (publish already stores resume offsets) | **the original rescans at install time**; an earlier arm may have written the bytes, so reuse can install where the original skips | up to 72 extra scans when every option is enabled (fewer on defaults), ≈ up to ~0.5 s at ~7 ms/scan | duplicate list (32 signatures ×2, `0C 83 88 94…` ×10, two steering patterns ×6); `docs/improvements.md:71` already rejects a plain cache | fidelity risk (do not ship) |
| allocator registry | `std::unordered_set<uint32_t>` under a `CRITICAL_SECTION`: a CRT node allocation per game allocation and per free, `insert` may throw inside `noexcept` (`hooks.cpp:5092-5143`) | fixed-capacity open-addressing table (or a sorted `std::vector` with linear insert) behind the same lock; no allocation, no exceptions | only membership is observable, so semantics are preserved; table must grow without rehashing mid-game if the original had no bound | removes one malloc/free per allocation and one pointer chase per `contains`; also removes a terminate path | `hooks.cpp:5093,5118,5132,5140`; comment at 5088-5091 | safe (no write-set change) |
| garage lookup | up to 2 game model lookups per call and no memo (`hooks.cpp:728-753`); the model table is static | one-entry hash→index memo (same pattern as Popup) or the existing `ModelNameFromHash` index | lookup result depends only on the static table, so a memo cannot go stale | saves the second lookup on the fallback path; garage ops are rare | `hooks.cpp:739-747` | safe (no write-set change) |
| popup / model lookup | 512-slot direct-mapped hash cache already exists (`hooks.cpp:118-138`, `185-203`); the angle is re-found by scanning all 512 hashes on every hit (`hooks.cpp:153-162`) | precompute hash→angle once (the table is static) or break at the last match while scanning backwards; merge the two copied resolve lambdas | the original takes the **last** matching angle; a forward map must preserve that | removes a 512-iteration scan per popup check (called per vehicle) | `hooks.cpp:110-165`; `docs/improvements.md:28` | safe (no write-set change) |
| path handling | `dlc_discovery_post`: unchecked `new` of 0x104, ignored `GetModuleFileNameA`, `strcat_s` failure leaves the module path, 0x104 truncation (`hooks.cpp:3894-3953`) | `std::filesystem::path`/`std::wstring` + `GetModuleFileNameW` with the size returned, and `PathCchCombine`; heap buffer sized from the module path | paths >255 chars currently register the wrong content dir; a fix changes that (a bug fix, but observable) | no truncation/UB, one allocation instead of `new`+`strcpy_s`+`strcat_s` per candidate; called once per DLC discovery | `docs/improvements.md:95-98` documents the bug; `hooks.cpp:3896-3919` | safe for valid paths (behaviour fix above 255 chars) |
| config parsing | `Config::RawOption` scans 15 section names and builds `std::string`s per option call, then `std::stoi` throws (`config.cpp:39-51`) | parse the INI once into a flat `unordered_map<string_view,bool>` at load; non-throwing `strtol`; keep the throwing path behind `OPCEP_PARITY_BASELINE` | `std::stoi` throwing on `Key = true` is the original behaviour; dropping it diverges | hundreds of Option calls × up to 15 IniReader lookups removed; no exception path; startup-only, unmeasured | `docs/improvements.md:87-90` deliberately preserves the throw; `config.cpp:50` | behaviour change (needs opt-in) |
| ABI lookup | `AbiByName` linear `strcmp` over ~128 names, called 125× at install (all startup) (`hooks.cpp:5323-5468`; `helpers.hxx:145`) | constexpr FNV-1a table sorted by hash + binary search (or template `Abi<"name">()` binding the address at compile time) | none — same strings, same addresses | ~16k strcmp → ~125×7 hash comparisons; well under 1 ms of the 1.87 s pass | 125 `Abi("…")` call sites counted across `source/*.ixx`; table at `hooks.cpp:5329-5458` | safe (no write-set change), low value |

Checked and already fine (no change proposed): the OOC archive cache (`hooks.cpp:4670-4707`, one-time
decode/build with the archive retained by design) and the sorted model-name index
(`catalogue.cpp:31-61,76-113`).

## Top 5 by value/risk (across both parts)

1. **NewMemorySystem: hook the 0xD56060 init thunk once** instead of rewriting the ctor call + `atexit` skip
   (2 sites → 1, original ctor kept). One function entry, one dword vtable write; the only real variable is
   CRT-init ordering. `[IDA]`
2. **CutsceneFixes: one hook at `sub_C9A640` (0xC9A640)** replaces two call-site rewrites; its only two
   callers are the ones already patched. Largest site-count reduction found (3 → 1). `[IDA]`
3. **NMS registry: flat open-addressing set** (`hooks.cpp:5093`) — removes a heap allocation on every game
   allocation/free and a terminate-on-OOM path, with no observable behaviour change. Pure win, no game-side risk.
4. **GarageModelFix: move both hooks to the prologues 0x9659A0 / 0xC68B90** (each function has exactly one
   caller, so the mid-body `movzx`/`mov` anchors buy nothing). Low risk, 2 fewer fragile offsets. `[IDA]`
5. **BuildingAlphaFix site 2: NOP only the `jnz` (6 bytes) instead of `test`+`jnz` (13 bytes)** — unless the
   flags from before the `test` are live, this halves the patch; verify with a disassembly of the branch target
   before shipping. `[IDA]`

**Verification note.** Every VA, enclosing function, instruction and xref count in Part A marked `[IDA]` was
read from session 823177f2. The "cleaner entry" arguments are design inferences from that disassembly, not
runtime-tested; the write-set delta each would cause is not in `tests/parity`. Part B rows are code-level facts
from the cited lines; the 1.87 s / 264-scan figure comes from `docs/improvements.md:64` and was not re-measured.

## Late findings (second pass, same day)

A narrower second pass over 14 of the options confirmed the rows above and added
four points:

- **PopupHeadlightsFlag is installed twice.** The port installs the EAX and EDX
  twins with two independent `if`s, while the original handler is `if`/`else`:
  the two arms share the resume slots, so on a view where both patterns match the
  second install can clobber the first resume. This is a correctness risk in the
  arm, not just an entry-point preference.
- **SCOSignatureFix's `-32` site is only verified on 1.0.8.0.** The byte is at an
  instruction boundary there, `patch_operations.json` records the offset as 0,
  and the `-32` anchor is unverified on the other builds. Extending the signature
  so it ends at the branch would remove the fixed offset from the arm.
- **LoadDLCs writes the same sites twice.** `loading.ixx` issues the dummy-call
  and struct-fixup installs twice per build block (`0x4137CD` / `0x4144D4` on
  1060, `0x41356D` / `0x414274` on 1070, `0x48F558` / `0x4903D4` on 1080). The
  second write is idempotent today, so the matrix cannot see it, but it is dead
  work and a trap for anyone editing the arm.
- **StartupTimeFix's multi-match loop is load-bearing.** It patches three
  separately inlined WinINet request builders; narrowing it to one match would
  silently drop two. Keep `PatternAll` there.

The wider first pass (24 options, including the twelve outside the second pass's
brief) is what the tables above are built from; nothing from it was dropped.
