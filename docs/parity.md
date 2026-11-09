# Parity

What has to match the original ZolikaPatch 7.65 ASI, and the faults reproduced on
purpose. A guard that turns one of these into a non-fault is a fidelity bug, not a
fix.

## Contracts pinned by tests

| contract | evidence |
| --- | --- |
| INI grammar (feather-ini-parser 1.41): 255-byte line buffer (a 256th byte sets a sticky `failbit` and the parse then spins -- see the faults table), no live BOM branch (a first line starting with `0xEF` is a malformed line, so a BOM-prefixed `[Options]` header is lost -- see the faults table), `#`/`//` comments only, strtok-on-`=` keys that keep a trailing space, values that keep their spaces, empty values not stored, last duplicate wins, a re-opened section starts empty, case-sensitive sections and keys | `zp_tests`, `TestConfigGrammar`; the 255-byte boundary and the spin: `zp_fault_parity` |
| Read boundary is `std::stoi` on the stored text with the arm's default | `zp_tests`, `TestConfigGrammar` |
| One key's stored flag is the parse's *answer*, not `== 1`: `AddOptionsInMenu` is enabled by `2` (and by `-1`), disabled by `0`, keeps the absent default `1` and the catalogue's build mask, while an ordinary key still requires exactly `1`; the old `== 1` rule is asserted to answer false for `2`, which is the divergence it replaced | `zp_tests`, `TestOptionGates` (9 checks; the falsification run is quoted in the key's section) |
| The INI faults, by behaviour: `std::invalid_argument` of exactly that type on `Key=abc` and on a spaces-only `Key= `, nothing on an empty `Key=` (which reads the arm default); `std::out_of_range` of exactly that type past `int`, with `INT_MAX`/`INT_MIN` still read; a key-before-section store killing a child process with `0xC0000005` after its diagnostic line, while empty values, lines without `=` and lines starting with `=` never reach the store; a first-line BOM leaving the `[Options]` header unparsed so the key line under it dies in the same null store (a child process); and the line-buffer boundary -- a 255-byte line parses and its child exits 0, a 256-byte line leaves the parse spinning, so its child must still be running after the test's 5 s timeout and the test terminates it | `zp_fault_parity` (29 checks; the two null stores and both line-buffer cases are fresh processes) |
| The two light tables: `Car<N>Name` / `Car<N>Angle` / `Car<N>Offset` with `N` = 1..512, the section gate, an empty name skipping the row, the hash stored before the value, the degrees-to-radians conversion, and the reader running before any option is read | `zp_model_tables` |
| Build classification: the four version fields concatenated (`1.0.7.0` -> 1070, `1.2.0.43` -> 12043) and the original's switch; an unsupported executable is `unknown` and applies nothing | `zp_tests`, `TestBuilds` |
| Option catalogue: 99 keys with the shipped value, the absent-key default and the build mask | `source/options.inc` (generated), `zp_tests` |
| The loader preamble's ten salt words: the shipped `.data` values when its probe hits, the recovered fallback set when it misses | `zp_salt_probe` (1.0.8.0 hit; 1.0.0.0 and 1.0.4.0 miss) |
| The two guarded native entries (`sub_100038E0` / `sub_10003980`): the invoke calls the handler and answers `1` (the handler's own return value is ignored), the resolve answers the handler address without calling it; a fault inside either guarded region (the handler call or the resolver call) answers `0` and the process keeps working; a resolver that answers null is the invoke's skip (`mov al,1`) while the resolve answers null; a null resolver (`dword_1015BDE0` = 0) is caught as `0`, and the *caller's* read through that zero faults | `zp_native_guard` (27 checks; the caller fault is a child process that must exit `0xC0000005` with the report saying `RESOLVED=0`) |
| With no arm installed the pipeline changes nothing on any supported image | `zp_dump_empty` (9 images) |
| The save converter's walk (`sub_10051B30`): `SAVE` at `+0xC`, eighteen 9-byte chunk headers in file order, the three size tests (`4C45`/`1741`/`0E77`) and the three patch values they select (`74`/`EB`, `0CBE`/`0E6E`, `75 26`/`90 90`), on both real saves and on synthetic new/old layouts; a chunk size below nine throws `std::bad_alloc`; a header shorter than `0x110` is not rejected on its header when the file itself carries the magic, and a file shorter than the magic offset is decided by a fresh process's stack | `zp_save_walk` (258 checks; every expected value is recomputed from the file by the test's own parser, not by the converter) |
| The `NewMemorySystem` arena (`0x10038360` allocate, `0x10038430` free, `0x100385A0` membership): `unit = max(0x10, align)`, `want = max(size, unit)`, a `want + 2*unit` request, the `((base-1)/unit + 2)*unit` rounding whose distance at `[pointer-4]` is always in `[unit, 2*unit)`, the `0xCD` fill over exactly `want` bytes and not past them, membership for a live pointer but not a freed one, not `pointer+1`, not a stranger address, a null free as a no-op, the freed block reused by the next same-size request, and a failed request answering null (the `test edx,edx`) with the arena still usable afterwards | `zp_nms_arena` (246 checks, driven through `MemoryArena::Get`; a double free is deliberately not asserted - see the faults table) |
| The `OutOfCommissionFix` init (`sub_1004B750`): `dword_10159C00` has no writer, so three calls build three archives (one 182,272-byte archive allocation each) and not one is ever freed; after every call `dword_10159C78` is the archive's 182,272 bytes and `dword_10159BFC` is that same count, and the archive the count covers hashes to the pinned `fnv1a 0xB5AF7B10` with its header's `16`/`0xE9` words, one entry, a 179,536-byte record and the name `"finale2.sco"`; a resource whose Adler-32 trailer is damaged (the decode answers 0 with the table complete) and one whose first opcode can never terminate (the decode answers 0 having written nothing) are both registered and built into an archive, with no throw; the fake-pak entry is `"finale2.sco"`, an 11-byte name of capacity 15, `file_size` 179,536 and the table it was handed | `zp_ooc_init` (54 checks, driven through `OocInit`; the init, the decode, the push_back, the vector's clear and the resource are the module's own, and the two damaged-resource runs damage the recovered resource in place) |
| The DFA file-API bodies (`0x10004820` create, `0x10004720` size, `0x10004760` seek, `0x10004740` read, `0x10004790` close): a real file opened through the create body, sized, seeked forward and from the end, read at each position, then closed through the rebuilt body; the value is really closed (`DuplicateHandle` answers `FALSE`/`6`), the transcribed reader answers `FALSE`/`6` for it and that answer equals `ReadFile`'s own, null / already-closed / `0xDEADBEEF` all answer `FALSE`/`6` because the body has no argument guard, the success path leaves last error untouched, and `fs:[0]` is the same before and after every call; the frame's filter is `EXCEPTION_CONTINUE_EXECUTION`, so a fault in the guarded call never answers -- a child process whose callee writes one probe byte on entry and then faults must still be running at the test's watchdog, with the probe byte present and no answer written | `zp_dfa_close` (27 checks, driven through `Launcher::DfaBodiesForTest`; the fault case is a child the test terminates and reaps) |
| The generated catalogue and INI cannot drift from their sources | `zp_catalogue_drift`, `zp_ini_drift` |
| Every signature an arm scans is the original handler's own string: its per-image hit count and first-hit RVA equal that string's, so an arm cannot patch a site the original never touches | `zp_signature_parity` (`tools/check_signature_parity.py`; the class, the falsification and the per-arm record are in the signature-parity section) |
## Preserved faults

| fault | condition | site |
| --- | --- | --- |
| `std::invalid_argument` escapes the loader | a non-numeric `[Options]` value (`Key=abc`, or `Key=` whose value is spaces only, `Key= `) | `Config::Integer` -> `std::stoi`, uncaught; `zp_fault_parity` asserts the exact type |
| `std::out_of_range` escapes the loader | a numeric value outside `int` | same; `zp_fault_parity` asserts the exact type, with `INT_MAX`/`INT_MIN` still read and one past each throwing |
| `std::invalid_argument` / `std::out_of_range` escape the loader from a light-table value | a `[PopupLightAngles]` `Car<N>Angle` or `[HeadlightConeOffsets]` `Car<N>Offset` with no digits (`abc`, or a space-only value) or out of range (`1e999`), with a non-empty `Car<N>Name` | `Config::ReadModelTables` -> `std::stof`, uncaught; the original's inlined `strtof` and its two throw helpers (`0x100529BC` "invalid stof argument", `0x100529B1` "stof argument out of range"), reached with the row's hash already stored (`0x10052B0C` before `0x10052C14`, `0x1005265E` before `0x10052763`) |
| null section dereference | a key line before the first `[section]` line | `Feather::Parse`, `(*current)[key]` with `current` null; it kills the process, so `zp_fault_parity` runs it in a child and asserts exit `0xC0000005` after the child's diagnostic line (empty values, lines without `=` and lines starting with `=` never reach the store) |
| a BOM-prefixed INI faults; the file is not dropped | the first line starts with the UTF-8 BOM (`EF BB BF`), so the line it prefixes is lost | the oracle has no BOM branch: the 1.41 source's `if (line[0] == 0xEF)` cannot fire (`char` is signed -- `movsx eax, [ebp+esi+Source]` at `0x10059E93` -- so the byte promotes to `-17` and never equals the literal `239`), and `sub_10059D70` carries no `0xEF` compare and no `memmove`. The BOM line reaches the ordinary dispatch, fails `cmp dl, 5Bh ; '['` at `0x10059E5E`, and has no `=`, so `strtok` leaves the second token null (`strtok(&String, "=")` at `0x1005A11C`) and the line is dropped whole -- including the `[Options]` header it carried. The loader zeroes the 0x30-byte INI object first (`memset(&obj, 0, 0x30)` at `0x10052419`), so `current` (`+0x20`) stays null and the next key line dies on `mov esi, [eax+20h]` / `mov ecx, [esi]` at `0x1005A2DA`-`0x1005A2E3`. `zp_fault_parity` runs it in a child that first records the header being swallowed *without* faulting, then asserts exit `0xC0000005` after `PHASE=parse-start` |
| the parse never terminates on a line at or over 256 bytes | any line with a 256th byte before its newline | `sub_10059D70` reads lines with `getline(&String, 0x100)` at `0x10059E0E`. Measured against MSVCP140 (`14.51.36247.0`; the call goes through the import at `0x100631C0`, so this is the DLL's `basic_istream<char>::getline`, not image code): 255 bytes is the longest line that is stored and completed, a 256th byte stores 255 and sets `failbit` while `eofbit` stays clear, and the loop's only exit is the `eofbit` test (`test byte ptr [eax+ecx+0Ch], 1` / `jz 0x10059E00` at `0x1005A53E`). Nothing clears `failbit`: the body's `out.clear()`/`out.str("")` clear the *local stringstream*, never the file stream. So every later `getline` is a sentry no-op that leaves the buffer untouched and the loop re-parses the same 255 bytes for ever (a key-shaped line re-stores the same value, a section-shaped one re-creates its key map). `zp_fault_parity` pins both sides in child processes: 255 exits `0` after reading the values, 256 is still spinning when the test's 5 s wait expires and is then terminated |
| the NewFiles texture helpers fault on a scan miss | the original's one-time scans (`0x10051730`, `0x10051610`, `0x100517E0`) match nothing | `NewfilesObject` / `NewfilesCallObject` / `NewfilesCallAdd` read or call through the null the scan left, as the original's unguarded `mov eax,[esi]` with `ESI = 0` (`0x100517CE`), its `call edx` (`0x1005170F`) and its `call esi` (`0x1005187B`) do. None of the nine cells misses the object string; 1.2.0.43 has no image here, and 1.2.0.59 (not a gated build) misses it because the code has a different instruction order (`rebuilt.md` §2m) |
| the DLC register call's `this` is undefined | every `LoadDLCs` run | the original calls the register entry (image base + the stored register RVA) with `ECX` left over from the preceding `strcpy_s`: `mov ecx,dword_1015AD70 / mov esi,dword_1015804C / … / lea esi,[esi+ecx] / call esi` (`0x10023220`-`0x10023276`). The callee uses it only through the loader-callback call this key replaces with `0x10023480` and through a branch its non-zero flag argument skips, so the value is never dereferenced. The transcription (`dlc_discovery`) leaves `ECX` exactly where the original leaves it -- no `this` is invented and no register is set up |
| the steer consumer faults on a preamble miss | a build whose model-info array, bone getter or setter scan matches nothing leaves `dword_1015AD78` / `dword_10159B88` / `dword_1015D63C` / `dword_10159CDC` zero | `sub_10004C70` reads them unguarded (`mov eax,[eax+ecx*4]`, `call edx`, `call dword_1015D63C`); the transcription is the original's instruction stream, so a miss faults at the same instruction. 1.0.8.0 has all four patterns |
| the headlight-shadow check faults on a null lookup or object | a build whose `dword_10159C0C` scan matched nothing, or a call with a null object | `sub_1001AD20`'s `call dword_10159C0C` (`0x1001AD5C`) and `movsx eax, word [edi+2Eh]` (`0x1001AD62`) are both unguarded in the original; the rebuild calls first and loads second, so it faults on the same instruction of either condition (the key's section below records the rebuild) |
| the cutscene chain faults on an unresolved native or a missed camera site | the game's native resolver answers null (`dword_1015BDE0` unresolved, or the resolver faults and the layer answers zero), or chain B's first scan matched nothing so `dword_1015BDE4` / `dword_1015BDF0` are still zero | `sub_100388C0` reads the resolver's answer unguarded (`mov ecx,[eax+0Eh]`, `0x100388EE`) and writes through the published poke (`dec esi` / `mov byte [esi],1`, `0x10038913`-`0x10038935`); the transcription keeps every hop and both stores unguarded, so it faults at the same instruction on the same condition. All nine stored baselines record both chain B installs, so every supported image matches both patterns; the resolver scan is the native layer's, measured on 1.0.8.0 |
| the payload init re-runs on every call and leaks its archive | every call of the wrapper the second `E8` installs | the original's one-shot guard `dword_10159C00` is read at `0x1004B77B` and never written anywhere in the image, so `sub_1004B750` decodes, registers and builds again each time, hands the game a fresh archive and never frees the previous one. The rebuild keeps that structure -- no writer, no free -- so the behaviour is the same, and `zp_ooc_init` pins it: three calls answer three distinct archives, the archive allocation counter records one 182,272-byte block per call, no recorded archive is ever freed, and the first is still intact after the other two builds. The guard's skip target is the shared epilogue (`jne 0x1004C333`), and EAX still holds the guard word there, so a guard that was ever set would answer its own value as the archive pointer |
| the decoded payload is registered and mounted even when its own checks fail | a resource whose decode does not end on `05 FA`, does not fill exactly 179,536 bytes, or does not match its Adler-32 trailer | `sub_1004B570` answers 179,536 or 0 and `sub_1004B750` discards the answer (`call sub_1004B570` at `0x1004B798`, no test of `EAX`), so the partially decoded buffer is registered and archived as it stands. The rebuild discards it the same way. Unreachable with the pinned resource, which passes all three checks; `zp_ooc_init` reaches both shapes by damaging the pinned resource in place (the Adler-32 trailer alone: decode 0, table complete, archive byte-identical to the intact run; the first opcode: decode 0, table untouched, archive still 182,272 bytes with the registered entry's header, record and name) and asserts that both still register and answer an archive |
| a corrupt save makes the converter throw or check uninitialised bytes | a save whose 9-byte chunk header carries a size below 9, or a file shorter than its header | `sub_10051B30`'s `add eax,0FFFFFFF7h` (`0x10051C14`) feeds `operator new` (`0x100603F1`) unchecked, so the underflowed payload throws `std::bad_alloc` out of the trampoline; a short header read leaves the `cmp dword ptr [eax],45564153h` (`0x10051BD1`) reading the uninitialised stack buffer. The rebuild keeps the same `operator new` / `fread` / `free` sequence and the same unchecked header read, so the same input faults at the same step, and `zp_save_walk` asserts both: the size-underflow throw by type, in-process, at chunk 0 and at chunk 9; the short-header case as "not rejected on the header" in-process and as a fresh process's answer for a file shorter than the magic offset |
| the load trampoline stores an unspecified EAX in the game's frame | every savegame load | the replayed `mov [esp+1E0h],eax` (`0x100521E7`) stores the prepare step's return value although `sub_10051FD0` is `void`. The transcription keeps the instruction, so the slot receives whatever EAX the compiled prepare leaves; the original's receives whatever its own prepare left. Both are unspecified and the game's own store at the site wrote a defined value, so the original already replaces it with an undefined one - the rebuild keeps that fault, and only the undefined value itself differs |
| a double free is unguarded | the same pointer handed to the arena's free twice | `sub_10038430` has no membership check in front of the free: `0x10038449` tests the incoming pointer for null and nothing else, `0x10038457` recovers the base from `[pointer-4]`, and `0x10038474` calls `HeapFree` on it unconditionally. The rebuild keeps the same order (free, then erase) and the same missing guard. The second free therefore reads `[pointer-4]` from a block the heap owns again and hands `HeapFree` a base derived from the heap's own links rather than from the original's arithmetic; whether that is a no-op, a `STATUS_HEAP_CORRUPTION` termination or silent corruption is the heap's decision, so `zp_nms_arena` asserts only the knowable part (the pointer stops being a member after the first free) and does not run a double free |
| the arena's request size is a 32-bit sum | a request of `2^32 - 2*unit` bytes or more (`0xFFFFFFE0` at the `0x10` floor) | `0x1003838B`'s `lea eax,[edi+esi*2]` adds `want + 2*unit` in 32 bits, so the request wraps to a tiny block while the `memset` at `0x100383C9` still fills `want` bytes - the allocate returns a block it cannot fill and the fill runs off the heap. The rebuild computes `want + unit * 2u` in `uint32_t` too, so the same input wraps the same way; the failure path the test asserts uses `0x7FFFFFF0`, whose sum `0x80000010` does not wrap, and the wrapping case is recorded here rather than run |
| the DFA close-handle frame retries a faulting call for ever | a fault inside the guarded `CloseHandle` call -- an import slot whose target faults; no handle value reaches it | `0x10004790`'s scope table (`0x10153FC0`) names the filter `0x100047EC` (`or eax,-1; ret`, i.e. `EXCEPTION_CONTINUE_EXECUTION`) and the handler `0x100047F0` (the `return TRUE` arm). The filter's answer *resumes* the faulting instruction, so the fault is retried and the handler is never entered; the wrapper has no answer for a faulting call at all. The rebuild compiles the same frame -- byte-identical except the four relocated operands (`rebuilt.md` §2l) -- and `zp_dfa_close` replaces the test exe's `CloseHandle` import slot with a callee that writes one probe byte on entry and then faults; the child must be *still running* at the 1 s watchdog with the probe byte present and no answer written, and the test then terminates and reaps it. Unreachable through the wrapper's own argument: `CloseHandle` validates every handle value and returns `FALSE` |
| the `MiscFixes` first literal is base-dependent, so the original's first write fires on some launches only | any launch whose relocation delta makes the fixup at `0x739731` read `C7 06`; every other launch misses the scan | the original handler `0x10023DD0` scans its own string `C7 06 ? ? ? ? 74 19` (`ZolikaPatch_reconstruction/evidence/handler_table.json`, whose own `game_hits` row records **0 hits** on the unrelocated 1.0.8.0 image). The fixup at `0x739731` (`0x016703C4`) covers the literal's first two bytes, so only a delta whose high word is `0x0560` makes them read `C7 06` -- measured witness `0x05600000`, one hit at RVA `0x739733` -- and the arm then writes `EB` at `0x739739`; at delta `0` or the dump's pinned `0x2FC00000` it has no hit. The transcription keeps the original's string verbatim (`source/misc.ixx:225`), so its *effect* depends on the game's live base exactly as the original's does: on most launches the original writes nothing there. Our dump pins one delta, at which the arm is inert, and that matches the stored baseline (also taken at a zero delta), so the scoreboard is measuring the original's common case. Widening the literal over the fixup bytes would make us write where the original does not -- a divergence dressed as a robustness fix |
| the ZMenuIV version compare can read an uninitialised stack word | a `ZMenuIV.asi` whose `GetZMenuVersion` returns without writing its first out-parameter | `sub_10052300` reserves the slot with `sub esp, 0Ch` (`0x10052303`) and never initialises it; the slot is written only by the library's own `call eax` (`0x10052337`), and `cmp [ebp+var_8], 35DD1h` (`0x1005233C`) then decides between nothing and `MessageBoxA` + `TerminateProcess` (`0x10052345..0x10052362`). With a library that writes neither out-parameter the compare reads whatever the caller's stack held, so the outcome is undefined -- the original's, kept: the transcription is the same instruction stream over the same two unzeroed locals. Unreachable with a library that writes the version it is asked for, and inert in the harness (no library at all: the first `jz` at `0x1005231D`) |

Two of the rows above used to be assumptions, and so did the CRLF note below them.
All three are measurements now, and the two facts that were missing were both in the
oracle's own loader:

- **The long line: the tree reproduces the spin instead of splitting.** The
  measurement is in the row and it confirms the boundary at 255: the fact that was
  missing was `getline`'s own behaviour, which is not in the image at all -- the call
  at `0x10059E0E` goes through the MSVCP140 import (`0x100631C0`), so it belongs to
  the DLL and was measured on this machine (`14.51.36247.0`; the game folder ships no
  `msvcp140.dll`, so the oracle loads the system redist, which is that copy). The loop
  has no other bound -- the tail at `0x1005A53E` tests only `eofbit`, and the body's
  `out.clear()`/`out.str("")` clear the *local stringstream*, never the file stream --
  so the original re-parses the same 255 bytes for ever. `Feather::Parse` now models
  the two flags and spins on the same input, so the shipped ASI's startup hangs
  exactly where the original's does. The shipped INI cannot produce that input (the
  user's `game_refs/ZolikaPatch.ini` has a 192-byte longest line); a hand-edited one
  can, and hanging there is the correct outcome, not a reason to add a guard.
  `zp_fault_parity` pins both sides with child processes (255 exits 0, 256 must still
  be running when the test's 5 s wait expires).
- **The BOM: the oracle faults, and the tree now does too.** The missing fact was
  what `sub_10056130` converts: it is `INI`'s file constructor, and it converts the
  *file name*, not the file. It builds the `wstring_convert<codecvt_utf8_utf16>`
  (vftable at `[ebp-68h]`, `0x100561DB`), converts the `std::string` it was handed and
  stores the result in the object's first member, then opens that name --
  `sub_10059B80` constructs a `std::ifstream` and calls
  `_Fiopen(wchar_t const*, 1, 0x40)` at `0x10059C5B` -- and parses the *stream*
  (`sub_10059D70`). The string is the name the preamble built at `0x10052425`:
  `ZolikaPatch.ini`, fifteen bytes. So the file's bytes are never converted, and after
  the dead branch (see the row) a BOM-prefixed first line is simply malformed: it
  loses its `[section]` header, and the first key line under it stores into the null
  `current` and faults. That is the row under Preserved faults, and `zp_fault_parity`
  shows both halves in a child.
- **CRLF folding is measured, not assumed**: mode `1` at `0x10059C5B` is `ios::in`
  with no `ios::binary` (`0x20`), so the CRT folds `\r\n` before the parser sees it,
  and the tree folds it itself.

## Rebuilt pieces

Not every body is transcribed. A piece is rebuilt in C++ when its original stream
cannot be compared under `verify_bodies.py`'s operand-slot rule -- an exception frame
or a TLS static-init guard -- or when every call it makes lands in another rebuild.
An indexed absolute table access used to be on that list and no longer is: the tree
registers the table (`Bodies::RegisterTable`) and the proof compares the operand's
table *contents* instead of its displacement, so a body that reads a registered table
is transcribed like any other (`rebuilt.md` §1, §2b and §2c; the model-name lookup
`0x10003F00` was the first, the steer consumer `0x10004C70` the largest until the
`FreezeCarFix` spawn probe `0x10037510` (394 instructions) took the row). A table is
nameable whether the map calls it `?name` (a C++ object) or `_name` (an `extern "C"`
array owned by another module), and a table the original keeps in the uninitialised
tail of a section reads as the zeros the loader maps there -- two properties the
registry needed before it could cover any of the 512-slot model tables.
[`rebuilt.md`](rebuilt.md) is the catalogue: recovered VA and size, the exact
instruction pattern that blocks each one, what the rebuild reproduces, how that was
verified, the residual risk, and the order to check them in play. This file stays the
record of *behaviour* and *faults*; the rebuild reasons are not repeated here.

## Status

Measured on 1.0.8.0 with `tools/parity_report.py 9`: `build 9: 98 key(s) closed, 0
open, 1 oracle-verified (baseline incomplete)`. The oracle-verified key is the
`SkipLauncher` baseline gap (see below). On 1.0.0.0 and 1.0.4.0 the same class also
covers `CarDynamicShadowFix`'s baseline gap, and that row names those two builds
only. `NewSavesCompatibility`, the last substantial
feature key, is closed by the converter recorded in its own section below;
`HighQualityReflections` closed with the site-arithmetic resolution recorded below.

- **98 of 99 option keys closed**; the two former no-handler keys
  (`AddOptionsInMenu`, `FixPCIDKickbug`) are now implemented too, so the scoreboard's
  count is backed by arms rather than by two empty cells (their effects are
  conditional on the companion library and on the live registry, so the cells stay
  empty and the count does not move). `AddOptionsInMenu`'s gate is the original's own
  `std::stoi(value) != 0` (`setnz byte_10158038`), not the house `== 1`, so `2`
  enables it (see the key's section), and the `ZMenuIV.asi` version guard the same
  walker calls unconditionally (`sub_10052300`) is transcribed and wired at startup;
- **175 hook bodies transcribed, registered and proven whole** (`zp_body_proof`, a
  linear sweep of each body's exact instruction count); the rest of the original's
  bodies are still to transcribe, and the compiled bodies listed in
  [`rebuilt.md`](rebuilt.md) are rebuilt in C++ instead, with no proof row. That file
  is the catalogue of rebuilt pieces: what blocks each transcription, what the rebuild
  reproduces, how that was verified and what it leaves unproven. The longest is the
  `FreezeCarFix` spawn probe (`0x10037510`, 394 instructions / 1,453 bytes; see the
  section below); before it the steer/engine/plane consumer
  (`0x10004C70`, 320 instructions / 1,343 bytes) held the row, and before that
  `HighQualityReflections`' detour
  (`0x1001F030`, 157 instructions / 858 bytes), `CutsceneFixes` chain B's body
  (`0x100388C0`, 146 instructions / 592 bytes) and the livery counter
  (`0x1003D270`, 37 instructions). The pair at 78 instructions / 309
  bytes each (`DualVehicleHeadlights`) is what exposed the proof tool's old
  256-byte sweep window: it compared 64 of 78 and still reported PASS. The tool
  now sweeps 4096 bytes and fails a row that decodes fewer instructions than its
  budget, so a truncated sweep can never pass again. This pass closed **class A**:
  the IDE parser payload (`0x10003FA0`, 92), the popup-headlights checker
  (`0x10044310`, 125) and the headlight-cone check (`0x1001AD20`, 46) are transcribed,
  which needed one comparator extension (a displacement *inside* a registered table
  resolves to that table plus its offset, for the popup checker's eight interleaved
  columns) and one layout change (`Config::ModelTable` became a view over four
  C-linkage arrays, because the map can only name an object, not a member at an offset
  inside one). `rebuilt.md` §2e/§2f/§2g. The pass before added
  the steer consumer (`0x10004C70`, 320 instructions), the largest class-A piece,
  and both siren checks (`0x10005F70` 191, `0x10006260` 53), the first pieces
  transcribed through a base-plus-displacement table operand; the steer consumer's two
  wheelangle tables and the siren list are registered and the proof compares their
  contents (`rebuilt.md` §2c/§2d). Before that came `BadPlayerModelCrashFix`'s
  model-name lookup (`0x10003F00`, 21 instructions), the first class-A piece
  transcribed: its table is
  registered and the proof compares the table's contents (`rebuilt.md` §2b). Before it
  came `NewSavesCompatibility`'s trampoline; the pass before that added the
  `HighQualityReflections` detour; before that the cutscene body and
  `OutOfCommissionFix`'s two (the branch body at 7 instructions and the mounting
  wrapper at 41; see that key's section), and `ForceCarHeadlightShadows`' hook
  thunk (`0x1001ADB0`, 9 instructions) was the one before them. The fifteen before
  that were the vehicles
  pass's: the steer trio's
  model-registry lookup and its two detour bodies, `GarageModelFix`'s two site bodies
  and two helpers, and `NoLiveryLimit`'s five site bodies and three helpers (see the
  section below); earlier passes added twelve (`RestoreDeathMusic`'s gate body and
  its three native wrappers, `MouseFix`'s eight pieces) and fourteen
  (`NewMemorySystem`'s eleven small table methods, `NewFilesCompatibility`'s
  registration tail, `LoadDLCs`' two bodies);
- launcher: both arms implemented. `LaunchWithPlayGTAIV` rewrites the two launch
  slots and `zp_launcher_slots` verifies them per build; `SkipLauncher` carries the
  original's 25-row table, both collectors, the DFA file-API stage (IAT capture, six
  installs, two collected jumps) and the ROM message-window stage (two installs, the
  WM_ROM service and its two bodies). The write set matches the original's recovered
  handlers byte for byte on all nine images (see the section below); the key reads
  OPEN only because the stored baseline is a partial recording of this feature;
- every signature an arm scans is now the original handler's own string, and its
  per-image hit set is pinned against it (`zp_signature_parity`). Four arms had been
  scanning a shortened copy that selected the original's site only on 1.0.8.0; see
  the signature-parity section;
- the 18 full/per-option per-build dump hashes and the oracle differential are not
  yet produced.

### `FreezeCarFix` and `VRAMFix`, the two keys the empty-cell scoreboard hid

Both keys were counted closed on 1.0.8.0 while writing **nothing**, and the reason
is specific: `parity_report.py` excludes every byte that is a branch displacement
(the four bytes after an `E8`/`E9`, found from either cell or the pristine image),
because the baseline recorded one stand-in address for every body target. Every row
of `FreezeCarFix`'s baseline cell on 1.0.8.0 is such a displacement byte -- the
sixteen sites were already `E8`s, so only their operands changed -- so the key
closed with an empty cell, and it would have kept closing whatever target an `E8`
named. `VRAMFix`'s cell is empty on 1.0.8.0 because its signature is absent there,
which is the original's own skip. The scoreboard's closure is therefore necessary
but not sufficient for these two keys; the site and body-target evidence is the
independent cross-check against `ZolikaPatch_reconstruction`
(`.agents/rewrite/08-independent-crosscheck.md`) together with the oracle
disassembly of the two handlers, and it is what this section records.

**`FreezeCarFix`** (shipped default 1, catalogue mask all builds). The original's
handler `0x10037C70` makes seventeen `E8` installs at sixteen distinct sites: the
primary signature's `+7`, the guard's `+11`, the ten dispatch arms at `+0x24`/
`+0x3D`/`+0x56`/`+0x6F`/`+0x88`/`+0x9E`/`+0xB4`/`+0xCA`/`+0xE0`/`+0xF6`, four
one-call patterns, and the short prefix's `+7` -- the same site as the primary, so
the original writes that one twice. The arm reproduces that shape exactly
(`source/vehicles.ixx`):

| piece | recovered VA | instructions | how |
|---|---:|---:|---|
| spawn guard | `0x100372F0` | 16 | transcribed |
| dispatch thunk 0x24 (object) | `0x10037330` | 9 | transcribed |
| dispatch thunk 0xCA (heli) | `0x10037360` | 9 | transcribed |
| dispatch thunk 0x6F (car) | `0x10037390` | 9 | transcribed |
| dispatch thunk 0xE0 (boat) | `0x100373C0` | 9 | transcribed |
| dispatch thunk 0x3D (ped) | `0x100373F0` | 9 | transcribed |
| dispatch thunk 0x56 (ped) | `0x10037420` | 9 | transcribed |
| dispatch thunk 0x88 (vehicle object) | `0x10037450` | 9 | transcribed |
| dispatch thunk 0x9E (bike) | `0x10037480` | 9 | transcribed |
| dispatch thunk 0xF6 (plane) | `0x100374B0` | 9 | transcribed |
| dispatch thunk 0xB4 (train) | `0x100374E0` | 9 | transcribed |
| spawn probe | `0x10037510` | 394 | transcribed |
| model-name lookup | `0x10003F00` | 21 | **rebuilt in C++** |
| "who" wrapper | `0x1005B290` | 61 | **rebuilt in C++** |
| chat print | `0x1005B530` | 45 | **rebuilt in C++** |
| bounded formatter | `0x10001280` | 13 | reused: `source/stability.ixx` proves it |
| `/GS` cookie check | `0x1005FE7B` | 5 | reused: the CRT's `__security_check_cookie` |

The three rebuilds are compiler output, not a hand-written instruction stream:
the name lookup carries an indexed absolute operand in a `/GS` frame
(`push [0x100DE770 + esi*4]`), the "who" wrapper builds a 0xA0-byte native call
block around `sub_100038E0` (`lea esi,[esp+30h]` over four records), and the print
wrapper is the same block with the cleanup call (`call 0x1005DE40`). They are
rebuilt on the native layer's exported `Native::Invoke`/`Native::Salt` with the
recovered block layout, and the probe calls them through the same `E8 rel32` shape
the original uses. One unobservable difference: the original's `memset` covers
`0xA0` bytes, so the block's seventeenth argument word (`args[16]`, at `+0xA0`)
keeps stack garbage, while the rebuild zero-initialises the whole struct. Both
wrappers pass one argument, so the word is never read on either side.

Recovered detail that is behaviour, kept 1:1:

- The primary signature is the gate. On 1.0.0.0, 1.0.4.0 and 1.0.0.4 it has no
  hit, and the original wraps every later scan in the same `if (result)`, so those
  builds write nothing -- no arm is invented for them.
- `DebugPrintSpawns` is read **only** inside the `FreezeCarFix` gate and only
  raises the flag: `movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1;
  mov [0x10159C15],cl`. `FreezeCarFix=0, DebugPrintSpawns=1` leaves it 0.
- The resume words are the targets of the `E8`s the install overwrites, read in
  the publish tier; the short prefix's write is the original's own duplicate.
- The ped-object array `dword_1015AD78` is resolved by this arm's own publish
  (the operand four bytes before the recovered anchor), because the original's
  loader preamble resolves it unconditionally and the `BadPlayerModelCrashFix`
  arm's copy is behind that key.
- The probe's cookie check keeps the original's `EAX` contract: the model index
  is answered past a final `call __security_check_cookie`, and the guard keeps the
  original callee's `EBP` frame and `ret 0Ch`.

**`VRAMFix`** (shipped default 1). The original's handler `0x1004AAE0` scans
`8B 44 24 10 D9 2C 24 83 C4 1C C3` and writes `E9` + rel32 to `0x1004AAB0` at the
match. The signature is present on 1.0.4.0, 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.1.1.0 and
1.1.2.0, and absent on 1.0.0.0, 1.0.8.0 and 1.1.3.0. The handler has no build gate
-- the scan is the gate -- so this arm adds none; a miss writes nothing. The shim
(`0x1004AAB0`, 13 instructions) is transcribed; its verdict helper (`0x1004AA80`,
12 instructions) is rebuilt in C++ -- compiler output whose compares are
`test eax,eax / js set`, `cmp dword_1015D630,0 / jne set`,
`cmp eax,3E800000h / jge clear` (a signed compare of the float bits against
0.25f) -- and the shim keeps the original's `fldcw [esp]` tail, which reads the
`E9` site's return address as the FPU control word. The arm lives in
`source/misc.ixx` with the other one-off installers although the catalogue
attributes the key to the graphics module; moving it is only a move.

**Attribution.** `tools/gen_body_targets.py` now reads each `Apply<Key>` function
as a block instead of line by line, so an install call split over two lines
(`BenchmarkFix`, `ImprovedShaderStreaming`, `SuperLODFix`), a helper install
(`WriteCallNoRead` in `BorderlessWindowed`), a `target = &abi_x` assignment
(`LoadDLCs`) and a file-scope case table the block iterates (`FreezeCarFix`'s
`kSpawnCases`) all attribute their bodies. That filled the nine rows the
cross-check found missing (`BorderlessWindowed` x2, `SuperLODFix`,
`ImprovedShaderStreaming` x2, `BenchmarkFix` x2, `LoadDLCs` x2) and the ten
`FreezeCarFix` thunks; `tests/body_targets.tsv` is generated, so the drift guard
(`zp_body_targets_drift`) stays meaningful.

**Measured.** `parity_report.py 9` is `98 key(s) closed, 0 open, 1 oracle-verified`
(the same closed count as before -- the cell was already scored closed -- now with
64 displacement rows behind it); `dump_matrix.py --only 9` is `947` full-cell
bytes, `differing 0`, `extra 57` (SkipLauncher), `98/99`; `verify_bodies.py` is
`164 body(ies), 0 failure(s)` (the 13 new rows included; the probe is the largest
transcribed body at 394 instructions). `parity_report.py` on builds 5, 6, 7, 8, 11
and 12 no longer lists `VRAMFix` open, and the `Patch7RemoveModChecks`
catalogue alias on 1.0.6.0/1.0.7.0 is now reported as an oracle-verified baseline
shortfall rather than an open key (see its own section below); `CarDynamicShadowFix`
is reported as an oracle-verified baseline omission on 1.0.0.0 and 1.0.4.0 only
(`tests/oracle_verified.tsv`, per key and build) and scored normally elsewhere -- not
open there, and not counted as closed on those two builds. The
independent cross-check now reports build 9 `confirmed 89, different 0, recon_only
0, ours_only 0` (`FreezeCarFix` counted confirmed), no `VRAMFix` recon-only row on
any of the six builds, and part 2 `173 row(s): 2 disagreement(s), 0 with no key in
our body_targets.tsv` -- the two remaining attribution disagreements are the two
the cross-check report already resolved against the ASI (`LocalSettingsPath`'s
`0x100512F0` and the shared steer detour `0x100051B0`).


### The native-script call layer, and the key it closed

`DrivebyFOVFix` was blocked on the ASI's own scripting layer: its helper resolves
the drive-by FOV native through the game's native-handle resolver. That layer now
exists as `source/native.ixx`, and the key is closed with it.

| piece | VA | instructions | how |
|---|---:|---:|---|
| zero a pending output record | `0x100038C0` | 5 | transcribed |
| one-argument call, salt `dword_101580B8` | `0x1005B3F0` | 46 | transcribed |
| zero-argument call, hash from `ECX` | `0x1005B150` | 44 | transcribed |
| copy pending records out, dword result | `0x1005DE40` | 21 | transcribed |
| copy pending records out, byte result | `0x1005DE80` | 21 | transcribed |
| guarded resolve-and-invoke | `0x100038E0` | 59 | rebuilt as C: `__try`/`__except` frame (`rebuilt.md` §3 class B); behaviour asserted by `zp_native_guard` |
| guarded resolve-only | `0x10003980` | 52 | rebuilt as C: same frame (class B); behaviour asserted by `zp_native_guard` |

The last two are compiler output for a C function with `__try`/`__except`
(`GuardedInvoke` / `GuardedResolve`): their frames carry a cookie-xored scope table
whose handler fields are addresses inside the original image, so there is no
instruction stream to transcribe -- the same reason the launcher's DFA close-handle
body is rebuilt. Both keep the original ABI and behaviour, and `zp_native_guard` now
asserts the split instead of arguing it from the disassembly:

- the normal path: the invoke resolves the hash, **calls** the handler with the block
  and answers `1` (the handler's own EAX is ignored -- the original's `push edi;
  call eax; add esp,4` is followed by `mov al,1`); the resolve answers the handler
  address and does not call it;
- a fault **inside the guarded region** -- the handler call, or the resolver call
  itself -- is caught and answers `0` (`xor al,al` at `0x1000396A`, `xor eax,eax` at
  `0x100039FC`), and the process carries on: the test's next normal call still works;
- a resolver that answers **null** is the invoke's `test eax,eax / je 0x1000393F`
  (the `je` at `0x10003937`), which skips the call and still reports success
  (`mov al,1`); the resolve has no `test` at all and answers the null to its caller;
- a **null resolver** (`dword_1015BDE0` unresolved) is caught by the same frame as
  `0`, and the *caller* then faults when it reads through the zero (`DrivebyFOVFix`'s
  `[eax+0Eh]` at `0x1000646E` is the measured consumer). That last one kills the
  process, so the test runs the caller in a child and asserts the child's
  `0xC0000005`, with `RESOLVED=0` written before the read so "the guard answered 0"
  and "the caller faulted" cannot be confused with "the guard itself faulted".

`Native::SetResolverForTest` exists for that test: one store to the slot `Register()`
would fill and no other state; the shipped path never calls it.

The resolver is not invented. The original's loader preamble fills `dword_1015BDE0`
at `0x10052E88..0x10052E9E` by scanning the game module for `08 0B C2 8B F0 E8`
(1.0.8.0 site `0x625300`) and taking the target of the call whose opcode is the
pattern's last byte, `match + 10 + [match + 6]`. That is `0x626AF0`, the game's
native-handle resolver, and `Native::Register()` reproduces the scan with
`find_pattern` at startup. A missing pattern skips the store exactly as the
original's `jz` does, so the preserved fault is a null resolver: the layer's SEH
catches the call and answers zero, then the caller (for `DrivebyFOVFix`, the
`[eax+0Eh]` read at `0x1000646E`) faults where the original faults. `zp_native_guard`
measures both halves: the guard's zero in-process, and the caller's fault in a child
process.

Two ABI details the transcribed pieces rely on. The game resolver reads the hash
from **ESI**, not from the argument registers (`0x626AF0` opens `mov eax, esi`), and
both original entries move the `ECX` argument there around the call, saving and
restoring the register; the C entries call a naked `ResolveInEsi` for that, because
C++ cannot ask for a register. And the one-argument wrapper's first register
argument is unused -- its payload travels in `EDX` -- so `Native::CallBool` is
declared with a discarded `ECX` parameter to make the compiler place the value
there.

Two call targets differ from the original's, both inside the proof's
call-is-an-immediate rule (below): the two wrappers call a local `NativeZeroBlock`
instead of the CRT `memset`, because the inline assembler rejects the intrinsic's
name in a direct call (C2420), and `DrivebyFOVFix`'s helper calls a six-byte `jmp`
forwarder through the layer's published resolve entry instead of the original's
direct `E8` to `0x10003980`, because a module interface cannot export a naked
target and the C entry's decorated name is not nameable from inline asm.

### `BadPlayerModelCrashFix`, closed on the same layer

The key had been reverted as blocked on that library. Its remaining pieces turned
out to be transcribable: every call the installed code makes lands on a piece the
tree already carries or on a piece small enough to transcribe here, and the last
piece — the name-table walk — is now the original's instructions too, with its table
registered.

| piece | VA | instructions | how |
|---|---:|---:|---|
| the site's trampoline | `0x10037BA0` | 16 | transcribed |
| the payload it calls | `0x10037AC0` | 65 | transcribed |
| the GTAC.dll "who" call | `0x1005B290` | 61 | transcribed |
| the GTAC.dll chat print | `0x1005B530` | 45 | transcribed |
| its one-argument native wrapper | `0x1005B3F0` | 46 | the layer's copy, called through a forwarder |
| the model-name lookup | `0x10003F00` | 21 | transcribed, row `bpm_model_name`; the table it walks is registered (`Bodies::RegisterTable`, `0x100DE770`, 153820 bytes) and the proof compares the table's contents (`rebuilt.md` §2b) |
| the bounded formatter | `0x10001280` (`0x10001240`, `0x10001230`) | 13 + 21 + 2 | transcribed and proven (rows `bpm_format`, `bpm_format_engine`, `bpm_format_options`) |

The arm jumps to the trampoline at the game's model-resolution entry (game
`0x7CF803`, three bytes into the function, so `EBP` is live) and publishes the
resume `match+10` before the jump, because the resume is the instruction the jump
covers. What the bodies read from the tree is unchanged: `sub_100038C0`,
`sub_1005DE40`, `sub_1005B150` and `sub_1005B3F0` come from
`source/native.ixx`, `sub_10003E50` from `source/shared.ixx`, `sub_100038E0`
through the layer's published invoke entry and `dword_10159C0C` through
`source/shared.ixx`'s slot. The six `jmp` forwarders (one instruction each)
exist for the same reason `DrivebyFOVFix`'s does: a module interface cannot export
a naked function, so the arm fills each slot from the body registry by the piece's
recovered VA.

**The name table is no longer a rebuild.** `0x10003F00` walks the original's
38,455-entry name table and hashes each name with `sub_10003E50`; its two data
operands are *indexed* absolute addresses (`push dword ptr [esi*4 + 0x100DE770]` and
`mov eax, dword ptr [esi*4 + 0x100DE770]`), which is exactly the operand the proof
could not compare. The tree now names the table instead of avoiding it:
`Bodies::RegisterTable` records the module's copy, the original's VA and the table's
length, `tests/bodies.tsv` carries the matching `table` row, and the comparator
canonicalises those two displacements to `table:<content hash>`. The body is the
original's instruction stream, including the 10-byte loop padding; the table is the
recovered `source/model_names.inc` in the table's own order, with the extraction
recipe and the pinned image's SHA-256 in its header. What stays unproven is the
`call` to the resolver: it goes through a forwarder, like the five beside it.

The formatter used to be the key's second rebuild (`_vsnprintf_s` with `_TRUNCATE`).
It is now the original's own instructions: the wrapper, the engine call with the
`STANDARD_SNPRINTF_BEHAVIOR` bit (`or ecx, 2`) through the `__stdio_common_vsprintf`
slot, and the two-dword options block. A probe of the old rebuild against the
original's engine call showed identical buffer contents at every truncation and a
different (unread) return value; `rebuilt.md` §2 quotes it.

**The preamble's salt block, and what the layer had wrong.** `Native::CallBool`'s
body reads `dword_101580B8`, so the layer has to resolve the original loader
preamble's ten salt words, not seed one constant. The probe is
`0x10052E10..0x10052E1C`:

```
10052e10  mov  ecx, offset a6868A9541451E8 ; "68 ? ? ? ? 68 A9 54 14 51 E8"
10052e15  call sub_10003830
10052e1a  test eax, eax
10052e1c  jnz  short loc_10052E82
```

and `loc_10052E82` is the resolver scan (`GetModuleHandleA(0)` again, then the
`08 0B C2 8B F0 E8` probe at `0x10052E88`). The ten stores at
`0x10052E1E..0x10052E78` are in the fall-through, so they run **only when the
probe misses**. The hit path copies nothing from the game image: `jnz` jumps over
every store, so each slot keeps its shipped `.data` dword. No other code writes
these ten words -- the only xrefs are the preamble store and the readers below.

| store | slot | miss value | shipped `.data` (hit) | consumer |
|---|---:|---:|---:|---|
| `0x10052E1E` | `0x10158054` | `0x6B86410E` | `0x511454A9` | `sub_1005B1E0`, two-argument native wrapper |
| `0x10052E28` | `0x101580C8` | `0x406B4B20` | `0x570F5725` | `sub_1005B290`, the BPM "who" wrapper |
| `0x10052E32` | `0x10158050` | `0x6DCD115A` | `0x3B0C6738` | `sub_10050A50` |
| `0x10052E3C` | `0x101580B8` | `0x62BFDB37` | `0x4E61480A` | `sub_1005B3F0`, `Native::CallBool` |
| `0x10052E46` | `0x10158028` | `0x931F73A2` | `0x6A6B4F18` | `sub_1005B490`, one-argument byte wrapper |
| `0x10052E50` | `0x101580A8` | `0x7F6A60D3` | `0x79A95BF9` | `sub_10050A50` |
| `0x10052E5A` | `0x10158040` | `0xC9DBDA90` | `0x4BF36A32` | `sub_10037AC0`, the BPM payload |
| `0x10052E64` | `0x1015803C` | `0xA4EA0691` | `0x022B2DA9` | `sub_10004C70`, `sub_100388C0` |
| `0x10052E6E` | `0x101580D4` | `0x09399528` | `0x75E005F1` | `sub_10006450`, drive-by FOV helper |
| `0x10052E78` | `0x1015805C` | `0x1CC648EA` | `0x62E319C6` | `sub_10050A50` |

The stores are followed by `dword_1015BDE0`'s resolver sequence at
`0x10052E96` (`mov ecx,[eax+6]; add eax,0Ah; add eax,ecx; mov
dword_1015BDE0,eax` at `0x10052E9E`), which is the scan `Native::Register()` also
reproduces.

On 1.0.8.0 the probe hits (game VA `0xB3878C`), so the runtime values are the
shipped set: `0x4E61480A` for B8, `0x75E005F1` for D4, `0x570F5725` for C8,
`0x4BF36A32` for 40. On 1.0.0.0 and 1.0.4.0 the scan has no hit, so the stores run
and the same slots become `0x62BFDB37` / `0x09399528` / `0x406B4B20` /
`0xC9DBDA90`. `source/native.ixx` now carries the ten words, seeds each with its
shipped value and applies the miss set; `Native::Register()` calls
`Native::ResolveSalts()` before any arm. `zp_salt_probe` pins both branches on
those three real images, through that same function and `find_pattern`.

Two layer bugs came from seeding one slot with the miss constant instead:
`source/native.ixx` set B8 to `0x62BFDB37`, so `Native::CallBool` asked for a
hash 1.0.8.0 does not define; and `source/misc.ixx`'s `DrivebyFOVFix` helper read
its own slot seeded with the D4 miss value `0x09399528`, so on 1.0.8.0 it
resolved the wrong native and then dereferenced the answer at `0x1000646E`
unguarded. The helper now reads the layer's `0x101580D4` slot. The unguarded
dereference stays -- that is the original's.

**The duplicate `0x1005B3F0` body is retired.** With the layer's B8 slot correct,
`BadPlayerModelCrashFix` no longer needs its own 46-instruction copy of the
one-argument wrapper. `BpmCheckBody` calls the layer's body through a six-byte
`jmp [zpatch_bpm_call_bool]` forwarder -- the same pattern the arm already uses
for `sub_100038C0`/`DE40`/`1005B150` -- and the arm's `zpatch_bpm_salt_b8`
slot is gone; its C8 and 40 salts stay local. `BpmCheckBody` keeps its 65
instructions and its proof (the call target is an immediate on both sides), and
`bad_player_model_call_bool` is removed from `tests/bodies.tsv` (73 -> 72
bodies).

**The fault these bodies keep.** On a build whose ped-array anchor is absent, the
original's preamble skips the store into `dword_1015AD78` and leaves it zero, and
the payload's first array read (`mov esi,[ecx+eax*4]`) dereferences it. 1.0.0.0 is
such a build: the anchor `85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07` has no hit
there, while the site itself does (`0x89AE53`). The arm keeps the original's shape --
no guard, and the store is skipped when the scan misses -- so it faults in the same
place; the offline dump cannot see it, because nothing dereferences the slot there.

### The vehicle arms closed in this pass

- `IncreaseVehicleModelLimit` (anchor `0x8DD143`): writes the dword `0x400` through
  the operand of the `mov ecx, 0xF3E8E8` at match+77, which is the model-limit
  object the allocator is constructed with. The operand is a *live address*: the
  loader has already applied the image's relocations when the arm runs, so the cast
  is the whole conversion (`GamePointer`, now identity). `docs/parity.md`'s
  "captured operands are live addresses" section records the measurement and why the
  old preferred-base rebase was wrong in the game.
- `EpisodicVehicleSupport`: on 1.0.7.0 and 1.0.8.0 it retargets two absolute
  episode-flag operands to this module's own word (initial 2, the game's value) as
  the original's four fixed-RVA installs -- build 8: `0x21500` and `0x7D91AE`;
  build 9: `0x95040` and `0x684ABE` -- never as a pattern hit, because the operand
  those words hold is a relocated address;
  every gated build then gets the 23 signature-driven NOP rows, in the original's
  row order (two all-hits rows, two rows with a second window).
- `HighFPSBikePhysicsFix` (anchor `0xB9C05B`): jumps to the clamp detour at
  `0x1001EF60`, whose published resume is the site's own `call` (match+9).
- `DualVehicleHeadlights` (anchor `0x8F7CE5`): two 78-instruction bodies --
  `0x100064F0` for the new builds, `0x10006630` for the old ones -- that draw both
  headlights through the site's own calls and tail-jump to the site's `pop edi`
  (match+338).

**A recovered-data conflict, recorded rather than papered over.**
`DualVehicleHeadlights`' catalogue row carries the mask `0x20` (bit 5 alone,
1.0.4.0). The original's own two arm masks are in `recovered_build_gates.inc`:
`@old` = `0x62` (build ids 1, 5, 6 -- 1.0.0.0, 1.0.4.0, 1.0.0.4) and `@new` =
`0x4BF80` (ids 7, 8, 9, 10, 11, 12, 13, 15, 18 -- 1.0.6.0 through 1.2.0.43), and the
original's handler picks the body by the build id it classified (`cmp [build], 1` ..
`cmp [build], 6` at `0x100067F3`). So the arm consults `@old` for the jump to
`0x10006630` and `@new` for the jump to `0x100064F0`; their union, `0x4BFE2`, is
every supported build, and the 1.0.8.0 baseline the scoreboard compares does carry
the write. `Loader::Enabled` would therefore never install this arm on 1.0.8.0, so
this one arm gates on `Catalogue::Read("DualVehicleHeadlights")` plus those two
recovered masks.

The catalogue row is wrong, and the fault is in `tools/gen_options.py`, not in the
inventory's prose: `mask_for()` splits a build cell on `,` and `/` and keeps only the
tokens that are a known version string or a bare integer, silently dropping the
rest. This key's cell is `doc: 1000/1040/1004 — **code: no gate** (§10)`, so the
first and third tokens are dropped (`doc: 1000`, `1004 — ...`) and only the middle
`1040` survives -- `1 << 5` = `0x20`. Two ways out, both outside this tree's feature
code: make `mask_for` reject a cell it cannot parse completely (so the next prose
cell fails loudly instead of picking one build), or move the prose out of the build
column and list the code's own mask (`ALL`, i.e. `0x4BFE2`). Until one lands, the
catalogue and the arms disagree on this key.

Transcribed and available for the arms that need it (`source/shared.ixx`):
`sub_10003E50` (the `hash:`/quote config-value resolver, 67 instructions) and the
1.0.8.0 `CModelInfoStore::getModelByKey` lookup (`dword_10159C0C`, resolved with the
original's own pattern). `AllowHashesForIDE` needs exactly that resolver (its site is
the game's hash function at `0x45CF50`). The native-script call layer is
`source/native.ixx` (above); its public surface is `Native::Call`, `CallBool`,
`Resolve`, `Invoke`, `Resolver`, `Salt`, `ResolveSalts` and `Register`.

### `ForceCarHeadlightShadows`, closed on the published float slot

This was the last blocked key for a mechanical reason: its arm writes a *pointer to a
float* into the shadow function's `movss` operand. The original writes its own image's
`dword_101580C0` there (`0x101580C0`); this module writes its own word; and the stored
baseline records the reference dump's `0xD0000000` placeholder for that word rather
than either address. `Bodies::RegisterPublishedImage` is what tells the dump which
word it is -- the registry for an address-shaped operand rather than a module word a
body reads -- and the dump substitutes the placeholder only for a word the run
actually wrote. The arm writes the real address; only the tool speaks the baseline's
convention. Nothing in the mod writes a placeholder and no guard was added for this.

The write set on 1.0.8.0, in the original handler's order (all RVAs are game RVAs;
`match` is 0x8F7C5B for the anchor rows):

| site | write | what it is |
|---|---|---|
| `0x8F7590` | `E9` -> `0x1001ADB0` | the shadow function's entry, hooked to the shim; the resume is match+12 |
| `0x8F7C61` (match+6) | `EB` | the `jne +79h` after `cmp al,1` / `test bl,bl`, forced |
| `0x8F7CBA` (match+95) | `E9` -> match+476 | the second `jne +177h`, forced; the five bytes start on its `0F 85` opcode, so its sixth byte becomes unreachable remainder |
| `0x8F7D2E` (match+211) | dword -> this module's offset float | the `movss xmm2, [0xD95B68]` operand (the game's word there is `0.0f`) |
| `0x8F7DD6` (match+379) | `EB` | the `jne +5Fh` on the same target, forced |
| `0x8F73EC` (`0x8F73E6`+6) | dword `0x504` | the first shadow buffer's `push` immediate, restored |
| `0x8F7422` (`0x8F741C`+6) | dword `0x104` | the second shadow buffer's `push` immediate, restored |
| `0x8F719F` (`0x8F7199`+6) | `0F 84 9E 00 00` | the `je` that skips the shadow, restored |

The last three are the two keys' interaction, not dead writes. The arm reaches them
through the handler tail (the original's `sub_1000E870`, which only runs with this key
on) and every one of them is a no-op on a pristine image; they exist to undo exactly
the three rewrites `CarDynamicShadowFix` makes in its `@noFCHS` half (the 0x504 and
0x104 pushes and the `jz`->`jmp`), which that arm skips while this key is on. The
arm order is load-bearing and reproduced: `InstallCarDynamicShadowFix` runs first and
its `mov dword [0x8F7422], 0x100` (the EA00 step) is unconditional, so with both keys
on it destroys this arm's own `8D 44 24 50 50 68 04 01 00 00` signature and the second
restore never happens -- measured in a combined cell, which leaves `0x8F7422` at `00`
and `0x8F7423` at `01`, exactly the two bytes the reference's notes record for the
original under the same pair of keys.

The scoreboard cell is 15 rows: the eight non-branch ones (two `EB`s, two `E9`
opcodes and the four placeholder bytes) plus seven displacement bytes it excludes.

**What is transcribed and what is rebuilt.**

| piece | VA | instructions/bytes | how |
|---|---|---|---|
| the hook thunk | `0x1001ADB0` | 9 instructions | transcribed and proven (`force_headlight_shadows_shim`) |
| the offset check | `0x1001AD20` | 139 bytes, 46 instructions | transcribed and proven (`force_headlight_shadows_check`) |

The shim is the whole body: store `ECX` into `dword_1015BDD0`, `pushad`, call the
check, `popad`, replay the prologue the `E9` replaced (`push ebp` / `mov ebp,esp` /
`and esp,-16` / `sub esp,0xD4`) and tail-jump to the published resume. The replay is
why the resume has to be read from the site before the site is written: those twelve
bytes belong to the function the hook replaced, so the continuation must run with the
frame already established.

The check is the original's own instructions: a `/GS` frame
(`mov eax,___security_cookie` / `xor eax,ebp`, `call __security_check_cookie` at
`0x1005FE7B`), a tail-merged epilogue shared by its two returns, `movss` for the float
store and a `nop dword ptr [eax]` loop alignment. Seed `dword_101580C0` with 2.0f, walk
the `dword_1015ADB8` hash table from slot 0, ask `dword_10159C0C` for each hash's model
index and compare it with the object's signed word at `+0x2E`, and on the first hit
store that slot's float from `dword_1015B5C0`. The lookup runs before the object is
read and neither is guarded, so a null lookup faults on the call (`0x1001AD5C`) and a
null object faults on the load (`0x1001AD62`), as in the original.

**The walk past the table is no longer a divergence.** The original's walk reads one
word past its 512 hashes (`0x1015B5B8`) when all 512 slots are filled. That word is
zero in the shipped image (the `.data` tail past the file's raw size) and is written
before the check can run by the ASI's own car-shadow code (`mov [0x1015B5B8],ecx` at
`0x100128B5`, `mov [0x1015B5B8],eax` at `0x10014F98`) with the game's car-parts shadow
function address — it is `CarDynamicShadowFix`'s published slot, read by six sites
including the two `call ds:[0x1015B5B8]` pairs in `detour_100068A0`. This tree already
publishes that value into `zpatch_carparts_shadow_fn` (same formula, same recovered
site), so the transcription makes the word **the same slot** rather than a placeholder:
the four objects sit in the original's order in `source/config.ixx` (hashes, the
shadow pointer, the check's resume, the offsets) and the hash table is registered with
the original's extended 2,052-byte range so the proof compares the read. The rebuild's
old bound (stop at slot 512) is gone with it; `rebuilt.md` §2g carries the evidence and
the one thing left unproven (that the two code paths really do produce the same
address).

**The two 512-slot tables, and the reader that fills them, are recovered.**
`dword_1015ADB8` / `dword_1015B5C0` are filled by the loader preamble's
`sub_100523C0` from `[HeadlightConeOffsets]`, before any handler installs. The
reader is now `Config::ReadModelTables` (`source/config.ixx`), driven from
`RegisterVehicles()` -- the first registration after `Shared::Register()` has made
`sub_10003E50` callable, and before anything reads an option or writes an image
byte -- and both checkers read the recovered tables
(`Config::HeadlightConeOffsets()`, `Config::PopupLightAngles()`) instead of a
module copy.

What `0x10052419..0x10052D9B` does, recovered instruction by instruction:

| piece | instruction(s) | what |
|---|---|---|
| section gate | `call sub_10055F10` / `test al,al` / `jz` (`0x10052477..0x10052489`, `0x10052921..0x10052933`) | a missing section skips its whole loop |
| row counter | `mov edi,1` / ... / `inc edi` / `cmp [ebp-5Ch],200h` / `jl` (`0x10052939..0x10052D9B`) | `Car1` ... `Car512`, 512 iterations; `Car513` is never asked for |
| key | `sub_1005BEF0` prepends the literal `"Car"` to the ordinal's `std::to_string`, then `memmove(...,4,"Name",4)` / `memmove(...,5,"Angle",5)` / `memmove(...,6,"Offset",6)` (`0x10052525`, `0x100526BE`, `0x10052B6C`) | `Car<N>Name` / `Car<N>Angle` / `Car<N>Offset`, no zero padding |
| empty name | `call sub_1005AF10` (string == `""`) / `test al,al` / `jnz` (`0x10052631..0x10052638`, `0x10052AE3..0x10052AEA`) | the row is skipped, value and all |
| hash | `call sub_10003E50` / `mov dword_1015C628[ecx],eax` and `mov dword_1015ADB8[ecx],eax` (`0x1005264A`, `0x10052AFC`) | the raw stored text (leading space, quotes, `hash:` included) is resolved and stored *before* the value is read |
| value | `call sub_1005BFF0` / `mov [errno],0` / `call strtof` / `cmp esi,[ebp-0CCh]` / `cmp [edi],22h` (`0x10052745..0x10052781`, `0x10052BF6..0x10052C32`) | `std::stof`, unguarded: `jz 0x100529BC` is `_Xinvalid_argument("invalid stof argument")`, `jz 0x100529B1` is `_Xout_of_range("stof argument out of range")` |
| angle | `movss` / `cvtps2pd` / `mulsd [0x10152358]` / `divsd [0x10152370]` / `cvtpd2ps` / `movss dword_1015BE28[esi]` (`0x1005286E..0x10052893`) | degrees → radians in double, narrowed once |
| offset | `fstp [ebp-0FCh]` / `movss dword_1015B5C0[esi]` (`0x10052C1D`, `0x10052C50`) | stored raw |

The consequences, all pinned by `zp_model_tables`: a name with no value stores its
hash and leaves the value at the table's zero, so that model resolves to `0.0f` /
`0` radians rather than to the checker's default; a value with no name is never
parsed, so even junk there is inert; a missing row leaves a zero hash at its own
slot while later rows keep their ordinals, which is where the checker's walk
stops; a malformed value throws out of the loader (after its hash was stored)
exactly as `std::stof` does; a full section fills all 512 slots. `zp_model_tables`
also runs the live `game_refs/ZolikaPatch.ini` through the transcribed resolver:
78 offset rows → 78 slots, so the user's actual configuration is the one this
reads.

The walk's end is no longer a divergence: `sub_1001AD20` is transcribed, and its read
past the 512th hash lands on the same word the original's does (the
`CarDynamicShadowFix` shadow-function slot, reproduced as the same storage, with the
check's own resume word after it — `rebuilt.md` §2g). A section that fills all 512
slots with resolvable names therefore walks out of the table here exactly as it does
there; the only thing that could still differ is the *value* of that slot, and both
sides derive it from the same recovered site.

### The five keys behind `sub_10004C70`, closed

The steer/engine/plane trio (`MovingSteeringWheelSupport`, `ShakingEngineBlock`,
`MovingPlaneParts`), `GarageModelFix` and `NoLiveryLimit` shared one piece: the
original's steer/render consumer `sub_10004C70` (1,343 bytes, 320 instructions),
which the trio's detour calls. It is **transcribed instruction for instruction**
(row `steer_consumer`: 320 instructions, 56 absolute operands, 2 table operands), and
all five keys are closed on 1.0.8.0 **and on the other eight images** (each key's
per-option cell is byte-identical to its baseline on builds 1, 5, 6, 7, 8, 9, 11, 12
and 13; build 1 is measured with isolated single-key cells because its per-option dump
faults at `NoImpostorHeightCutoff` by design).

**Why it was the last rebuilt piece, and what changed.** It is compiler output for C
-- a `/GS` frame (`mov eax,___security_cookie` / `xor eax,ebp` at `0x10004C76`;
`call @__security_check_cookie@4` at `0x100051A6`), SSE double conversions around
every float expression (`cvtps2pd` / `mulsd` / `divsd` / `cvtpd2ps`), the MSVC
float-`!=` idiom (`ucomiss` at `0x10004DDF`, `lahf`/`test ah,44h`/`jnp`) and `cmova`
for the two `>` comparisons. None of that blocks a transcription: the tree transcribes
`/GS` and SSE bodies elsewhere, every game entry it reaches is called through a slot
the loader resolves (`call dword_10159C0C`, `call edx` with `edx = dword_10159B88`,
`call dword_1015D63C`, `call dword_10159CDC`, `call dword_1015AD4C`) and every
constant is a plain `.rdata` qword (`-pi` at `0x101523A8`, `pi`/`180` at
`0x10152358`/`0x10152370`, and the rest). What blocked it was exactly two operands:
`mov eax,dword ptr [esi*4+0x1015A538]` (`0x10004CD0`) and
`movss xmm0,dword ptr [esi*4+0x10159D38]` (`0x10004E00`) -- the wheelangle
model/angle tables, read as *indexed absolute* addresses, which the whole-body
comparator compares literally, against arrays (`zpatch_wheelangle_models`/`_angles`
in `source/misc.ixx`) whose address cannot be the original's. Two properties of those
tables were outside the registry's reach and are now covered (`rebuilt.md` §2c): the
original keeps them past the raw size of its `.data` (raw `0x1000` of a `0x504D4`
virtual extent, so the file holds none of their bytes and they are zeros when
loaded), and the arrays are `extern "C"` in `misc.ixx`, so the map names them
`_zpatch_…` rather than `?zpatch_…`. `tools/verify_bodies.py` now reads a section's
uninitialised tail as the zeros the loader maps and resolves both decorations. The
comparison rules themselves are unchanged: `summarise` and `table_mismatch` compare
the same shapes they always did.

**What the body is.** The transcription is the original's instruction stream, so the
blocks below are what it computes rather than a reimplementation of them; every
operand points at the same thing the original's does (see the operand map under
`rebuilt.md` §2c):

| block | original (1.0.8.0) | transcription |
|---|---|---|
| the vehicle | `dword_1015AD38`, stored by the detour before the call | `zpatch_steer_vehicle`, same word |
| steering block gate | `byte_1015AD40` | `zpatch_steer_enabled` |
| engine block gate | `byte_1015AD41` | `zpatch_engine_enabled` |
| plane block gate | `byte_10159C17` and `[vehicle+0x1354] == 5` | `zpatch_plane_enabled` and the same field |
| steer lock | `dword_1015AD38[0xE18]` -> `+0x84`, read after `+0x10D8` | the same two reads, float division then widen |
| wheelangle table | `dword_1015A538[]` / `dword_10159D38[]`, non-zero slot, 512 bound | the IDE hook's tables (`source/misc.ixx`), registered as 2048-byte tables |
| model word | `+0x2E`, sign-extended for the array index, `-1` skipped | the same instructions |
| bone chain | `dword_1015AD78[model]` -> `+8` -> `+0xB4` -> `+0xC`, then `sub_10003F40` | the same chain, calling the transcribed `sub_10003F40` |
| bone matrix | `dword_10159B88(vehicle, entry[0x14])`, then identity at 0/1/2/4/5/6/8/9/10 | the same nine stores with the same `0x3F800000` immediate |
| wheel tilt | `dword_1015D63C(matrix, angle*pi/180)`, then `dword_10159CDC(matrix, steer/lock*-pi)` | the same order and the same two setters |
| engine phase | `(float)(script*step + phase)` in double, stored to `dword_1015AD3C`; `> 0.01` sets `byte_1015AD42 = 1`, `< -0.01` clears it | the same `comisd`/`cmova` sequence, phase kept as float bits |
| engine scale | `(state & 8) ? (float)(rpm*0.75 + 0.25) : 0`, then `(state & 0x10)` forces `1.0f`; `mulss` with the phase | the same |
| plane parts | parts 89, 91, 90, 92 with `count 4` and `script*suspension*100`; 96, 97 (`pitch`, `±0.5` in double), 94, 95 (`roll`, `-0.5`), 93 (`heave`, `+0.5`, `count 5`), all with `1,0,0` and `count 3` for the surfaces | the same ids, order, counts and argument pushes |
| tail | `dword_10159D34 = script float`, return the slot pointer | the same |

The two soap-opera details that are deliberately not "improved": every hop and every
indirect call is unguarded (a preamble miss faults, see the fault table), and the
`!=` on the timestep is true for a NaN pair, as the original's `jnp` path is.

**What the two table operands' proof does and does not say.** Both wheelangle tables
are all-zero at load -- the original's are in its uninitialised `.data` tail, ours are
zero-initialised and filled by the same IDE hook -- so both hash to the same content
value (`707280d9b4da7bba`, 2048 bytes of zeros). The PASS therefore pins that each
operand reads *a zero-filled table registered for this row*, and the content check
**cannot tell the two tables apart**: a transcription that read the angle table where
the model table is meant would pass it. What pins the identity is the manifest
declaration (`rebuilt.md` §2c pairs `0x1015A538` with `_zpatch_wheelangle_models` and
`0x10159D38` with `_zpatch_wheelangle_angles`, per row) together with the
instruction's own semantics -- the slot that goes through `dword_10159C0C` and is
compared with the model index is the model table, and the value multiplied by `pi`
and divided by `180` is the angle table. Recorded here rather than left implied by a
PASS line.

**The detours.** `sub_100051B0` (10 instructions) and `sub_100051E0` (10) are
transcribed and proven: `mov dword_1015AD38, esi`; `pusha`; `call sub_10004C70`;
`popa`; then the ten bytes the jump replaced (`mov edx,[esi]; mov eax,[edx+128h];
mov ecx,esi; call eax`, or the `eax`/`edx` swap on the older variant);
`test al,al`; `jmp dword_1015AD44`. The arm publishes the resume (match+16, the
`jnz` the removed call's flags feed) and the part-drive target (the call operand at
match+423/+427 on the 1.0.6.0-and-later site, +140/+144 on the older one), and the
two builds' handlers each add the shared IDE parser hook through `sub_10004110`
(`E9` at the parser anchor's -10, resume at -1). `1.0.8.0` writes:

| site | RVA | target |
|---|---|---|
| the vtable call | `0x6538A0` (`E9` at the anchor's +2) | detour `0x100051B0` |
| the shared IDE hook | `0x646546` (`E9` at the parser anchor's -10) | misc's `0x100040E0` body |

Published on 1.0.8.0: resume `0x6538AE`, part-drive `0x4F65F0` (the game function
the site's own `call` names), IDE resume `0x64654F`. The three keys differ only in
which switch byte their arm sets (`0x10054A7A` / `0x10054B56` / `0x10054AE8` in the
original's handler), so enabling one runs one consumer block and enabling two runs
two -- exactly the original's per-key switches. The steer keys install the shared
IDE hook, which is also how the wheelangle table gets filled: in the original that
is the whole point of `sub_10004110`, and this tree installs the *same* body
(`source/misc.ixx`, reached through the body registry by recovered VA) rather than a
second copy, so both key families share one table set.

**`GarageModelFix`.** All four pieces are transcribed and proven; no helper needed a
rebuild. `sub_10046B90` (19) copies the model's `+0x3C` dword into four byte fields
of the garage object (`0x40`, `0x13`, `0x41`, `0x46`), unless `byte_10159B85` is set
-- the original's DllMain sets that when `CitizenIV.exe` is loaded
(`0x10055DFB..0x10055E17`), which this tree reproduces in `ResolveSteerHelpers`.
`sub_10046BD0` (55) packs those four bytes back into a name hash, asks the game's
model lookup for the index, accepts 1..31999, and otherwise falls back to the fixed
hash `0xC703DB5F`, storing the index at `+0x10` and clearing bit 0 of `+0x30`; its
answer (`EAX`) is what the second site body re-pushes. The two site bodies are
`sub_10046C80` (8, replays `mov edx,[edi+0x20]; fld [edx+0x34]`, resumes at
match+6) and `sub_10046CA0` (7, replays `movzx ecx,[esi]; push eax; push ecx`,
resumes at match+5). Writes on 1.0.8.0: `E9` at `0x868BB0` (to `0x10046C80`) and
`0x5659C3` (to `0x10046CA0`); published resumes `0x868BB6` and `0x5659C8`.

**`NoLiveryLimit`.** Ten pieces, all transcribed and proven: the
`"%s_sign_%d"` hasher `sub_1003D220` (26), the variant counter `sub_1003D270` (37),
the 0x10-byte name copy `sub_1003D2F0` (6), the three livery site bodies
`sub_1003D310`/`D340`/`D370` (11/11/12) and the two collectors
`sub_1003D3A0`/`D3E0` (14/14). The two formatter calls are the one substitution:
`sub_10001280`, the original's bounded `vsnprintf` wrapper, is now the transcribed
body in `source/stability.ixx` (row `bpm_format`), reached through this module's
six-byte `jmp` forwarder so the call stays an immediate; the two resolver calls go
through `NoliveryResolve`, a forwarder to `Shared::ValueResolver()`. Both are called
by `E8`, which the proof compares as an immediate, so the bodies are otherwise whole.
The handler
(`sub_1003D420`) is an either/or: the first collector variant publishes the count
loop's lookup (the call operand at match-6/-2) and the D/E resume (match+90) and
jumps `match+2` to body D; failing that, the older variant publishes its own pair
(match-8/-4, match+84) and jumps `match` to body E; then the livery anchor publishes
its resume (match+13) and the three sites at the anchor, -199 and -237 jump to
bodies A, C and B. On 1.0.8.0 the older collector is absent, so the writes are:

| RVA | body | published slot |
|---|---|---|
| `0x64A53D` (the collector's +2) | D `0x1003D3A0` | fn `0x3A490`, D/E resume `0x64A595` |
| `0x797A29` | A `0x1003D310` | A/B/C resume `0x797A36` |
| `0x797962` (anchor -199) | C `0x1003D370` | |
| `0x79793C` (anchor -237) | B `0x1003D340` | |

The livery sites' arm does not rebase anything: `dword_10159BA4` is a game function
address (`0x3A490`, `CModelInfo`'s hash query) read out of the site's own call
operand and only ever called with `call edx`, and every other slot is a module word.

**What the scoreboard proves and what it does not.** The five keys' per-option cells
are byte-identical to their baselines (0 missing, 0 extra, 0 differing on all nine
images), which proves the write sets. The consumer's *runtime* behaviour -- the
matrix the two setters write, the part calls' arguments, the phase arithmetic -- is
not executed by the offline tools: the body is proven to be the original's
instructions with the original's operands, but nothing here makes the game call it.
Residual: a wiring mistake the operand map (`rebuilt.md` §2c) and the proof would both
miss, or a table the IDE hook fills differently, would only show in play. Recorded
here as the one unverified surface.

### The misc keys closed in this pass

Four keys in `source/misc.ixx` closed with their original arms and bodies. Six of
the seven bodies are transcribed and proven whole; the heavy helpers are compiler
output for C functions and are rebuilt with the same ABI, state words and unguarded
lookups:

| key | writes on 1.0.8.0 | bodies |
|---|---|---|
| `SpeedupBugFix` | `E9` at RVA `0x571B0` (game `0x4571B0`) | `sub_10050540`, 93 instructions to its `ret` (109 with the C++ static-init funclet), rebuilt in C++ (`rebuilt.md` §3 class B) |
| `Patch4RemoveModChecks` | `E9` at RVA `0x3CBF70` (game `0x7CBF70`) | `sub_100054E0`, 8, transcribed |
| `CopSirensFlag` | `E9` at RVAs `0x4FABE6`, `0x4FEBD3`, `0x646546` | `sub_10006310` 8, `sub_10006330` 8, `sub_100040E0` 9, transcribed; both model checks (`sub_10005F70` 191, `sub_10006260` 53) transcribed (`rebuilt.md` §2d) |
| `PopupHeadlightsFlag` | `E9` at `0x653A64`, `EB` at `0x653A6E`, the `fld` operand at `0x653AB6` (our pi/2 float, the baseline's `0xD0000000` placeholder), `E9` at `0x646546` | `sub_10044530` 10, `sub_10044560` 10, `sub_100040E0` 9, transcribed |

#### `SpeedupBugFix`: an ungated handler, so the row differs per build only in where it matches

`SpeedupBugFix` is one of the keys whose body has **no proof row**: `sub_10050540`
(93 instructions to its `ret`; IDA's 109 count includes the `__CxxFrameHandler3`
funclet) carries a C++ static-local-init EH frame and the `_Init_thread_header` /
`_Init_thread_footer` TLS guard for its one-time clock scan, so it is reimplemented in
C++ rather than transcribed. Neither the `/GS` cookie nor the SSE arithmetic is the
blocker — the tree transcribes both elsewhere; the EH frame and the TLS guard are
(`rebuilt.md` §1). The same call the tree makes for the launcher's DFA close-handle
body, which has the frame and no other blocker.

The handler is `sub_100506B0`, reached from the dispatcher at `0x10055A62` and
**not build-gated** -- the option's value alone decides whether it runs:

```
push  offset aSpeedupbugfix ; "SpeedupBugFix"   0x10055A23
call  sub_1005C160                              0x10055A35
...
cmp   esi, 1                                    0x10055A5D
jnz   short loc_10055A67                        0x10055A60
call  sub_100506B0                              0x10055A62
```

So its row is the same on every build. The handler scans, and on a hit it reads
the eight operands at fixed **site-relative** displacements before overwriting
the site; a miss is its own `test eax,eax; jz` return, with no write at all:

```
mov   ecx, offset "50 64 89 25 00 00 00 00 83 EC 34"   0x100506CD
call  sub_10003830                                     0x100506D2
test  eax, eax                                         0x100506D7
jz    loc_1005077D                                     0x100506D9
lea   ebx, [eax-15h]      ; site = match - 21          0x100506DF
mov   eax, [ebx+2Fh]      ; -> dword_10159350          0x100506E8
mov   eax, [ebx+0BAh]     ; -> dword_10159364          0x100506F0
mov   eax, [ebx+0D1h]     ; -> dword_10159360          0x100506FB
mov   eax, [ebx+0F7h]     ; -> dword_1015935C          0x10050706
mov   eax, [ebx+0FFh]     ; -> dword_10159368          0x10050711
mov   eax, [ebx+10Eh]     ; -> dword_1015934C          0x1005071C
mov   eax, [ebx+11Dh]     ; -> dword_10159354          0x10050727
mov   eax, [ebx+129h]     ; -> dword_10159358          0x10050732
mov   byte ptr [ebx], 0E9h                             0x1005074B
mov   [esi], eax          ; esi = ebx+1, eax = sub_10050540-ebx-5   0x1005076F
```

**How the eight pointers are obtained on each build: from the matched site's own
operands, at fixed displacements.** Not from a per-build table, not scanned
separately, not computed. Each displacement is the disp32 field of one `movss`
in the game's timer-init function, so the four bytes before the field are that
instruction's opcode and ModRM -- and those bytes are identical on every image
that carries the signature:

| `[ebx+disp]` | 1.0.8.0 slot | 1.0.8.0 instruction |
|---|---|---|
| `+0x02F` | `dword_10159350` | `0x4571DB` `F3 0F 10 05` `movss xmm0,dword ptr [0x18CAE9C]` |
| `+0x0BA` | `dword_10159364` | `0x457266` `F3 0F 11 0D` `movss dword ptr [0x18CAEA0],xmm1` |
| `+0x0D1` | `dword_10159360` | `0x45727D` `F3 0F 11 05` `movss dword ptr [0x18CAEA4],xmm0` |
| `+0x0F7` | `dword_1015935C` | `0x4572A3` `F3 0F 11 1D` `movss dword ptr [0x1752078],xmm3` |
| `+0x0FF` | `dword_10159368` | `0x4572AB` `F3 0F 11 0D` `movss dword ptr [0x18CAEAC],xmm1` |
| `+0x10E` | `dword_1015934C` | `0x4572BA` `F3 0F 11 1D` `movss dword ptr [0x18CAEB0],xmm3` |
| `+0x11D` | `dword_10159354` | `0x4572C9` `F3 0F 11 15` `movss dword ptr [0x18CAEA8],xmm2` |
| `+0x129` | `dword_10159358` | `0x4572D5` `F3 0F 11 05` `movss dword ptr [0x1752074],xmm0` |

**The sites, per build** (byte scan of every image in `game_refs`; the arm jumps
to `match - 21`):

| build | version | match | site | the eight operands |
|---|---|---|---|---|
| 1 | 1.0.0.0 | **no hit** | -- | the handler returns at `test eax,eax; jz` |
| 5 | 1.0.4.0 | `0x4544C5` | `0x4544B0` | all eight, same encodings, writable `.data` |
| 6 | 1.0.0.4 | `0x457395` | `0x457380` | same |
| 7 | 1.0.6.0 | `0x5AFBF5` | `0x5AFBE0` | same |
| 8 | 1.0.7.0 | `0x5AFDD5` | `0x5AFDC0` | same |
| 9 | 1.0.8.0 | `0x4571C5` | `0x4571B0` | same |
| 11 | 1.1.1.0 | `0x454BC5` | `0x454BB0` | same |
| 12 | 1.1.2.0 | `0x454C05` | `0x454BF0` | same |
| 13 | 1.1.3.0 | `0x5AD565` | `0x5AD550` | same |

On every one of those sites `match - 21` is `55 8B EC 6A FF` (`push ebp; mov
ebp,esp; push -1`, the SEH prologue), the eight operand encodings are the table
above, and every target is a writable `.data` word. 1.0.0.0 is the original's own
clean skip. The three builds with **no image** in `game_refs` -- 1.0.6.1 (15),
1.1.0.0 (10) and 1.2.0.43 (18) -- are *unverified*, not verified: neither this
tree nor the IDB says where their signature matches or whether their timer-init
function has the same layout.

The body reads its clock through its own one-time scan
(`1B D9 89 5C 24 24 0F 88`) and calls `match - 51`:

```
mov  ecx, dword_101A849C                                0x10050683
add  ecx, 0FFFFFFCDh                                    0x10050689
call ecx                                                0x1005068C
mov  [esi], eax / mov [esi+4], edx                      0x1005068E / 0x10050692
```

That pattern also hits once on every image above, and `match - 51` is the clock
function's own `55 8B EC 83 E4 F8` prologue on all nine (`0x456F60` on 1.0.8.0).
The arithmetic is the original's: `scale = 1000.0 /
QueryPerformanceFrequency`, then `(float)scale`, `(float)(scale * 1000.0)`,
`(float)(scale * 0.0010000000474974513)` and
`(float)(0.0010000000474974513 / scale)` written to the four pairs, then the
64-bit stamp stored in `*self`. A missing scan leaves the slot zero, so the call
faults on `0xFFFFFFCD` exactly where the original's does.

**What this tree does.** `ResolveSpeedupBugFix` (`source/misc.ixx`) scans the same
signature and then validates the site *before it publishes anything*: for each of
the eight operands the four bytes before the field must be the recovered encoding
and the dword must be a writable word of the image in use. Only when all eight
pass are they published, and only a resolved site is installed -- a site that
fails either test publishes nothing and installs nothing. On every image that
carries the signature all eight pass, so the published pointers and the `E9` are
the original's row, and the per-key cell is byte-identical to the pre-guard dump
on all nine images (build 9: 4 bytes, the `E9` at `0x571B0` plus three canonical
displacement bytes; the second displacement byte is already the prologue's `8B`).
The guard is measured, not asserted: with the first operand's `F3` replaced by
`F4`, or its dword replaced by `0x10003E50` (a module-coordinate address), the
same run writes **0 bytes** -- a clean skip instead of a store through a value
that is not a timing float.

**The one divergence this records.** On an image where the 11-byte sequence
matched some *other* function, the original would still publish the eight dwords
and install, and the rebuilt body would then store floats through them -- the
fault a guard-free port inherits. This tree skips instead. No image in
`game_refs` has such a site (all eight matches are the timer-init function), so
the divergence is unreachable with the images that can be tested; the rule
applied on an inapplicable build is the original's own clean-skip path, not a
fault we introduce. `tests/signatures.tsv` records the per-build sites, the eight
displacements and their encodings.


`Patch4RemoveModChecks` and `RemoveGFWLUpdatePopup` are the two flags of the
original's one shared handler (the stub `sub_100054E0` plus the helper
`sub_10005340`, 145 instructions). `loading.ixx` owns the GFWL branch; `misc.ixx`
owns the Patch4 branch: a one-time scan for
`33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57`, then a zero store to the dword
the matched code points at (`site + 30`). **That signature has no hit on
1.0.8.0**, so on the user's build the branch is the scan and nothing else -- the
key's visible effect there is the stub install, which is what the original does
too. Recorded split: the original has one helper reading both flags, while this
tree has one helper per module; only one body can sit at the site and the misc
arm installs later, so a run with *both* keys enabled performs the Patch4 branch
and not the GFWL writes. Each key alone is 1:1, and the shipped INI has both off.

The `CopSirensFlag` checkers (`sub_10005F70`, 191 instructions;
`sub_10006260`, 53) are **transcribed** (rows `copsirens_check_a`/`_b`): the thirteen
built-in police names and the `+siren` list are the original's own instructions, its
own `.rdata` name strings and its own resolver, and the table they walk is registered
so the proof compares the operand by content with the base register still compared
(`rebuilt.md` §2d). The `PopupHeadlightsFlag` checker (`sub_10044310`, 125) and the
shared IDE payload (`sub_10003FA0`, 92) are **transcribed** too (`rebuilt.md` §2f and
§2e): the payload is the original's own three string tests, its two append walks and
its `strtof`, filling a 512-slot model-hash list per category when the game's parser
stores a `popuplights`, `siren` or `wheelangle:<degrees>` entry; the checker is its own
lookup, its own bounded walk of the `+popuplights` list and its own eight-column scan
of the `[PopupLightAngles]` tables. Both keys install that same IDE body at the same
site, as the original's two handlers do. The lookups go through `dword_10159C0C` and
the names through the transcribed `sub_10003E50`, both unguarded, so a missing
resolver faults where the original's indirect call does.
The popup shims' second (EDX) pattern has no hit on 1.0.8.0; its body is still
transcribed and proven. The `[PopupLightAngles]` table the checker walks is filled
from the INI by `Config::ReadModelTables` (`Config::PopupLightAngles()`); an empty
section leaves every hash zero, which is what keeps the checker's own pi/2 default
-- the shipped INI's shape. `ForceCarHeadlightShadows`' checker reads the
`[HeadlightConeOffsets]` pair the same way; that key's section above records the
recovered reader, its faults and the word its walk reads past the table.

### `RestoreDeathMusic`, closed on the native layer's wrappers

The key's handler (`0x10050BD0`) scans three signatures and writes nothing but an
`E8` and its displacement at each site; all three sites are already `E8` calls in
the image, so the only bytes that differ from the pristine image are the twelve
displacement bytes, and the stored baseline's twelve rows are exactly those. That
is why the key read "closed" before it had an arm: both sides were all-branch and
the scoreboard excludes branch bytes. It now has its arm.

| piece | VA | instructions | how |
|---|---|---:|---|
| the gate body | `0x10050A50` | 41 | transcribed |
| the two-argument native wrapper | `0x1005B1E0` | 48 | transcribed |
| the byte native wrapper | `0x1005B490` | 46 | transcribed |
| the zero-argument bool wrapper | `0x1005B360` | 44 | transcribed |
| the install body | `0x10050AD0` | 75 | rebuilt as C, no proof row |

- The three sites: `0x81E3C5` (the track-select function entry, written at the
  match's `-13`), `0x81E509` (the gate call, also `-13`) and `0x81E53A` (the second
  gate call, at the match).
- The gate body reads the original's salt words `dword_1015805C`,
  `dword_101580A8` and `dword_10158050`; the two wrappers it calls read
  `dword_10158054` and `dword_10158028`. All five are copied from
  `Native::Salt(ref)` in `RegisterAudio()`, never from a literal, so a hit build
  keeps the shipped `.data` values and 1.0.0.0/1.0.4.0 get the preamble's fallback
  set (the rule the section above records).
- The wrappers are transcribed in `audio.ixx` rather than added to the layer
  because only four of the layer's pieces are exported and none of the wrappers
  is: they reach `sub_100038C0`, `sub_100038E0`, `sub_1005DE40`, `sub_1005DE80`
  and `sub_1005B150` through six-byte `jmp` forwarders filled from the body
  registry, exactly like `BadPlayerModelCrashFix`'s. Their `memset` call names a
  local cdecl forwarder (the inline assembler rejects the intrinsic's name, C2420).
- **The two counts the brief carried do not survive measurement**: `sub_10050A50`
  is 41 instructions to its `ret`, and `sub_10050AD0` is 75 (0x10050AD0 ..
  0x10050BC8), not 91. The 91 is the *handler* `0x10050BD0`'s count (0x10050BD0 ..
  0x10050D09), which the install path never executes as a body.
- The install body is rebuilt because it is compiler output: an EH4 frame
  (prologue `0x10050AD3..0x10050AF4` pushing `__except_handler4`, handler
  `0x100619C1` with its scope table at `0x10154384`, epilogue
  `0x10050BAE..0x10050BC8`), and a clock/seed block that calls the static CRT's own
  helpers (`0x1005FFC3`, `0x1005FF77`), which reach the CRT's per-thread state
  through `fs:[2Ch]` and the TLS index `dword_10159328` -- image-internal addresses
  a rebuilt module cannot name. The rebuild keeps the state word `dword_101A84B8`,
  calls the transcribed gate, resolves `6A 06 68 ? ? ? ? 8D 8C` once and writes
  `rand() % 5 + 1` into the `push` immediate at site+1, which is the original's
  write.
- **The one unproven divergence in this key, recorded rather than hidden.** The
  original resolves the site *inside* a guard it keeps at `0x10050B07..0x10050B21`:
  it reads `dword_101A84C0` (`.bss`, so 0 on a fresh process), compares it with the
  CRT per-thread word at `[tls + 4]`, and jumps over the whole clock/scan/seed block
  when `dword_101A84C0 <= that word`. Should that comparison always skip on a fresh
  process, the original would resolve nothing and write nothing; the rebuild
  resolves on the first call and always writes. The guard's meaning is CRT-internal
  and cannot be settled without running the original ASI, so the rebuild keeps the
  writing path (the intent both reference trees implement) and this note records
  that the guard's branch and the exact draw are the parts of `0x10050AD0` that are
  not proven 1:1.

### `MouseFix`, closed with all eight pieces transcribed

The arm writes seven branches and five `cvtsi2ss` replacements at ten sites; the
scoreboard's 51-row baseline cell matches ours byte for byte (our dump has 53 rows,
the two extra being displacement bytes of the two `E8` sites, which the scoreboard
excludes by design).

| piece | VA | instructions | role |
|---|---|---:|---|
| axis reader, float | `0x10024060` | 30 | X or Y * 1/128 in st(0) |
| axis reader, negated | `0x100240C0` | 23 | -127.5 * the axis |
| axis reader, int | `0x10024120` | 22 | `(int)(v * 1/128 * 255)` |
| axis reader, flagged | `0x10024170` | 28 | the negated form, sign flipped by the invert byte |
| angle step | `0x10024440` | 62 | advances pitch at +144h and yaw at +148h |
| angle site body | `0x10024560` | 25 | xmm save/restore, then the site's `-15` resume |
| advance site body | `0x100243A0` | 23 | xmm save/restore, then the site's `+1862` resume |
| advance helper | `0x100241E0` | 138 | patches three 8-byte windows at resume+34h/+5Eh/+6Bh |

- The published words are the sites' own operands, read in the publish tier: the
  two axis pointers (`0x17945D0`, `0x17945C0`), the invert byte `0x10FC0D0`, the
  game's frame clock `0xB4D3A0` and the two resumes (`0xA81B1F` and `+1878`). Each
  is a live address when the arm runs -- the loader has already relocated the image,
  and the offline dump relocates its copy the same way -- so it is stored as read
  ("captured operands are live addresses", above).
- The literals the bodies read are the original's `.data`, declared with the type the
  instruction reads and initialised with the number it is, never with a bit pattern:
  `0.0078125`, `255.0`, `0.075000003` (`0.075f` printed to 9 significant digits and read
  back as a double; the original's slot is that decimal, not a widened `0.075f`),
  `0.025`, `-1.0` (double and float), `3.1415927` / `6.2831855` / `-3.1415927` (the
  original's values, not pi and 2pi), `127.5f`, `0.1f`; every decimal round-trips to the
  original's bytes. Hex survives only where the bytes are not a number: the two `xorps`
  sign masks (four single-precision sign bits at `0x101523F0`, two double-precision ones
  at `0x101523E0`; 16 bytes because `xorps` reads sixteen, hence `alignas(16)`), and
  `CutsceneFixes`' `andps` sign mask `0x7FFFFFFF`. `Vehicles`' `sub_10004C70` constants
  and `CutsceneFixes`' float pool are written the same way, each filled with the shortest
  significant-digit decimal that reproduces its recovered bytes (an integral value keeps
  its `.0`/`f` so the type stays visible; that does not change the bits).
- The advance helper (`0x100241E0`) is transcribed rather than rebuilt. Its only
  indirect call is the game's `VirtualProtect`, which the original reaches through
  its own IAT slot (`FF 15`), so ours is a `call dword ptr [slot]` as well: the
  proof compares the memory operand, not the address. Its three window images are
  the original's immediates (`F3 0F 11 86 44 01 00 00`, `F3 0F 11 8E 48 01 00 00`,
  `D9 9E 48 01 00 00 8B 5C`), and the disabled forms are `90` fill plus `DD D8`
  (`fstp st(0)`) for the x87 window -- the pop must stay, because the enclosing
  routine pops that value later and eight calls of `nop` fill would overflow the
  x87 stack.

### `OutOfCommissionFix`, closed on its recovered payload

The key's writes on 1.0.8.0 are one `E9` at RVA `0x622E97` (game VA `0xA22E97`, the
first anchor's +7) to the 7-instruction branch body `0x1004C3C0`, and one `E8` at
RVA `0x770733` (game VA `0xB70733`, the second anchor itself) retargeted to the
41-instruction wrapper `0x1004C350`. The second site is already an `E8` in the
pristine image, so only its displacement changes and the scoreboard excludes those
four bytes as a branch; the `E9`'s five bytes are the key's only comparable ones.

The refusal this key carried twice was right about the shape and wrong about the
reach: the payload is a deterministic transform of bytes the original carries, and
the registration is the ASI's own, not the game's.

- **The resource.** 163,796 bytes of `.rdata` at VA `0x100B6798` (file offset
  `0xB5B98`), first bytes `2A 73 63 72`. Carried verbatim as
  `source/ooc_finale2.inc`, whose header pins the image SHA-256, the VA and length,
  the extraction recipe, and the decoded table's own hashes -- the
  `source/model_names.inc` precedent.
- **The decode is a pure transform, and that is what decided the key.**
  `sub_1004B150` is one opcode step of a small LZ decoder: four literal forms and
  five back-reference forms, with `05` (and `00`-`03`) as the no-progress opcode.
  `sub_1004B570` drives it from the resource's first byte into a 179,536-byte
  (`0x2BD50`) buffer, stops on the no-progress opcode, requires the `05 FA`
  terminator and the exact output length, and checks the Adler-32 trailer the
  stream itself carries (zlib's variant: `a` seeded 1, `b` seeded 0, both reduced
  modulo `0xFFF1`, 5552-byte blocks). Neither function reads the game image, calls
  the game, or allocates anything but the output: the only state they touch is the
  three output pointers (`dword_101A842C`/`30`/`dword_10196374`) and the source's
  lower bound (`dword_101887D4`, the resource's base - 16, which every stream
  position satisfies, so the original's source test is constant-true). On the
  pinned resource the decode is 446 opcodes, ends at `0x27FCE`, and the trailer
  `0x41E794AB` matches the decoded table (`sha256 86d2bb26…`, starting
  `73 63 72 0E`, `fnv1a 0xC883859F`).
- **The registration is the ASI's own.** `sub_1004B750` registers the table as the
  single fake-pak entry (`finale2.sco`, 179,536 bytes) in its own `std::vector` at
  `0x1016E43C`, then the transcribed `sub_1004ACE0` wraps it: a 20-byte header (magic
  `0x86723DFF`, `3`, the entry count, the name bytes + 16 per entry, the words `16`
  and `0xE9`), one 16-byte record (stored size, stored offset, index, page count,
  padding word), the name, then the payload at `0x800` with 688 bytes of zero padding
  -- 182,272 bytes in all, `fnv1a 0xB5AF7B10`.
- **The one game call is a call the original makes through a published pointer**,
  exactly like every other arm's: the wrapper formats
  `"memory:$<archive>,<bytes>,0:"` and passes it to the function the `E8` at
  `match-0x33` names (`0xA22BE0` on 1.0.8.0 -- the device register that walks the
  `0xA0`-stride table at `0x1399810` looking for a free slot), then replays the
  function the site's own `E8` named (`0xACBE60`). Nothing is registered with the
  game that the original does not register the same way.

| piece | VA | instructions | how |
|---|---|---:|---|
| the branch body (the `E9`'s target) | `0x1004C3C0` | 7 | transcribed, proved |
| the mounting wrapper (the `E8`'s target) | `0x1004C350` | 41 | transcribed, proved |
| the decoder's opcode step | `0x1004B150` | 406 | transcribed, proved (`rebuilt.md` §2a) |
| the decode driver + Adler-32 | `0x1004B570` | 159 | transcribed, proved (same) |
| the init (decode, register, build) | `0x1004B750` | 991 | rebuilt as C: EH frame + C++ container helpers (`rebuilt.md` §3 class B), behaviourally pinned by `zp_ooc_init` (`rebuilt.md` §2k) |
| the in-memory archive builder | `0x1004ACE0` | 314 | transcribed, proved, with its twelve helpers (`rebuilt.md` §2h) |
| the key's own handler | `0x1004C3F0` | 4185 (IDA's total 4199) | rebuilt as C: EH4 frame + C++ string builder (`rebuilt.md` §3 class B) |

- **The handler is compiler output, and it is the arm, not the decoder.**
  `sub_1004C3F0` is 16,311 bytes of C++: an EH4 prologue
  (`push -1; push 0x10061940`, scope table in `.data`), a `/GS` cookie, and the
  original's string builder -- every pattern string is assembled one character at a
  time through `std::to_string`-shaped digit loops, `strtol` and `toupper`, which is
  why 4,185 instructions fill its 16,311 bytes (IDA's per-function total is 4,199 with
  the out-of-line funclet). It scans the branch anchor with
  `sub_1005B6A0`, publishes `dword_10158034` = `match+0x11`, `VirtualProtect`s
  `match+7` and writes the `E9` and its displacement to `sub_1004C3C0`; then it
  scans the wrapper anchor, publishes `dword_10159BF8` = the target of the call at
  `match-0x33` (`sub_10001670`: `site + 5 + disp32`) and `dword_10159C04` = the
  original target of the call at the match, and calls `sub_1005BA80`, which writes
  `E8` plus the displacement to `sub_1004C350` and returns the old target. The
  rebuild publishes the same four words in the publish tier and writes the same two
  branches in the install tier.
- **Both transcribed bodies are `/GS`-framed compiler output and are transcribed
  anyway**, because both are plain functions with a stable register contract: the
  wrapper's prologue reads `___security_cookie`, xors it with `EBP` and its epilogue
  calls `__security_check_cookie`, and the proof compares all 41 instructions. A
  byte-level comparison agrees as well: the two bodies are identical to the
  original's bytes apart from the six absolute operands (two resume slots, the
  mount slot, the replayed-call slot, the cookie and the two string literals), which
  are what must differ.
- **The decoder and the builder are transcribed and behaviourally proven.**
  `zp_dump --ooc-selftest` runs the module's own decode, registration and archive build
  against the pinned resource and compares each result with the fingerprint the
  recovered bytes carry: the stream's Adler-32 trailer, the decoded table's
  `fnv1a 0xC883859F`, the archive's 182,272 bytes and `fnv1a 0xB5AF7B10`, and the
  header's `16`/`0xE9` words. It answers `PASS`. Because the decode path is now the
  original's own 565 instructions and the builder its own 314 (plus its twelve
  helpers), that PASS is also a behavioural proof of those transcriptions. The shipped
  ASI never calls the selftest -- the payload path only runs when the game calls the
  wrapper, which no offline tool can reach, so this is the only way the code executes
  at all.
- **The init's own semantics are a test** (`zp_ooc_init`, 54 checks,
  `rebuilt.md` §2k): the selftest proves what the transform produces, not when it
  runs or what a failed decode does. The test drives `OocInitialise` itself and pins
  the guard (`dword_10159C00` has no writer, so three calls build three archives and
  free none), the cursor (`dword_10159C78` = the archive's 182,272 bytes, published
  into `dword_10159BFC`), the registration on a damaged resource (both failure
  classes, no throw) and the entry (`"finale2.sco"`, 179,536 bytes, out of the
  module's own `std::vector`). The shipped ASI never calls the seam either.
- **Two build facts the arm inherits.** The branch anchor (`A9 39 44 24 18`, five
  bytes that start on the last byte of the `mov eax, <magic>` preceding the compare)
  is absent from 1.0.0.0 and present exactly once on the other eight images; the
  wrapper anchor is absent from 1.0.0.0, 1.0.4.0 and 1.0.0.4 and present exactly
  once on 1.0.6.0 and later. A miss is the original's own clean skip, so those
  builds install only the half their image carries.

### The three loading keys closed in this pass

`NewMemorySystem`, `NewFilesCompatibility` and `LoadDLCs` in `source/loading.ixx`.
The first two are new arms; `LoadDLCs` is the earlier attempt's blocker resolved: its
discovery pass is reimplemented, and the EDX contract the attempt could not verify
turned out to be unobservable (measured below, not assumed).

| key | writes on 1.0.8.0 (RVA) | bodies |
|---|---|---|
| `NewMemorySystem` | `E8` at `0x107A` retargeted to `0x10038670` (the opcode is already `E8`, so only the displacement changes), `E9` at `0x107F` to `0x108C` | vtable `0x100B5038`: eleven methods transcribed, three rebuilt |
| `NewFilesCompatibility` | `E8` at `0x496149` and `0x496228` retargeted, `E9` at `0x70A970` | `sub_10051600` 4, transcribed; `sub_10051890` / `sub_100518D0` rebuilt |
| `LoadDLCs` | six fixed-RVA writes per build: `E9` at `0x49040E`->`0x49042C`, `E8` at `0x49043A`->`0x48FBB0`, twelve `90`s at `0x49043F`, `E9` at `0x49035C`->`0x4903B8`, `E8` at `0x48F558`->body, `E9` at `0x4903D4`->body | `sub_10023480` 2, `sub_10023490` 19, `sub_10023150` 223, all transcribed |

**`NewMemorySystem`.** The arm is the original handler's: the allocator-initialisation
call at `0x107A` is retargeted to the vtable installer and a jump at `0x107F` skips
the five-slot clear it replaces, resuming at `0x108C` (anchor
`3D 80 99 99 59 73 05`, RVA `0x109C`). The singleton the installer writes into comes
from the `6A 01 B9 00 00 40 06` anchor's `mov esi, <address>` operand at `+8`
(`0x19CF9F0`), a live address read as captured like every other captured game
pointer ("captured operands are live addresses", above).
The table is the original's 24-slot vtable at `0x100B5038`, read entry by entry:
slots 10 and 12 point at the RGSC-login stub and the savegame null path, which this
unit already owns, and slots 8/9/20/21 at the two-instruction MP-sync stub — the
original's own table shares them, so the rebuild does too. `audio.ixx` owns that
stub's body but keeps it internal to its unit, so the table gets a second copy with
its own proof row. Eleven methods are transcribed (constructor 17 instructions,
reject-pointer 2, self 2, null 2, no-op `ret 0` 1, paired no-op 1, reject 2, sync 2,
slot-18 thunk 5, no-argument no-op 1, installer 5) and the three arena methods are
rebuilt because their registry callees cannot be transcribed: `0x1005C610` (the
`std::_Hash` emplace) **has an exception frame** — `push -1; push SEH_1005C610;
… mov fs:[0], eax` at `0x1005C613`-`0x1005C632` — so it is class B, and the free body
inlines the `std::_Hash` erase unlink over the original's map globals
(`dword_101A8424`/`dword_101A8418`/`dword_101A8410` at `0x100384CC` onward), which a
module's `std::unordered_set` does not expose — `rebuilt.md` §3 class D. The bodies'
own `/GS` frames are not the reason, those are transcribable. The
rebuild reproduces the original's alignment scheme instruction for instruction —
`unit = max(0x10, align)`, `want = max(size, unit)`, `HeapAlloc(want + 2 * unit)`,
align to `((base - 1) / unit + 2) * unit`, store the distance from the base at
`[pointer - 4]`, fill `want` bytes with `0xCD`, register under the critical section at
`0x101A0808` — free recovers the base from that distance and unregisters, and the
membership slot queries the registry. Only membership is observable, so the registry
is a plain set behind an equivalent lock. A failed allocation returns null exactly
where the original's `test edx, edx` does; no guard was added.

That scheme is not argued from the disassembly any more: `zp_nms_arena`
(`tests/nms_arena.cpp`, `rebuilt.md` §2j) drives the three bodies through
`MemoryArena::Get` — the same three functions `PublishNmsVtable` puts in the table —
on the process heap, with no game image, and asserts each clause: 42 `(size, align)`
pairs for the `unit` rounding and `want`, the distance's `[unit, 2*unit)` range, the
`want + 2*unit` block as `HeapSize` reports it, the `0xCD` fill over exactly `want`
(the end made observable by poisoning a block, freeing it and taking it again), the
membership answers, a null free as a no-op, the freed block reused, and the failed
request answering null with the arena still usable — 246 checks. A double free is the
one thing it does not run, and cannot: see the preserved-fault row above.

**`NewFilesCompatibility`.** Gated on the original's two refused neighbours
(`GTAC.dll`, `JacksIVFixes.asi`), in both the publish and the install. The feature has
two halves, and they touch different game systems:

- **The script half.** The publish reads the anchor `68 64 28 C8 67 E8` — the
  `push 0x67C82864` / `call _registerNative` pair just before the end of
  `CScriptCommands::RegisterCoreScriptCommands` (RVA `0x70A954`, the call at
  `0xB0A959`) — and computes `match + 10 + displacement` = `_registerNative`
  (`0x625620` on 1.0.8.0). The install replaces the function's own `ret` at
  `0xB0A970` (0x1C bytes after the anchor's `push`) with the registration tail
  (`pushad` / `call` / `popad` / `ret`, transcribed and proven), which registers
  native command `0x2C89A1F9` with handler `sub_100515D0` (`mov eax,[ecx]` /
  `mov dword ptr [eax],1` / `ret` — it marks its first argument and returns it).
- **The radio half.** The install retargets the two per-slot update calls in the
  `RadioLogo<N>` TXD loaders (`sub_8960B0` for a station's first load, `sub_896180`
  for a reload), whose pristine targets `sub_8936A0`/`sub_893680` are ten
  instructions each: `if (name[0]) SetTexture(sprite, name); return name[0] != 0;`.
  The two rebuilt update bodies keep that shape and
  the same ABI (`__fastcall`, the dictionary/sprite in `ECX`, the hash/name on the
  stack, plain `ret`, the answer in `AL`; the site's caller discards it — the next
  instructions at `0x89614E` are `mov esi,[esi]` / `test esi,esi`) and insert a
  membership test before the call: a sprite whose name the just-loaded TXD does not
  contain keeps its previous texture, where the pristine call clears it. Their own
  stream is transcribable but three of their four callees are TLS-guarded scans
  (`rebuilt.md` §3 class C) and two would need `__fastcall` entry shims, so they
  stay class D there. They hash the file name through the transcribed resolver
  `sub_10003E50` (`Shared::ValueResolver()`), not a C++ hash, so a `hash:NNNN` name
  resolves as it does in the original.

The three scan-resolved helpers are `g_pCurrentTxd` (`8B 35 …`, the current texture
dictionary), `rage::pgDictionary<grcTexture>::Lookup` (`85 C9 53 55 56 57 74 40 …`,
falling back to the older builds' `53 55 8B 6C 24 0C 56 57 EB 06`) and
`CSprite2d::SetTexture` (`8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0` minus `0x14`); no
"file registry" is involved, and the "append" is a texture set. All three keep the
original's unguarded behaviour: a scan miss reads or calls through the null it left.
`rebuilt.md` §2m identifies each against 1.0.8.0, carries the per-image resolution
table, and records why the three cannot be driven by a synthetic test — their object
is the game's live TXD, not a parameter.

**`LoadDLCs`.** The original handler does not scan. It switches on the build id and,
for each of 1.0.6.0, 1.0.7.0 and 1.0.8.0, stores three game entry points and rewrites
six sites at fixed RVAs with instructions it computes in place. The rows below were
read from the handler's own `cmp eax, 7/8/9` blocks (`lea edi,[eax+RVA]`,
`lea ebx,[eax+RVA]`, `add esi,RVA`, and the body stores `mov eax, 0x10023480/90`)
and the sites were checked against `GTAIV_1060.exe`, `GTAIV_1070.exe` and
`GTAIV.exe`: each carries the shape its row replaces. The arm therefore writes at
fixed RVAs rather than scanning — the reference tree's per-row signature scans would
silently skip a write the original always performs — and the key's build mask is
exactly those three builds.

| build | register | apply | loader callback | sites |
|---|---:|---:|---:|---|
| 1.0.6.0 | `0x4136E0` | `0x414080` | `0x414560` | `0x41450E`, `0x41453A`, `0x41453F` (12 nops), `0x41445C`, `0x4137CD`, `0x4144D4` |
| 1.0.7.0 | `0x413480` | `0x413E20` | `0x414300` | `0x4142AE`, `0x4142DA`, `0x4142DF` (12 nops), `0x4141FC`, `0x41356D`, `0x414274` |
| 1.0.8.0 | `0x48F440` | `0x48FE00` | `0x490460` | `0x49040E`, `0x49043A`, `0x49043F` (12 nops), `0x49035C`, `0x48F558`, `0x4903D4` |

Four of the twelve filled bytes are inside a `BRANCH` classification in the scoreboard
(the pristine bytes under the fill contain `E8`s), so `parity_report` compares seven of
the key's bytes; all twelve are written.

The two bodies are transcribed and proven: the loader-callback replacement
(`0x10023480`, 2 instructions, `mov al,1 / ret 8`) and the DLC manager's
initialisation tail (`0x10023490`, 19). The tail clears five request slots, and on
the first call runs the loader callback and the discovery pass before setting the
loaded byte at `+0xA4`; its epilogue is the game function's own, so it returns to
that function's caller. The discovery (`sub_10023150`) is transcribed too, and
proven whole (`dlc_discovery`, 223 instructions): the `0x104`-byte module-path buffer
allocated unchecked and never freed, `GetModuleFileNameA` + `PathRemoveFileSpecA`, the
two named folders (TLaD id 2, TBoGT id 4, the `\dlc\…\content\` layout first and the
legacy `\TLAD\` / `\TBoGT\` fallback second, with the second `PathFileExistsA` probe
the original makes even when the primary existed), then 252 probes `\Episode3\` …
`\Episode254\` whose ids are `2n`, each registration building the `0x168`-byte
request (path at `+0`, mode dword `1` at `+344`, flag `1` at `+358`) and calling the
published register and apply entries, and finally the object's `+0x166` byte set to
`2`. It was the one rebuilt body in this key with no EH frame, no TLS static-init
guard and no indexed absolute operand, so it is the original's own instruction
stream; `rebuilt.md` §2 records how it was verified and what stays unproven.

*The EDX contract, measured.* The earlier attempt refused the key because it could not
verify what `sub_10023150` must leave in EDX. It leaves whatever its last callee left:
after its final call the only instructions are `mov eax,[esp+0x10]`, three pops,
`xor ecx,esp`, the `mov byte [eax+0x166],2` store, `call __security_check_cookie`
(`cmp ecx,cookie / jne / ret` — ECX and flags only), `mov esp,ebp`, `pop ebp`, `ret`.
The body passes EDX through untouched to its own `ret`, which returns to game
`0x4931FF` — the instruction after the only call to the patched function
(`call 0x890320` at `0x4931FA`). That instruction is `call 0x8CCCD0`: neither `EAX`
nor `EDX` is read, and neither image mentions `0x890320` anywhere else (checked as a
branch target and as a dword), so the value is unobservable. The transcription runs
the same calls in the same order, so it ends on the same value on every path (the
game's apply entry on a hit path, `PathFileExistsA` on a miss path); the formatter's
own EDX is always overwritten by the `strcat_s`/`PathFileExistsA` that follow it. The
one piece of the reference's model that can be checked statically is `sub_10001280`:
it loads `edx = [ebp+0xC]` (its size argument, `0x40`) before its call and never
reloads it, so if it ever were the last callee the value would be `0x40`.

The register entry is a `__thiscall` that the original calls without setting `ECX`
(left over from the preceding `strcpy_s`); with its flag argument non-zero it skips
the only branch that would use `this`, and the one call that still reads it is the
loader callback this key replaces. The transcription leaves `ECX` exactly where the
original leaves it, instead of inventing a `this` — recorded above under preserved
faults.

### `CutsceneFixes`, and why the proof can compare its constants

The key was recorded here as over budget because its body reads sixteen absolute
operands the proof cannot decide: five float/double constants, the frame-time
accumulator, two game pointers, a `VirtualProtect` import slot and the loader
salt. That is a limit of the *proof*, not of the transcription.
`verify_bodies.py` compares an absolute operand as a **slot**
(`movss xmm0, [abs]` on both sides, whatever the address), so a body that reads
its own module slots is provable instruction for instruction; what the arm owes
is the *values*, published at the VAs the original reads them from. The key is now
closed on every image the scoreboard can measure (1.0.0.0 stops earlier, at
`NoImpostorHeightCutoff`'s preserved fault).

The key is two halves of one chain in the original, run by two loader arms under
one option:

| piece | original | arm |
|---|---|---|
| chain B's body | `sub_100388C0`, 146 instructions / 592 bytes | transcribed, `Bodies::Register(..., 0x100388C0)` |
| chain B's install | worker `sub_10038B40`: scan 1 at `0x1003B8E0`, scan 2 at `0x1003D1E8`, an `E8` at match-5 in both | two `injector::MakeCALL(match - 5, ...)`, one per recovered signature |
| chain B's publishes | `dword_1015BDE4 = match + [match-4]`, `dword_1015BDF0 = that + 0x4F` | `PublishCutsceneFixes`, before the installs |
| chain A's install | worker `sub_1003DA50` (two build-variant texts) publishes the match; callback `0x10044160` writes one `EB` there | `find_pattern` first text, fall back to the second, `WriteMemory<std::byte>(..., Bytes::ShortJmp)` |

The body's slots, and where each value comes from:

| slot | original VA | value |
|---|---|---|
| `zpatch_cutscene_target` | `dword_1015BDE4` | published: the site's `E8` destination (1.0.8.0 game `0xC9A640`, the camera function) |
| `zpatch_cutscene_poke` | `dword_1015BDF0` | published: that + `0x4F`; the body's `dec esi` lands the store on the immediate at + `0x4E` |
| `zpatch_cutscene_virtual_protect` | `0x10063010` (the ASI's IAT slot) | the module's own `&VirtualProtect` |
| `zpatch_cutscene_accum` | `dword_1015BDE8` | .bss double, zero at load |
| `zpatch_cutscene_zero` | `0x101522F0` | `0x0000000000000000` (0.0) |
| `zpatch_cutscene_limit` | `0x10152340` | `0x3FD554C985F06F69` (0.3333) |
| `zpatch_cutscene_abs_mask` | `0x101523D0` | `0x7FFFFFFF` |
| `zpatch_cutscene_eps_a` | `0x101522F8` | `0x3D4CCCCD` (0.05f) |
| `zpatch_cutscene_eps_b` | `0x101522FC` | `0x3F000000` (0.5f) |
| the native salt | `dword_1015803C` | the layer's resolved salt (`Native::Salt`), `0x022B2DA9` on a probe hit |

The `value` column above is the original's .rdata, i.e. the provenance of the slot;
in the source the five float/double slots are declared as the numbers those bits
encode (`0.0`, `0.3333`, `0.05f`, `0.5f`), each literal round-tripping to exactly
the bytes listed, and only `zpatch_cutscene_abs_mask` stays a hex word because it is
an `andps` sign mask rather than a value.

Two call targets differ from the original's, both inside the proof's
call-is-an-immediate rule: the body's `call 0x10003980` names a six-byte `jmp`
forwarder through the native layer's published resolve entry, because the
original's target is the layer's C entry (`Native::Resolve`, rebuilt from
`sub_10003980`) and a module interface cannot export a naked function to call
directly; and the two epilogues `call __security_check_cookie`, which is the
CRT check the original's own `E8` names (`0x1005FE7B`).

What the sweep covers. The row's 146 instructions run to the `ret 4` at
`0x10038B0D` (that is exactly 592 bytes); the restore block at
`0x10038B10..0x10038B3F` follows that `ret` and is reached only by the six `ja`
exits, so the linear sweep stops before it — the same boundary the original's own
`ret` sets. Everything before it is compared: the four `rep movsd` block copies
the sweep reaches (the body's fifth is in the restore block), both
`fstp`/`ucomisd`/`lahf`/`jnp` result tests, the sixteen absolute operands and the
`comiss` cascade. Swept past the `ret` with the same comparator, the restore block
compares clean too: 161 instructions and `0x280` bytes, the original function's own
length, so the short/near jump choices match as well.

The scoreboard can only see one of the key's nine changed bytes — the `EB` —
because the other eight are the two installed `E8`s' displacements, which the
report excludes by design. Closure is therefore `missing 0, extra 0, differing 0`
on `0x89A840`'s `EB`, backed by the body proof for the behaviour behind it and by
`tests/signatures.tsv` for the three anchors.

### `HighQualityReflections`, and the site arithmetic that closed it

The key had been recorded as self-contradictory: the handler publishes
`resume = esi + 0x13` and `builder = esi + dword(esi + 0x0F) + 0x13`, and with
`esi` read as the `E9` site the resume looked like the last byte of a five-byte
`call` at `site + 0x0F` while the builder read the `E8` opcode as a displacement
and wrapped. The bytes say that premise was one byte off: the call's opcode is at
`site + 0x0E`, not `site + 0x0F`.

The site, in the pristine 1.0.8.0 image at `0xC51311` (the only hit of
`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`):

```
0C51311  8B 87 38 09 00 00    mov eax, [edi+938h]     ; 6 bytes, the E9 lands here
0C51317  6A 08                push 8
0C51319  68 1D 13 00 00       push 131Dh
0C5131E  50                   push eax
0C5131F  E8 4C EA F1 FF       call 0B6FD70h           ; 5 bytes, site+0x0E..site+0x12
0C51324  6A 00                push 0                  ; site+0x13, an instruction boundary
0C51326  6A 10                push 10h
```

So `site + 0x0F` is the call's first displacement byte (`4C`), not its opcode:
`dword(site + 0x0F) = 0xFFF1EA4C`, and `site + 0x13 + 0xFFF1EA4C = 0xB6FD70`, the
call's target. `site + 0x13` is the `push 0` after the call -- an instruction
boundary -- while `site + 0x14` is the second byte of that `push`, mid-instruction.
A scanner that returned match+1, making `esi` the `site + 1` and the resume
`site + 0x14`, would therefore land the body in the middle of an instruction.

The scanner settles it independently. `sub_1005B6A0(signature, module)` returns the
first element of the hit vector, or zero when the scan is empty:

```
1005B6CC  mov eax, [ebp+Block+4]
1005B6D2  mov edx, [ebp+Block]
1005B6D5  sub eax, edx
1005B6D7  sar eax, 2
1005B6DA  test eax, eax
1005B6DC  jz short loc_1005B6E2
1005B6DE  mov esi, [edx]          ; the first vector element
1005B6E2  xor esi, esi
1005B71D  mov eax, esi
1005B72A  retn
```

and the element the scan stores (`sub_10003290`, the parser/scanner reached from
the constructor `sub_1005B7D0`) is the match start, not match+1:

```
1000357B  mov edx, 1
10003580  sub edx, [ebp-74h]      ; 1 - pattern length
10003583  mov [ebp-64h], edx
...
100035C1  lea eax, [edx+esi]      ; current byte - (length - 1) = first pattern byte
100035CE  mov [ecx], eax          ; hits.push_back(hit)
```

So on a hit the helper answers the match pointer itself, `0xC51311`, not
`match+1`.

The handler E9s the same site the scanner returned (`sub_1005B5D0` writes the `E9`
at `ESI`) and publishes:

```
10022C89  call sub_1005B6A0
10022C8E  mov esi, eax
10022CA0  call sub_1005B5D0       ; writes the E9 at ESI
10022CA5  lea ecx, [esi+13h]      ; resume
10022CA8  mov dword_10159CFC, ecx
10022CAE  mov eax, [esi+0Fh]      ; the call's rel32
10022CB1  add eax, 13h
10022CB4  add eax, esi
10022CB6  mov dword_10159D04, eax ; the call's target
```

The alternative was never needed: with `esi` = the match / `E9` site, the resume is
match+0x13 (after the call at match+0x0E) and the builder is match + [match+0x0F]
+ 0x13 = the call target. The key is implemented that way: the arm scans the
signature, publishes `match+0x13` and `match+0x13 + [match+0x0F]`, registers the
detour at `0x1001F030`, and writes the `E9` at the match. The body (`0x1001F030`,
157 instructions / 858 bytes) replays the block's twelve draw-command-builder calls
with `(index, 0x11D, 0)` and tail-jumps through the published resume; its row is
`hqr_body` in `tests/bodies.tsv`, and `tests/signatures.tsv` records the site.

The shape mismatch that started the question is real but harmless: the `E9` covers
five bytes of a six-byte `mov`, leaving the `mov`'s last operand byte at `site+5`
unexecuted, exactly as the original leaves it. The scoreboard records the key's five
changed bytes at `0x851311..0x851315`, but only the `E9` opcode counts: the other
four are the displacement, which the report excludes. Closure is `missing 0,
extra 0, differing 0`.

### `NewSavesCompatibility`, closed on its rebuilt converter

This was the last key held open by an implementation gap rather than by the
baseline's own recording. The handler (`0x10052200`, 170 bytes) only scans two
anchors and writes the jump; the work is behind the jump, in two compiled-C steps
and the two hooks the converter installs.

**The handler's two scans.** `68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08` lands
on `push 0x11D0380` at game `0x857CAF`; the pushed operand is the game's save-name
buffer, which the game fills from `sprintf(buffer, "%s%02d", "SGTA4", slot)` - the
format at `0xD85E40`, the prefix getter returning `0xD85F80` - so the name a load
sees is `SGTA400`..`SGTA412`, the file names in the user's savegame directory. The
second scan lands on `0x85B093`:
`89 84 24 E0 01 00 00` (`mov dword ptr [esp+1E0h],eax`), `8B 45 08`
(`mov eax,dword ptr [ebp+8]`), `53` (`push ebx`), `33 DB` (`xor ebx,ebx`). The arm
publishes `match+0xD` as the resume and writes `E9` to `0x100521E0` - exactly the
five bytes the baseline records. The write is gated on the *first* anchor because
the original returns before its second scan when that one misses
(`jz loc_10052299`, `0x1005222B`); 1.0.0.4.0 carries the second anchor but not the
first, so the original writes nothing there and neither does this tree.

**The trampoline** (`0x100521E0`, 8 instructions, transcribed and proven as
`new_saves_tramp`):

```
60                     pusha
E8 <prepare>           call sub_10051FD0
61                     popa
89 84 24 E0 01 00 00   mov dword ptr [esp+1E0h], eax
8B 45 08               mov eax, dword ptr [ebp+8]
53                     push ebx
33 DB                  xor ebx, ebx
FF 25 <resume>         jmp dword ptr [dword_1015AD88]
```

**The converter** (`sub_10051B30`, 337 instructions in its own bytes; IDA's total is
363 with the out-of-line EH blocks) is ordinary file IO, rebuilt
in C++ with the original's call sequence and faults:

- `fopen_s(&Stream, path, "rb")`, `fread(Buffer, 0x110, 1, Stream)`, then
  `cmp dword ptr [eax], 45564153h` (`0x10051BD1`) on the dword at offset `0xC`
  - "SAVE";
- eighteen iterations of `fread(&chunk, 9, 1, Stream)` (`0x10051C01`),
  `mov eax, [ebp-13Bh]` (the dword at `+5`), `add eax, 0FFFFFFF7h` (size - 9,
  `0x10051C14`), `call operator new`, `fread(block, 1, size-9, Stream)`, `free`.
  Three of them are test-and-stored: `cmp dword_1015AD90, 4C45h` ->
  `byte_1015AD98` (iteration 9), `cmp ..., 1741h` -> `byte_1015AD99` (13),
  `cmp ..., 0E77h` -> `byte_1015AD9A` (16);
- `fclose`, then the three site patches, each site resolved once through a
  function-local static - the original's `_Init_thread_header` / `_footer` guards.

The patches, with the original's own instructions:

| site | condition | write | original instructions |
| --- | --- | --- | --- |
| game `0x872185` (`74 37 80 3D ...`) | chunk 9 has the newer size | `74` (`jz rel8`) or `EB` (`jmp rel8`) | `mov ecx,0EBh` / `mov eax,74h` / `cmovnz eax,ecx` / `mov [esi],al` (`0x10051D35`-`0x10051D42`) |
| game `0x7D8C50` (`68 BE 0C 00 00 50 E8`) | always | `E8` at `+6` + call to `0x10051A90`; `0CBEh` or `0E6Eh` at `+1`; `75 26` or `90 90` at `-5` | `mov byte [esi],0E8h` (`0x10051DD7`) / `mov [edi], sub_10051A90-esi-5` (`0x10051E00`) / `mov ecx,0E6Eh` `mov eax,0CBEh` `cmovnz` `mov [v21],eax` (`0x10051E3B`-`0x10051E4A`) / `mov ecx,9090h` `mov eax,2675h` `cmovnz` `mov [v24],ax` (`0x10051E6E`-`0x10051E7B`) |
| game `0x8DA090` (`56 E8 ... E8 ... A1 ...`) | always | `E8` at `+6` + call to `0x10051AD0`; the read entry republished from the later call | `mov byte [esi],0E8h` (`0x10051F08`) / `mov [edi], sub_10051AD0-esi-5` (`0x10051F31`) / `mov ecx,[esi+1Eh]` `add ecx,1Eh` `mov eax,[ecx]` `add eax,4` `add eax,ecx` `mov dword_1015AD94,eax` (`0x10051F53`-`0x10051F62`) |

**The prepare step** (`sub_10051FD0`, 157 instructions) is rebuilt as well and
needs only what the original needs: the save-name buffer the handler captured
(`cmp Src,0` / `jz`, `0x10051FFA`), the game's own save-directory function (found
by `55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28` and called as
`push &suffix; push buffer; push 0; call eax` into the 312-byte `byte_101A8308`
buffer - 312 is the gap to the next `.bss` object at `0x101A8440`), and then the
path: `strlen(directory)`, a `\` (written as the word `5Ch`), `strlen(Src)`,
`memmove`, `call sub_10051B30` with the copy.

**The two hooks** are transcribed with their exact frames: `sub_10051A90`
(`sub esp,0E7Ch`; `call dword_1015AD94` with its own buffer and the caller's size,
the caller's buffer never touched, the call's value returned) and `sub_10051AD0`
(`sub esp,10h`; `cmp byte_1015AD99,0` before and after `call dword_1015AD8C`, with
a twelve-byte `push 0Ch` read on both sides and the second read's value returned
when the block is present). Both were C++ rebuilds until they were proven
(`rebuilt.md` §2).

| piece | VA | instructions | how |
| --- | ---: | ---: | --- |
| load trampoline | `0x100521E0` | 8 | transcribed, row `new_saves_tramp` |
| prepare step | `0x10051FD0` | 157 | rebuilt: TLS static guards, `std::string`, EH frame (`rebuilt.md` §3 class B) |
| converter | `0x10051B30` | 337 (IDA's total 363 with the out-of-line EH blocks) | rebuilt: CRT file IO, `operator new`, TLS guards (`rebuilt.md` §3 class B); its walk, three flags and three patch values are behaviourally proven by `zp_save_walk` (`rebuilt.md` §2i), its writes are not |
| read hook | `0x10051A90` | 20 | transcribed, row `new_saves_read_hook` |
| second-load hook | `0x10051AD0` | 33 | transcribed, row `new_saves_second_hook` |

**What is measured.** The trampoline proves whole (8 of 8 instructions). All six
signatures occur exactly once on every image that carries them (`GTAIV.exe`: name
`0x857CAF`, load `0x85B093`, branch `0x872185`, read `0x7D8C50`, reload
`0x8DA090`, directory `0x61E7D0`); the name anchor is absent from 1.0.0.4.0 and the
branch anchor from 1.0.0.0, 1.0.4.0 and 1.0.0.4, which is why 1.0.0.4.0 writes
nothing while the older builds still write the jump. The scoreboard cell is the
five bytes at `0x45B093` (`E9` plus the four displacement bytes the report
excludes): `missing 0, extra 0, differing 0`, and the other eight stored builds
keep the key closed too.

The converter's walk is no longer replayed by a script: it is `zp_save_walk`
(`tests/save_walk.cpp`, `rebuilt.md` §2i), 258 assertions. On both real saves in
`game_refs/saves/GTA IV/savegames/user_ee000000deadc0de` (`SGTA400`, `SGTA401`) the
test's own parser reads `SAVE` at `0x0C` and the eighteen `BLOCK` headers, and the
walk answers the same eighteen tags and sizes at the same file offsets; chunks 9 /
13 / 16 measure `0x3AB1` / `0x1729` / `0x0CC7`, so the three flags are `0` and the
converter restores `74`, `0CBEh` and `75 26` - the older layout, exactly what the
original's three `cmovne` pairs produce. A synthetic newer layout (chunk 9 `0x4C45`,
13 `0x1741`, 16 `0x0E77`) answers `EB`, `0E6Eh`, `90 90`; a synthetic save built
from the real one's ids and sizes answers the old triple again; and a save whose only
newer chunk is 13 answers the old triple too, which is what keeps chunk 13's flag
from moving the three values. Both preserved faults are asserted (see the table
above): the size-underflow `std::bad_alloc` by type, in-process, at chunk 0 and at
chunk 9, and the unchecked header read as "a file carrying the magic is not rejected
on its header" plus a fresh process's answer for a file shorter than the magic offset.

**What is not measured.** The conversion path runs only when the game loads a
savegame, so no offline harness executes the three patch writes or the two hooks. The
test proves the *decision* — which of the three values a given file selects — and not
the writes: that `0CBEh`/`75 26`/`74` land at game `0x7D8C50`/`0x872185` is the
scoreboard cell's and the body proof's business, and the two hooks' frames and the
prepare step's path building still need an in-game save load.

### Still blocked

- `FreezeCarFix` needs its 1,421-byte probe and the spawn path around it; the steer
  consumer `sub_10004C70` it also touched is now in place (see the section above).

### Captured operands are live addresses

A captured pattern operand -- a dword read out of a `mov`/`cmp`/`push` operand and
cast to a pointer -- is an address of the image **as the loader mapped it**, not of
the file. `GTAIV.exe` carries `DYNAMIC_BASE` (`DllCharacteristics` `0x140`), so the
loader relocates the executable on essentially every run; `ZolikaPatch.writes.log`
shows live bases `0x4B0000` and `0xA60000`, and the crash dumps' `in GTAIV.exe
(+offset)` pairs imply `0x420000`, `0x4D0000`, `0x8B0000`, `0xCC0000`, `0xDE0000`,
`0xFC0000`. By the time any arm runs, every `.reloc` entry has been applied, so the
dword in the instruction already names the live object.

The tree used to convert such an operand again, `hostBase + (value -
OptionalHeader.ImageBase)`, on the premise that the game sits at its preferred base
(it does not). In a relocated game that moves every pointer by the run's whole
relocation distance: `SpeedupBugFix` wrote eight dwords through mis-based pointers,
`IncreaseVehicleModelLimit` wrote `0x400` through one, `BadPlayerModelCrashFix` read
its ped array through one, and `MouseFix` read three axis words through them. The
offline dump was *correct* under that formula, which is why no check caught it.

Two halves fix it, and they must stay together:

- **The arms treat the operand as the address it is.** `GamePointer` /
  `GameImagePointer` are identity casts (`loading.ixx`, `vehicles.ixx`, `misc.ixx`);
  the two inline rebases (`InstallNoPhysicsSleep`'s threshold, `CrashMsgFix`'s thunk)
  and `WritableImageWord`'s range test use the live address too, and the two
  `GameAddress` helpers were deleted where a plain read reads better
  (`stability.ixx`'s ped array, `audio.ixx`'s three MouseFix words). This is the
  behaviour the original has and `readable/` uses; the arm-written bytes are
  unchanged in the dump.
- **The dump relocates its copy at a pinned base.** `tests/dump_main.cpp` maps the
  image at the image's preferred base when that address is free and otherwise at the
  fixed `kFixedImageBase` (`0x30000000`), so the delta is one of exactly two
  constants (`0` or `0x2FC00000`) and never the allocator's choice; it applies every
  HIGHLOW entry with `delta = hostBase - OptionalHeader.ImageBase` and refuses any
  base it did not pin. It refuses a file with no `.reloc`, a `.reloc` that runs past
  the buffer, a fixup outside the buffer, a malformed block and a non-HIGHLOW entry
  type -- loudly, with the RVA -- instead of skipping them. Every dump run then
  asserts the property that was violated: at `IncreaseVehicleModelLimit`'s anchor,
  the unrelocated `mov ecx, imm32` operand must become `hostBase + RVA`, the pointee
  must be a writable dword of the buffer, and a byte-equal result fails the run.

The dump's rows still speak the baselines' coordinate system: `0xD0000000` for a
substituted published pointer, and the *original* image's bytes for every byte a
mod wrote, so a relocated dword the loader moved is not reported as a write set.
Building 9's per-key file is byte-identical before and after the change (12,898
lines, 0 differing lines), and `tools/parity_report.py 9` still reports 98 closed /
0 open / 1 oracle-verified.

**A signature may not spell out a relocated dword.** The same class of bug sits
above the arms: a recovered signature that bakes in an absolute address matches the
file image and never the running game, so the arm silently never fires. Two were
found and fixed against the oracle's own strings:

- `IncreaseVehicleStructLimit` scanned `68 34 EE DA 00` (the narrowed 1.0.8.0 push
  operand). The original's own pattern is `68 ? ? ? ? 6A 32 8B C8` (in the ASI at
  file offset `0x12AF74`, handler `0x10023020`), with the pushed name wildcarded and
  the write at `+6` (the `push 32` count). Its one occurrence on each of the nine
  images means the arm now writes the row every baseline already records:
  `0x64` at `0x6C79C1` / `0x6717A1` / `0x5BD661` / `0x7EA641` / `0x7EA871` /
  `0x64A2B1` / `0x754131` / `0x753B91` / `0x614C71` (builds 1, 5, 6, 7, 8, 9, 11,
  12, 13; build 9 was already closed, the other eight close with this) -- exactly
  the row the oracle's own install writes there.
- `EpisodicVehicleSupport`'s two flag stores scanned `54 D7 12 01 02 ...`, the game's
  absolute `dword_1012D754`. The original does them as four fixed-RVA installs
  (build 8: `0x21500`, `0x7D91AE`; build 9: `0x95040`, `0x684ABE`), so the arm now
  writes `ImageBase() + RVA` exactly as the recovered table says. This is also what
  restores the eight placeholder bytes the build-9 cell had lost.

`tools/check_relocated_literals.py` (ctest `zp_relocated_literals`) is the general
guard: for every literal `find_pattern` in `source/*.ixx` it finds the hits in each
of the nine images, then fails if a *concrete* byte of the hit lies in the high half
of a HIGHLOW dword (the half a 64 KiB-aligned delta moves) -- i.e. the pattern spells
out an address. It reports the file, line, arm and the offending pattern; with
`68 34 EE DA 00` restored it fails on `vehicles.ixx:2343
(InstallIncreaseVehicleStructLimit)` and names the dword, and it currently passes
over 1,704 pattern hits.

## Transcribing a body

A body-bearing key is committed only when all four parts exist — plus a fifth when the
body reads a table through an indexed absolute operand:

1. **The body's asm**, transcribed into the owning module's tail, with the recovered
   VA in a comment. A naked function in a module interface unit must be `static` at
   module scope: in a namespace, or with external linkage, MSVC rejects it. Each
   module reads in one order — the header, the published state with the original's
   addresses, the plain C++ helpers, `Publish<Key>`, `Install<Key>`, `Register<Key>`,
   then a marked *Transcribed bodies* tail holding every `__asm` block — so the intent
   is read before the instruction stream. Readable code above a body's definition
   names it through a plain `static` prototype (`static void Foo();`): MSVC rejects
   `__declspec(naked)` on a declaration (C2488, `'naked' can only be applied to
   non-member function definitions`), so only the definition in the tail keeps the
   attribute. The prototype changes neither linkage nor code generation — the body is
   still `static` at module scope — and the arm's registration keeps the body's
   address.
2. **The published module words** the body reads, written *before* the site is
   installed: the original reads a site's original bytes for its resume, so
   publishing after the write resumes into the jump itself.
3. **The registry entry** `Bodies::Register(&body, recoveredVa)`, so the dump can
   express the branch in the original's coordinates.
4. **The proof row** in `tests/bodies.tsv` — name, recovered VA, symbol resolved from
   the linker map, instruction budget — and `zp_body_proof` must PASS. A body whose
   stream cannot be compared under the proof's operand-slot rule is rebuilt in C++
   instead, and then has no proof row: an exception frame, a TLS static-init guard, or
   a call graph whose callees cannot be transcribed. `[rebuilt.md](rebuilt.md)` lists
   every such piece with its blocking instruction pattern; `SpeedupBugFix`'s body,
   `OutOfCommissionFix`'s init and handler, the save converter, the NewFiles updates
   and the NewMemorySystem arena methods are the current examples (the OOC archive
   builder and the steer consumer `sub_10004C70` were on that list and are now
   transcribed). A `/GS`
   cookie is **not** a blocker: the tree transcribes those. A rebuilt
   piece whose result is data rather than a side effect can be pinned a second way,
   without the game: `OutOfCommissionFix`'s decode and archive build run from
   `zp_dump --ooc-selftest` and are compared with the fingerprints the recovered
   bytes carry (that key's section). The decode half is now the original's own
   instructions, so that selftest also proves the transcription. A rebuilt piece whose
   result is state that later calls read can be pinned by driving it, which is what
   `zp_save_walk` does for the converter's decision, `zp_nms_arena` for the arena's
   scheme, distance word, fill, membership and free path, and `zp_ooc_init` for the
   OOC init's re-run, its cursor, its registration on a failed decode and its entry
   (`rebuilt.md` §2i, §2j, §2k). The three `NewFilesCompatibility` scans mark the
   limit of that rule: their result is the game's live TXD state and a call into the
   game's own sprite code, not a value this module computes, so `rebuilt.md` §2m
   records the identification and the live check that has to stand in for a test.

**An indexed absolute table read is no longer a rebuild reason.** If the body reads a
table through `[table + reg*4]` (or another indexed absolute form), the fifth part is
the registration, and the body is transcribed with the operand absolute:

5. **The table registration** — `Bodies::RegisterTable(&table, originalVa, sizeof
   table)` next to the arm's `Bodies::Register` calls, the table's recovered contents
   in the module in the original's order, and the matching `table <VA> <symbol>
   <bytes>` row in `tests/bodies.tsv`. The comparator then compares the operand as
   `table:<content hash>`, so the row proves the body reads the *same table*; a
   transcription that reads a different registered table fails with
   `table contents differ`, and a displacement no row names still compares literally.
   `0x10003F00`'s `bpm_model_name` row is the worked example (`rebuilt.md` §2b).

**Independent byte audit (2026-11-07).** A second implementation — its own PE reader, map
reader and comparator, which charges every differing byte to a *slot* (a memory operand's
absolute displacement), an *immediate* (a `call`/`push`/`mov` value naming our own code or
storage) or a registered-*table* displacement and fails on anything else — re-audited all 151
proof rows and found every one of the 2,965 differing bytes accounted for (717 slot fields /
2,135 bytes, 241 immediate fields / 674 bytes, 57 table fields / 156 bytes, 0 unclassified),
with 314 internal branch targets landing on the same relative offsets, all nine registered
tables resolving through the linker map at their declared length, 33 pushed string literals
identical, 614/614 injected mutations detected, and its one finding — two `BenchmarkFix` slot
comments naming the EmissiveLerp slots — a documentation defect, now fixed; the report is
`../../.agents/rewrite/06-body-audit-full.md`, outside this repository.

## What the scoreboard can and cannot compare

`parity_report.py` compares a key's changed bytes against the stored baseline and
excludes **branch displacement bytes**: those baselines wrote one stand-in body
address for every arm, so the four bytes of a displacement are meaningless there.
The set of displacement RVAs is derived from the pristine game image (an `E8`/`E9`
opcode one to four bytes before the RVA), which is the only source that works for an
arm installed over a site that was already a branch.

Because of that exclusion the scoreboard proves bytes but not body identity: an arm
that registers the wrong reference VA still matches. Body identity comes from
`tests/body_targets.tsv` (the arm names its body, the table names its VA) and from
the instruction-level proof.

`verify_bodies.py` sweeps each body linearly for exactly the row's instruction
count, so the manifest budget is the body's true length, not a cap: the budget is
the count to the first `int3` in the original. A row that is short would silently
truncate the proof on both sides, and a row that is long reads into padding (and
fails if either side decodes fewer instructions), which is why the number is part
of the evidence rather than a hint.

Body identity is instruction-level except for a call's target: the proof compares
a `call` as an immediate, not as the function it names. Several implemented bodies
call our equivalent helper instead of the original's compiled target:

- `ped_models_in_cutscenes` calls audio's `ValueResolverThunk`, a
  one-instruction `jmp` through the published resolver slot, because the resolver
  (`ValueResolverBody`) is `static` in `shared.ixx` and an external-linkage naked
  function in a module interface unit is rejected (C2488), so an importer cannot
  emit the original's direct `E8` to it;
- `local_settings_detour` and `local_settings_fallback_detour` call our
  `LocalSettingsStep`, a reimplementation of the original's compiled helper;
- `savegame_fix_copy` calls `SavegameCopyBlock`, which forwards to `std::memcpy`;
- `native_call_bool` and `native_call_hash` call `NativeZeroBlock`, a cdecl
  forwarder to the CRT `memset`, because the inline assembler rejects the intrinsic
  name in a direct call (C2420);
- `driveby_fov_helper` calls `DrivebyResolveEntry`, a six-byte `jmp` through the
  native layer's published resolve entry, because the layer's C entry is not
  nameable from inline asm in another module unit.
- the `BadPlayerModelCrashFix` bodies call `BpmZeroBlock` (a cdecl forwarder to the
  CRT `memset`), `BpmFormat` (the transcribed `0x10001280`, row `bpm_format`),
  `BpmModelNameBody` (the transcribed `0x10003F00`, row `bpm_model_name`, through the
  `BpmResolverEntry` forwarder) and five one-instruction `jmp` forwarders
  into the layer's pieces. The forwarders are the same shape as `DrivebyResolveEntry`
  and for the same reason; both the formatter and the name lookup are proven, and the
  name lookup's table is registered (`rebuilt.md` §2b);
- `cutscene_chain_b` calls `CutsceneResolveEntry`, the same six-byte forwarder shape
  into the native layer's resolve entry (the original's `E8` to `sub_10003980`), and
  its two epilogues call the CRT's `__security_check_cookie`, which is the function
  the original's `E8` names.

The stored baselines have a gap of their own: they were produced by the
reference tree before its `ApplyCarDynamicShadowFixNoE` pass was wired into the
installed arms, so on the builds that fill runs the baseline lacks the six-byte NOP
fill this tree writes from the recovered `@noE` row. `CarDynamicShadowFix` therefore
reports six extra bytes on 1.0.0.0 and 1.0.4.0; it is a baseline omission, not a
difference in the bytes both sides write, and the oracle-verified class (below)
excuses exactly those two cells while the key is closed and scored normally on
1.0.8.0.

### The per-option dump pins its image base

`zp_dump` maps its copy of the game image at a **fixed** base, so the relocation
delta is a constant and the dump's bytes are a function of the source image and the
arms only -- never of the run's ASLR. The image's own preferred base (`0x00400000`)
is tried first; a Win32 process cannot have it, because the loader maps the NLS
section `C_1252.NLS` at `0x00400000` before any user code runs and the heap follows
it, so the 26 MB range is never free. Measured: a no-CRT, custom-entry 32-bit probe
fails `VirtualAlloc(0x400000, 0x18B2000)` with `ERROR_INVALID_ADDRESS` 487 on its
first instruction, `VirtualQuery` showing the NLS view at `0x400000`; the same
happens with the tool linked at `0x10000`/`0x20000000`/`0x400000`, with ASLR on or
off, and with a static-import DLL at `0x400000` (the DLL was relocated to
`0x960000`). The reference tree records the same and builds its dump 64-bit for that
reason (`../readable/CMakeLists.txt:172-185`). The copy therefore goes to
`0x30000000` (`tests/dump_main.cpp`'s `kFixedImageBase`), the `.reloc` pass runs with
the fixed delta `0x2FC00000`, and `ApplyRelocations` refuses any base it did not pin.
The tools that map the image are linked at `/BASE:0x20000000` with
`/DYNAMICBASE:NO` (`CMakeLists.txt`) so their own images cannot collide with either
address; **any future tool that maps the image must do the same**, or the harness is
ASLR-dependent again.

Why the pin matters: at a run-dependent delta, a signature whose fixed bytes cover
the **high half of a HIGHLOW fixup** matches on some runs and not others, and the
dump's own high-half suppression can hide an arm's write on some runs and report it
on others. The recorded row is `MiscFixes`' first literal (`C7 06 ? ? ? ? 74 19`):
it matches at RVA `0x739733` when the fixup at `0x739731` (`0x016703C4`) is
relocated by a delta whose high word is `0x0560` (witness `0x05600000`, measured),
because that makes the fixup's bytes read `C4 03 C7 06` and the `74 19` two bytes
later still follow; the arm then writes `EB` at `0x739739`, one row no clean delta
has. With delta `0x055F` the same bytes read `C4 03 C6 06` and the signature does not
match. That literal is the original handler's own string, so the base-dependence is
the original's, not a transcription bug; it is recorded under Preserved faults.

The defect was measured on the pre-pin binary, whose `VirtualAlloc` delta was the
allocator's: six runs of the **same** `zp_dump.exe` gave
`E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327` five times and
`FD0060368E17939F38EC9C63490C526F3964C1512513EFA664EC2F4C1D630B19` once. The
differing run was missing exactly one row, `NewMemorySystem 001082 00`: at that
run's delta the relocated high half at `0x1082` happened to equal the byte the arm
wrote, so the row convention suppressed it. (The older recorded variants were
`9BAB526C…` at a delta with the `0x0560` `MiscFixes` row and `C454EC38…` once at
delta `0x05D9`.) With the pin, six runs are byte-identical and equal the clean hash.

The guard for the class is `tools/check_relocated_literals.py` (ctest
`zp_relocated_literals`): every pattern-like literal in `source/*.ixx`, **including
the table rows** (`kMiscFixSites`, the `kModChecks*` tables, the launcher and audio
tables -- the old `find_pattern("...")`-only extraction could not see them), is
measured in each of the nine images at six deltas (`0x00010000`, `0x01000000`,
`0x05600000`, `0x05D90000`, `0x2FC00000`, `0xFFFF0000`), and its hit set (count and
every RVA) must be identical at every delta to the unrelocated hit set. A literal
whose hit set changes is classified against the key's recovered original strings: the
`MiscFixes` literal is the original's own string and is a preserved fault; a literal
that differs from a safe original string is a failure to fix. The full sweep --
319 literals (254 distinct patterns) x 9 images x 6 deltas -- currently finds
exactly one changed literal, `misc.ixx:225`, the `MiscFixes` row above, and no
literal of the second
kind.

## Signature parity: the original's own pattern strings

A byte comparison cannot see a **shortened** signature. On the build the baselines
were taken from (1.0.8.0) a shortened copy of a handler's signature happened to
select the original's first match, so every baseline byte agreed; on another build it
selected a different function, and the arm patched a site the original never touches
(or missed the one it does). The independent cross-check against
`ZolikaPatch_reconstruction`'s recovered tables
(`.agents/rewrite/08-independent-crosscheck.md` §2.2/§3) found four arms in this class.
All four were real. Each now scans the string its own handler passes to the scanner
(the handler decompilation is the source: `0x100055C0`, `0x10046B30`, `0x10037290`,
`0x10036FD0`):

| arm | the handler's own signature | what the shortened string did |
| --- | --- | --- |
| `AimProjectileCrashFix` | `8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8` | `8B 44 24 ? 89 41 ? 8B 44 24 ? 89 41`: two hits on 1.0.0.4/1.0.4.0, the first at a different `ret 0Ch` function |
| `TryToSkipAllErrors` | `81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00` | `81 EC 40 08 00 00`: two hits on seven images, the first at a different `sub esp,840h` |
| `MPNikoCrashFix` | `74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00` | wildcarded the bytes that distinguish the site and lengthened the tail: a hit on 1.0.0.0/1.0.0.4/1.0.4.0 and on no 1.0.6.0 build, where the original matches nothing |
| `BlackscreenFix` | `75 54 8B 48 28 8B 40 2C` | `75 ? 8B 48 ? 8B 40 ? 51 8B 0D`: a hit on 1.0.0.4/1.1.1.0/1.1.2.0 where the original matches nothing |

The same check over every arm found 17 further literals that were a truncation or a
wildcarding of the original's string: `FreezeGunFix`, `GroupHackFix`,
`PathNodeCrashFix`, `WantedLevelCrashFix`, `EpisodeSwitchFreezeFix` (three strings),
`SCOSignatureFix`, `RemoveAllWeaponsProtection`, `HighFPSSpeedupFix`,
`BuildingDynamicShadows` (two), `InfiniteCarBottomDistance`, `BikeFeetFix`,
`BikePhoneAnimsFix`, `PipeBombDropIconSupport` and `TeleportProtection`. Every one of
them selected the same sites as the original's string on all nine images, so no
baseline could distinguish them either; they are the original's own strings now.
Those 21 signatures are the only source changes: 14 in `source/stability.ixx` (22 call
sites, because a publish and its install share a string), 2 in `source/graphics.ixx`
(3 call sites) and 5 in `source/vehicles.ixx`. 21 rows of `tests/signatures.tsv`
recorded the shortened strings and are corrected; its `IncreaseVehicleStructLimit`
row still spelled out the relocated operand (`68 34 EE DA 00`) that the arm itself had
already replaced with `68 ? ? ? ? 6A 32 8B C8`.

**Anchor movement.** A signature's length is not the anchor: an arm's sites are
offsets from the match, so only a first-hit change moves a write. These are the four
arms whose first hit moved, and the only images on which it did (RVA of the pattern's
first byte; the arm writes at that RVA, or at +81 for `BlackscreenFix`):

| arm | 1.0.0.0 | 1.0.0.4 | 1.0.4.0 | 1.0.6.0 | 1.0.7.0 | 1.0.8.0 | 1.1.1.0 | 1.1.2.0 | 1.1.3.0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `AimProjectileCrashFix` | 0x5d6780 | **0x5ac560** (was 0x1bc88) | **0x6e3150** (was 0x1abd8) | 0x64c440 | 0x64c330 | 0x632620 | 0x67aa00 | 0x679e70 | 0x5fcfb0 |
| `TryToSkipAllErrors` | **none** (was 0x1ff800) | **0x55f60** (was 0x3700) | **0x53240** (was 0x1e10) | 0x1a89f0 | 0x1a8cb0 | 0x56680 | **0x526b0** (was 0x1e50) | **0x52700** (was 0x1e70) | 0x1a8510 |
| `MPNikoCrashFix` | **none** (was 0x591541) | **none** (was 0x496371) | **none** (was 0x42d451) | 0x5c1ef1 | 0x5c1e71 | 0x59a7f1 | 0x60df61 | 0x618b91 | 0x565291 |
| `BlackscreenFix` | 0x7b218 | **none** (was 0x885e8) | 0x2beba8 | 0x889c8 | 0x889d8 | 0x2c6ef8 | **none** (was 0x10a448) | **none** (was 0xf1ea8) | 0xc828 |

Every other literal's first hit is unchanged on every image, so the 1.0.8.0 bytes are
unchanged by the whole change. Measured, not only argued: the per-option 1.0.8.0 dump
written with the pre-fix strings and the one written with the fixed strings hash
identically (`SHA-256 E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327`).
Every one of the 21 new strings is also present **verbatim** in the original ASI's own
image (an ASCII string search over `game_refs/ZolikaPatch_original.asi.off`), so the
provenance is the binary itself, not only the reconstruction's recovered tables.

**The gate.** None of the four handlers has a build ladder: each scans one signature
and tests the scan's answer (`if (result)`). The exact signature *is* the gate — a
build where it has no hit gets no write because there is no site to write. That is
what the tree does now: an arm writes only when its `find_pattern` on the original's
own string is non-empty. The other fourteen handlers were checked the same way: each
one's decompilation has no build-id comparison except `0x1001BE40`
(`PipeBombDropIconSupport`), whose guard is `if (dword_1015BE04 >= 6)`; that key's
catalogue mask is `0x4BFC0` (build ids 6, 7, 8, 9, 10, 11, 12, 13, 15, 18 — every id
the loader can produce that is >= 6), so `Loader::Enabled` already applies exactly
that gate. `RemoveRGSCLoginRequirement` is the one arm in this set with a real
either/or ladder: its handler (`0x10045490`) patches its primary signature when it
hits and otherwise, only when the build id is 9, patches the fixed fallback site;
`source/loading.ixx` has the same shape (the original's primary string, and a
`Build::v1080`-gated fallback site).

**The guard.** `tools/check_signature_parity.py` (ctest `zp_signature_parity`) keeps the
class out. Per `find_pattern` literal in `source/*.ixx` it names the catalogue key from
the installer/publisher function, takes the key's original signature strings from the
recovered tables (`handler_table.json`'s handler strings, `option_signatures.json`,
and the ASI's own stored strings for a key neither covers), associates the literal when
it is an exact copy or a loosening of one, and requires the literal's hit set (count
and every RVA, hence the first-hit RVA) to equal the original's on all nine images. A
recovered association that differs is a failure; `--strict` also fails on a harmless
loosening and on a stale `signatures.tsv` row. It is falsified against this bug class:
on the tree before the fix it exits 1 naming exactly the four arms, for example

```
FAIL stability.ixx:1469 (InstallAimProjectileCrashFix -> AimProjectileCrashFix)
  ours    : 8B 44 24 ? 89 41 ? 8B 44 24 ? 89 41
  original: 8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8
    GTAIV_1004: hit count 2 vs 1; ours 2 hit(s), first RVA 0x1bc88, original 1 hit(s), first RVA 0x5ac560
    GTAIV_1040: hit count 2 vs 1; ours 2 hit(s), first RVA 0x1abd8, original 1 hit(s), first RVA 0x6e3150
```

and on the fixed tree it exits 0 (`132 signature(s) measured on 9 image(s): 0
mismatch(es), 0 loosening(s), 100 without an original signature`). The literals it
reports as "no recovered original signature" are derived anchors — a pool lookup, a
site's own bytes, or a multi-scan arm's row group such as `EpisodicVehicleSupport` —
and are outside its claim by design.

**Scoreboard.** The stored baselines already carried the original's sites for these
arms (the reference tree's arms scan the original's strings), so the fix *closes* the
keys per build instead of moving them. With a binary built from this tree:
1.0.8.0 is `98 closed / 0 open / 1 oracle-verified`; 1.0.0.4, 1.1.1.0, 1.1.2.0 and
1.1.3.0 are `98 / 0 / 1`; 1.0.4.0 is `97 / 0 / 2` (the second oracle-verified key is
`CarDynamicShadowFix`'s baseline omission, excused on 1.0.0.0 and 1.0.4.0 -- see
below), and 1.0.6.0/1.0.7.0 are `97 / 0 / 2`, the second key being the
`Patch7RemoveModChecks` baseline shortfall. 1.0.0.0 cannot be scored by the
one-process dump: its full-cell run faults at `NoImpostorHeightCutoff` by design, so
it is scored one key per process instead, where it is `96 closed, 0 open, 2
oracle-verified` with `NoImpostorHeightCutoff` the one designed fault cell, every arm
above closed. The comparison also ran against a freshly built
binary while the artifacts in `build/` were older than `source/`, which closed
`VRAMFix` on the six builds where its signature exists; that arm was already in the
tree and is not part of this change.

## A second incomplete baseline: `SkipLauncher`

`SkipLauncher` reads OPEN on every image and it is **not** a missing arm. The stored
cell is a partial recording of the feature, and the bytes it lacks are ones the
original's own handlers write. Its parity is therefore decided against the oracle —
the recovered handlers at `0x10045CE0` (main table and collectors), `0x10004850`
(DFA) and `0x10004620` (ROM) — not against the baseline.

How that was measured. `ZolikaPatch_reconstruction/evidence/skip_launcher_ops.json`
holds the recovered rows, their signature anchors, offsets, bytes and the hook
targets; the comparison replays those rows on each pristine image (matching the
scanner's executable range, applying the handlers' own guards and the E8/E9
displacements with the recovered body VAs) and diffs the result against this tree's
dump. The row data is the evidence, so the comparison is against the original, not
against a restatement of the transcription under test. On all nine images the two
write sets are identical: no missing byte, no extra byte, no differing byte.

Why the baseline cannot be the oracle here. Its producer is the reference tree's
*other* implementation: `../readable/source/xliveless.cpp` carries this same
25-row table plus the DFA and ROM stages, but nothing in that tree calls
`ApplySkipLauncher`; the live arm `ApplySkipLauncherOption` implements seven of the
25 rows (0, 2, 3, 4, 5, 6, 9) and neither hook stage. The stored cell is that
seven-row arm's output, so it cannot contain the rows the live arm never scanned.

The smallest counter-example is 1.0.0.0, where three different write sets exist for
one key:

| source | bytes | what it writes |
|---|---:|---|
| original handlers | 14 | row 2's two NOP pairs (4), row 10's `90 A3` (2), the second collector's two sites (8) |
| reference live arm | 4 | row 2's two NOP pairs only (it never scans row 10 or the collectors) |
| stored baseline | 2 | row 2's first NOP pair only |

So the baseline is not even self-consistent with the reference implementation that
was supposed to have produced it: even a 7-row arm writes four bytes there. On
1.0.8.0 the original writes 108 bytes, the baseline records 8, and every one of
those 8 is inside the original's set (the baseline never invents a byte).

### Per-image totals

`rows` is the set of main-table entries whose signature matched, `collectors` the two
stub sites per pattern, `DFA` the capture plus installs plus collected jumps that the
stage's gates let through (the four EFLC builds exclude it), `ROM` its two installs.
"baseline" counts the bytes the stored `b{n}-per.txt` cell records for this key.

| image | rows matched | collectors | DFA | ROM | original bytes | baseline bytes | baseline lacks |
|---|---|---:|---:|---:|---:|---:|---:|
| 1.0.0.0 (b1) | 2, 10 | 2 | capture | 0 | 14 | 2 | 12 |
| 1.0.4.0 (b5) | 0, 3, 4, 7, 8, 20 | 4 | capture | 2 | 485 | 9 | 476 |
| 1.0.0.4 (b6) | 0, 1, 5, 9, 10, 11–22 | 4 | capture + 4 installs | 2 | 224 | 9 | 215 |
| 1.0.6.0 (b7) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | capture + 6 installs + 2 | 2 | 117 | 8 | 109 |
| 1.0.7.0 (b8) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | capture + 6 installs + 2 | 2 | 117 | 8 | 109 |
| 1.0.8.0 (b9) | 1, 6, 9, 10, 21, 22 | 4 | capture + 6 installs + 2 | 2 | 108 | 8 | 100 |
| 1.1.1.0 (b11) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | none (EFLC gate) | 2 | 78 | 8 | 70 |
| 1.1.2.0 (b12) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | none (EFLC gate) | 2 | 78 | 8 | 70 |
| 1.1.3.0 (b13) | 1, 6, 9, 10, 21, 22 | 4 | none (EFLC gate) | 2 | 70 | 8 | 62 |

`0x4BFE2` (every supported build) is the key's own mask, so every image above runs
the arm; the ROM stage runs even on the images whose DFA stage is gated off, and even
when the XLivelessAddon companion is loaded (that flag skips the main table and the
DFA stage only).

### Row sites per image

Each cell is the site the row's signature resolves to (RVA), or `–` where the
signature is absent. A row is only applied where it has a site, exactly like the
original: 19 of the 25 rows have none on 1.0.8.0 because they belong to other
launcher versions, and the original skips them there too — no row is unmatched that
the original matches.

| row | anchor | b1 | b5 | b6 | b7 | b8 | b9 | b11 | b12 | b13 |
|---:|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x1012DC74 | – | 0x071580 | 0x1260C0 | – | – | – | – | – | – |
| 1 | 0x1012DCE0 | – | – | 0x126BC6 | 0x00289E | 0x00287E | 0x0900DE | 0x07359E | 0x07316E | 0x16941E |
| 2 | 0x1012DD5C | 0x00293A | – | – | – | – | – | – | – | – |
| 3 | 0x1012DDB0 | – | 0x071E8A | – | – | – | – | – | – | – |
| 4 | 0x1012DE10 | – | 0x071F1D | – | – | – | – | – | – | – |
| 5 | 0x1012DE60 | – | – | 0x126C5C | – | – | – | – | – | – |
| 6 | 0x1012DEB0 | – | – | – | 0x00293D | 0x00291D | 0x09017D | 0x07363D | 0x07320D | 0x1694BD |
| 7 | 0x1012DF28 | – | 0x071E8E | – | – | – | – | – | – | – |
| 8 | 0x1012DF80 | – | 0x052128 | – | – | – | – | – | – | – |
| 9 | 0x1012E0D0 | – | – | 0x1269B9 | 0x00264D | 0x00262D | 0x08FE8D | 0x07334D | 0x072F1D | 0x1691CD |
| 10 | 0x1012E134 | 0x002946 | 0x071E98 | 0x126BCB | 0x0028A3 | 0x002883 | 0x0900E3 | 0x0735A3 | 0x073173 | 0x169423 |
| 11 | 0x1012E1A0 | – | – | 0x074D0B | – | – | – | – | – | – |
| 12 | 0x1012E1F0 | – | – | 0x081225 | – | – | – | – | – | – |
| 13 | 0x1012E23C | – | – | 0x0812F5 | – | – | – | – | – | – |
| 14 | 0x1012E290 | – | – | 0x081375 | – | – | – | – | – | – |
| 15 | 0x1012E2E8 | – | – | 0x0813EF | – | – | – | – | – | – |
| 16 | 0x1012E340 | – | – | 0x0815E3 | – | – | – | – | – | – |
| 17 | 0x1012E394 | – | – | 0x083BF5 | – | – | – | – | – | – |
| 18 | 0x1012E3F8 | – | – | 0x083EA2 | – | – | – | – | – | – |
| 19 | 0x1012E45C | – | – | 0x1280B0 | – | – | – | – | – | – |
| 20 | 0x1012E4B0 | – | 0x072BA0 | 0x127930 | – | – | – | – | – | – |
| 21 | 0x1012E518 | – | – | 0x126F77 | 0x002D35 | 0x002D15 | 0x0905C5 | 0x073A25 | 0x073605 | 0x169905 |
| 22 | 0x1012E574 | – | – | 0x126E4F | 0x002B32 | 0x002B12 | 0x0903C2 | 0x073832 | 0x073402 | 0x169702 |
| 23 | 0x1012E5EC | – | – | – | 0x003890 | 0x003870 | – | 0x074580 | 0x074160 | – |
| 24 | 0x1012E658 | – | – | – | 0x004270 | 0x004250 | – | 0x074F60 | 0x074B40 | – |

### The 1.0.8.0 cell, byte by byte

"differing" counts only the bytes that differ from the pristine image (a store over
an identical byte is not a dump row, and a displacement can coincide with its
preimage). "baseline" is how many of those bytes the stored cell records.

| row | site | differing bytes | baseline |
|---:|---|---:|---:|
| 1 | 0x0900DE | 0 (jump target only) | 0 |
| 6 | 0x09017D | 6 | 6 |
| 9 | 0x08FE8D | 4 | 2 |
| 10 | 0x0900E3 | 2 | 0 |
| 21 | 0x0905C5 | 16 | 0 |
| 22 | 0x0903C2 | 14 | 0 |
| collector `A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75` | 0x74FAA0, 0x74FAD0 | 8 | 0 |
| collector `33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3` | 0x74FAC0, 0x74FB00 | 8 | 0 |
| `dfa_init` | 0x055EFC | 4 | 0 |
| `create_file` | 0x917CD3 | 5 | 0 |
| `close_handle` | 0x917E6B | 4 | 0 |
| `set_file_pointer` | 0x917E92 | 5 | 0 |
| `get_file_size_ex` | 0x917EE1 | 5 | 0 |
| `read_file` | 0x917D38 | 5 | 0 |
| collected jump 0 | 0x0701B7 | 5 | 0 |
| collected jump 1 | 0x07031B | 5 | 0 |
| `rom_reset` | 0x08FE46 | 4 | 0 |
| `rom_set` | 0x090A20 | 5 | 0 |

Total: 108 differing bytes, the baseline records 8 of them (row 6's six NOPs and
row 9's two `xor` bytes) and no byte the original does not write. Hence
`parity_report.py 9` prints `OPEN SkipLauncher: baseline 8, ours 108, missing 0,
extra 57, differing 0`: the 57 "extra" bytes are the ones the baseline omits minus
the 43 displacement bytes the scoreboard excludes by design.

### What the launcher stage reimplements, and what it cannot prove

- The 25-row table, the collectors, the DFA and ROM install rows are in-place
  instruction patches; the eight hook bodies are the callable part. Seven are
  transcribed assembly and proven whole (`rom_set` 4 instructions, `rom_reset` 28,
  `dfa_init` 2, `create_file` 19, `set_file_pointer` 9, `get_file_size_ex` 7,
  `read_file` 10). The five DFA file-API wrappers (`create_file`, `close_handle`,
  `set_file_pointer`, `get_file_size_ex`, `read_file`) are `__cdecl` forwarders to
  the kernel32 imports; four are transcribed, and each `call ds:[…]` reads a module
  slot filled with the same function the original imported (the static CRT has no
  IAT). `create_file` keeps the original's `test eax, eax` fallback and its unguarded
  inner indirect call — a captured slot whose dereferenced target is null faults, as
  it does in the original. The fifth, `close_handle` (0x10004790), keeps its C++
  rebuild because its original stream is an EH4 frame with the filter and the unwind
  continuation inside the same byte range, so there is nothing linear to prove
  (`rebuilt.md` §3 class B, §2l). Its scope table (`0x10153FC0`) names the filter
  `0x100047EC` = `or eax,-1; ret`, i.e. **`EXCEPTION_CONTINUE_EXECUTION`**, and the
  handler `0x100047F0` = the `return TRUE` arm — so a fault in the guarded
  `CloseHandle` call is *retried*, not answered, and the handler is unreachable. The
  rebuild reproduces that shape (the compiled body is byte-identical to the original
  except the four relocated operands) and `zp_dfa_close` pins it in a child process
  whose faulting callee never returns. Nothing reachable changes either way:
  `CloseHandle` validates every handle value and returns `FALSE`, so no argument can
  reach the fault path. The five bodies' behaviour is asserted offline against a real
  file (`zp_dfa_close`: the round trip, the closed handle's `FALSE` +
  `ERROR_INVALID_HANDLE`, null, double close and a never-opened value, the
  pass-through equality with `CloseHandle`, and `fs:[0]`).
- The ROM service is a live-process component: a `std::thread` that registers
  `"MessageWindow"` (falling back to `"MessageWindow2"`), creates a message-only
  window, publishes the handle from `WM_NCCREATE` and answers `WM_ROM` (43858) with
  the recovered action table. `StartRomService` spins on `Sleep(0)` until the window
  exists, with no bound: if class registration succeeds and window creation fails,
  the game hangs there. The original does the same, so the fault is preserved rather
  than timed out. Its thread object is a module global that is never joined, also as
  in the original.
- The two collectors keep the original's shape: a non-empty check and then the pair,
  with no `size() > 1` guard. A pattern that matches has at least two hits on every
  supported image (the table above counts 2 and 4–5; 1.0.0.0 has no hit for the first
  pattern and the collector is skipped), so the shape cannot fault here; on an image
  where it did, the original would write through the same out-of-range slot.
- The DFA stage is suppressed when `XLivelessAddon.asi` is loaded, when `GTAC.dll` is
  loaded, and on the four EFLC builds, and it is one-shot; the ROM stage is one-shot
  and runs regardless. Those are the original's own guards (`0x1000487B` onward).
- **Not wired:** the original's *other* DFA call, from the MiscFixes handler
  (`0x10024018`). It is a no-op whenever `SkipLauncher=1` because the DFA one-shot is
  already set, so it cannot change this key's cell or any other cell measured above;
  it would only matter for `SkipLauncher=0, MiscFixes=1` in the game, and that arm
  lives in `misc.ixx` rather than the launcher module.

## A preserved fault that the tools cannot measure

`NoImpostorHeightCutoff` reproduces the original's fault on 1.0.0.0: the pattern
matches once and the original writes `hits[1]` through an uninitialised vector
entry (`mov eax,[ebp-0x6C]; mov esi,[eax+4]` at `0x100536F1`, write `66 89 06`). The
arm keeps that shape - no `pattern.size() > 1` guard - so on 1.0.0.0 the dump writes
the first pair (which is exactly what the stored baseline's single pair records) and
then faults `0xC0000005`, as the original does.

Consequence: `parity_report.py 1` and `dump_matrix.py` fault at that arm on
1.0.0.0, so the 1.0.0.0 cell of the 18-cell matrix cannot be measured for keys that
run after it. That is the fault being faithful, not the tree being broken; the
reference tree guards it, which is why its baseline carries only the first pair.
Guarding it here to make a tool happy would be the fault-avoidance divergence the
policy forbids.

`tools/single_key_matrix.py` measures the fault as a recorded outcome instead of
an abort. It writes every catalogue key `0` and exactly one key `1`, one
subprocess per key, and prints the exit code of every key on every image (9 builds
x 99 keys); the 1.0.0.0 `NoImpostorHeightCutoff` cell prints
`AV [expected, preserved original fault]` and every other cell prints `ok`.

That shape matters for attribution. A one-line INI
(`[Options]\nSpeedupBugFix=1`) is **not** a single-key document: every key it
omits keeps its default, and most defaults are `1`, so such a run executes almost
the whole catalogue. The 1.0.0.0 fault above therefore surfaces from a
"SpeedupBugFix only" INI without SpeedupBugFix being involved at all --
`SpeedupBugFix`'s own signature has no hit on 1.0.0.0, so that arm is the
original's clean skip there. Only the explicit all-`0` document isolates a key.


## The ten keys that had only the baseline: arbitrated against the original ASI

`tools/parity_report.py` compares every catalogue key against the per-option cells
`../readable/` produced. Ten keys were recorded there **only** as bytes the
reference arm wrote, with nothing independent behind them: `VRAMFix`,
`BenchmarkFix`, `OverrideGTADatPathTo`, `VSyncFix`,
`Patch6and7RemoveModChecks`, `Patch7RemoveModChecks`, `AddOptionsInMenu`,
`DebugPrintSpawns`, `FixPCIDKickbug`, `LaunchWithPlayGTAIV`. The reference tree
has already been shown incomplete twice (`FreezeCarFix` missing entirely and six
bytes of `CarDynamicShadowFix` on 1.0.4.0), and `ZolikaPatch_reconstruction/`
carries no recovered table for any of the ten either, so the original binary
(`game_refs/ZolikaPatch_original.asi.off`, IDA base `0x10000000`) is the only
independent source. This section is that arbitration; the method is
`.agents/rewrite/09-baseline-only-keys.md`.

All ten are **read** inside the original's config walker `sub_100523C0`; eight
dispatch to a handler and four have no install handler at all (the key's whole
effect is a flag, a string copy, a registry delete, or a library load). The
handler's own strings and per-build site VAs are now pinned in
`tests/signatures.tsv` (`BenchmarkFix.*`, `VSyncFix.*`, `LaunchWithPlayGTAIV.*`;
`VRAMFix`'s row already carried them).

| key | handler | recovered write set (from the handler's own instructions) | our arm | verdict |
|---|---|---|---|---|
| `VRAMFix` | `0x1004AAE0` | pattern `8B 44 24 10 D9 2C 24 83 C4 1C C3`, `E9` at the match + rel32 to `0x1004AAB0`; **no build gate**, the scan is the gate; one site on 1.0.4.0/1.0.0.4/1.0.6.0/1.0.7.0/1.1.1.0/1.1.2.0 | `InstallVRAMFix` scans the same string and jumps to the body registered `0x1004AAB0`; no extra gate | agrees |
| `BenchmarkFix` | `0x10005690` | gate `build != 9 && build != 13`; pattern `E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`, two hits; `E9` at `match[0]+8` -> `0x10005650`, `match[1]+8` -> `0x10005670`; resumes `match[0]+13`/`match[1]+13` | same sites, targets and gate; the second resume was the first match's | fixed |
| `OverrideGTADatPathTo` | none (inline) | no image write: when the value is not `"0"`, `strcpy_s` it into the 64-byte `aCommonDataGtaD` (`0x10158068`, seeded `common:/data/gta.dat`) | `CopyOverrideDatPath` reads the same key, tests `"0"`, copies into the same-size buffer | agrees (no handler) |
| `VSyncFix` | `0x100503B0` | gate `build not in {1,2,3,4,5,9,13}`; two patterns; a 4-byte module pointer `&unk_10159BC0` at `match+13` and at `match+30` | `InstallVSyncFix` writes `&zpatch_vsync_flag` at the same two offsets; the catalogue mask is the same gate | agrees |
| `Patch6and7RemoveModChecks` | `0x10044710` | gate `build == 7 || build == 8`; 16 fixed-RVA bytes on 1.0.6.0 and 21 on 1.0.7.0 (rows below) | per-build signature rows resolve to exactly those RVAs and bytes | agrees |
| `Patch7RemoveModChecks` | **none of its own** | it is the OR alternative in `0x100523C0`, feeding the same `0x10044710`: with only `Patch7RemoveModChecks=1` the original writes the full 16/21 bytes | gated only on `Patch6and7RemoveModChecks`, so it wrote nothing | fixed, baseline short |
| `AddOptionsInMenu` | none of its own | no dedicated install handler, but **not write-free**: sets `byte_10158038`; `sub_10052380` then loads `IVMenuAPI.asi`, and only when that load succeeds does the caller run `sub_1001EDE0` (scan `6A 0A 56 50 FF D2 A1`, `mov byte ptr [match+1], 6`) plus `sub_10050BD0` and the two `0x10050A30`/`0x10050A40` callback registrations | `source/menu.ixx`: the probe, the writer, the three death-music installs (the second half of the original's OR), both registrations and all four callbacks | implemented (see the section above) |
| `DebugPrintSpawns` | none | no image write: read **only** inside the `FreezeCarFix` gate, raises `byte_10159C15`, which the spawn probe `0x10037510` reads | read inside the same gate (`zpatch_spawns_debug`) | agrees (no handler) |
| `FixPCIDKickbug` | none | no image write: `SHDeleteValueW(HKEY_CURRENT_USER, L"Software\\Classes\\Software\\Microsoft\\XLive", L"PCID")` | `source/misc.ixx`: the same call behind the same `== 1` gate plus the launch arm's module guard (the registry value is the user's, not the image's) | implemented (see the section above) |
| `LaunchWithPlayGTAIV` | `0x100441B0` | gate `sub_10003A10` (a live `PlayGTAIV.exe` process) plus a switch with cases 1/5/7/8/9; 16 bytes `"PlayGTAIV.exe\0e\0"` at two fixed RVAs per case | same sites, payload, build list; the gate was `Game::Module()==nullptr`, not the process check | fixed |

### `BenchmarkFix`: the second resume was the first match's

The bug is invisible to every cell comparison, because a resume word is a plugin
global, not an image byte. The handler publishes a **per-match** resume before it
writes either site:

```
100056F7  mov eax, [edx]            ; the first match
100056FF  add eax, 0Dh
10005702  mov dword_1015AD50, eax   ; body A's resume
...
1000574C  mov ecx, [ebp+Block]
1000574F  mov eax, [ecx+4]          ; the second match
10005752  add eax, 0Dh
10005755  mov dword_1015AD54, eax   ; body B's resume
```

and the bodies read their own word: `10005667 jmp dword ptr [dword_1015AD50]` in
`sub_10005650`, `10005687 jmp dword ptr [dword_1015AD54]` in `sub_10005670`. Our
`InstallBenchmarkFix` published `match[0]+13` into both, so body B tail-jumped
into body A's continuation (the two matches are in different functions). Fixed to
publish the second match's own `+13`; on all five builds that carry two hits the
sites and targets are unchanged, so no cell moves.

### `Patch7RemoveModChecks`: the OR, an alternate handler

`0x100523C0` reads the two keys and ORs them into one flag, then calls the same
handler. The instructions (also quoted in the fix's comment):

```
10055360  push offset "Patch6and7RemoveModChecks"
1005539E  cmp eax, 1
100553A1  jz short loc_100553F5          ; -> write the OR flag = 1
100553A8  push offset "Patch7RemoveModChecks"
100553F0  cmp eax, 1
100553F3  jnz short loc_100553FC         ; neither key -> no handler
100553F5  mov byte ptr [ebp-0D1h], 1
1005543F  cmp byte ptr [ebp-0D1h], 0
10055448  call sub_10044710
```

`sub_10044710` is the fixed-RVA writer: `build == 7` takes
`0x392E <- 0xEB`, `0x42E7..0x42ED <- 0x90`, `0x3982 <- E9 (to 0x3A09)`,
`0x3A14 <- 0xEB`, `0x38AC <- 0x00`, `0x397E <- 0x00` (16 bytes);
`build == 8` takes `0x390E <- 0xEB`, `0x42C7..0x42CD <- 0x90`,
`0x3962 <- E9 (to 0x39E9)`, `0x39F4 <- 0xEB`, `0x389C <- E9 (to 0x38AE)`,
`0x388C <- 0x00`, `0x395E <- 0x00` (21 bytes).

The arm now gates on `Loader::Enabled("Patch6and7RemoveModChecks") ||
Loader::Enabled("Patch7RemoveModChecks")`, so a Patch7-only INI installs that whole
set. The stored baseline's Patch7 cell is a **different arm**: the reference tree's
own `ApplyPatch7RemoveModChecks` writes only the "all checks in one function"
jump(s) -- 5 bytes on 1.0.6.0 (`0x3982..0x3986`) and 10 on 1.0.7.0 (`0x389C..0x38A0`,
`0x3962..0x3966`) -- so ours is a strict superset and the scoreboard reports
`extra 11, missing 0, differing 0` on both builds.
That is a baseline shortfall, not a regression. It is recorded in
`tests/oracle_verified.tsv` (`Patch7RemoveModChecks`, builds `7,8`, whose anchor is
this section), so `python tools/parity_report.py 7` and `8` read `97 key(s)
closed, 0 open, 2 oracle-verified (baseline incomplete)`.

### `LaunchWithPlayGTAIV`: the process gate, not the module guard

The handler's first instruction is the guard, and only then the build switch:

```
100441C3  call sub_10003A10          ; Toolhelp32 walk for "PlayGTAIV.exe"
100441C8  test eax, eax
100441CA  jz loc_100442F4            ; not launched through PlayGTAIV -> no write
100441D5  cmp eax, 1                 ; case 1
100441DA  mov ebx, 9F043Ch
100441DF  mov [ebp+var_C], 98FA0Ch
...                                  ; cases 5, 7, 8, 9 likewise
10044236  mov esi, dword_1015AD70    ; the game module's relocation delta
```

then two 16-byte stores per case. Sites (game RVAs): 1.0.0.0
`0x9F043C`/`0x98FA0C`; 1.0.4.0 `0x9EA184`/`0x9DD8A0`; 1.0.6.0
`0x9F8F40`/`0xA9D0D8`; 1.0.7.0 `0x9F9F70`/`0xA9E118`; 1.0.8.0
`0xA508F8`/`0xA5A5E4`. The payload is `PlayGTAIV.exe\0e\0`
(`50 6C 61 79 47 54 41 49 56 2E 65 78 65 00 65 00`), the same two
`mov dword ptr [..], imm32` rows the compiler emitted from a 16-byte
`strcpy_s`. Our arm already had the sites, the payload and the build list right
(the 16-byte literal `LaunchGTAIV.exe\0` has exactly two hits, at exactly those
RVAs, on the five cases; 1.0.0.4 also has two and EFLC one, and the switch has no
case there), but it substituted `Game::Module()==nullptr` for the handler's
process check: a direct `GTAIV.exe` launch rewrote slots the original leaves
alone. It now runs `PlayGtaivLauncherRunning()` (`CreateToolhelp32Snapshot` /
`Process32First` / `wcscmp "...PlayGTAIV.exe"` / `Process32Next`) first and keeps
the module guard as the offline-dump guard, so the game behaviour is the
handler's and the pinned-base dump stays inert on all nine builds (0 rows: the
dump attaches an image, not a loaded module, so the whole arm is outside it).

### `OverrideGTADatPathTo` and `DebugPrintSpawns`: no handler, no write

`OverrideGTADatPathTo` and `DebugPrintSpawns` are implemented and their empty
baseline cells are correct: the first only copies into a plugin global, the
second only sets a flag that is read inside `FreezeCarFix`'s gate. Neither writes
an image byte on any build, so no cell can or should show one.

### The two former unimplemented keys: `AddOptionsInMenu` and `FixPCIDKickbug`

Both keys had no arm in the tree before this pass, and one of them is not
write-free. Both are now implemented. The build-9 per-option dump is byte-identical
before and after (SHA-256 `E20AF4EA…2845327`), because both arms' effects are
conditional on state the offline harness does not have: `AddOptionsInMenu` needs
`IVMenuAPI.asi` to load, and `FixPCIDKickbug` is a live-only registry delete. The
same holds for the one entry point beside the key that is *not* the key: the
`ZMenuIV.asi` version guard, which has no gate at all and whose missing library is
the original's own clean skip (both arms are inert in the harness, so the dump hash
is the proof; the guard's section below records it).

#### `AddOptionsInMenu` (`source/menu.ixx`)

The key is read into `byte_10158038` and the stored flag is the **parse's answer**,
not its equality with 1: the walker reads the key at `0x1005327B` with the default
`"1"` (`push offset "1"` at `0x1005323F`) and stores `std::stoi(value) != 0`. The
whole sequence, from the original's own instructions:

```
100532FD  push 0Ah ; Radix                       ; std::stoi is base 10
100532FF  push eax ; EndPtr
10053300  push esi ; String
10053301  call ds:strtol
10053307  add esp, 0Ch
1005330A  mov ecx, eax
1005330C  cmp esi, [ebp-0C8h]
10053312  jnz short loc_1005331F                 ; a conversion happened
10053314  push offset aInvalidStoiArg ; "invalid stoi argument"
10053319  call ds:?_Xinvalid_argument@std@@YAXPBD@Z
1005331F  mov eax, [ebp-0DCh]
10053325  cmp dword ptr [eax], 22h ; '"'         ; errno == ERANGE (34)
10053328  jz loc_10055C62                        ; -> _Xout_of_range
1005332E  test ecx, ecx
10053330  mov byte ptr [ebp-4], 0
10053334  mov edx, [ebp-64h]
10053337  setnz byte_10158038
```

The tail is the answer to everything about this key's gate: `test ecx,ecx` then
`setnz byte_10158038`, so **`AddOptionsInMenu=2` enables it exactly as `1` does** and
`-1` does too, while the parse is still `std::stoi` (base 10, with the
`_Xinvalid_argument` / `_Xout_of_range` throws the tree already reproduces). The
out-of-line twin of that inlined block is `sub_100014B0`, and it says the same thing
in C: `strtol(String, &EndPtr, Radix)`, `if (String == EndPtr)
std::_Xinvalid_argument("invalid stoi argument")`, `if (*errno == 34)
std::_Xout_of_range("stoi argument out of range")`.

The arm now asks `Loader::EnabledNonZero("AddOptionsInMenu")` instead of
`Loader::Enabled`, in both places it gates on the key (`PublishAddOptionsInMenu`
and `InstallAddOptionsInMenu`). `Loader::EnabledNonZero` is the smallest mechanism
that carries one key's rule without touching another key's: it keeps the catalogue's
build mask and absent-key default and reads through the same `Catalogue::Read`
(`std::stoi` with the throws), and differs from `Loader::Enabled` only in the final
comparison (`!= 0` against `== 1`). Every other arm still calls `Loader::Enabled`.

Proven by the contract suite, `TestOptionGates` in `tests/test_main.cpp` (9 checks):
`AddOptionsInMenu` is enabled by `"2"`, by `"1"` and by `"-1"`, disabled by `"0"`,
enabled by an absent key (the catalogue's absent default `"1"`), refused on a build
its mask excludes, and an ordinary key (`MiscFixes`) still requires exactly `1`.
The falsification is in the same test: `Loader::Enabled` -- the old gate -- is
asserted **not** to enable the key for `"2"`, and running the new check against the
old predicate fails it:

```
PASS gate: AddOptionsInMenu=2 enables the key (the original stores != 0)
FAIL gate: FALSIFICATION -- the old == 1 rule against the same requirement
PASS gate: AddOptionsInMenu=1 enables the key
PASS gate: AddOptionsInMenu=0 disables the key
PASS gate: an absent AddOptionsInMenu keeps the default 1
85 check(s), 1 failure(s)
```

Its consumer `sub_10052380` is the companion-library probe, quoted in full:

```
10052380  cmp byte_10158038, 0
10052387  jz  short loc_100523BA
10052389  push offset "IVMenuAPI.asi"; call ds:GetModuleHandleA
10052394  test eax, eax
10052396  jz  short loc_1005239B
10052398  mov al, 1
1005239A  retn
1005239B  cmp byte_101A843C, 0
100523A2  jnz short loc_100523B3
100523A4  push offset "IVMenuAPI.asi"; call ds:LoadLibraryA
100523AF  test eax, eax
100523B1  jnz short loc_10052398
100523B3  mov byte_101A843C, 1
100523BA  xor al, al
100523BC  retn
```

Only when it answers 1 does the config walker run the three integration steps. Loading
a DLL from `DllMain` runs under the loader lock, exactly as the original's config
function did; the arm does not defer the load to a thread or to an `InitializeASI`-only
path, and that is the original's behaviour preserved, not an oversight. The walker's
call sites are:

```
100557d2  cmp esi, 1                       ; DoNotPauseOnMinimize
100557d5  jnz short loc_100557E0
100557d7  mov byte_1015AD43, 1
100557de  jmp short loc_100557E9
100557e0  call sub_10052380
100557e5  test al, al
100557e7  jz  short loc_100557EE
100557e9  call sub_1001EDE0
...
10055b81  cmp esi, 1                       ; RestoreDeathMusic
10055b84  jnz short loc_10055B8F
10055b86  mov byte_10159B86, 1
10055b8d  jmp short loc_10055B98
10055b8f  call sub_10052380
10055b94  test al, al
10055b96  jz  short loc_10055B9D
10055b98  call sub_10050BD0
...
10055be6  call sub_10052380
10055beb  test al, al
10055bed  jz  short loc_10055C39
10055bef  push offset sub_10050A40
10055bf4  push offset sub_10050A30
10055bf9  sub esp, 8
10055bfc  mov edx, offset aDeathMusic        ; L"Death Music"
10055c01  push offset aPrefMusicVolum        ; "PREF_MUSIC_VOLUME"
10055c06  push ecx                          ; a3: a leftover ECX, never read
10055c07  mov ecx, offset aPrefDeathmusic    ; "PREF_DEATHMUSIC"
10055c0c  call sub_100522B0
10055c14  mov edx, offset aPauseGameOnFoc    ; L"Pause Game On Focus Loss"
10055c19  push offset sub_1001EDC0
10055c1e  push offset sub_1001EDB0
10055c23  sub esp, 8
10055c26  push offset aPrefVsync             ; "PREF_VSYNC"
10055c2b  push ecx                          ; a3: a leftover ECX, never read
10055c2c  mov ecx, offset aPrefFocusloss     ; "PREF_FOCUSLOSS"
10055c31  call sub_100522B0
```

The first step is the writer `sub_1001EDE0`, transcribed whole (36 instructions):

```
1001ede0  push ebp
1001ede1  mov ebp, esp
1001ede3  sub esp, 0Ch
1001ede6  mov eax, ___security_cookie
1001edeb  xor eax, ebp
1001eded  mov [ebp-4], eax
1001edf0  push edi
1001edf1  call sub_1001ECB0
1001edf6  push 0
1001edf8  call ds:GetModuleHandleA
1001edfe  mov edx, eax
1001ee00  mov ecx, offset "6A 0A 56 50 FF D2 A1"
1001ee05  call sub_10003830
1001ee0a  test eax, eax
1001ee0c  jz  short loc_1001EE33
1001ee0e  lea edi, [eax+1]
1001ee11  lea eax, [ebp-8]
1001ee14  push eax
1001ee15  push 40h
1001ee17  push 1
1001ee19  push edi
1001ee1a  call ds:VirtualProtect
1001ee20  lea eax, [ebp-8]
1001ee23  mov byte ptr [edi], 6
1001ee26  push eax
1001ee27  push [ebp-8]
1001ee2a  push 1
1001ee2c  push edi
1001ee2d  call ds:VirtualProtect
1001ee33  mov ecx, [ebp-4]
1001ee36  xor ecx, ebp
1001ee38  pop edi
1001ee39  call @__security_check_cookie@4
1001ee3e  mov esp, ebp
1001ee40  pop ebp
1001ee41  retn
```

The `sub_1001ECB0` call is a C++ rebuild, not a transcription: the original is
compiler output for a function with an EH4 frame and a TLS static-init guard, and
those are the blocking instructions:

```
1001ecb3  push 0FFFFFFFFh
1001ecb5  push offset SEH_1001ECB0
1001ecba  mov eax, large fs:0
1001ecd1  lea eax, [ebp-0Ch]
1001ecd4  mov large fs:0, eax              ; the EH4 frame
1001ece6  mov ecx, [eax+ecx*4]
1001ecfb  call __Init_thread_header       ; the one-time-scan TLS guard
```

The rebuild reproduces its observable rule: one scan of
`75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15`, the pre-write word cached
at `word_1015AD48`, then `9090` when `byte_1015AD43` is set and the cached word back
when it is clear, written through the original's `VirtualProtect` pair. Because the
DoNotPauseOnMinimize arm writes the same two bytes earlier in this tree, the scan and
the pre-write word are resolved in the publish tier (the original's first call sees
the pristine bytes because it is the one that writes them).

The second step is the death-music installer `sub_10050BD0`, transcribed whole (133
instructions). It makes three scans and three `E8` installs; the first block is:

```
10050bd0  push ebp
10050bd1  mov ebp, esp
10050bd3  sub esp, 0Ch
10050bd6  mov eax, ___security_cookie
10050bdb  xor eax, ebp
10050bdd  mov [ebp-4], eax
10050be0  push ebx
10050be1  push esi
10050be2  push edi
10050be3  push 0
10050be5  call ds:GetModuleHandleA
10050beb  mov edx, eax
10050bed  mov ecx, offset "0F 57 C0 F3 0F 11 86 20 02 00 00"
10050bf2  call sub_10003830
10050bf7  mov ebx, ds:VirtualProtect
10050bfd  test eax, eax
10050bff  jz  short loc_10050C44
10050c01  lea edi, [eax-0Dh]
10050c04  lea eax, [ebp-8]
10050c07  push eax
10050c08  push 40h
10050c0a  push 1
10050c0c  push edi
10050c0d  call ebx
10050c0f  lea eax, [ebp-8]
10050c12  mov byte ptr [edi], 0E8h
10050c15  push eax
10050c16  push [ebp-8]
10050c19  push 1
10050c1b  push edi
10050c1c  call ebx
10050c1e  lea eax, [ebp-8]
10050c21  push eax
10050c22  push 40h
10050c24  push 4
10050c26  lea esi, [edi+1]
10050c29  push esi
10050c2a  call ebx
10050c2c  mov eax, offset sub_10050AD0
10050c31  sub eax, edi
10050c33  sub eax, 5
10050c36  mov [esi], eax
10050c38  lea eax, [ebp-8]
10050c3b  push eax
10050c3c  push [ebp-8]
10050c3f  push 4
10050c41  push esi
10050c42  call ebx
```

The second block is the same sequence with the pattern
`C7 86 04 02 00 00 04 00 00 00 E9`, `lea edi, [eax-0Dh]` and
`mov eax, offset sub_10050A50`; the third scans
`E8 ? ? ? ? 84 C0 74 C6 8B BE`, takes the match itself (`mov edi, eax`) and also
targets `sub_10050A50`. The epilogue is the /GS frame's:

```
10050cf9  mov ecx, [ebp-4]
10050cfc  pop edi
10050cfd  pop esi
10050cfe  xor ecx, ebp
10050d00  pop ebx
10050d01  call @__security_check_cookie@4
10050d06  mov esp, ebp
10050d08  pop ebp
10050d09  retn
```

The third step is the two `AddIVMenuOption` registrations. `sub_100522B0` is a C++
rebuild (a plain handler: `GetModuleHandleA("IVMenuAPI.asi")`, then
`GetProcAddress(..., "AddIVMenuOption")`, then the call). Its arguments are the
family, the wide title, the fixed `"MENU_DISPLAY_ON_OFF"` type, the preference name,
`1`, `0`, and the getter/setter pair. The original's third stack argument (the
`push ecx` above) is never read by `sub_100522B0`, so the rebuild does not carry it;
the fourth argument, the preference name, is what the export receives.

The death-music install body `sub_10050AD0` is a C++ rebuild too: the same EH4 frame
and TLS guard (`10050af1 lea eax,[ebp-0Ch]` / `10050af4 mov large fs:0,eax`;
`10050b13 mov ecx,[eax+ecx*4]` / `10050b28 call __Init_thread_header`) plus the CRT's
own thread-state helpers. It reproduces the original's observable rule: ask the gate,
scan `6A 06 68 ? ? ? ? 8D 8C` once, write `rand() % 5 + 1` at site+1, answer 1.

The pieces, and how each is carried:

| piece | recovered VA | instructions | how |
|---|---:|---:|---|
| writer `sub_1001EDE0` | `0x1001EDE0` | 36 | transcribed, `ivmenu_writer` |
| death-music installer `sub_10050BD0` | `0x10050BD0` | 133 | transcribed, `ivmenu_installer` |
| death-music gate `sub_10050A50` | `0x10050A50` | 41 | transcribed, `ivmenu_death_gate` |
| two-argument wrapper `sub_1005B1E0` | `0x1005B1E0` | 48 | transcribed, `ivmenu_call_two` |
| zero-argument bool wrapper `sub_1005B360` | `0x1005B360` | 44 | transcribed, `ivmenu_call_zero_bool` |
| byte wrapper `sub_1005B490` | `0x1005B490` | 46 | transcribed, `ivmenu_call_byte` |
| Death Music getter `sub_10050A30` | `0x10050A30` | 2 | transcribed, `ivmenu_death_get` |
| Death Music setter `sub_10050A40` | `0x10050A40` | 6 | transcribed, `ivmenu_death_set` |
| focus-loss getter `sub_1001EDB0` | `0x1001EDB0` | 4 | transcribed, `ivmenu_focus_get` |
| focus-loss setter `sub_1001EDC0` | `0x1001EDC0` | 6 | transcribed, `ivmenu_focus_set` |
| focus-site writer `sub_1001ECB0` | `0x1001ECB0` | — | C++ rebuild (EH4 + TLS guard) |
| death-music install body `sub_10050AD0` | `0x10050AD0` | — | C++ rebuild (EH4 + TLS guard) |
| library probe `sub_10052380` | `0x10052380` | — | C++ rebuild (plain handler) |
| `AddIVMenuOption` wrapper `sub_100522B0` | `0x100522B0` | — | C++ rebuild (plain handler) |
| ZMenuIV version guard `sub_10052300` | `0x10052300` | 38 | transcribed, `zmenu_version_guard` (not this key's arm -- see below) |

The four callbacks are the original's streams, not approximations:

```
10050a30  movzx eax, byte_10159B86          ; Death Music getter
10050a37  retn

10050a40  push ebp                          ; Death Music setter
10050a41  mov ebp, esp
10050a43  cmp [ebp+arg_4], 0
10050a47  setnz byte_10159B86
10050a4e  pop ebp
10050a4f  retn

1001edb0  xor eax, eax                      ; focus-loss getter
1001edb2  cmp byte_1015AD43, al
1001edb8  setz al
1001edbb  retn

1001edc0  push ebp                          ; focus-loss setter
1001edc1  mov ebp, esp
1001edc3  cmp [ebp+arg_4], 0
1001edc7  setz byte_1015AD43
1001edce  pop ebp
1001edcf  jmp sub_1001ECB0
```

The gate and its three wrappers are the same recovered pieces `source/audio.ixx`
carries for `RestoreDeathMusic`; that module keeps them internal, and this arm needs
them under its own key, so `source/menu.ixx` carries and proves its own copies. Only
one copy is live in any configuration: audio's when `RestoreDeathMusic` is on (this
arm then skips `sub_10050BD0`, because the original's OR calls it once), and the
menu's when `RestoreDeathMusic` is off and the library loaded. The shared
`byte_10159B86` is audio's `zpatch_audio_death_music_flag`, so the menu's getter and
setter drive the same byte whichever gate is installed.

**What is covered and what remains.** The load, the one-byte write, the three
death-music installs, both registrations and all four callbacks are reproduced, and
so is the `ZMenuIV.asi` version guard below (which is not this key's). What is not:

- the **`IVMenuAPI.asi` library itself**: only its `AddIVMenuOption` export is
  called, and that call is the library's code, not a body to transcribe. No other
  `IVMenuAPI.asi` entry point is referenced anywhere in the ASI (the only two
  `GetProcAddress` callers are `sub_100522B0` and `sub_10052300`);
- the **`push ecx` third argument** of `sub_100522B0` is an undefined leftover
  register; `sub_100522B0` never reads it, so the rebuild's omission is not
  observable;
- no in-game run: whether the menu renders the two options and the callbacks fire is
  a runtime property.

The harness is inert by construction: `zp_dump` has no `IVMenuAPI.asi` next to it,
`LoadLibraryA` fails, and the probe's answer is the original's clean skip. The proof
is the per-option dump: build 9's file is byte-identical before and after this pass
(SHA-256 `E20AF4EA…2845327`, 384,422 bytes, 12,898 lines).

#### The `ZMenuIV.asi` `GetZMenuVersion` guard: reproduced (not this key's)

The original calls it **unconditionally**, and this tree now does the same. The
walker's only reference to `sub_10052300` is the `call` at `0x1005527B`, with no key
test and no build test in front of it: it is reached by falling through from
SuperLODFix's handler and returns to VSyncFix's read:

```
10055271  cmp esi, 1
10055274  jnz short loc_1005527B
10055276  call sub_10046A10                 ; SuperLODFix's handler
1005527B  call sub_10052300                 ; <- the ZMenuIV guard, unconditional
10055280  push offset "0"                   ; VSyncFix's default, next key
```

`sub_10052300` is one 118-byte body, 38 instructions, with a plain /GS frame and no
exception frame or TLS static-init guard, so it is transcribed rather than rebuilt
and proved whole (`zmenu_version_guard` in `tests/bodies.tsv`, 38 instructions,
`PASS zmenu_version_guard (38 instruction(s), 6 absolute operand(s))`):

```
10052300  push ebp
10052301  mov ebp, esp
10052303  sub esp, 0Ch
10052306  mov eax, ___security_cookie
1005230B  xor eax, ebp
1005230D  mov [ebp+var_4], eax
10052310  push offset "ZMenuIV.asi"; call ds:GetModuleHandleA
1005231B  test eax, eax
1005231D  jz short loc_10052368            ; no library -> clean skip
1005231F  push offset "GetZMenuVersion"
10052324  push eax; call ds:GetProcAddress
1005232B  test eax, eax
1005232D  jz short loc_10052368            ; no export  -> clean skip
1005232F  lea ecx, [ebp+var_C]; push ecx
10052333  lea ecx, [ebp+var_8]; push ecx
10052337  call eax                         ; GetZMenuVersion(&version, &unused)
10052339  add esp, 8
1005233C  cmp [ebp+var_8], 35DD1h
10052343  jnb short loc_10052368           ; 220625 or newer -> nothing
10052345  push 10h                         ; MB_ICONERROR
10052347  push offset Caption
1005234C  push offset "Incompatible/outdated version of ZMenu detected! The game will now exit."
10052351  push 0; call ds:MessageBoxA
10052359  push 0; call ds:GetCurrentProcess
10052361  push eax; call ds:TerminateProcess
10052368  mov ecx, [ebp+var_4]
1005236B  xor ecx, ebp
1005236D  call @__security_check_cookie@4
10052372  mov esp, ebp
10052374  pop ebp
10052375  retn
```

What each step is, and what the tree carries:

- **the probe**: `GetModuleHandleA("ZMenuIV.asi")` — the exact library name, no path,
  no extension guessing. A missing library takes the first `jz` to the epilogue: the
  original's own **clean skip**, no log, no write, no error, no retry.
- **the export lookup**: `GetProcAddress(h, "GetZMenuVersion")` — the exact export
  name. A missing export is the second `jz`, the same clean skip.
- **the call and the comparison**: the export is called with `&version` (a dword at
  `[ebp-8]`) and `&unused` (a 4-byte slot at `[ebp-0Ch]`) pushed in that order, and
  `cmp [ebp-8], 35DD1h` / `jnb` is an **unsigned** "below 0x35DD1 = outdated"
  (0x35DD1 = 220625). Nothing is written back to the library and nothing else reads
  either slot. The dword is *not* initialised, and the transcription does not zero it
  either: a library that answers without writing it makes the compare read the
  original's own undefined stack word — the fault profile, kept.
- **the answer**: no log, no store, no patch. Below the threshold the mod shows
  `MessageBoxA(0, "Incompatible/outdated version of ZMenu detected! The game will now
  exit.", "", MB_ICONERROR)` and then calls
  `TerminateProcess(GetCurrentProcess(), 0)`, and nothing after it runs.

The caption is the image's shared **empty** string: the `push offset Caption` at
`0x10052347` targets `0x10063CD6`, a lone `0x00` byte in `.rdata` (the walker uses the
same address as a `std::string` source through `sub_10001530`), so the original's box
has an empty title bar. Reproduced, not tidied up. All four of the body's string
literals were checked byte for byte against the ASI (`ZMenuIV.asi`,
`GetZMenuVersion`, the 72-byte message text and the empty caption).

**Where it is wired.** `RegisterMenu` subscribes it to the **publish tier** with no
gate of any kind. The original's call belongs to the startup config walk rather than
to a key's arm, and the publish tier is the tree's first pass over a load: the
guard's verdict — carry on, or message and terminate — is therefore taken before any
arm's install, and no build mask or INI value can suppress or delay it. The
original's exact position *inside* the walk (after SuperLODFix's install, before
VSyncFix's read) is an interleave the tree's tier split does not express; for this
call the move is unobservable, because it reads no site, writes no site, and its only
other outcome ends the process.

The five imports it makes are taken through slots
(`GetModuleHandleA`, `GetProcAddress`, `MessageBoxA`, `GetCurrentProcess`,
`TerminateProcess`), the same shape the transcribed writers use for
`GetModuleHandleA`/`VirtualProtect`, and the body is registered at its recovered VA
so the dump expresses its branches in the original's coordinates.

**Inert in the harness, and that is the measurement.** `zp_dump` and `zp_tests` have
no `ZMenuIV.asi` next to them, so the guard takes its first `jz` on every one of the
dump's 99 per-option cells and in the ASI load check; the build-9 per-option dump is
byte-identical to the pinned hash (`E20AF4EA…2845327`, 384,422 bytes, 12,898 lines)
with the guard registered and running, and `zp_tests`' 84 checks still pass.

That the call site really fires was measured, not assumed: with a one-shot file
marker written from the same handler for the length of one build (a temporary
measurement, removed before the final binary; the marker string is not in the shipped
ASI), a single `zp_tests` run -- which loads the ASI once -- produced exactly one
marker line. So the guard is reached unconditionally once per load, and its clean
skip is what leaves the dump untouched.

#### `FixPCIDKickbug` (`source/misc.ixx`)

The whole effect is one call, whose result the original ignores:

```
100523C0: v332 = sub_1005C160(v469, "FixPCIDKickbug", "1");
          ...
          if ( v334 == 1 )
            SHDeleteValueW(HKEY_CURRENT_USER,
                           L"Software\\Classes\\Software\\Microsoft\\XLive",
                           L"PCID");
```

No image byte is written and there is no build gate; the absent default is `"1"` and
the shipped INI carries `0`. The arm is `Loader::Enabled("FixPCIDKickbug")`, the same
`std::stoi(...) == 1` gate, with the launch arm's module guard in front of it: the
offline dump attaches an image, not a loaded module, and the registry value is the
user's, not the image's. Measured on this machine: the value
`HKCU\Software\Classes\Software\Microsoft\XLive\PCID` was
`11327764130696386362` before and after a full per-option dump (the `FixPCIDKickbug`
cell sets the key to 1), so the guard is what keeps the harness from deleting it.


### Measured

```
python tools/parity_report.py 5    97 closed, 0 open, 2 oracle-verified
python tools/parity_report.py 6    98 closed, 0 open, 1 oracle-verified
python tools/parity_report.py 7    97 closed, 0 open, 2 oracle-verified
python tools/parity_report.py 8    97 closed, 0 open, 2 oracle-verified
python tools/parity_report.py 9    98 closed, 0 open, 1 oracle-verified
python tools/parity_report.py 11   98 closed, 0 open, 1 oracle-verified
python tools/parity_report.py 12   98 closed, 0 open, 1 oracle-verified
python tools/parity_report.py 13   98 closed, 0 open, 1 oracle-verified
```

and an independent image scan of each handler's own pattern (never reading the
baseline) agrees with our cell on every image, for every one of the ten keys; the
scan tool and the per-build table are in
`.agents/rewrite/09-baseline-only-keys.md`.

The two former unimplemented keys leave every scoreboard line unchanged, and that is
the expected result, not a missing measurement: their cells are empty on both sides
because the harness has no `IVMenuAPI.asi`, no `ZMenuIV.asi` and no live registry.
What *did* change in the tree is the `AddOptionsInMenu` gate (now the original's
`!= 0`) and the ZMenuIV guard (now transcribed and installed); neither moves a dump
byte, and the identical hash is the proof. The gates for the implemented arms are:

```
cmake --build --preset release                          clean
ctest --preset release                                  22/22 pass (final runs: 122.9 s,
                                                        201.3 s; `zp_relocated_literals`
                                                        is most of it)
python tools/parity_report.py 9                         98 closed, 0 open, 1 oracle
build 9 per-option dump, before == after                SHA-256 E20AF4EA…2845327,
                                                        384,422 bytes, 12,898 lines
python tools/verify_bodies.py …                         175 body(ies), 0 failure(s)
python tools/check_signature_parity.py --game ../game_refs
                                                        132 signature(s) on 9 images:
                                                        0 mismatch(es), 0 loosening(s)
python tools/check_oracle_verified.py                   3 oracle-verified row(s), 0 failure(s)
python tools/check_fault_dialogs.py                     12 test source(s), 0 failure(s)
zp_dump --ooc-selftest                                  ooc selftest: PASS
python tools/gen_body_targets.py --check                body_targets.tsv up to date (100 rows)
```

`ctest` stays at 22 tests because the gate's check lives in `zp_tests` (nine added
checks: 84 in total, 0 failures) rather than in a target of its own; `verify_bodies`
rose from 174 to 175 rows with the guard.

## The oracle-verified class: baselines that are incomplete

`SkipLauncher`, `CarDynamicShadowFix` on 1.0.0.0/1.0.4.0 and
`Patch7RemoveModChecks` on 1.0.6.0/1.0.7.0 are measured against the oracle rather
than the baseline: their stored cells are missing bytes the original writes, so a raw
comparison reports them open. That is accurate about the comparison and misleading
about the work. `tests/oracle_verified.tsv` is the sidecar that says so; the
scoreboard reports those keys *apart* from the open ones and never counts them as
closed.

**A row is per key and build.** Its shape is

```
key <TAB> builds <TAB> why <TAB> the anchor in docs/parity.md <TAB> how it was measured
```

`builds` is the comma-separated list of build ids the excuse covers, or `*` for every
build the key's catalogue mask allows. The scoreboard consults the row only on the
builds it names and scores the key normally everywhere else, so an omitted build
cannot hide behind an excused one. That is load-bearing for the second row:
`CarDynamicShadowFix` is closed on 1.0.8.0 and differs only on 1.0.0.0/1.0.4.0, so a
global row would report a real 1.0.8.0 regression as a baseline gap. With the
per-build row, `python tools/parity_report.py 5` reads

```
build 5: 97 key(s) closed, 0 open, 2 oracle-verified (baseline incomplete)
```

while `python tools/parity_report.py 9` is unchanged at `98 key(s) closed, 0 open, 1
oracle-verified (baseline incomplete)`.

`tools/check_oracle_verified.py` (ctest `zp_oracle_verified`) keeps the class honest.
A row must name a catalogue key, a build the catalogue mask allows *and* the measured
evidence covers, an existing heading in this file (`docs/parity.md#...`), a real
justification and a real measurement. The evidence is pinned to the omissions
measured so far -- `SkipLauncher` on every build, `CarDynamicShadowFix` on 1.0.0.0
and 1.0.4.0, `Patch7RemoveModChecks` on 1.0.6.0 and 1.0.7.0 -- and a row may not
excuse a build outside its omission: flipping the `CarDynamicShadowFix` row to `*`
fails the guard (it names the builds the evidence does not cover), instead of hiding
behind the closed 1.0.8.0 cell.

### The `@noE` fill: `CarDynamicShadowFix` on 1.0.0.0 and 1.0.4.0

The stored baseline's `CarDynamicShadowFix` cells miss the same six bytes on the two
builds where pattern E is absent. On 1.0.4.0 the baseline cell has 21 rows and none at
`0x46E028`; our cell has the same 21 plus six `90` bytes there, so the scoreboard
counts `extra 6`. On 1.0.0.0 the same comparison is only available from the isolated
one-key cell (the shared per-option cell faults at `NoImpostorHeightCutoff`), and it
shows the same six bytes at `0x44DF59`. The original's own handler writes them.

The path is the C/D/E branch of the shared installer `sub_10010B60` (ASI VA; image
base `0x10000000`). The C pattern `83 F8 14 7D 05 83 F8 06 7D 14` matches 1.0.4.0 at
RVA `0x46DFEA` (C) and 1.0.0.0 at RVA `0x44DF1B` (C); the handler saves the match in
`[ebp-0x330C]` at `0x100162A1`..`0x100162A6`. At the C/D/E tail it scans pattern E and
fills the six bytes only when the scan misses:

```
10019039  e8 f2 a7 fe ff     call 0x10003830         ; scan pattern E: match, or 0
1001903e  85 c0              test eax,eax
10019040  0f 85 aa 1c 00 00  jne  0x1001ACF0          ; E present -> no fill
10019046  8b 8d f4 cc ff ff  mov  ecx,[ebp-0x330C]    ; the C match
1001904c  8d 50 06           lea  edx,[eax+0x6]      ; eax is 0 here: six bytes
1001904f  83 c1 3e           add  ecx,0x3e           ; C + 0x3E: 0x46E028 / 0x44DF59
10019052  e8 29 86 fe ff     call 0x10001680         ; fill
```

`0x10001680` is the fill helper, and it is a fill, not a store of ours:

```
10001698  6a 40              push 0x40               ; PAGE_EXECUTE_READWRITE
1000169e  ff 15 10 30 06 10  call dword [0x10063010] ; VirtualProtect(a1, size, 0x40, ...)
100016a5  68 90 00 00 00     push 0x90
100016ab  e8 be f6 05 00     call 0x10060d6e         ; memset(a1, 0x90, size)
```

So the value is six `90` bytes. The gate that admits both builds is the E scan:
pattern E (`F6 86 68 02 00 00 04 0F 85 E6 00 00 00`) has exactly one hit in the nine
images, on 1.0.0.4 at RVA `0x64A6AB`; it has none on 1.0.0.0 or 1.0.4.0, so `eax` is
zero and the fill runs. The six bytes it NOPs are the game's 6-byte `jz` at C+0x3E:
`0F 84 E6 00 00 00` on 1.0.4.0, `0F 84 ED 00 00 00` on 1.0.0.0.

The baseline cannot contain that write. It was produced by `../readable` before that
tree wired its `ApplyCarDynamicShadowFixNoE` pass (the same `MakeNOP(match + 62, 6)`,
`../readable/source/vehicles.ixx`) into the installed arms; the stored per-build dumps
predate the source that lists it. The oracle, not the baseline, decides the write, and
it writes the six NOPs.
