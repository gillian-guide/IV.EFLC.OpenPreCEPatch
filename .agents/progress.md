# Progress â€” readable-fusion

Newest last. One entry per work session; the exact gate output is what counts.

## 2026-11-06 â€” the native-script call layer, and DrivebyFOVFix

**Closed:** `DrivebyFOVFix` (78 -> 79 keys on 1.0.8.0). The key was blocked on the
original's native-script call layer; that layer now exists as `source/native.ixx`.

**What was built**

- `source/native.ixx` â€” the layer. Five pieces transcribed as `static
  __declspec(naked)` bodies and proven whole:
  `sub_100038C0` (5), `sub_1005B3F0` (46), `sub_1005B150` (44), `sub_1005DE40`
  (21), `sub_1005DE80` (21). Two pieces rebuilt as C because they are compiler
  output for `__try`/`__except` functions whose SEH scope table names
  image-specific handler addresses: `sub_100038E0` (59) and `sub_10003980` (52).
  The rejected-`memset` call inside the two wrappers (inline asm cannot name the
  intrinsic, C2420) goes through a local `NativeZeroBlock`.
- `Native::Register()` resolves `dword_1015BDE0` exactly as the original's loader
  preamble does (`0x10052E88..0x10052E9E`): scan the game image for
  `08 0B C2 8B F0 E8`, then `match + 10 + [match + 6]`. On the 1.0.8.0 image that
  is `0x626AF0`, the game's native-handle resolver â€” verified through the real
  code path in `zp_dump` (resolver RVA `0x226AF0`, probe removed afterwards).
- The layer's ABI: hash in **ESI** for the game resolver (C++ cannot ask for a
  register, so a naked `ResolveInEsi` carries it), block in EDX, handler called
  cdecl with the block.
- `source/misc.ixx` â€” the `DrivebyFOVFix` arm: `PublishDrivebyFOVFix` reads the
  site's own `subss xmm1, xmm2` at the anchor's +5 for the resume; the install
  jumps to the transcribed shim (`sub_100064B0`, 5) whose helper (`sub_10006450`,
  20) resolves the FOV native through the layer. Write set on 1.0.8.0: an `E9` at
  RVA `0x45031F` plus its displacement to `0x100064B0`, the original's bytes.
- `CMakeLists.txt` (one module line), `source/dllmain.cpp` +
  `tests/dump_main.cpp` (`import native;` and `Native::Register()` before the
  feature registrations), `tests/bodies.tsv` (7 new rows, 62 total),
  `tests/signatures.tsv` (DrivebyFOVFix site + the layer's resolver scan
  comment), `docs/parity.md` (layer section, status, blocked list, call-target
  substitutions).

**Gates (final runs)**

- `cmake --build --preset release` â€” success.
- `python tools/parity_report.py 9` â€” `build 9: 79 key(s) closed, 20 open`
  (`OPEN DrivebyFOVFix` is gone; was 78 closed).
- `ctest --preset release` â€” `100% tests passed out of 9`.
- `python tools/verify_bodies.py ...` â€” `62 body(ies), 0 failure(s)`.

**Not done / not verifiable here**

- No game launch: the in-game behaviour of the resolver walk and the native call
  is transcribed/proven but not executed against the running game.
- `AGENTS.md`'s "Current state" still says 78 keys / 52 bodies and is now stale;
  it was outside this task's edit scope.
- `BadPlayerModelCrashFix` still needs the GTAC.dll branch
  (`sub_1005B290`/`sub_1005B530`/the sprintf chain) and the original's
  38,455-entry model-name table; the layer it needed is now available.

## 2026-11-06 â€” the misc keys: SpeedupBugFix, Patch4RemoveModChecks, CopSirensFlag, PopupHeadlightsFlag

Four catalogue keys in `source/misc.ixx` closed on 1.0.8.0 (80 Ã¢â€ â€™ 84 keys,
67 Ã¢â€ â€™ 73 proven bodies).

- `SpeedupBugFix`: the arm replaces the game's timer init (`0x4571B0`, anchor
  `50 64 89 25 â€¦` at `0x4571C5`) and publishes the eight absolute float pointers
  at the anchor's `+26/+165/+188/+226/+234/+249/+264/+276`. The body
  (`sub_10050540`, 109 instructions) is compiled C (SEH + /GS + the thread-safe
  static guard around its own clock scan), so it is reimplemented in C++ with the
  original arithmetic (`1000.0 / QueryPerformanceFrequency`, then the four
  `scale`/`scale*1000`/`scale*0.001â€¦`/`0.001â€¦/scale` floats and their four
  copies) and the game clock at scan-match Ã¢Ë†â€™ 51. **No proof row** (documented in
  `docs/parity.md`); the arm still registers `0x10050540` for the dump.
- `Patch4RemoveModChecks`: the 8-instruction shared stub `sub_100054E0` is
  transcribed and proven; `misc.ixx` owns only the Patch4 branch of
  `sub_10005340` (one-time scan for `33 C4 89 84 24 A8 01 â€¦`, then zero the
  dword at site+30). That signature has **no hit on 1.0.8.0**, so the branch is a
  scan and nothing else there; the GFWL branch stays in `loading.ixx`.
- `CopSirensFlag`: three transcribed bodies (8/8/9) at `0x10006310`,
  `0x10006330`, `0x100040E0`; the checkers (`sub_10005F70` 191,
  `sub_10006260` 53, `sub_10003FA0` 92) are compiled C and rebuilt in C++.
- `PopupHeadlightsFlag`: two transcribed bodies (10/10) at `0x10044530` /
  `0x10044560` (the EDX twin's pattern is absent on 1.0.8.0; the body is still
  proven) and the rebuilt `sub_10044310` (125). The `fld` operand at the
  anchor's `+82` is retargeted to our pi/2 float; `Bodies::RegisterPublished`
  makes the dump write the baseline's `0xD0000000` placeholder.
- Both siren/popup keys install the same IDE hook (`0x100040E0` + `sub_10003FA0`)
  at `0xA46546`, as the original's two handlers do. The `[PopupLightAngles]` /
  `[HeadlightConeOffsets]` INI table parse is still not in this tree (the
  shipped sections are empty; recorded in `docs/parity.md`).
- Recorded split: the original's one helper reads both the Patch4 and GFWL flags;
  this tree has one helper per module, and the misc arm installs later, so a run
  with both keys enabled performs Patch4 but not the GFWL writes. Each key alone
  is 1:1.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` Ã¢â€ â€™ `build 9: 84 key(s) closed, 15 open`;
`ctest --preset release` Ã¢â€ â€™ `100% tests passed out of 9`;
`python tools/verify_bodies.py â€¦` Ã¢â€ â€™ `73 body(ies), 0 failure(s)`.

**Not done / not verifiable here**: no game launch (the shims/rebuilt helpers are
proven at instruction level where transcribed, but the rebuilt helpers' live
behaviour is not executed); `AGENTS.md`'s "Current state" still says 79/62 and is
outside this task's file scope, so the parent must refresh it (84/73 now).

## 2026-11-06 â€” the loader preamble's salt rule, and the two wrong constants

**What was wrong:** `Native::CallBool`'s body read a slot seeded with `0x62BFDB37`
and `DrivebyFOVFix`'s helper read its own slot seeded with `0x09399528`. Both are
the original preamble's *probe-miss* values; on 1.0.8.0 the probe hits, so the
original runs with the shipped `.data` values (`0x4E61480A` for `dword_101580B8`,
`0x75E005F1` for `dword_101580D4`).

**The recovered rule** (IDA `idZolikaPatch.asi.i64`, preamble `sub_100523C0`):

```
10052e10  mov  ecx, offset "68 ? ? ? ? 68 A9 54 14 51 E8"  ; 0x1012B710
10052e15  call sub_10003830                                 ; pattern scan
10052e1a  test eax, eax
10052e1c  jnz  short loc_10052E82                           ; hit: skip all ten stores
10052e1e..10052e78  ten `mov dword_10158xxx, imm`           ; miss: fallback set
10052e88  ... resolver scan
10052e96  mov ecx,[eax+6]; add eax,0Ah; add eax,ecx; mov dword_1015BDE0,eax
```

A hit copies nothing from the game image; it jumps past every store, leaving the
ASI's `.data`. The ten slot/value/consumer rows are in `docs/parity.md`. On the
nine images: 1.0.8.0 hits at game `0xB3878C`; 1.0.0.0 and 1.0.4.0 miss (the other
six supported images also hit); misses substitute `0x62BFDB37` / `0x09399528` /
`0x406B4B20` / `0xC9DBDA90` for B8/D4/C8/40.

**Changes**

- `source/native.ixx` â€” ten `zpatch_native_salt_10158xxx` words seeded with the
  shipped values; `Native::ResolveSalts()` scans the probe and applies the miss
  set; `Native::Register()` calls it before any arm. `NativeCallBoolBody` reads
  the resolved B8 slot; `Native::Salt(reference)` exposes a slot for the tools.
- `source/misc.ixx` â€” only the DrivebyFOVFix salt declaration/comment and one
  `mov ecx` operand: the helper now reads the layer's D4 slot instead of a local
  `0x09399528`.
- `source/stability.ixx` â€” `BadPlayerModelCrashFix`'s duplicate 46-instruction
  `0x1005B3F0` body is retired. `BpmCheckBody` calls the layer's copy through a
  six-byte `jmp [zpatch_bpm_call_bool]` forwarder (slot filled by
  `RegisteredBody(0x1005B3F0)`), its local B8 salt and the now-unused
  `BpmCleanupBoolEntry`/`zpatch_bpm_cleanup_bool` are gone; C8/40 stay local.
- `tests/salt_check.cpp` + `zp_salt_probe` (new 10th ctest): loads
  `GTAIV.exe`, `GTAIV_1000.exe` and `GTAIV_1040.exe`, calls
  `Native::ResolveSalts()` and asserts the probe branch and all ten slots per
  image; 33 checks. Also disassembly-confirmed in the built ASI that
  `NativeCallBoolBody` reads slot `0x1014714C` (B8) and `DrivebyFovHelperBody`
  reads `0x10147160` (D4).
- `tests/bodies.tsv` â€” `bad_player_model_call_bool` row removed (73 -> 72).
- `docs/parity.md` â€” the rule table, the two fixes and the retirement; body count
  73 -> 72; public surface adds `Salt`/`ResolveSalts`.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 84 key(s) closed, 15 open`
(exit 1, as always with `SkipLauncher` open by construction);
`ctest --preset release` -> `100% tests passed out of 10`;
`python tools/verify_bodies.py ...` -> `72 body(ies), 0 failure(s)`.

**Not verifiable here**: the helper's live FOV arithmetic and `Native::CallBool`
against the real resolver need a running game; the input value and the body's
slot operand are what were proven. `AGENTS.md` still says 73 bodies (now 72) and
is outside this task's file scope.

## 2026-11-06 â€” RestoreDeathMusic and MouseFix (audio.ixx)

**Closed:** `MouseFix` (84 -> 85 keys on 1.0.8.0 in this module's share; the tree
total was 88 after another pass closed three loading keys). `RestoreDeathMusic` was
already *counted* closed â€” its whole baseline cell is branch displacements, which
the scoreboard excludes â€” and now has its arm.

**What was built (source/audio.ixx, 12 new proven bodies)**

- `RestoreDeathMusic`: the gate body `0x10050A50` (41) transcribed, plus the three
  native-script wrappers its calls need, transcribed here because the layer does
  not export them: `0x1005B1E0` (48, two-argument, salt `0x10158054`),
  `0x1005B490` (46, byte, salt `0x10158028`), `0x1005B360` (44, zero-argument
  bool). They reach `0x100038C0`/`0x100038E0`/`0x1005DE40`/`0x1005DE80`/
  `0x1005B150` through jmp forwarders filled from the body registry. The five salts
  are copied from `Native::Salt(ref)` at registration, never literals. The install
  body `0x10050AD0` is **rebuilt in C++** (no proof row): it is compiler output
  with an EH4 frame (handler `0x100619C1`, scope table `0x10154384`) and a
  clock/seed block calling CRT-internal helpers (`0x1005FFC3`, `0x1005FF77`) that
  read `fs:[2Ch]` and the TLS index `dword_10159328`.
- `MouseFix`: all eight pieces transcribed â€” `0x10024060` (30), `0x100240C0` (23),
  `0x10024120` (22), `0x10024170` (28), `0x10024440` (62), `0x10024560` (25),
  `0x100243A0` (23), `0x100241E0` (138). None carries an EH frame, and the advance
  helper's only indirect call is the game's `VirtualProtect` through a published
  slot (the original's `FF 15`). The arm writes the reference's 25-write set: five
  `E9`s, two `E8`s, five NOP pairs and five 4-byte `movd` writes, at the ten
  recorded sites.
- `tests/bodies.tsv` +12 rows (96 total), `tests/signatures.tsv` +12 site rows (all
  verified unique on `game_refs/GTAIV.exe`), `docs/parity.md` gets both key
  sections and the `OutOfCommissionFix` blocker.

**Measured correction to the brief**: `sub_10050A50` is 41 instructions and
`sub_10050AD0` is **75** (0x10050AD0..0x10050BC8), not 91; 91 is the *handler*
`0x10050BD0`'s count (0x10050BD0..0x10050D09), which is not a body.

**Skipped â€” `OutOfCommissionFix`, exact blocker**: the payload is 163,796 bytes of
`.rdata` at `0x100B6798` decoded into a 179,536-byte table; carrying it needs a new
~1 MB recovered-data file outside this pass's edit set, and the code around it
(`sub_1004C3F0`, 16,311 bytes; `sub_1004B750`; the archive builder) is C++ with EH4
frames, `std::string` SSO temporaries and CRT import calls, i.e. a new module to
rebuild. The `E9` was left uninstalled as instructed: it only widens the game's
script check to accept the magic the missing table produces.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 88 key(s) closed, 11 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests
passed out of 10`; `verify_bodies.py` -> `96 body(ies), 0 failure(s)`; the full
1.0.8.0 cell still has `0 differing`.

**Not verifiable here**: no game launch â€” the death-music gate's three native calls
and the mouse axis arithmetic are proven at instruction level where transcribed, but
the rebuilt install body's runtime scan/`rand()` and the in-game mouse feel are not
executed. `AGENTS.md`'s "Current state" (84 keys / 72 bodies) is outside this task's
file scope and is stale.

## 2026-11-06 â€” the three loading keys: NewMemorySystem, NewFilesCompatibility, LoadDLCs

**Closed:** three of the four remaining loading keys: 84 -> 87 keys on 1.0.8.0 from
this pass (the gate reads 88 because `audio.ixx`'s MouseFix landed in the same
window). Fourteen new proof rows of mine (72 -> 86); the tree's total at the final
gate is 98 with the audio rows. The fourth key, `NewSavesCompatibility`, is reported
below with its blocking condition named, not attempted.

**What was built (source/loading.ixx)**

- `NewMemorySystem`: the original handler's two writes (the allocator-init `call` at
  RVA `0x107A` retargeted to the vtable installer, and a jump at `0x107F` skipping the
  five-slot clear, resuming at `0x108C`), the singleton published from the
  `6A 01 B9 00 00 40 06` anchor's `mov esi, <address>` operand (`0x19CF9F0`, rebased
  through `GamePointer`), and the 24-slot vtable read entry by entry from the
  original's `0x100B5038`. Eleven methods transcribed and proven (ctor 17,
  reject-pointer 2, self 2, null 2, `ret 0` 1, paired no-op 1, reject 2, MP-sync 2,
  slot-18 thunk 5, no-arg no-op 1, installer 5); the three arena methods rebuilt in
  C++ with the original's alignment scheme (`unit = max(0x10, align)`,
  `want = max(size, unit)`, `HeapAlloc(want + 2*unit)`, `((base-1)/unit + 2)*unit`,
  the distance at `[p-4]`, `memset(0xCD)`, registry under a critical section; free
  recovers the base; membership query). Slots 10/12 reuse the RGSC-login stub and the
  savegame null path this unit already owns, as the original's table does; slots
  8/9/20/21 get a second copy of the 2-instruction MP-sync stub because `audio.ixx`
  keeps its body internal.
- `NewFilesCompatibility`: the original's gate (`GTAC.dll` / `JacksIVFixes.asi`, in
  both publish and install), the registrar's call operand published from
  `68 64 28 C8 67 E8` (`match + 10 + disp` = `0x625620`), the two update-call
  retargets and the registrar `ret` jump. The registration tail (`pushad/call/popad/
  ret`, 4) is transcribed and proven; the two update bodies are rebuilt with the
  original ABI and call order, hashing through `Shared::ValueResolver()`
  (`sub_10003E50`), and the three helpers use the original's own signatures
  (`8B 35 ? ? ? ? 6A 00 50 E8 â€¦`, `85 C9 53 55 56 57 74 40 â€¦` /
  `53 55 8B 6C 24 0C 56 57 EB 06`, `8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0` minus
  `0x14`), unguarded on a miss as the original is.
- `LoadDLCs`: the handler does not scan â€” it switches on the build id and writes six
  fixed-RVA sites per build. The rows were read from the handler's own `cmp eax, 7/8/9`
  blocks and checked in all three images (1060/1070/1080, both the site shapes and the
  dump's write sets for builds 7 and 8). Three entry points published per build
  (register/apply as RVAs, the loader callback absolute). Bodies `0x10023480` (2) and
  `0x10023490` (19) transcribed and proven; `sub_10023150`, the discovery, rebuilt in
  C++ with the original's call sequence (unchecked 0x104 module-path buffer,
  GetModuleFileNameA + PathRemoveFileSpecA, the TLaD id-2 / TBoGT id-4 groups with the
  content/legacy fallback and the original's second probe, 252 `\Episode3\`..`\Episode254\`
  probes with id = 2n, the 0x168-byte request with mode at +344 and flag at +358, then
  the object's +0x166 = 2).
- **The EDX contract the earlier attempt refused is unobservable**, and that is
  measured, not assumed: `sub_10023150` returns with EDX = its last callee's (the only
  instructions after the last call are the object store, pops, `xor ecx,esp`,
  `call __security_check_cookie` â€” ECX only â€” and the epilogue); the body passes it
  through untouched; and its only caller (game `0x4931FA`) is immediately followed by
  `call 0x8CCCD0`, which reads neither EAX nor EDX, while no other reference to
  `0x890320` exists in either image (checked as branch targets and as a dword). The
  rebuild keeps the same call sequence anyway, so EDX ends on the same value on every
  path (the game's apply on a hit, `PathFileExistsA` on a miss).
- `docs/parity.md`: status counts, two new preserved-fault rows (the file-registry
  helpers' unguarded null on a scan miss; the DLC register call's undefined `this`),
  the loading-keys section with the per-build table, and the `NewSavesCompatibility`
  blocker.
- `tests/bodies.tsv` (14 rows), `tests/signatures.tsv` (the three keys' anchors and the
  LoadDLCs fixed-site table).

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 88 key(s) closed, 11 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests passed
out of 10`; `verify_bodies.py` -> `98 body(ies), 0 failure(s)`.

**Not done / not verifiable here**: `NewSavesCompatibility` stays open. It needs
`sub_10051B30` (the save converter: compiled file IO, the 18-chunk walk and three
conversion-time site patches at `0x872185`, `0x7D8C50`, `0x8DA090`) plus
`sub_10051FD0`'s save-directory path build; its trampoline (`0x100521E0`, 8
instructions) is transcribable but calls that prepare step, so shipping the write
without the converter would corrupt save loading. No game launch: the allocator
vtable's live behaviour, the file-registry helpers' scans and the DLC discovery's
folder probes are proven at instruction level (or rebuilt faithfully) but not executed
against a running game.

## 2026-11-06 â€” the steer consumer `sub_10004C70`, and five keys

**Closed:** `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `MovingPlaneParts`,
`GarageModelFix`, `NoLiveryLimit` â€” 88 -> 93 keys on 1.0.8.0, and each one's
per-option cell is byte-identical to its baseline on all nine images (build 1 is
measured with isolated single-key cells, because its per-option dump faults at
`NoImpostorHeightCutoff` by design).

**Characterisation (IDA `ZolikaPatch.asi.i64`, 320 instructions / 1,343 bytes).**
Compiler output for C, not a transcribable stream: `/GS` frame
(`mov eax,___security_cookie` at `0x10004C76`, `call @__security_check_cookie@4` at
`0x100051A6`), SSE double conversions around every float expression (`cvtps2pd` /
`mulsd` / `divsd` / `cvtpd2ps`), the MSVC float-`!=` idiom (`ucomiss` `lahf`
`test ah,44h` `jnp` at `0x10004DDF..0x10004DE7`) and `cmova` for the two `>`
comparisons. It is a pure consumer: reads `dword_1015AD38` (the vehicle the detour
stores), the three switch bytes, the model word at `+0x2E` and the vehicle's
`+0xE18/+0xF64/+0x10C8/+0x10D8/+0x1354/+0x1F00..1F3C` fields; writes only the pair
of matrices and its own state words (`byte_1015AD42`, `dword_1015AD3C`,
`dword_10159D34`); every game call goes through a loader-resolved slot
(`dword_10159C0C`, `dword_10159B88`, `dword_1015D63C`, `dword_10159CDC`,
`dword_1015AD4C`, `sub_10003980`).

**What was built (source/vehicles.ixx, 15 new proven bodies, 98 -> 113).**

- The consumer, rebuilt in C++ with the original's statement order, call sequence
  and state words; no proof row (docs/parity.md has the assumption table).
- Transcribed and proven: `sub_10003F40` (43, the model-registry lookup),
  `sub_100051B0` / `sub_100051E0` (10/10, the two detour variants),
  `sub_10046B90` (19, garage colour), `sub_10046BD0` (55, garage model lookup,
  /GS frame + the two `dword_10159C0C` calls), `sub_10046C80` / `sub_10046CA0`
  (8/7, garage site bodies), `sub_1003D220` (26, livery hasher), `sub_1003D270`
  (37, livery counter), `sub_1003D2F0` (6, name copy), `sub_1003D310/340/370`
  (11/11/12, livery site bodies) and `sub_1003D3A0/D3E0` (14/14, collectors).
  The only rebuilt pieces are `sub_10001280` (the bounded formatter -> `NoliveryFormat`,
  `_vsnprintf_s` + `_TRUNCATE`) and a one-line forwarder to `Shared::ValueResolver()`.
- Preamble-like resolution in `ResolveSteerHelpers()`: `dword_1015AD78` (rebased),
  `dword_10159B88` (its pattern has two hits; the original takes the first,
  `0x315390`), `dword_1015D63C`/`dword_10159CDC` (-0x30), `_stricmp`, `strcpy_s`, and
  the `CitizenIV.exe` probe for `byte_10159B85`.
- The steer keys install the shared IDE parser hook that fills the wheelangle table
  by jumping to misc's body through `Bodies::All()` (no second copy): published slot
  `zpatch_ide_hook_slot`, same as CopSirens/PopupHeadlights.

**Write sets on 1.0.8.0** â€” steer trio: `E9` at `0x6538A0` (detour) and `0x646546`
(IDE hook), publishing resume `0x6538AE`, part-drive `0x4F65F0`, IDE resume
`0x64654F`. `GarageModelFix`: `E9` at `0x868BB0` (body `0x10046C80`) and `0x5659C3`
(body `0x10046CA0`), resumes `0x868BB6`/`0x5659C8`. `NoLiveryLimit`: `E9` at
`0x64A53D` (D), `0x797A29` (A), `0x797962` (C), `0x79793C` (B), publishing
fn `0x3A490`, D/E resume `0x64A595`, A/B/C resume `0x797A36`.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 93 key(s) closed, 6 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests passed
out of 10`; `verify_bodies.py` -> `113 body(ies), 0 failure(s)`; the shipped-INI full
cell is `0 differing, 3 missing, 57 extra`, the three missing being open keys
(`NewSavesCompatibility`, `OutOfCommissionFix`, `CutsceneFixes`) and the extras
`SkipLauncher`'s incomplete baseline.

**Not verifiable here**: the rebuilt consumer's runtime arithmetic (the two setters,
the part calls' values, the engine phase) is not executed by the offline tools; it is
pinned by the disassembly-derived table in `docs/parity.md`, and every piece around
it is proven instruction for instruction. No game launch. `AGENTS.md`'s "Current
state" (88 keys / 98 bodies) is outside this pass's edit set and is stale.

## 2026-11-06 - `ForceCarHeadlightShadows` closed (93 -> 94 keys, 113 -> 114 bodies)

**What closed it.** The key's arm writes a *pointer to a float* into the shadow
function's `movss` operand, and the stored baseline records the reference dump's
`0xD0000000` placeholder for that word. `Bodies::RegisterPublishedImage` (the
registry `source/bodies.ixx` already exposed) names the word for the dump; the arm
writes the real module address and the mod writes no placeholder.

**Write set on 1.0.8.0** (game RVAs; anchor match = 0x8F7C5B): `E9` at `0x8F7590`
-> `0x1001ADB0` (shadow function entry; resume match+12); `EB` at `0x8F7C61`,
`E9` at `0x8F7CBA` -> match+476 and `EB` at `0x8F7DD6` (the three shadow
branches forced); the dword at `0x8F7D2E` -> this module's offset float (the
`movss xmm2,[0xD95B68]` operand; the game's word is 0.0f); and the handler tail's
three restores at `0x8F73EC` (0x504), `0x8F7422` (0x104) and `0x8F719F` (`je
+9Eh`), which are no-ops on a pristine image and undo `CarDynamicShadowFix`'s
`@noFCHS` rewrites when both keys are on.

**Transcribed vs rebuilt.** The hook thunk `0x1001ADB0` (9 instructions) is
transcribed and proven (`force_headlight_shadows_shim`, new row in
`tests/bodies.tsv`); the per-model offset check `0x1001AD20` (139 bytes,
..0x1001ADAA) is rebuilt in C++ with the same state words (`dword_1015BDD0`,
`dword_101580C0` seeded 2.0f, `dword_1015B5BC`, the `dword_1015ADB8` /
`dword_1015B5C0` 512-slot tables) and the same unguarded call-then-load order, so
it has no proof row: it is compiler output (/GS frame, tail-merged epilogue,
`movss` store, `nop dword ptr [eax]` alignment). One unavoidable divergence: the
original's walk runs off its own table into the following .data; the rebuild stops
at the 512th slot (unreachable through the INI).

**Gates (final runs).** `cmake --build --preset release` -> success;
`python tools/parity_report.py 9` -> `build 9: 94 key(s) closed, 5 open` (exit 1,
`SkipLauncher` still open by construction; the key is not in the OPEN list);
`ctest --preset release` -> `100% tests passed out of 10`;
`tools/verify_bodies.py` -> `114 body(ies), 0 failure(s)`. The key's per-option cell
is byte-identical to the baseline on builds 5, 6, 7, 8, 9, 11, 12 and 13, and on
build 1 in an isolated single-key cell (15 rows; only the four `E9` displacement
bytes differ, which the scoreboard excludes by design).

**Open, recorded in `docs/parity.md`.** The `[HeadlightConeOffsets]` table the
checker walks is still not parsed from the INI (the same shared gap as
`[PopupLightAngles]`): with the shipped INI's empty sections the checker's 2.0f is
the original's own value, but a section with rows (as in `game_refs/ZolikaPatch.ini`)
would use per-model offsets in the original and 2.0f here. Fixing it means the
loader preamble's reader (`sub_100523C0`) plus its unguarded `std::stof`, which
covers both sections and is a pass of its own.

## 2026-11-06 â€” the loader preamble's light tables: `[HeadlightConeOffsets]` and `[PopupLightAngles]`

**Closed:** the gap the previous entry recorded. The two 512-slot tables are now
read from the INI, so `ForceCarHeadlightShadows` and `PopupHeadlightsFlag` publish
per-model offsets/angles from the user's actual `ZolikaPatch.ini` instead of the
constant, and keep the constant exactly where the original does (an empty section,
an absent row, or a hole before the model's row).

**Recovered reader (`sub_100523C0`, `0x10052419..0x10052D9B`).** Two structurally
identical loops, `[PopupLightAngles]` first (`0x10052419`) and
`[HeadlightConeOffsets]` second (`0x10052901`):

- `call sub_10055F10` (the section lookup) / `jz` skips a missing section's loop;
- `mov edi,1` â€¦ `inc edi` / `cmp [ebp-5Ch],200h` / `jl` â€” `Car1`..`Car512`, 512
  iterations; `Car513` is never asked for;
- the key is the literal `"Car"` (`sub_1005BEF0`, which prepends it to
  `std::to_string(ordinal)`) plus `"Name"` (`memmove ...,4`), then separately plus
  `"Angle"` (`...,5`) or `"Offset"` (`...,6`);
- `sub_1005AF10(value, "")` / `jnz` â€” an absent or empty name skips the whole row,
  so its value is never even fetched;
- `sub_10003E50(name)` -> `dword_1015C628[slot]` / `dword_1015ADB8[slot]`, stored
  *before* the value (`0x1005265E` before `0x10052763`, `0x10052B0C` before
  `0x10052C14`);
- the value is the inlined `std::stof`: `mov [errno],0` / `call strtof` /
  `cmp endptr,start` -> `0x100529BC` `_Xinvalid_argument("invalid stof argument")`,
  `cmp [errno],22h` -> `0x100529B1` `_Xout_of_range("stof argument out of range")`.
  Both escape the loader uncaught. A value with trailing junk (`5abc`) is truncated
  instead;
- the angle is `(double)value * pi / 180` (`0x1005286E..0x10052893`, pi at
  `0x10152358`, 180 at `0x10152370`), the offset is stored raw.

**Where it lives.** `Config::ReadModelTables(Config::Ini(), resolve)` in
`source/config.ixx` (`ModelTable`: 512 hashes + 512 floats;
`Config::PopupLightAngles()` / `Config::HeadlightConeOffsets()`), called once from
the top of `RegisterVehicles()` â€” the first registration after `Shared::Register()`
has published the resolver's two CRT slots, and before anything reads an option or
writes an image byte, so the tables are complete and a malformed row throws exactly
where the original's preamble does. `vehicles.ixx` and `misc.ixx` lost their local
`zpatch_fchs_*` / `zpatch_popup_*` table copies and their checkers read the config
tables; the 2.0f seed and the pi/2 seed are unchanged, and are what the checkers
keep when the tables are empty. Nothing in the reader hashes a name itself: the
caller passes `Shared::ValueResolver()`.

**Tests.** New `zp_model_tables` (`tests/model_tables.cpp`, 55 checks in ctest, 56
with a real INI path): the section gate, the `1..512` ordinal and `Car01`/`Car0`/
`Car513` non-keys, index-based fill with holes, an empty name skipping the row, a
name with no value storing the hash and leaving `0.0f`, a value with no name never
parsed, `std::invalid_argument` on `abc`/space-only, `std::out_of_range` on
`1e999`, `5abc` -> `5.0f`, the hash-before-value order, `[PopupLightAngles]`
before `[HeadlightConeOffsets]` on a throw, a full 512-row table and `Car513`
ignored, the degrees->radians constants (90 -> `0x3FC90FDB`, 180 -> `0x40490FDB`),
and the reader driving the transcribed `sub_10003E50` against a reference
implementation of its body. Manually run against `game_refs/ZolikaPatch.ini`:
`78 offset row(s) -> 78 slot(s), 0 angle row(s) -> 0 slot(s)`.

**Gates (final runs).** `cmake --build --preset release` -> success;
`python tools/parity_report.py 9` -> `build 9: 94 key(s) closed, 5 open` (the same
five as before; no image byte moved â€” the tables are read-only startup state);
`ctest --preset release` -> `100% tests passed out of 11`;
`tools/verify_bodies.py` -> `114 body(ies), 0 failure(s)`.

**Docs.** `docs/parity.md`: the gap paragraphs under `ForceCarHeadlightShadows` and
the misc keys are replaced with the recovered grammar table, the fault rows are in
Ã‚Â§Preserved faults, and `zp_model_tables` is in Ã‚Â§Contracts. `AGENTS.md`'s harness
sentence still lists the old executables and could name `zp_model_tables`.

## 2026-11-06 â€” `OutOfCommissionFix`: the payload carried, the key closed

**Decided first, then built.** The key had been refused twice because its payload
looked like game-side registration. It is not: `sub_1004B150` + `sub_1004B570` are a
pure LZ decode of the 163,796 bytes the ASI embeds, `sub_1004B750`/`sub_1004ACE0`
are the ASI's own registration and archive build, and the only game call is one the
original makes through a pointer published from the game image (the device register
at `0xA22BE0`), exactly like every other arm's calls. So the table is a deterministic
transform and was carried.

**Characterisation that mattered (IDA `ZolikaPatch.asi.i64`).**
`sub_1004C3F0` is **not** the decoder: it is the key's own handler (16,311 bytes /
4,199 instructions, EH4 frame, `/GS`, the original's one-character-at-a-time string
builder). It scans `A9 39 44 24 18`, publishes `dword_10158034 = match+0x11`,
`VirtualProtect`s `match+7` and writes `E9` to `sub_1004C3C0`; then scans
`E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`, publishes `dword_10159BF8` (the target of
the call at `match-0x33`, `sub_10001670`) and `dword_10159C04` (the old target of the
call at the match), and calls `sub_1005BA80`, which writes `E8` + displacement to
`sub_1004C350` and returns the old target. The decoder is `sub_1004B150` (opcode
step) + `sub_1004B570` (driver: `05 FA` terminator, exact 179,536-byte output,
Adler-32 trailer). `sub_1004B750` = malloc(0x2BD50) + decode + push_back into the
`std::vector` at `0x1016E43C` + build + `dword_10159BFC = dword_10159C78`.

**What was built**

- `source/ooc_finale2.inc` â€” the 163,796-byte resource, verbatim, with the image
  SHA-256 (`720fbcebâ€¦`), VA `0x100B6798`, file offset `0xB5B98`, the extraction
  recipe, the decoded table's `sha256 86d2bb26â€¦` / `fnv1a 0xC883859F`, and the
  archive's 182,272 bytes / `fnv1a 0xB5AF7B10` in its header (1,035,979 bytes of
  source). The decode was verified independently in Python first: 446 opcodes,
  ends at `0x27FCE`, trailer `0x41E794AB` matches, and the table is byte-identical
  to `../readable/data/finale2.sco`.
- `source/ooc.ixx` (new module `ooc`) â€” the decoder (`OocStep`/`OocDecode`, the nine
  opcode forms and the two fault paths), the Adler-32, the fake-pak entry/vector,
  the archive builder (`OocBuildArchive`: 20-byte header, 16-byte record, name,
  payload at `0x800`, padding to the page), the init (`OocInitialise`, re-running on
  every call like the original's writerless guard), a rebuilt `sub_10001280`
  formatter, and `OocSelfCheck()`.
- The two bodies, transcribed: `OocBranchBody` (7, `0x1004C3C0`) and `OocWrapBody`
  (41, `0x1004C350` â€” the brief's "29" does not survive measurement). Both proved
  whole, and a byte comparison shows them identical to the original apart from the
  six absolute operands.
- The arm: publish tier reads the resume and the two call targets, install tier
  writes `E9` at `match+7` and retargets the `E8` at the second anchor.
- `CMakeLists.txt` (one module), `source/dllmain.cpp` + `tests/dump_main.cpp`
  (`import ooc;`, `RegisterOoc()`, and `zp_dump --ooc-selftest`), `tests/bodies.tsv`
  (+2 rows), `tests/signatures.tsv` (+2 anchors), `docs/parity.md` (the blocked
  section replaced by the closed one, two new preserved-fault rows, the rebuilt-body
  list).

**Gates (final runs)**

- `cmake --build --preset release` â€” success.
- `python tools/parity_report.py 9` â€” `build 9: 96 key(s) closed, 3 open`
  (`OutOfCommissionFix` gone; 94 before this pass, 96 with the cutscene pass beside
  it).
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `117 body(ies), 0 failure(s)` (both new rows
  PASS: `ooc_branch` 7, `ooc_wrap` 41).
- `zp_dump --ooc-selftest` â€” `ooc selftest: PASS` (the rebuilt decode, the table's
  `fnv1a`, the archive's length and `fnv1a`, the header words).
- The key's per-option cell is the baseline's 9 rows with our two displacements
  (excluded as branch bytes); the full shipped-INI cell differs from the same cell
  with the key off by exactly those 9 bytes.

**Not verifiable here.** No game launch: whether the game accepts the mounted
in-memory archive and loads `finale2.sco` from it is a runtime property. The artifact
is 1,637,376 bytes (the resource is 163,796 of them). `AGENTS.md`'s "Current state"
(94 keys / 114 bodies) is outside this pass's file scope and is stale.

## 2026-11-06 â€” HighQualityReflections, and the site arithmetic

**Closed:** `HighQualityReflections` (96 -> 97 keys on 1.0.8.0; the two remaining
open keys are `NewSavesCompatibility` and `SkipLauncher`).

**The question.** The triage recorded the handler's publish as
`resume = esi + 0x13` and `builder = esi + dword(esi + 0x0F) + 0x13` and read `esi`
as the `E9` site; that made the resume look like the last byte of a five-byte
`call` at `site + 0x0F` and the builder wrap. The bytes show the premise was one
byte off: on 1.0.8.0 the site is `0xC51311` (`8B 87 38 09 00 00 6A 08 68 1D 13 00 00
50 E8`, one hit), the `E9` lands on the six-byte `mov eax,[edi+938h]`, and the
site's own `E8` is at `site + 0x0E` (`E8 4C EA F1 FF`, target `0xB6FD70`), not
`site + 0x0F`. So `site + 0x0F` is the rel32, `dword(site+0x0F) = 0xFFF1EA4C`,
`builder = site + 0x13 + that = 0xB6FD70` (the call target), and `site + 0x13` is
the `push 0` after the call -- an instruction boundary; `site + 0x14` is the second
byte of that push, so the `esi = match+1` alternative would not resume at all.

**Scanner convention.** `sub_1005B6A0` returns the first hit-vector element
(`mov esi,[edx]` / `mov eax,esi` / `retn`), and the scanner stores the match start,
not match+1: `sub_10003290` computes `edx = 1 - length`, then
`lea eax,[edx+esi]` (current byte minus length-1) and writes it to the vector. So
`esi` is the match / `E9` site and the original arithmetic is coherent as written.

**What was built**

- `source/graphics.ixx` â€” `HqrBody` (`sub_1001F030`, 157 instructions / 858 bytes,
  static naked, the original's eight-register dump and twelve builder calls),
  `PublishHighQualityReflections` (`match+0x13`, `match+0x13 + [match+0x0F]`) and
  `InstallHighQualityReflections` (`Bodies::Register(..., 0x1001F030)`, `E9` at the
  match).
- `tests/bodies.tsv` â€” `hqr_body 1001F030 ?HqrBody@@YAXXZ 157`.
- `tests/signatures.tsv` â€” the site row (`0xC51311`, the 15-byte signature).
- `docs/parity.md` â€” the resolution section with the site and handler/scanner
  instruction boundaries, the status line (`97 closed, 2 open`) and the blocked
  list.

**Gates (final runs)**

- `cmake --build --preset release` â€” success (exit 0); `--preset trace` also built
  and its suite passed.
- `python tools/parity_report.py 9` â€” `build 9: 97 key(s) closed, 2 open`; the two
  OPEN lines are `NewSavesCompatibility` and `SkipLauncher` (both known baseline
  gaps), `HighQualityReflections` is not among them.
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `118 body(ies), 0 failure(s)`
  (`PASS hqr_body (157 instruction(s), 133 absolute operand(s))`).

**Not verifiable here.** No game launch: the twelve draw-command-builder calls and
the resume are transcribed/proven but not executed in the running game.
`AGENTS.md`'s "Current state" (96 keys / 117 bodies) is outside this pass's file
scope and is stale.

## 2026-11-06 â€” NewSavesCompatibility, closed on its rebuilt converter

**Closed:** `NewSavesCompatibility` (`build 9: 97 -> 98 key(s) closed, 1 open`; the
one open key is `SkipLauncher`'s baseline gap).

**What the original does (read end to end in the IDB).** The handler `0x10052200`
does not convert anything: it scans `68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08`
(game `0x857CAF`; the pushed operand is the save-name buffer the game fills with
`sprintf(buffer, "%s%02d", "SGTA4", slot)`) and
`89 84 24 E0 01 00 00 8B 45 08 53 33 DB` (game `0x85B093`), publishes `match+0xD`
and writes `E9` to `0x100521E0`. Behind that trampoline:

- `sub_10051B30` (363 instructions) opens the path with `fopen_s(...,"rb")`, reads
  the `0x110` header and `cmp dword ptr [eax],45564153h` at `+0xC`, then walks
  eighteen chunks: `fread(&chunk,9,1)`, the dword at `+5` is the total size,
  `add eax,0FFFFFFF7h` (size-9), `call operator new`, `fread(block,1,size-9)`,
  `free`. Chunks 9/13/16 are compared with `4C45h`/`1741h`/`0E77h` and stored in
  `byte_1015AD98/99/9A`. After `fclose` it patches three sites, each resolved once
  through `_Init_thread_header`/`_footer`-guarded statics: `0x872185` (byte `74` or
  `EB` from the chunk-9 size), `0x7D8C50` (`E8`+hook, read size `0CBEh` or `0E6Eh`,
  branch `75 26` or `90 90` from the chunk-16 size) and `0x8DA090` (`E8`+hook, and
  the read entry republished from `[match+1Eh]+1Eh+4`).
- `sub_10051FD0` (157 instructions) asks the game's save-directory function
  (`55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28`, called with `0`, the
  312-byte `byte_101A8308` and a suffix byte), builds
  `"<directory>\<save name>"` as a `std::string` and calls the converter.
- The two hooks it installs are compiled C: `sub_10051A90` (`sub esp,0E7Ch`, reads
  into its own scratch, keeps the read's value) and `sub_10051AD0` (`sub esp,10h`,
  twelve extra bytes before and after the original call when chunk 13 is new).

Nothing needed game-side state beyond the directory function and the save-name
buffer, both reached exactly as the original reaches them, so both steps were
rebuildable.

**What was built**

- `source/loading.ixx` â€” the arm (`PublishNewSavesCompatibility` /
  `InstallNewSavesCompatibility`, the install gated on the *first* anchor as the
  original returns before its second scan), the eight-instruction trampoline
  (`NewSavesTrampoline`, `pusha`/`popa` as `__emit 0x60/0x61`), and the four
  rebuilt pieces with the original's call shape: `NewSavesReadHook` (0x10051A90),
  `NewSavesSecondLoadHook` (0x10051AD0), `NewSavesPatchSites`, `NewSavesConvert`
  (0x10051B30), `NewSavesPrepare` (0x10051FD0). Everything is registered in
  `Bodies::Register`; the arm is `Loader::Enabled`-gated.
- `tests/bodies.tsv` â€” `new_saves_tramp 100521E0 ?NewSavesTrampoline@@YAXXZ 8`;
  the four rebuilt pieces are named in the comment as rebuilds with no row.
- `tests/signatures.tsv` â€” the six recovered scans (name, load, branch, read,
  reload, directory) with the per-image absence note.
- `docs/parity.md` â€” status (98/1, 119 bodies), the key's section with the quoted
  instructions and the transcribed/rebuilt table, and two new preserved-fault rows
  (the corrupt-save allocation throw / uninitialised header check, and the
  trampoline's unspecified-EAX store).

**Gates (final runs)**

- `cmake --build --preset release` â€” success (exit 0).
- `python tools/parity_report.py 9` â€” `build 9: 98 key(s) closed, 1 open`; the only
  OPEN line is `SkipLauncher`, `NewSavesCompatibility` is gone (was 97 closed).
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `119 body(ies), 0 failure(s)`
  (`PASS new_saves_tramp (8 instruction(s), 1 absolute operand(s))`).
- Cross-build parity spot check: `NewSavesCompatibility` has no OPEN line on builds
  1, 5, 6, 7, 8, 11, 12 and 13 either (1.0.0.4.0 writes nothing because its image
  lacks the first anchor, as the original does).

**Not verifiable here.** No game launch: the conversion path runs only when the game
loads a savegame, so the three site patches and the two hooks have not executed. The
file walk was replayed offline against the two real saves in
`game_refs/saves/GTA IV/savegames/user_ee000000deadc0de` (`SGTA400`/`SGTA401`: `SAVE`
at `0x0C`, eighteen `BLOCK` chunks, sizes `0x3AB1`/`0x1729`/`0x0CC7`, i.e. the older
layout), which validates the walk and the size conditions only.

## 2026-11-06 â€” SpeedupBugFix: per-build recovery, and the fault that was not this key

**The reported bug was misattributed.** `zp_dump --exe ..\game_refs\GTAIV_1000.exe
--build 1 out.txt build\win-x86\only-speedup.ini` does fault `0xC0000005`, but not
in `SpeedupBugFix`. A one-line INI is not a single-key document: every absent key
keeps its default (mostly `1`), so that run executes nearly the whole catalogue. A
per-key bisect on 1.0.0.0 (99 keys, each alone) finds exactly one faulting key:
`NoImpostorHeightCutoff`, the documented preserved original fault (its pattern
hits once on 1.0.0.0 and `graphics.ixx` indexes `pattern.get(1)`, i.e. reads past
the hit vector). Adding `NoImpostorHeightCutoff=0` to the same INI makes build 1
exit 0 (433 bytes). `SpeedupBugFix` alone on 1.0.0.0 is 0 bytes / exit 0: its
signature has no hit in `GTAIV_1000.exe`, which is the original handler's own
clean skip (`test eax,eax; jz 0x1005077D`).

**Recovered from the IDB (`ZolikaPatch.asi.i64`).** Handler `sub_100506B0` is
reached from one call site (`cmp esi,1; jnz; call sub_100506B0` at `0x10055A5D`
.. `0x10055A62`) with **no build gate**. It reads its eight operands at fixed
site-relative displacements (`lea ebx,[eax-15h]`, then `mov eax,[ebx+2Fh]` ..
`[ebx+129h]`), writes `E9` at `ebx` and `sub_10050540-ebx-5` at `ebx+1`. So the
original's row is identical on every build; only the match VA differs. The eight
displacements are the disp32 fields of eight `movss` in the game's timer-init
function, with the same opcode bytes on every image.

**Sites recovered** by byte scan of all nine images: 1.0.0.0 none; 1.0.4.0
`0x4544C5`, 1.0.0.4 `0x457395`, 1.0.6.0 `0x5AFBF5`, 1.0.7.0 `0x5AFDD5`, 1.0.8.0
`0x4571C5`, 1.1.1.0 `0x454BC5`, 1.1.2.0 `0x454C05`, 1.1.3.0 `0x5AD565`. All eight
operands validate on every one (same encodings, writable `.data` targets), and the
body's clock scan `1B D9 89 5C 24 24 0F 88` hits once per build with `-51`
landing on the clock function's `55 8B EC 83 E4 F8` prologue. 1.0.6.1, 1.1.0.0 and
1.2.0.43 have no image here: unverified, not verified.

**What changed.** `source/misc.ixx`: `ResolveSpeedupBugFix` validates each of the
eight operand fields (four recovered opcode bytes before the field + a writable
in-image target) before publishing any of them, and the install only runs for a
resolved site; a site that fails is a clean skip. `tools/single_key_matrix.py`
(new): the true single-key matrix, every key `0` and one `1`, one subprocess per
key, per-build exit-code table, with the 1.0.0.0 `NoImpostorHeightCutoff` cell
recorded as the expected fault. `tests/signatures.tsv` and `docs/parity.md`: the
per-build rows, the quoted handler/body instructions, the validation rule and the
one recorded divergence (an image whose signature matched another function would
be a skip here, where the original would install and its body would store through
the operands).

**Gates (final runs).** `cmake --build --preset release` exit 0;
`python tools/parity_report.py 9` -> `build 9: 98 key(s) closed, 1 open` (the
open key is still `SkipLauncher`); `ctest --preset release` -> 11/11;
`python tools/verify_bodies.py ...` -> `119 body(ies), 0 failure(s)`;
`python tools/single_key_matrix.py` -> 891 cells, 890 exit 0, one expected fault
(`NoImpostorHeightCutoff` on 1.0.0.0). SpeedupBugFix's per-key dump is
byte-identical to the pre-change dump on all nine images (build 9: 4 bytes).

## 2026-11-06 â€” the rebuilt set: six pieces upgraded, the rest catalogued

**What this pass was for.** Ten-odd pieces of the tree were C++ rebuilds with no proof
row â€” the weakest evidence in the project. The rule for "cannot be transcribed" was
re-derived from the comparator instead of from "it is compiler output": a body is
blocked only by an *indexed absolute* table operand, an exception frame, a TLS
static-init guard, or a call graph of other rebuilds. A `/GS` cookie and SSE code are
not blockers, and three `/GS` bodies were transcribed as part of this pass.

**Upgraded (six new proof rows, 119 -> 125 bodies).** The bounded formatter
(`0x10001280` 13, `0x10001240` 21, `0x10001230` 2), NewSaves' two installed hooks
(`0x10051A90` 20, `0x10051AD0` 33) and LoadDLCs' discovery (`0x10023150` 223). Five
C++ rebuilds are gone: the three copies of `_vsnprintf_s` (stability, vehicles, ooc â€”
the other two now reach the one transcribed body through a six-byte
`jmp [zpatch_format_entry]`), the two save hooks, and `DlcDiscovery` +
`BuildEpisodeSuffix`. The DLC body is the only rebuilt piece with no EH frame, no TLS
guard and no indexed absolute, so it is now the original's own instructions.

**How it was verified.** Proof rows (exact counts); a byte comparison
(`.scratch/byte_compare.py`) that classifies every differing instruction â€” the DLC
body is 187/223 byte-identical, the rest differ only in absolute operands and two
branch encodings; a probe of the original's engine call (`__stdio_common_vsprintf`
with options 2) against the old `_vsnprintf_s` rebuild â€” identical buffer contents at
every truncation, a different unused return value; and a call-to-symbol check
(`.scratch/resolve_calls.py`) so a `call` that lands on the wrong helper cannot hide
behind the proof's call-as-immediate rule.

**Recorded, not upgraded.** `docs/rebuilt.md` is new: every rebuilt piece with VA,
size, instruction count, the specific operand/frame pattern that blocks it, what the
rebuild reproduces, how that was verified, residual risk, and a risk-ordered "check
these first in play" list (save converter/prepare first). `docs/parity.md` now points
at it instead of repeating the reasons, and its body count and stale rebuild claims are corrected.
Follow-ups ranked in `rebuilt.md` Ã‚Â§4: the OOC decoder pair (transcribable today),
the NewFiles updates (two `__fastcall` shims), the three transcribable DFA wrappers
(launcher.ixx, not this pass's file), the NMS arena methods.

**Gates (final runs).** `cmake --build --preset release` exit 0;
`ctest --preset release` 12/12; `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` -> `125 body(ies), 0 failure(s)`;
`zp_dump --ooc-selftest` -> `ooc selftest: PASS`.

**Not verifiable here.** None of the six bodies can be made to run offline: the
formatter runs on livery/OOC/DLC paths, the save hooks on a savegame load, and the
discovery pass on DLC-manager init. `rebuilt.md` Ã‚Â§2 and Ã‚Â§5 say so per piece.

**One defect was caught by the new checks, not by the proof.** The first
`dlc_discovery` transcription attached `dlc_tlad_exists`/`dlc_tbogt_exists` to the
register block, while the original's `jne` lands on the *second* `PathFileExistsA`
probe (a "content" folder that exists is probed again, and a failed probe still skips
registration). `zp_body_proof` passed it because a `jcc` immediate compares as a slot,
so the target is never compared. The byte comparison showed two `jne` displacements 18
bytes off; after moving the labels, `.scratch/branch_targets.py` (new) reports
`125 row(s), 154 internal branch target(s), 0 failure(s)`. That check is the top
follow-up in `rebuilt.md` Â§4 â€” it belongs in `tools/verify_bodies.py`, and the whole
row set already passes it.


## 2026-11-06 â€” the transcribable backlog: OOC decoder pair and four DFA forwarders

**Transcribed, six pieces (125 â†’ 131 proof rows).** The pieces `docs/rebuilt.md` Â§4
listed as transcribable are now the original's own instructions, registered with
their recovered VAs and proved whole:

| row | VA | insns | bytes | byte-identical |
|---|---|---:|---:|---:|
| `ooc_step` | `0x1004B150` | 406 | `0x41F` | 357 |
| `ooc_decode` | `0x1004B570` | 159 | `0x1D7` | 149 |
| `dfa_create_file` | `0x10004820` | 19 | `0x2F` | 17 |
| `dfa_set_file_pointer` | `0x10004760` | 9 | `0x17` | 8 |
| `dfa_get_file_size_ex` | `0x10004720` | 7 | `0x11` | 6 |
| `dfa_read_file` | `0x10004740` | 10 | `0x1A` | 9 |

Every non-identical instruction differs in an absolute operand or a `call`
immediate â€” no opcode, register, frame offset or branch (`.scratch/byte_compare.py`
with the row names as arguments; it used to be limited to the six rows of the last
pass, now it takes an optional name list). The five C++ rebuilds are gone from
`source/ooc.ixx` and `source/launcher.ixx`.

**One blocker in the old catalogue was not real.** `0x10004820` was filed as class C
with a `__try/__except` frame; the binary has none â€” 19 instructions, `test eax,eax;
je` between the captured-slot call and the `CreateFileA` fallback. Under the tree's
own rule it had no reason to stay rebuilt, so it is transcribed too and moved to the
proof rows. `0x10004790` (close-handle) does have the EH4 frame (filter at
`0x100047EC`, continuation at `0x100047F0`, 45 instructions in `0x81` bytes) and stays
the one rebuilt DFA body; `docs/rebuilt.md` Â§3 now files it under class B where the
blocker actually is, with corrected counts.

**The OOC pair is behaviourally proven, not just shape-proven.**
`zp_dump --ooc-selftest` now runs the original's own 565 instructions against the
recovered resource and still prints `ooc selftest: PASS` â€” the decoded table's
`fnv1a 0xC883859F`, the archive's 182,272 bytes and `fnv1a 0xB5AF7B10`, the header's
`16`/`0xE9` words. That is the first rebuilt body in this tree whose *behaviour* is
pinned offline.

**Two assembly details worth keeping.** (1) The decoder's three `call memcpy` sites
go through `OocCopyBlock` because the inline assembler refuses the intrinsic; `/OPT:ICF`
folds it into `loading.ixx`'s identical `SavegameCopyBlock` â€” same arguments, same
caller-cleans `add esp, 0Ch`. (2) The original's alignment NOPs (`0F 1F 00` and
`66 0F 1F 44 00 00`) cannot be written as `nop dword ptr [eax]` in the inline
assembler and are emitted as `_emit` bytes; the five-byte one is load-bearing because
the assembler only forms the four-byte encoding and every later instruction would sit
a byte earlier, failing the branch-target check.

**Gates (final runs).** `cmake --build --preset release` exit 0 (Trace also builds);
`ctest --preset release` 12/12; `python tools/parity_report.py 9` â†’
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` â†’ `131 body(ies), 0 failure(s)`;
`zp_dump --ooc-selftest` â†’ `ooc selftest: PASS`; `.scratch/branch_targets.py` â†’
`131 row(s), 206 internal branch target(s), 0 failure(s)`.

**Docs.** `docs/rebuilt.md`: new Â§2a with the table, the corrected `0x10004820`
diagnosis and the verification steps; Â§3 loses the two OOC decoder rows, the
create-file row and gains the close-handle row in class B; Â§4 drops the two done
items and records that the internal-branch check is now in `verify_bodies.py`;
Â§6 says 131. `docs/parity.md`: 125 â†’ 131 bodies, both OOC decoder rows marked
transcribed, the launcher paragraph rewritten (four of five forwarders transcribed),
and the "same reason the DFA helpers are rebuilt" references narrowed to
close-handle.

**Open, not mine to edit.** `readable-fusion/AGENTS.md` line 11 still says "125 hook
bodies transcribed"; it is 131 now. That file belongs to the keeper.

## 2026-11-06 - the table registry: the first class-A piece transcribed

`source/bodies.ixx` gained a table registry beside the body one:
`Bodies::RegisterTable(uintptr_t ourTable, uint32_t originalVa, size_t bytes)` and
`Bodies::Tables()`. `tools/verify_bodies.py` gained the matching resolution: an
operand with an index and no base register, whose displacement is exactly a
registered table on that side, is compared as `[<index>*<scale>+table:<content
hash>]` instead of as a displacement. The index and scale are still compared, an
unregistered displacement keeps the literal comparison, and a table that resolves on
both sides with different contents fails as `table contents differ` (not as an
instruction difference). The hash covers the registered length; a 4-byte word that
addresses the image is replaced by the bytes it names, so a pointer table compares
its strings, not its two string-pool layouts.

The registration's offline projection is a `table <VA> <symbol> <bytes>` row in
`tests/bodies.tsv`: a 32-bit module's runtime vector is not in the file, and the
linker map is the only offline symbol-to-address bridge. `--selftest` gained four
table cases (registered PASS, changed contents FAIL, operand moved to another
registered table FAIL, unregistered operand FAIL literally).

The piece: `0x10003F00`, the model-name lookup (21 instructions, 62 bytes), row
`bpm_model_name`. The body keeps the original's absolute indexed operand shape and
the 10-byte `66 66 0F 1F 84 00 00 00 00 00` loop padding; 17 of 21 instructions are
byte-identical, the four differences are two table displacements, the resolver
`call` (a forwarder) and the `mov eax, offset` of our `**UNKNOWN**` literal. The
C++ rebuild (`BpmModelName`) is gone and `BpmCheckBody` now calls the transcribed
body. The arm registers the table next to its `Bodies::Register` calls; both sides
of the proof hash to `57ad8796b1b2652c` although their entries point at
`0x100651e0` and `0x10117bdc`.

**Gates (final runs).** `cmake --build --preset release` exit 0 (Trace also builds);
`ctest --preset release` 12/12; `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` -> `132 body(ies), 0 failure(s)` with
`PASS bpm_model_name (21 instruction(s), 0 absolute operand(s), 2 table operand(s))`;
`zp_dump --ooc-selftest` -> `ooc selftest: PASS`; `.scratch/branch_targets.py` ->
`132 row(s), 208 internal branch target(s), 0 failure(s)`.

**Docs.** `docs/rebuilt.md`: Â§1's operand table and class A rewritten around the
registry, new Â§2b (the piece, the mechanism, the hash, the recipe for the remaining
class-A pieces), Â§3 class A loses the model-name row, Â§4 item 3 rewritten, Â§5 item 2
extended, Â§6 counts 131 -> 132 and the table rows. `docs/parity.md`: 131 -> 132
bodies, the `BadPlayerModelCrashFix` section rewritten (the name table is no longer
a rebuild), "Transcribing a body" gains the table registration as its fifth part, the
call-target list names `BpmModelNameBody`.

**Open, not mine to edit.** `readable-fusion/AGENTS.md` line 11 still says "131 hook
bodies transcribed"; it is 132 now. `docs/review.md` still lists the DLC discovery,
the model-name lookup and the bounded formatter as rebuilt-in-C++; the first two were
already transcribed before this pass.

## 2026-11-06 (later) â€” class A closed: the IDE payload, the popup checker, the cone check

**Closed:** the last three rebuilt class-A pieces are transcribed. 135 -> 138 proof
rows; 98 keys closed / 0 open / 1 oracle-verified unchanged.

| piece | VA | insns | bytes | row | byte-identical |
|---|---|---:|---:|---|---:|
| IDE parser payload | `0x10003FA0` | 92 | 314 | `copsirens_ide_parse` | 72/92 |
| popup-headlights checker | `0x10044310` | 125 | 534 | `popup_headlights_check` | 77/125 |
| headlight-cone check | `0x1001AD20` | 46 | 139 | `force_headlight_shadows_check` | 36/46 |

Every difference is an absolute operand, a table displacement, a `call`/`push`
immediate or a string offset; no mnemonic, register, frame offset, size or branch form
differs, and each body is the original's exact size, so every internal branch sits at
the same relative offset (`138 row(s), 292 internal branch target(s), 0 failures`).

**What changed besides the bodies**

- `tools/verify_bodies.py`: `Tables.resolve` now also resolves a displacement that
  falls *inside* a registered table to that table plus its offset, and the offset is
  part of the compared operand (`[eax+table:<hash>+4]`). That is what the popup
  checker's eight interleaved columns need â€” a 32-bit module cannot make the linker
  emit a symbol four bytes into an array. A wrong column is an *instruction*
  difference; the self-test gained the case (`in-table column offset PASS (26 operands
  resolved), changed column offset FAIL (instruction)`) and the other thirteen cases
  are unchanged.
- `source/config.ixx`: `Config::ModelTable` is now a two-span *view* over four
  C-linkage arrays (`zpatch_popup_angle_hashes`/`_values`,
  `zpatch_headlight_cone_hashes`/`_values`), because the map names an object and never
  a member at an offset inside one. Storage-neutral: `ReadModelTables` fills the same
  arrays at the same slots. The cone block also holds, in the original's order, the
  two words between its hashes and its offsets â€” `zpatch_carparts_shadow_fn`
  (dword_1015B5B8) and `zpatch_fchs_resume` (dword_1015B5BC) â€” so the cone walk's read
  past slot 511 lands on the same word the original's does.
- Nine registered tables (up from six), all content-equal on both sides
  (`.scratch/table_report.py`): `100DE770` `57ad8796b1b2652c` (real names), the cone
  hash table `1015ADB8` `02610d9b8ad1da81` over its extended 2,052 bytes, and
  `867e6dd4bf81dfd6` for every 2,048-byte all-zero table
  (`1015A538`, `10159D38`, `10159380`, `1015CE28`, `1015C628`, `1015BE28`, `1015B5C0`).

**The neighbour word.** `dword_1015B5B8` is zero in the shipped image (the `.data`
tail past the file's raw `0x1000`) and is written before the check can run by the
ASI's car-shadow code: `mov [0x1015B5B8],ecx` at `0x100128B5` with
`ecx = eax+0xB+[eax+7]`, and `mov [0x1015B5B8],eax` at `0x10014F98`. It is
`CarDynamicShadowFix`'s shadow-function pointer (six read sites, four of them the
`call ds:[0x1015B5B8]` pairs in `detour_100068A0`). This tree already publishes that
value into `zpatch_carparts_shadow_fn` by the same formula from the same recovered
site, so the word is reproduced as **the same slot**, not a copy and not a zero
placeholder.

**Gates (final runs).** `cmake --build --preset release` exit 0;
`ctest --preset release` 12/12; `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` -> `138 body(ies), 0 failure(s)`;
`python tools/verify_bodies.py --selftest ...` -> all fifteen cases behave;
`zp_dump --ooc-selftest` -> `ooc selftest: PASS`; `.scratch/branch_targets.py` ->
`138 row(s), 292 internal branch target(s), 0 failure(s)`.

**Docs.** `docs/rebuilt.md`: new Â§2e/Â§2f/Â§2g, Â§2d's "what stays rebuilt" now points at
Â§2e, Â§3 class A emptied, Â§4 item 3 gains the fourth registry property, Â§5 item 7
rewritten, Â§6 counts 135 -> 138 and the new tools. `docs/parity.md`: 135 -> 138
bodies, the `ForceCarHeadlightShadows` "what is transcribed" table and its
"unavoidable divergence" paragraph rewritten (the divergence is gone),
`CopSirensFlag`/`PopupHeadlightsFlag` note rewritten.

**Open, not mine to edit.** `readable-fusion/AGENTS.md` line 11 still says "135 hook
bodies transcribed"; it is 138 now, and "The pieces that are *not* transcribed are
rebuilt in C++" no longer includes any class-A piece.

## 2026-11-06 (later) - class D's largest piece: the OOC archive builder and its twelve helpers

**What changed.** `0x1004ACE0` (314 instructions, 1,121 bytes) is transcribed in
`source/ooc.ixx`, and so are the twelve private helpers that were its class-D
blocker: `0x1004AB70` (5), `0x1004AB80` (4), `0x1004AB90` (9), `0x1004ABB0` (6),
`0x1004ABD0` (20), `0x1004AC00` (28), `0x1004AC40` (23), the vector's
`0x100564F0` (2), `0x100564E0` (2), `0x100564C0` (9), `0x10056500` (11) and the
entry's `c_str` `0x10057710` (6). 13 bodies, 439 instructions, 1,427 bytes; 392 are
byte-identical (89.3%) and the 47 differences are 5 absolute operands, 32 `call`
immediates and 10 `offset Block` immediates - no mnemonic, register, frame offset,
size or branch form differs, and every body is the original's exact size (1,121 for
the builder), so all internal branch targets agree.

**Why they could go.** None of the twelve has an exception frame, a TLS static-init
guard or an indexed absolute operand: SSE header/record zeroing, the `cmovne`
`/0x800` round-up, two loops over the vector, the append (`memcpy` + cursor at
`dword_10159C78`), the vector begin/end/size/clear and the `std::string::c_str`
capacity test. The builder itself is one `/GS` frame, EBP-relative locals and thirty
direct `E8`s. The one callee that stays rebuilt is `0x1005CD50`, the
`std::_Destroy_range` loop `0x10056500` calls: it is the CRT's `std::string`
destructor loop (capacity test, allocator block header past `0x1000`,
`_invalid_parameter_noinfo_noreturn`), i.e. image-internal CRT state; `OocDestroyEntries`
performs the same stores on the same 44-byte stride, and the free branch is
unreachable for the single SSO `"finale2.sco"` entry in both trees.

**New module state.** `zpatch_ooc_offset` = `dword_10159C78`, the builder's write
cursor; the init reads it back for the archive's byte count, which is what
`sub_1004B750` does after the call (`mov ecx, dword_10159C78 / mov dword_10159BFC,
ecx`). `zpatch_ooc_pak` is the fake-pak `std::vector<OocPakEntry>` (the original's
three pointers at `0x1016E43C/40/44`), declared `extern "C"` so the bodies name one
short symbol; the C++ file-scope `static` bodies keep the plain `?Ooc...@@YAXXZ` map
names instead of the anonymous namespace's churning `?A0x...` scope.

**Evidence.** `tests/bodies.tsv` gains 13 rows; `.scratch/ooc_builder/compare.py`
(the byte comparison, per body and in total) and `.scratch/steer/operand_map.py
builder` (19 distinct operands, 0 declared-but-unused, 0 mismatch; four expected
`/OPT:ICF` folds - vector-begin into `Game::Module`, free into `operator delete`,
allocate into `DlcAlloc`, memset into `AudioZeroBlock`). `zp_dump --ooc-selftest` ->
`ooc selftest: PASS` on the recovered 182,272-byte archive, `fnv1a 0xB5AF7B10`, header
`16`/`0xE9`: the builder's behaviour, not only its shape.

**The NMS arena methods (candidate 2) stay rebuilt, and the exact blockers are now
named.** The three streams have no EH frame and no TLS guard, but `0x10038360` calls
`0x1005C610`, the `std::_Hash` emplace, which **has an exception frame** (`push -1;
push SEH_1005C610; ... mov fs:[0], eax` at `0x1005C613`-`0x1005C632`) - class B.
`0x10038430` inlines the same container's erase unlink over the original's map globals
(`dword_101A8424` / `dword_101A8418` / `dword_101A8410` at `0x100384CC` onward), and
`0x100385A0` calls the `std::_Hash::find` `0x1005C5A0` over those globals. A
transcription would have to read this module's `std::unordered_set` at the original's
offsets - an unchecked layout assumption, not a proof. Named in `docs/rebuilt.md` Â§3
class D, Â§4 item 2 and `docs/parity.md`'s NewMemorySystem section.

**Gates (final runs).** `cmake --build --preset release` exit 0;
`ctest --preset release` 12/12; `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` -> `151 body(ies), 0 failure(s)`;
`python tools/verify_bodies.py --selftest ...` -> all fifteen cases behave;
`zp_dump --ooc-selftest` -> `ooc selftest: PASS`; `.scratch/branch_targets.py` ->
`151 row(s), 321 internal branch target(s), 0 failure(s)`.

**Docs.** `docs/rebuilt.md`: new Â§2h (the 13 bodies, the method, the folds, the one
rebuilt destroy loop), Â§1 class D rewritten (the builder was the class's example and
is gone), Â§2's branch counts, Â§3's OOC init row and class-D table, Â§4 item 2, Â§5 item
6, Â§6 counts 138 -> 151 and the two new tools. `docs/parity.md`: 138 -> 151, the OOC
table row, the "decoder and builder are transcribed and behaviourally proven" bullet,
the NewMemorySystem blocker paragraph, and the transcription-decision list.
`docs/review.md`: 119 -> 151 bodies, the rebuilt/transcribed lists, the stale
walk-end-divergence note removed.

**Open, not mine to edit.** `readable-fusion/AGENTS.md` line 11 still says "138 hook
bodies transcribed"; it is 151 now (and the twelve helpers are not "hook bodies",
they are the builder's own helpers - the sentence's "hook bodies" now covers
transcribed internals too).

## 2026-11-07 - the save converter's behaviour test: 258 assertions, and the two faults it can now assert

**What changed.** `sub_10051B30` (337 instructions) was the tree's highest-risk rebuilt
piece and its only *unexecuted* one: its evidence was a throwaway replay, a shape
argument (EH frame + TLS guards) and the five bytes its key's cell measures. It now has a
behavioural test, and the test runs the production code's own walk.

- `source/loading.ixx` - one new exported, test-only entry, `Loading::WalkSaveFile`
  (`Loading::SaveWalkDecision`: the 18 chunk tags and sizes in file order, the three flag
  bytes `byte_1015AD98/99/9A`, and the three patch values those flags select). It is not a
  second implementation: the converter is split into `NewSavesWalkFile` (open, the
  unchecked 0x110-byte header read, the magic at +0xC, the eighteen
  `fread(9)` / `operator new(size-9)` / `fread(payload)` / `free` iterations, the three
  size tests) plus the patch step it already had, and the new
  `NewSavesChoosePatch(forceRead, newChunk16)` is what *both* the patch step and the seam
  use to pick `74`/`EB`, `0CBEh`/`0E6Eh`, `75 26`/`90 90`. The seam patches nothing,
  installs nothing and writes none of the module's globals. The only production-path
  difference is *when* the three flags become module state (after the walk instead of
  inside its loop); the throw that ends a bad walk leaves the converter, so nothing can
  read the difference, and the write set is unchanged.
- `tests/save_walk.cpp` (new) + `CMakeLists.txt` (`zp_save_walk`, ctest entry 3). The
  oracle is the test's own parser (`std::ifstream`, written from the recovered format):
  it re-reads every file and computes the expected flags and the expected patch values
  from the literals recovered off the original's instructions.
- `docs/rebuilt.md` Â§2i (what is now proven and what is not), Â§3 class B's converter and
  prepare rows, Â§5 item 1, Â§6; `docs/parity.md`'s contracts table (one new row), the
  preserved-fault row for the corrupt save, and the NewSavesCompatibility section.

**The real data.** Two saves, one account
(`game_refs/saves/GTA IV/savegames/user_ee000000deadc0de`, `SGTA400` 1,977,934 bytes and
`SGTA401` 1,978,282); discovery is by content (`SAVE` at +0xC), so a second account is
covered. Both: `SAVE` at `0x0C`, eighteen `BLOCK` headers at `+0x110`, sizes `0xB9`,
`0xD9`, `0x1051`, `0x1C2B23`, `0x10F8`, `0xF`, `0xC8D`, `0xD55A`, `0x481`, `0x3AB1`,
`0x2F55`, `0xF9`, `0x549`, `0x1729`, `0x200`, `0x1916`, `0xCC7`, `0x34F1` (last chunk ends
at `+0x1E19C4`; both files are larger, which the walk never reads). Chunks 9/13/16 are
`0x3AB1`/`0x1729`/`0xCC7`, so the flags are `0/0/0` and the patch values are `74`,
`0CBEh`, `75 26` - the older layout, what the original's three `cmovne` pairs produce.

**The synthetic images.** `new_layout.bin` (9/13/16 = `0x4C45`/`0x1741`/`0x0E77`, rest 9)
-> flags 1/1/1, values `EB`, `0E6Eh`, `90 90`. `chunk13_only.bin` (only 13 = `0x1741`)
-> flags 0/1/0, values `74`, `0CBEh`, `75 26` (chunk 13's flag must not move the three
values; it only gates the second-load hook's twelve bytes). `old_layout.bin` (built from
the real save's own ids and sizes) -> flags 0/0/0, values `74`, `0CBEh`, `75 26`.

**The faults.** (a) A chunk size below 9 (`0` at chunk 0, `8` at chunk 9) reaches the
original's unchecked `add eax,0FFFFFFF7h` (`0x10051C14`) -> `operator new` and throws
`std::bad_alloc`; asserted in-process by type, so a different exception type fails. This
is the one expressible as an ordinary test. (b) A truncated header: the 0x110-byte read is
unchecked, so with `SAVE` at +0xC in the file the header is *not* rejected - observed
`bad_alloc` from the stale chunk header it then reads - and without the magic it is
rejected deterministically (both asserted in-process). (c) A file shorter than the magic
offset: the magic itself is uninitialised, so which branch runs is a property of the
*process*; that one runs in a child process (`--child <file> <report>`, `CreateProcessW`,
no shell), two 4-byte files that differ only in bytes the walk never reads, and the two
answers must be equal and one of `walked`/`notASave`/`bad_alloc` (observed `notASave` in
both). Also asserted: a missing path -> `noFile`, a 0x110-byte zero header -> `notASave`.

**The test can fail.** Mutating chunk 9's comparison to `0x1741` in `loading.ixx` fails
exactly two of the 258 checks and no others - `new_layout.bin: forceRead=0
(byte_1015AD98; chunk 9 size 0x4C45 vs 0x4C45)` and `new_layout.bin: branch opcode 0x74 at
site 872185h (chunk 9 size 0x4C45; newer layout -> 0xEB)`. The real saves stay green under
that mutation (their chunk 9 is `0x3AB1`), which is why the synthetic newer layout exists.

**Gates (final runs).** `cmake --build --preset release` exit 0; `ctest --preset release`
13/13 (was 12; the new test is `zp_save_walk`, 258 checks, 0 failures);
`python tools/parity_report.py 9` -> `build 9: 98 key(s) closed, 0 open, 1
oracle-verified (baseline incomplete)`; `python tools/verify_bodies.py ...` ->
`151 body(ies), 0 failure(s)`; `zp_dump --ooc-selftest` -> `ooc selftest: PASS`.

**What the test cannot express offline.** It proves the *decision*, not the writes: the
three site patches and the two hooks run only on a savegame load, so nothing here writes
the image; the prepare step (`0x10051FD0`, the `"<dir>\<name>"` path build and the game's
save-directory call) is not in reach either, since the seam takes a path; and whether the
walk's answer is the *right* answer for the data behind each chunk is a game-side
question. Â§5 item 1 keeps the in-game exercise.

**Open, not mine to edit.** `docs/review.md` still says "12 tests" and
`ctest --preset release # 12/12`; it is 13. `readable-fusion/AGENTS.md` line 11's body
count is still stale at 138/151 from the previous pass.

## 2026-11-07 - the two guarded native entries, executed at last: `zp_native_guard`

**What changed.** `0x100038E0` (59 instructions, guarded resolve-and-invoke) and
`0x10003980` (52, guarded resolve) are class-B rebuilds whose `__except` frame *is* the
behaviour, and that behaviour was argued from the disassembly only. It is now a ctest.
`zp_native_guard` (27 checks) runs both entries against a resolver the test installs,
with no game image and no ASI load. It is ctest 4, so the suite is 14.

- `source/native.ixx` - one new exported entry, `Native::SetResolverForTest(uintptr_t)`,
  a single store to `zpatch_native_resolver` (the slot `Register()` fills from the
  game's scan). It exists for the test; the shipped path never calls it. No other
  behaviour change: the two guards, the wrappers and the salts are untouched.
- `tests/native_guard.cpp` (new) + `CMakeLists.txt` (`zp_native_guard`, ctest entry 4).
  The fake resolver is naked because the layer calls its resolver with the hash in ESI
  and no stack argument (the game's resolver reads ESI; `0x626AF0` opens `mov eax,esi`);
  it answers a target the test picks, or 0.
- `docs/rebuilt.md` - section 1 class B's sentence, section 3 class B's two rows (the
  "how verified" cell no longer says "the fault path is unreachable offline"), section 5
  item 2, section 6 (a `tests/native_guard.cpp` bullet). `docs/parity.md` - a contracts
  row for the two entries, the native-layer section's behaviour paragraph rewritten
  around the four paths, and the resolver paragraph now says the test measures both
  halves.

**What the disassembly says, and what is asserted.** The original's scope table at
`0x10153EE8` has three-dword entries (`EnclosingLevel`, `FilterFunc`, `HandlerFunc`, no
address range), with filter `mov eax,1; ret` at `0x1000395A` and handler `... xor al,al;
ret` at `0x10003960`; the resolve's pair is `0x100039EC`/`0x100039F2`. So a fault while
the try level is 0 is caught wherever it was raised - including the resolver call.

- normal invoke: a target that returns 7 answers the original's `1`; the handler ran
  once and received the caller's block (`args[0] = 0xC0FFEE01`); the resolver saw the
  hash in ESI (`0x0BADF00D`).
- normal resolve: answers the handler address unchanged and did not call it; ESI too.
- fault inside the region: a handler that reads through null answers 0; a handler that
  writes to `0x10` answers 0; the process is still usable afterwards (the next normal
  call is asserted, so the frame unwound cleanly, not just "no crash yet"). A resolver
  that faults answers 0 through *both* entries - the invoke's `0x1000392B` and the
  resolve's `0x100039C9` are in the same frame.
- null handler: this is the **invoke only**. `test eax,eax / je 0x1000393F` (the `je`
  at `0x10003937`) skips `push edi; call eax; add esp,4` and falls to `mov [ebp-4],-2;
  mov al,1`: it answers **1** (success) without calling. The **resolve has no test at
  all**: it answers the null pointer to its caller (`0x100039D0`..`0x100039EB`).
- null resolver: the SEH catches `call [dword_1015BDE0]` with `dword_1015BDE0 = 0` and
  answers 0 in-process (both entries; asserted). The original's preserved fault is one
  level up: the *caller* reads through that 0. That read kills a process, so it is a
  child (`--child <report>`): the child installs the null resolver, calls
  `Native::Resolve`, writes `RESOLVED=0`, then reads `[eax+0Eh]` (the measured
  `DrivebyFOVFix` helper consumer, `0x1000646E`). The parent asserts the report says
  `RESOLVED=0` *and* the child exited `0xC0000005`, so "the guard answered 0" and "the
  caller faulted" cannot be confused with "the guard itself faulted".

**The test can fail.** Two mutations of the guards' fault path, both reverted (the file
is byte-identical afterwards: SHA-256 `CD5ACCCB...D02749A` before and after).
(a) Make the fault answer success - `return 1;` in the invoke's handler and
`return reinterpret_cast<void*>(1);` in the resolve's: exactly 7 of 27 checks fail and no
others, all of them the fault-value checks (`a handler that dereferences null answered
0`, the unmapped write, the resolver fault through both entries, the null resolver, and
the child's report `RESOLVED=nonzero`). The child's exit-code check still passed (the
read of `0x0000000F` is also an access violation), which is exactly why the report check
exists. (b) Make the guard re-raise - `EXCEPTION_CONTINUE_SEARCH` in both filters: the
seven normal-path checks pass, the process dies at the first fault call, and ctest
reports `***Exception: SegFault`, `0% tests passed, 1 tests failed out of 1`; the last
flushed line is the seventh normal-path PASS, which is why `Check` now flushes.

**Gates (final runs, after the revert and a full rebuild).** `cmake --build --preset
release` exit 0; `ctest --preset release` `100% tests passed out of 14` three times in a
row (6.42 s, 5.31 s and 5.63 s; no flake recurred); `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`; `python
tools/verify_bodies.py --original ... --candidate ... --bodies tests/bodies.tsv --map
...` -> `151 body(ies), 0 failure(s)`; `build\win-x86\Release\zp_dump.exe --ooc-selftest`
-> `ooc selftest: PASS`.

**What stays unproven.** The test pins the frame's four outcomes, not the faults' sources:
which real game faults actually raise inside the region is a play question, and the
caller-side fault uses a stand-in caller (the read the measured consumer performs), not
`DrivebyFOVFix` itself. `Native::SetResolverForTest` is compiled into the shipped ASI; it
is inert unless called, and the shipped path does not call it. The test calls
`Native::Resolve`/`Invoke` (the C entries), not the six-byte `jmp [slot]` forwarders the
arms reach them through.

**Open, not mine to edit.** `docs/review.md` line 14 still says "13 tests" and line 24
`# 13/13`; it is 14. (`readable-fusion/AGENTS.md` is current: it says 151 hook bodies.)

## 2026-11-07 - the arena, executed: `zp_nms_arena`, 246 assertions

The `NewMemorySystem` arena (`0x10038360` allocate, `0x10038430` free, `0x100385A0`
membership) was class D's remaining allocator: rebuilt from the disassembly because its
callees are the original's inlined `std::_Hash`, with no behavioural evidence - an
allocator whose only failure mode is corruption in play. It is now a ctest entry.

**The seam.** `source/loading.ixx` grew `MemoryArena::Get` (exported, test-only): the
three `__stdcall` entries the vtable publishes plus the registry lock's one-time setup.
`PublishNewMemorySystem` used to own the `InitializeCriticalSection`; both paths now call
`PrepareArenaRegistry`, a no-op on its second call. Nothing else in the module changed: the
four scheme lines are the original's, no site is written, no arm installed.
`tests/nms_arena.cpp` writes no files, so `.gitignore` is untouched.

**What is pinned.** 42 `(size, align)` pairs (sizes 0, 1, 0xF, 0x10, 0x11, 0x1000 against
alignments 0, 1, 4, 8, 0x10, 0x20, 0x100): `pointer % unit == 0` (the `cmovae` unit and
the `* unit` rounding), the distance at `[pointer-4]` in `[unit, 2*unit)` (the
`(base-1)/unit + 2` rounding - with `base = q*unit + r` the distance is `unit` at `r = 0`
and `2*unit - r` otherwise), `HeapSize(base) == want + 2*unit` (the `lea eax,[edi+esi*2]`
request), `size` bytes writable, and pairwise-disjoint live ranges. Then the three limits
(`align 3` rounds to unit, `align 0x30` is taken at face value, the flags argument is
ignored), the distance word and the `0xCD` fill over exactly `want` (including a poisoned,
freed, re-taken block whose slack stays untouched), membership (live/freed/stranger/
`pointer+1`/0; the original answers in AL, `setne al`, so only the byte is read), the null
free and the freed block reused, and the failed request: `0x7FFFFFF0` (sum
`0x80000010`, no wrap) answers null with no throw and the arena still works afterwards.

**The fill's end.** `memset(aligned, 0xCD, want)` cannot be observed on a fresh block
because the slack bytes are undefined. The test poisons a block with `0xAA` (all but the
four-byte distance word the free reads), frees it and asks for the same size until the
heap returns it - 16 attempts of 16 held probes, because the process heap is free to serve
a request from anywhere (210 consecutive suite runs never missed); a heap that never
returned a freed block would be reported, not skipped.

**The test can fail.** Three mutations, all reverted (the four scheme lines verified back
to the original afterwards): (a) `want + unit * 2u` -> `want + unit`: the `HeapSize`
check fails (1, 5 and 10 lines on three runs, depending on where the heap hands a
corrupted block back) and the process then dies with `0xC0000374` - the `want`-byte fill no
longer fits, so the slack cannot be shrunk; (b) `want = max(size, unit)` -> `want = size`:
28 failures (25 `HeapSize`, 3 fill), 218 pass, no fault; (c) the memset's count `want` ->
`size`: 3 failures, all in the fill class, 243 pass. The `size < unit` pairs are what
make (b) and (c) visible at all.

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 15/15 twice;
`python tools/parity_report.py 9` -> `build 9: 98 key(s) closed, 0 open, 1
oracle-verified (baseline incomplete)`; `python tools/verify_bodies.py ...` -> `151
body(ies), 0 failure(s)`; `zp_dump.exe --ooc-selftest` -> `ooc selftest: PASS`.

**What stays unproven, and why.** A double free is not tested and cannot be asserted
offline: the original's free has no membership check (`0x10038449` tests null only,
`0x10038457` recovers the base, `0x10038474` calls `HeapFree`), so a second free hands
`HeapFree` a base derived from the heap's own links; whether that is a no-op, a
`STATUS_HEAP_CORRUPTION` or silent corruption is the heap's decision. The rebuild keeps
the same missing guard and the same order (free, then erase); the test asserts only that
the pointer stops being a member. Also unmeasured: the container behind the registry
(only membership is observable), the lock under contention, and the wrapping request
(`0x1003838B` is a 32-bit add, so `>= 0xFFFFFFE0` wraps into a tiny block and a huge
memset - the original's fault, kept, recorded in `parity.md`, not run).

**Open, not mine to edit.** `docs/review.md` line 14 says "14 tests" and line 24 `# 14/14`;
it is 15/15. `readable-fusion/AGENTS.md`'s harness list also does not name `zp_nms_arena`.
Both are outside the files this pass was given.

## 2026-11-07 - the INI faults, executed: `zp_fault_parity`, 28 assertions

**Closed:** the INI faults `docs/parity.md` carried in prose now have a behavioural
test, `tests/fault_parity.cpp` (one executable, ctest `zp_fault_parity`, timeout 300 s
so a re-introduced spin fails instead of hanging the suite). It writes one report file
in the build tree (`fault_parity_child.report`, gitignored).

**What it asserts**

- `std::invalid_argument` of **exactly** that type (`typeid` pointer equality, not
  `catch (...)`) out of `Catalogue::Read` -- the loader's own entry -- for `Key=abc`
  and a spaces-only `Key= `; the parse stores the text first, so the throw is the read,
  and an empty `Key=` throws nothing and reads the arm default.
- `std::out_of_range` of exactly that type for `99999999999`, `-99999999999`,
  `INT_MAX + 1` and `INT_MIN - 1`, with `INT_MAX`/`INT_MIN` still read (the boundary
  really is `int`).
- A first-line BOM: no throw, the document has no section, and **all 99** catalogue
  keys read their arm default; the same text without the BOM sets every key, so the
  default check cannot pass vacuously.
- A key line before the first `[section]`: a **child process** exits `0xC0000005`, and
  its report holds exactly `PHASE=control-parsed` + `PHASE=parse-start` and no
  `PHASE=parse-returned`, so the crash is after the diagnostic point. In-process, the
  shapes that return before the store are quiet: an empty value, a line without `=`, a
  line starting with `=`, a comment; the same key line after a section is stored.
- A line at/over the 255-byte buffer: 255 bytes are kept whole, 256 are cut after 255,
  the remainder is parsed as a fresh line (a key can start there: `A=` + 253 `x` +
  `B=2` stores `B=2`), and the parse terminates and reads the rest of the file.

**Falsification.** Flipped the `typeid(std::invalid_argument)` comparison to `!=`,
rebuilt, ran: `FAIL invalid_argument: Key=abc throws ... (threw class
std::invalid_argument ("invalid stoi argument"))`, `28 check(s), 1 failure(s)`, exit 1.
Reverted and rebuilt.

**Two `docs/parity.md` corrections, both from the oracle, not from the test.**

- The long-line row was in **Preserved faults** and said the original splits; the
  oracle does not. `sub_10059D70` is the 7.65 parse body: a 256-byte buffer
  (`String`/`Source[255]`), `getline(&String, 0x100)` at `0x10059E0E`, and the loop
  tail `test byte ptr [eax+ecx+0Ch], 1 / jz 0x10059E00` at `0x1005A53E` -- i.e.
  `while (!eof())` (MSVC eofbit is 1). A 256+ byte line fills the buffer, so `getline`
  stores 255 bytes, consumes nothing and sets `failbit`, not `eofbit`; every later
  `getline` is a sentry no-op, so the original re-reads the same buffer **forever** (a
  startup hang). The tree's split is a divergence, and the row moved out of the table
  into the note, which now states the measured original behaviour.
- The BOM row's original-side mechanism is wrong: 1.41's `line[0] == 0xEF` is a
  signed-`char` compare (`-17` against the literal `239`), so it can never be taken --
  the image's `movsx` at `0x10059E93` shows `char` is signed -- and `sub_10059D70` has
  no compare against `0xEF` at all. The tree's drop is the 1.41 *source's* intent,
  pinned by the test; what the oracle's loader wrapper does with a BOM is unmeasured
  (it builds a `wstring_convert<codecvt_utf8_utf16>` around the bytes before the memory
  parse, `sub_10056130`), so the note records the oracle side as open rather than
  settled. This is a candidate fidelity question for `source/config.ixx`, not touched
  here.

`docs/review.md` counts updated (17 -> 18 tests; the run-history paragraph now says
which counts belong to which pass).

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 18/18
twice (7.67 s, 6.53 s); `python tools/parity_report.py 9` -> `build 9: 98 key(s)
closed, 0 open, 1 oracle-verified (baseline incomplete)`; `python tools/verify_bodies.py
...` -> `151 body(ies), 0 failure(s)`; `build\win-x86\Release\zp_dump.exe --ooc-selftest`
-> `ooc selftest: PASS`; `python tools/check_fault_dialogs.py` -> `10 test source(s), 0
failure(s)`.

## 2026-11-07 - the full byte audit: 151 rows, 2,965 differing bytes, 0 unclassified

An independent second implementation (own PE reader, own map reader, own comparator - it
does not import `tools/verify_bodies.py`) audited every row of `tests/bodies.tsv` against
the oracle, at the byte level: 151 rows, 4,846 instructions, 16,392 bytes on each side,
2,965 differing bytes. Every differing byte is charged to a class and the classes add up
exactly: 717 slot fields / 2,135 bytes (an absolute operand now naming our own storage), 241
immediate fields / 674 bytes (a call/push/mov value naming our own code or a literal), 57
registered-table fields / 156 bytes, and **0 unexplained**. 314 internal branch targets land
on the same body-relative offsets (an independent re-derivation, not the shipped proof).
All nine registered tables resolve through the linker map at their declared length (the
headlight hash pair's `sizeof(array) + sizeof(uint32_t)` = 2,052 included) and compare by
content. 33 pushed string literals compare equal. 512 of the 958 slot/immediate operands
resolve to a declared symbol or an interior address whose comment documents the original VA,
and all 512 pairs match.

The auditor was falsified per row before its clean run: 614/614 injected mutations detected
(opcode byte 151/151, plain immediate 119/119, internal branch 79/79, slot moved out of the
image 107/107, table displacement moved inside its table 7/7, budget one instruction short
151/151).

**One defect, fixed.** `source/loading.ixx` documented the two BenchmarkFix resumes as
`dword_10159CC4`/`dword_10159CC8`; those are EmissiveLerpFix's scale and resume words.
The bodies read `0x1015AD50`/`0x1015AD54` (`jmp dword ptr [0x1015AD50]` at
`0x10005659` and `[0x1015AD54]` at `0x10005679`), which is what the tree's slots and
arm already did - comment-only, fixed. This was F8 of `.agents/rewrite/05-body-audit.md`;
F1 and F13 there are also closed (the manifest budgets are the true lengths, checked in both
directions). The arm/write-set findings of 05 (F2, F3, F5) are untouched and out of this
audit's scope; F4's resolver thunk is still present and is recorded as O3 in the report.

Report: `.agents/rewrite/06-body-audit-full.md` (per-row table with VA, instruction count,
byte size both sides, class counts and verdict). Tool: scratch, outside the tree
(`.scratch/body-audit/audit_full.py`), JSON with every finding
(`.scratch/body-audit/audit.json`).

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 18/18 twice;
`python tools/parity_report.py 9` -> `build 9: 98 key(s) closed, 0 open, 1
oracle-verified (baseline incomplete)`; `python tools/verify_bodies.py ...` -> `151
body(ies), 0 failure(s)`; `build\win-x86\Release\zp_dump.exe --ooc-selftest` -> `ooc
selftest: PASS`; `python tools/check_fault_dialogs.py` -> `10 test source(s), 0
failure(s)`.

## 2026-11-07 - the OOC init, executed: zp_ooc_init, 54 checks

sub_1004B750 (991 instructions, class B) was the largest rebuilt piece with no
behavioural test; the selftest pinned only what the transform produces. tests/ooc_init.cpp
pins the init's own semantics through a test-only seam, OocInit in source/ooc.ixx (the
init, the three slots, the driver, the push_back, the vector's clear, the resource); the
seam is dropped from the shipped image by /OPT:REF (no OocInit symbol in the map).

The guard dword_10159C00 is named once in the image - at the read - so the branch is
never taken and the body re-runs and leaks: three calls, three distinct archives, one
182,272-byte allocation each, zero frees. Cursor dword_10159C78 = mount count
dword_10159BFC = 182,272, archive fnv1a 0xB5AF7B10, record 179536/0/1/88/688,
name finale2.sco at +0x24, payload at 0x800 equal to the decoded table.

A damaged Adler-32 trailer and a dead first opcode both still register and build, with
no throw - which is the original's own behaviour, not a guard. Falsified four ways:
a working guard 4 failures, fail-closed decode 3, wrong mount count 4, renamed entry 5.

Gates: build exit 0; ctest 19/19 twice; 98 closed / 0 open / 1 oracle-verified;
151 bodies 0 failure; ooc-selftest PASS; check_fault_dialogs 11 sources 0 failures.

## 2026-11-07 - the DFA close wrapper: identified from the scope table, fixed, and pinned

sub_10004790 (45 instructions, 0x81 bytes, EH4) was the last DFA body without a
behavioural test - and its description in docs/rebuilt.md was wrong. The scope table
0x10153FC0 names the filter 0x100047EC (`83 C8 FF; C3` = `or eax,-1; ret`) and the
handler 0x100047F0 (`mov eax,1`, the `return TRUE` arm), so the filter answers
EXCEPTION_CONTINUE_EXECUTION: a fault in the guarded `CloseHandle` call is retried, not
answered, and the handler is unreachable. The class-B row said "answering -1 when the
call faults"; that was the *disposition*, not an answer.

Which C shape that is, proved in bytes: compiling four candidate shapes at /O2 /MT /GS,
`__try { return CloseHandle(h); } __except (EXCEPTION_CONTINUE_EXECUTION) { return
TRUE; }` reproduces the oracle exactly - 45 instructions, 0x81 bytes, funclet bytes
identical at +0x5C/+0x60 - and differs in 16 bytes in 7 words, all four relocated
operands (+0x06 scope table, +0x0B handler, +0x1D cookie, +0x3F CloseHandle slot). The
tree's EXECUTE_HANDLER/-1 shape differed at +0x5C..+0x6F (the funclets swapped), and a
child with a faulting callee measured the difference: the oracle's shape never returns,
the tree's answered 0xFFFFFFFF. Per the parent's decision the rebuild now uses the
oracle's shape; module_body.py compares the shipped launcher.obj with the oracle and
reports `16 differing byte(s), all relocated; IDENTICAL except the relocated operands`.

tests/dfa_close.cpp (zp_dfa_close, 27 checks) drives the five bodies through a new
test-only seam, Launcher::DfaBodiesForTest in source/launcher.ixx (five addresses, the
four forwarder slots published by the same PublishDfaForwarders RegisterLauncher now
calls): a real 32-byte file opened by the create body, sized/seeked/read by the
transcribed ones, closed by the rebuilt one; the closed handle's FALSE/6 from
DuplicateHandle and from the reader, equal to ReadFile's own answer; null, double close
and 0xDEADBEEF all FALSE/6 (no argument guard - the only instructions before the call
are `mov [ebp-4],0` and `push [ebp+8]`); pass-through equality with CloseHandle;
fs:[0] unchanged across the call. The fault half runs in a child that replaces the
exe's own CloseHandle import slot (found through the PE import directory) with a callee
that writes one probe byte on entry and then faults: the parent's 1000 ms watchdog
requires it to be STILL RUNNING, the probe byte to be present and no ANSWERED line, then
terminates, reaps and asserts its own exit code. Writing the report before the patch
matters - the stream's close is the import being replaced.

Falsified twice: "does not close" -> 12 failures (closed-handle, null, double,
never-opened, frame failure path, whole child class); the divergent guard -> 5 failures,
all in the child class (ANSWERED=FFFFFFFF), the reachable surface still green - which is
exactly the unreachable-divergence shape. Seam dropped from the shipped image: 0 map
occurrences of DfaBodiesForTest/DfaBodies, and publish_check.py finds RegisterLauncher's
four slot stores still in the built ASI (4 of 4).

Gates: build exit 0; ctest 20/20 twice (zp_dfa_close 1.3 s); 98 closed / 0 open / 1
oracle-verified; 151 bodies 0 failure; ooc-selftest PASS; check_fault_dialogs 12
sources 0 failures.

## 2026-11-07 - the NewFiles scans: a texture dictionary, not a file registry, and not reachable offline

The three class-C pieces this round took - 0x10051730 (55 instructions), 0x10051610 (74)
and 0x100517E0 (53) - cannot be pinned by a synthetic test, and the labels this tree
carried for them were wrong. Read against the oracle and GTA IV 1.0.8.0:

- 0x10051730 returns **the current texture dictionary, *g_pCurrentTxd** (the global is
  at game VA 0x19CE510). The signature finds `mov esi,g_pCurrentTxd` at 0x85DF9B inside
  CSprite2d::SetTexture; the body reads that instruction's operand (`mov eax,[eax+2]`)
  and dereferences it (`mov eax,[eax]`).
- 0x10051610 is **rage::pgDictionary<grcTexture>::Lookup** (0x43A490 on 1.0.8.0; the older
  builds' fallback is 0x4059F0 on 1.0.0.0, 0x426430 on 1.1.1.0, ...).
- 0x100517E0 is **CSprite2d::SetTexture** (0x85DF40), because the signature lands at +0x14
  and the body calls `match-0x14`. It does not append: it releases the sprite's old
  texture, looks the name up in the current TXD and stores the new one (or clears it).

The consumers are the radio HUD's TXD loaders: __readRadioDat -> sub_8960B0 (first load)
and sub_896180 (reload), which loop the station list at 0x11F8114, build "RadioLogo%d",
call CTxdStore::loadFile/addRef/saveCurrentTxd/setCurrent and walk the sprite list at
0x11F8110 calling sub_8936A0/sub_893680. Those ten-instruction functions are
`if (name[0]) SetTexture(sprite,name); return name[0]!=0;` (name at +0x10/+0x10F, sprite at
+8/+4); the mod's update bodies add the membership test, so a sprite whose name the
just-loaded TXD lacks keeps its previous texture instead of being blanked. The other half
is not a file registry either: the published call operand is _registerNative (0x625620),
the replaced `ret` at 0xB0A970 is the tail of CScriptCommands::RegisterCoreScriptCommands,
and the mod registers native 0x2C89A1F9 with handler 0x100515D0.

Why no test: 0x10051730 takes no arguments (its input is GetModuleHandleA(0) and the game's
global); 0x10051610/0x100517E0 dispatch into game code. A fabricated dictionary or sprite
would assert the test's own stand-in, a fabricated image would only prove find_pattern found
the bytes the test embedded, and the per-string scan cache rules out a two-image fallback
test anyway. The only offline-observable part is the miss: in a test process
GetModuleHandleA(0) is the test exe, so 0x10051730 faults on `mov eax,[esi]` with ESI=0
(read at 0), and the other two call 0. That is one instruction of the fault path and no hit.

The per-image scan (nine images, base 0x400000) is rebuilt.md §2m, and it is the evidence a
test could not replace: the object string matches once on all nine; the primary membership
string on 1.0.8.0/1.1.3.0 and the older variant on the other seven; and on 1.0.0.0 the add
string's first hit is 0x4433E7, a release loop inside 0x443030, so `match-0x14` is
0x4433D3, mid-function. That is the original's own latent fault (the rebuild takes the same
first hit), and it is why a synthetic hit would prove nothing. 1.2.0.59's own SetTexture has
a different instruction order at 0x8D4189 (`8B CE 50`), so the object string misses there;
1.2.0.43 is gated (bit 18) but no image exists here, so its object hit is the one live
unknown.

Changed: docs/rebuilt.md (new §2m; class C/D rows, §4 item 1 and §5 item 10 corrected) and
docs/parity.md (the NewFilesCompatibility section, the preserved-fault row, the
rebuilt-piece policy paragraph). No code, no seam, no test: loading.ixx is untouched, and
its three comments still say "file registry"/"append" (the pass owned that file for test
exposure only).

Gates: build exit 0; ctest 20/20 twice (16.47 s / 12.72 s); 98 closed / 0 open / 1
oracle-verified; 151 bodies 0 failure; ooc-selftest PASS; check_fault_dialogs 12 sources
0 failures.

## 2026-11-07 - the destroy loop `0x1005CD50`: the free branch is unreachable, and no test can move it

`0x1005CD50` (`0x61` bytes, 38 instructions, IDA `ZolikaPatch.asi.i64`) is the
`_Destroy_range` loop the transcribed vector clear `0x10056500` calls at the OOC builder's
end. It destroys the fake-pak vector's elements: a 44-byte entry whose first 24 bytes are
the name `std::string` (inline buffer at +0, size +0x10, capacity +0x14) and whose five
dwords follow at +0x18..+0x28. The loop reads the capacity, frees the heap name when
`capacity >= 0x10`, then resets size=0, capacity=15 and the first byte, striding 0x2C.

The free branch is unreachable in both trees because the only entry ever registered is the
11-byte `"finale2.sco"` in an SSO string of capacity `0x0F`:

    1005cd60  8b 4e 14   mov ecx,[esi+14h]     ; the name's capacity
    1005cd63  83 f9 10   cmp ecx,10h
    1005cd66  72 27      jb  1005CD8F          ; 0x0F < 0x10: no free

and the capacity is the CRT copy constructor's own: `0x100578E8 mov ebx,0Fh` /
`0x100578F3 mov [esi+14h],ebx` on the SSO branch (`0x100578E0 cmp eax,10h / jae` takes the
heap path only for a size at or above 16). The `0x1000` block-header path and
`_invalid_parameter_noinfo_noreturn` are two comparisons behind the same guard.

The only push in the image is `0x1004C2A2 add dword_1016E440,2Ch` (the registration at
`0x1004C287`..), the growth helper `0x1005C350` has one caller (`0x1004C2AC`), and the
clear `0x10056500` one call site (`0x1004B12A`, `ooc_build_done`). The reachable states -
one entry, two entries (seam, or an allocation failure that unwinds past the clear), a
cleared-then-refilled vector - all carry the same `0x0F`; a long name cannot be produced
because `OocRegister` is the only push and hard-codes the name. The only observable is the
vector's size, which `zp_ooc_init` already asserts twice (`run.slots.entries == 0`,
`ClearEntries() == 0`) and whose guard input it also asserts (`name_capacity == 15`); the
three resets are dead bytes in a dead element that no seam exposes and no caller reads.

Why no test: exercising the free would need a new seam that registers a long, heap-backed
name - a state the production code cannot enter - so the assertion would be about
`OocDestroyEntries`' own model, not the original. A second registration asserted "empty"
would add nothing. The loop is still executed by the existing test (a wrong stride walks
off the allocation; an inverted guard hands `free` the inline buffer's first four bytes),
so a broken loop faults rather than passing silently.

Changed: docs/rebuilt.md only - §2h's "what stays rebuilt" paragraph and §2k's
entry-string bullet point at the new §2n, which quotes the guard, the copy constructor and
the five call sites. No code, no seam, no test; `source/ooc.ixx` untouched.

Gates: build exit 0; ctest 20/20 twice (13.81 s / 11.21 s); 98 closed / 0 open / 1
oracle-verified; 151 bodies 0 failure; ooc-selftest PASS; check_fault_dialogs 12 sources,
0 failures; zp_ooc_init 54 checks, 0 failures.

## 2026-11-07 - the literal pool: numbers, not hex bit patterns (steer consumer, CutsceneFixes)

**Changed:** `source/vehicles.ixx` (9 doubles + 2 floats) and `source/graphics.ixx` (2
doubles + 2 floats) - the slots the transcribed asm reads with `mulsd`/`movsd`/`divsd`/
`comisd`/`ucomisd` and `movss`/`mulss` - plus the convention note in `docs/parity.md`.
Each slot is now declared with the type its instruction reads and initialised with the
shortest significant-digit decimal that round-trips to the original's `.rdata` bytes (not
a hex word, no `bit_cast`, no explicit `alignas`: a scalar load does not need one on x86,
and the linker still placed every slot at the same address, all doubles 8-aligned).

- `double`: `-3.141592653589793`/`3.141592653589793` (0x101523A8/0x10152358), `180.0`,
  `0.5`, `-0.5`, `0.75`, `0.25`, `0.01`/`-0.01`, `0.0` (0x101522F0), `0.3333`
  (0x10152340, the double nearest 0.3333, not 1/3).
- `float`: `1.0f`, `100.0f`, `0.05f`, `0.5f`.
- Left as hex, because it is not a number: `zpatch_cutscene_abs_mask = 0x7FFFFFFF`, the
  `andps` sign mask (`movss` then `andps xmm0, xmm1`; the body clears the sign bit of a
  difference before comparing). Mutable slots (`zpatch_cutscene_accum`,
  `zpatch_engine_phase`, `zpatch_engine_timestep`) were outside the shape: they are
  init-0 state written by the body, not literal-pool constants.
- Same file, same convention, outside the hex shape: `BikePhysicsClamp`'s
  `kBikeSpeedThreshold` lost the `static_assert(std::bit_cast<uint32_t>(...) ==
  0x3BDA740E)` the rule forbids (the rule wants the number, not a bit-pattern assertion)
  and now reads `0.006666667f`, the shortest decimal that is 0x3BDA740E (the previous
  spelling, `0.0066666668f`, was 8 digits and not the shortest). Bits unchanged: the
  immediate appears once in the built ASI, as in the oracle, and the comparison's
  `0.006666666665` qword is the oracle's `0x10152308` (verified by disassembling the
  original's `sub_1001EF30`).

**Provenance:** every documented hex was re-read from the oracle at the VA in its comment
(16/16 MATCH), and every written literal re-parsed and packed to exactly those bits
(15/15). Note for the audio block: its `zpatch_mf_time_mul` byte pattern is the double
denoted by the decimal `0.075000003` (0x3FB333334015BC21), not `0.075f` widened
(0x3FB3333340000000).

**Byte proof:** baseline snapshot of each slot from the built ASI (`symbol -> VA`, PE
section mapping, zero-filled `.data` tail read as the zeros the loader maps), then the
same after the edit: all 16 checked lines (15 converted + the mask kept as hex)
byte-identical at unchanged VAs and whole-image occurrence counts unchanged (9 of the
converted patterns are unique; `pi`/`180.0`/`0.5` occur 3x, `1.0f` 12x, `0.5f` 18x, the
mask 155x, and the zero slot is zero-fill - the counts are inherited, not introduced,
which is why the address-based check is the strong one). `steer_consumer` and
`cutscene_chain_b` still pass the body proof with the operands compared as slots.

**Gates:** build exit 0; ctest 20/20 twice (24.26 s / 14.49 s, serial); `parity_report.py 9`
-> `build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`verify_bodies.py` -> `151 body(ies), 0 failure(s)`; `zp_dump --ooc-selftest` ->
`ooc selftest: PASS`; `check_fault_dialogs.py` -> `12 test source(s), 0 failure(s)`.




## 2026-11-08 - signature parity: the original handlers' own pattern strings

The class: an arm rescans a *shortened* (or wildcarded) copy of the signature its
handler scans. On 1.0.8.0 it happens to select the original's first match, so the
per-build byte baselines cannot see it; on another build it selects a different
function, and the arm patches a site the original never touches. The independent
cross-check found four such arms (`.agents/rewrite/08-independent-crosscheck.md`
§2.2/§3).

**Changed:** `source/stability.ixx` (14 literals), `source/graphics.ixx` (2),
`source/vehicles.ixx` (5) - each now the string the handler itself passes to its
scanner (`0x100055C0`, `0x10046B30`, `0x10037290`, `0x10036FD0`, `0x10037070`,
`0x10036F30`, `0x1003D7D0`, `0x100504B0`, `0x1001B0E0` ×3, `0x10045980`,
`0x10037100`, `0x10023B30`, `0x10005B20` ×2, `0x10022D80`, `0x1001BDD0`,
`0x1001BD60`, `0x1001BE40`, `0x10037170`). `tests/signatures.tsv` (21 rows
corrected, including the stale relocated `68 34 EE DA 00` row of
`IncreaseVehicleStructLimit`), `docs/parity.md` (new signature-parity section, one
contract row, one Status bullet), `CMakeLists.txt` (ctest `zp_signature_parity`),
new `tools/check_signature_parity.py`.

- Four arms patched a different site off 1.0.8.0: `AimProjectileCrashFix`
  (1.0.0.4/1.0.4.0 wrote at 0x1BC88/0x1ABD8; the original's only match is
  0x5AC560/0x6E3150), `TryToSkipAllErrors` (1.0.0.0/1.0.0.4/1.0.4.0/1.1.1.0/1.1.2.0
  wrote at 0x1FF800/0x3700/0x1E10/0x1E50/0x1E70; the original's only match is
  0x55F60/0x53240/0x526B0/0x52700, and on 1.0.0.0 it has *no* match, so the original
  writes nothing), `MPNikoCrashFix` (wrote `EB` on 1.0.0.0/1.0.0.4/1.0.4.0 where the
  exact signature is absent) and `BlackscreenFix` (wrote `E9`+rel32 on
  1.0.0.4/1.1.1.0/1.1.2.0 where it is absent).
- The 17 other literals selected the same sites as the original's string on all nine
  images (truncations and wildcards that never mattered), and are now the original's
  strings too. First-hit anchors moved on exactly the four arms above; every other
  arm's anchor is unchanged on every image.
- Gate: none of the four handlers has a build ladder - it scans one signature and
  tests the answer (`if (result)`), so the exact signature *is* the gate and a build
  with no hit gets no write. That is now the arm's behaviour. The arm with a real
  ladder in this area, `RemoveRGSCLoginRequirement` (`0x10045490`: primary signature,
  else the fixed fallback site only when build == 9), already reproduced it.
- Guard: `tools/check_signature_parity.py` maps each `find_pattern` literal to its
  catalogue key, takes the key's original strings from `handler_table.json`,
  `option_signatures.json` and (for keys neither covers) the ASI's own stored
  strings, associates exact copies and loosenings, and requires the per-image hit
  set (count and every RVA) to equal the original's on all nine images. Falsified
  against the bug: before the fix it exits 1 with e.g. `GTAIV_1004: hit count 2 vs 1;
  ours 2 hit(s), first RVA 0x1bc88, original 1 hit(s), first RVA 0x5ac560`; after it
  exits 0 (132 measured, 0 mismatches, 0 loosenings, 100 derived anchors outside the
  check). Re-shortening the Aim literal reproduces the failure.
- The 1.0.8.0 dump is byte-identical across the change: the per-option dump with the
  pre-fix strings and with the fixed strings both hash to
  `E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327`.

**Gates:** build exit 0; ctest 22/22 (154-190 s); `parity_report.py 9` ->
`98 closed / 0 open / 1 oracle-verified`; 6 -> `98/0/1`; 5 -> `97/1`
(`CarDynamicShadowFix`'s documented baseline omission); 7 and 8 -> `97/1` (the
`Patch7RemoveModChecks` attribution alias); 11, 12, 13 -> `98/0/1`; 1 can only be
scored from its written per-option file (the dump's post-loop full-cell run faults at
`NoImpostorHeightCutoff` by design) -> `96 closed, 2 open` (the CarDynamicShadow
omission and the designed fault cell), every arm above closed. `verify_bodies` ->
164 bodies, 0 failures; `zp_dump --ooc-selftest` PASS; `check_fault_dialogs` 12
sources, 0 failures; `check_relocated_literals` 1781 hits, 0 findings;
`crosscheck_reconstruction`: the four arms' disagreements are gone (build 1/5/6/12/13
now zero different/ours-only; build 11's only difference is the documented
SkipLauncher DFA-stage over-claim, 7/8's only recon-only key the Patch7 alias), and
with the already-present `FreezeCarFix`/`VRAMFix` arms the reconstruction agrees with
build 9 (89 confirmed, 0 different).

## 2026-11-08 (readable-fusion, asm-to-tail reorder): graphics, native, launcher done
- Task: keep every readable section at the top of each feature module and move every `static __declspec(naked)` body and `__asm` block to a marked tail. No behaviour change is allowed, so the gate is a byte-identical build-9 per-option dump.
- Done so far (each gated separately): `source/graphics.ixx` (14 bodies, 24 namespace members regrouped helpers -> Publish -> Install), `source/native.ixx` (7 bodies; the exported surface stays above its definitions, the two rebuilt guarded entries and `NativeZeroBlock` stay in the readable section), `source/launcher.ixx` (7 bodies; the row tables / ROM service namespace stays above the tail because `RomResetBody`'s asm calls `StartRomService`, and the rebuilt EH4 `CloseHandleBody` is plain C++ so it stays readable).
- MSVC fact (measured with cl 14.51): `__declspec(naked)` on a *declaration* is error C2488 ("'naked' can only be applied to non-member function definitions"). So the forward declarations above the tail carry the signature only (`static void Name();`) while every definition in the tail keeps `__declspec(naked)`; linkage is unchanged (all bodies stay `static` at module scope).
- Gates after graphics+native+launcher: build exit 0; ctest 22/22; `parity_report.py 9` 98 closed / 0 open / 1 oracle-verified; `verify_bodies.py` 164 bodies, 0 failures; build-9 per-option dump `E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327` before == after; `zp_dump --ooc-selftest` PASS.
- The reorder itself was done mechanistically (`.scratch/reorder/refactor.py`: parse top-level units with comment attachment, re-emit in a planned order) and audited by line-multiset comparison, so no source line is lost: the only new lines are the banner and the forward declarations.

## 2026-11-09 (readable-fusion, asm-to-tail reorder complete): all six modules, 98/0/1 held
- Layout contract now holds in `source/vehicles.ixx`, `graphics.ixx`, `audio.ixx`, `native.ixx`, `ooc.ixx` and `launcher.ixx`: module header, published state (the `extern "C"` slots), plain-C++ helpers, `Publish<Key>()`, `Install<Key>()`, `Register<Key>()`, then a marked tail ("Transcribed bodies...") holding every `static __declspec(naked)` body and `__asm` block. A script check (`.scratch/reorder/layoutcheck.py`) proves no `__asm`/`__emit` line survives above the banner: graphics 14 bodies, native 7, launcher 7, audio 21, ooc 20, vehicles 44 (34 explicit + 10 expanded from `ZP_SPAWNS_CASE`).
- Reordering only: the source lines were parsed into units (comment-attached) and re-emitted in a planned order; a line-multiset audit shows the only new lines are the banner and the forward declarations (graphics 14, native 7, launcher 7, audio 21, ooc 20, vehicles 44). Inside the readable namespaces the members were regrouped helpers -> Publish -> Install (graphics 24, launcher 11, audio 20, ooc 4, vehicles 46), keeping each module's original relative order inside a group, so every `Register*()` lambda keeps its call order (`InstallCarDynamicShadowFix` before `InstallForceCarHeadlightShadows` unchanged).
- Deviation, recorded: vehicles' `NoliveryFormat` was a naked body inside an anonymous namespace; it moved to the tail as a file-scope `static` (dedented), so its mangled symbol changes from the `?A0x...` form to `?NoliveryFormat@@YAXXZ`. No proof row names it (tests/bodies.tsv has only the `Nolivery*Body` rows) and nothing else changes. `NoliveryResolve` stays in its namespace because `RegisterVehicles` takes its address, which a file-scope forward declaration could not name. Only one comment needed a position fix: native.ixx "the two C entries below call it" -> "above".
- Gates after each module and again at the end: build exit 0; ctest 22/22; `parity_report.py 9` 98 closed / 0 open / 1 oracle-verified; `verify_bodies.py` 164 bodies, 0 failures; build-9 per-option dump `E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327` identical before and after; `zp_dump --ooc-selftest` PASS.

## 2026-11-09 (readable-fusion, deterministic dump base + relocation-delta guard)
- Defect: `zp_dump` VirtualAlloc'd its image copy anywhere, so the loader delta was the allocator's (64 KiB-aligned, run-dependent). Reproduced on the pre-change binary: six runs of one `zp_dump.exe --per-option --build 9` gave `E20AF4EA...45327` five times and `FD006036...30B19` once; the differing run was missing exactly one row, `NewMemorySystem 001082 00`, because the relocated high half at `0x1082` happened to equal the arm's byte and the row convention suppressed it.
- Why the prescribed preferred-base pin cannot work here: a Win32 process cannot own `0x00400000`. A no-CRT custom-entry 32-bit probe fails `VirtualAlloc(0x400000, 0x18B2000)` with 487 on its first instruction; `VirtualQuery` shows `C_1252.NLS` mapped at `0x400000` and the heap at `0x4A0000`, so the 26 MB range is fragmented. Unaffected by the tool base (0x10000/0x20000000/0x400000), ASLR on/off, or a static-import DLL at 0x400000 (relocated to 0x960000); the NLS mapping is a loader syscall (`ntdll!ZwInitializeNlsFiles`). `../readable/CMakeLists.txt:172-185` records the same and builds its dump 64-bit.
- Fix: `tests/dump_main.cpp` tries the image's preferred base first (delta 0 when it is free) and otherwise pins the copy at `kFixedImageBase = 0x30000000`; `ApplyRelocations` refuses any base it did not pin and runs the `.reloc` pass with the constant delta `0x2FC00000`. `tests/salt_check.cpp` uses the same mapping. `CMakeLists.txt` links both tools at `/BASE:0x20000000 /DYNAMICBASE:NO` so their own images cannot collide. Six runs of the fixed-base dump are byte-identical: `E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327`, 12,898 rows, every run.
- The pin exposed a second, live defect class: a signature whose fixed bytes span a fixup's high half fires on some launches and not others. `tools/check_relocated_literals.py` now sweeps every pattern-like literal in `source/*.ixx` (including the table rows the old `find_pattern("...")`-only extraction missed) at six deltas (0x00010000, 0x01000000, 0x05600000, 0x05D90000, 0x2FC00000, 0xFFFF0000), requiring the hit set (count and RVAs) to equal the unrelocated set at every delta, and classifies a changed literal against the key's recovered original strings. It runs in the existing `zp_relocated_literals` ctest (parallel over the nine images, ~4.5 min).
- Full sweep (319 literals, 9 images, 6 deltas): exactly one changed literal, `misc.ixx:225` (`MiscFixes`, handler 0x10023DD0) `C7 06 ? ? ? ? 74 19`, only at delta 0x05600000 (one hit at RVA 0x739733). It is the original handler's own string and the reconstruction's own `game_hits` records 0 hits on the unrelocated image, so it is a **preserved original fault**, not ours to fix: the original writes `EB` at 0x739739 on the launches whose base produces that delta and nothing on the rest. Left verbatim (1:1); recorded in `docs/parity.md` Preserved faults with the witness and in the pinned-dump section. 0 ours-to-fix, 0 unverified.
- Falsification of the guard's (b) branch: a synthetic base-dependent literal `C7 06 ? ? ? ? 74 19 8B` (the row plus one concrete byte, not in the evidence) hits only at 0x05600000 and is classified "ours to fix"; the real literal is classified "preserved original fault".
- Gates: six-run dump identical (above); `parity_report.py 9` 98 closed / 0 open / 1 oracle-verified; `dump_matrix.py --only 9` full cell 947 bytes, differing 0, missing 0, extra 57 (all `SkipLauncher`, the documented oracle-verified baseline gap), 98/99; `ctest --preset release` 22/22 twice (289 s and 341 s, `zp_relocated_literals` 259 s); `verify_bodies.py` 164 bodies, 0 failures; `zp_dump --ooc-selftest` PASS; `check_fault_dialogs.py` 12 sources, 0 failures; `check_signature_parity.py --game ../game_refs` 132 measured, 0 mismatches; `crosscheck_reconstruction.py` build 9: 89 confirmed, 0 different (build 11's one `different` is the documented SkipLauncher DFA-stage over-claim, 0 differing bytes).
- `docs/parity.md`: rewrote "The per-option dump pins its image base" (why 0x400000 is impossible, the fixed base, the pre-pin divergence witness, the guard) and added the `MiscFixes` preserved-fault row.


## 2026-11-09 (readable-fusion, the last two catalogue keys: AddOptionsInMenu and FixPCIDKickbug)
- Both keys were the tree's only no-arm keys. `FixPCIDKickbug` is the registry delete; `AddOptionsInMenu` is the IVMenuAPI integration and is not write-free. Both implemented; the build-9 per-option dump is byte-identical (`E20AF4EA...45327`), because the harness has no `IVMenuAPI.asi` and the registry arm is behind the launch arm's module guard.
- New `source/menu.ixx` (registered after `RegisterAudio()`, so the AddOptionsInMenu death-music installs can see whether `RestoreDeathMusic` already owns them): the `sub_10052380` probe (`GetModuleHandleA`/`LoadLibraryA("IVMenuAPI.asi")`, one-shot `byte_101A843C`), the writer `sub_1001EDE0` (36, transcribed), the installer `sub_10050BD0` (133, transcribed), the death-music gate `sub_10050A50` (41) and its three native wrappers `0x1005B1E0`/`0x1005B360`/`0x1005B490` (48/44/46), the four menu callbacks `0x10050A30`/`0x10050A40`/`0x1001EDB0`/`0x1001EDC0` (2/6/4/6), all transcribed and proven. Two C++ rebuilds: `sub_1001ECB0` and `sub_10050AD0` (EH4 frame + TLS static-init guard; the blocking instructions are quoted in the module and `docs/parity.md`), plus the two plain handlers `sub_10052380`/`sub_100522B0`.
- The original's `RestoreDeathMusic == 1 || sub_10052380()` split: audio's arm owns the first half, the menu arm the second, so `sub_10050BD0` runs once. The shared death-music word is audio's `zpatch_audio_death_music_flag` (declared `extern "C"` in menu.ixx), so the menu getter/setter drive the same byte whichever gate is installed. The focus site and its pre-write word are resolved in the publish tier because audio's DoNotPauseOnMinimize arm overwrites the same two bytes before this arm runs.
- `FixPCIDKickbug` in `source/misc.ixx`: `SHDeleteValueW(HKEY_CURRENT_USER, L"Software\\Classes\\Software\\Microsoft\\XLive", L"PCID")`, result ignored, behind `Loader::Enabled` + `Game::Module() == nullptr` (the value is the user's, not the image's). Measured: the machine's PCID value survived a full per-option dump (its cell sets the key to 1).
- Recorded: the `ZMenuIV.asi` `GetZMenuVersion` guard (`sub_10052300`, called unconditionally at `0x1005527B`, not part of this key) remains unimplemented and is named in `docs/parity.md`. The IVMenuAPI library's only referenced export is `AddIVMenuOption`; no other entry point is called anywhere in the ASI. The `push ecx` third argument of `sub_100522B0` is an undefined leftover register that the callee never reads.
- Bodies 164 -> 174 (`tests/bodies.tsv` +10 rows; the gate/wrappers duplicate audio's recovered VAs because a body has internal linkage and this arm needs them under its own key; only one copy is live per configuration). `tests/signatures.tsv` + the five AddOptionsInMenu site rows (the same recovered anchors the two audio arms use).
- Gates: build exit 0; `ctest --preset release` 22/22 (182.6 s); `parity_report.py 9` 98 closed / 0 open / 1 oracle-verified; build-9 per-option dump before == after (`E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327`, 384,422 bytes, 12,898 lines); `verify_bodies.py` 174 bodies, 0 failures; `check_signature_parity.py --game ../game_refs` 132 measured, 0 mismatches, 0 loosenings; `check_oracle_verified.py` 3 rows / 0 failures; `check_fault_dialogs.py` 12 sources / 0 failures; `zp_dump --ooc-selftest` PASS; `gen_body_targets.py --check` up to date (100 rows).
- Not verified: no game launch, so the menu render, the callbacks firing and the loaded library's own behaviour are runtime properties. `docs/rebuilt.md`'s "covers 164 bodies" line is now stale (outside this pass's edit set).

## 2026-11-09 (readable-fusion, per-build oracle-verified class)
- `tests/oracle_verified.tsv` now has five columns (`key`, `builds`, `why`, doc anchor, measurement) and one row per key-and-build set: `SkipLauncher *`, `CarDynamicShadowFix 1,5`, `Patch7RemoveModChecks 7,8`. The scoreboard consults a row only on the builds it names; `tools/check_oracle_verified.py` pins the measured omissions (all builds / {1,5} / {7,8}), requires an existing `docs/parity.md#...` heading, a real justification and measurement, and names the wrongly excused builds when a row is too broad. `tools/parity_report.py` and `tools/dump_matrix.py` both honour the per-build row (the matrix gained an `oracle` column and labels residuals `oracle-verified baseline omission`).
- Evidence for the @noE row, from the oracle: `sub_10010B60` at `0x10019039` scans pattern E and `jne 0x1001ACF0` when it hits; otherwise `mov ecx,[ebp-0x330C]` (the C match), `lea edx,[eax+6]`, `add ecx,0x3E`, `call 0x10001680`. `0x10001680` is `VirtualProtect(...,0x40); memset(a1,0x90,size); VirtualProtect(restore)`. On 1.0.0.0 C=0x44DF1B -> fill at 0x44DF59; on 1.0.4.0 C=0x46DFEA -> 0x46E028. Both images hold a 6-byte `0F 84 ...` there and the stored baselines have no row; ours writes six `90`s. Pattern E has one hit in the nine images (1.0.0.4, RVA 0x64A6AB), which is the gate.
- Falsification: with the CarDynamicShadowFix row set to `*`, the guard fails `excuses build(s) [6, 7, 8, 9, 11, 12, 13]; the measured omission covers only [1, 5]`; build 9's scoreboard alone does not catch it (the key is closed there), which is why the build scope is pinned in the guard. Row reverted.
- Gates (settled tree, one build, exit 0): ctest 22/22 (`zp_relocated_literals` 231.84 s; total 249.97 s); parity_report 5 = 97/0/2, 6 = 98/0/1, 7 = 97/0/2, 8 = 97/0/2, 9 = 98/0/1; check_oracle_verified 3 rows / 0 failures; check_signature_parity `132 measured, 0 mismatch(es), 0 loosening(s)`; verify_bodies 164/0; `zp_dump --ooc-selftest` PASS.
- docs/matrix.md regenerated: build 5 full-cell residual `extra 405: SkipLauncher 399 (oracle-verified baseline omission), CarDynamicShadowFix 6 (oracle-verified baseline omission), all oracle-verified baseline omissions`; per-option 5 = 97/0/2, 7/8 = 97/0/2, 9 = 98/0/1; build 1 = 96/0/2 isolated with `NoImpostorHeightCutoff` the designed fault (its full cell also hit the documented 0xC0000005 this run, so its buffered rows were not kept).
- Note: the shared tree was edited concurrently (`misc.ixx`, `launcher.ixx`, `loading.ixx`, the Patch7 row) before the parent committed 9790824; the numbers above are from the settled tree and the one build after it.

## 2026-11-09 (readable-fusion, the two AddOptionsInMenu divergences closed)
- Divergence 1, the gate. The original does not store `== 1` for `AddOptionsInMenu`: the walker reads the key at `0x1005327B` with default `"1"` (`push offset "1"` at `0x1005323F`) and stores the parse's *answer* -- `0x10053301 call ds:strtol` with `push 0Ah ; Radix` (base 10), the inlined std::stoi's `cmp esi,[ebp-0C8h]` / `jnz` -> `_Xinvalid_argument` and its `cmp [eax],22h` (errno == ERANGE 34) -> `_Xout_of_range`, then `0x1005332E test ecx,ecx` / `0x10053337 setnz byte_10158038`. So `2` (and `-1`) enable the key. New seam `Loader::EnabledNonZero(key)` in `source/loader.ixx`: same catalogue mask, same absent default, same `Catalogue::Read` (std::stoi with the throws), only the final comparison differs; `source/menu.ixx` asks it in both of its gate sites. Every other key still uses `Loader::Enabled` (`== 1`).
- Divergence 2, the ZMenuIV guard. `sub_10052300` is called unconditionally at `0x1005527B` (no key/build test in front of it; reached by falling through from SuperLODFix's handler at `0x10055276`, returns to VSyncFix's read at `0x10055280`). Recovered and reproduced 1:1: `GetModuleHandleA("ZMenuIV.asi")` -> `GetProcAddress(h,"GetZMenuVersion")` -> `call eax` with `(&version, &unused)` -> `cmp [ebp-8],35DD1h` / `jnb` -> below 0x35DD1 shows `MessageBoxA(0, "Incompatible/outdated version of ZMenu detected! The game will now exit.", "", MB_ICONERROR)` (the caption is the image's shared empty string at `0x10063CD6`) and `TerminateProcess(GetCurrentProcess(),0)`. No log, no store, no patch. Missing library or export is the original's clean skip (both `jz loc_10052368`). The body is 38 instructions with a plain /GS frame, so it is transcribed (`ZMenuVersionGuardBody`, `zmenu_version_guard` in `tests/bodies.tsv`, 38) and registered at `0x10052300`; the two out-parameter locals are deliberately not zeroed (a library that writes neither makes the compare read the original's uninitialised stack word -- recorded in the Preserved faults table). Wired unconditionally into the publish tier from `RegisterMenu`: the tree's first startup pass, so the verdict lands before any arm's install and no mask/INI value can suppress it; the original's mid-walk position is an interleave the tier split cannot express and is unobservable here (reads no site, writes no site, failure ends the process). Five imports through slots (GetModuleHandleA/GetProcAddress/MessageBoxA/GetCurrentProcess/TerminateProcess). All four string literals were diffed byte for byte against the ASI.
- The call site was measured to fire: with a one-shot file marker written from the same handler for one build, a single `zp_tests` run (one ASI load) produced exactly one marker line; marker removed, and the shipped ASI contains no marker string.
- Check + falsification: `TestOptionGates` in `tests/test_main.cpp` (`import loader`), 9 checks -- enabled by "2"/"1"/"-1", disabled by "0", absent default `1`, still refused on a build the mask excludes, `MiscFixes` still requires exactly `== 1`, and the old rule asserted false for "2". Running the same requirement through `Loader::Enabled` fails it: `FAIL gate: FALSIFICATION -- the old == 1 rule against the same requirement` / `85 check(s), 1 failure(s)` (exit 1); reverted, `84 check(s), 0 failure(s)`.
- Inertness: build-9 per-option dump byte-identical before and after both changes -- `E20AF4EAD811021BAB1D6475A3EC3E0900C9CDF341CD328515F7E76CC2845327`, 384,422 bytes, 12,898 lines (no `IVMenuAPI.asi`, no `ZMenuIV.asi` in the harness).
- Gates: build exit 0; `ctest --preset release` 22/22 (122.9 s); `parity_report.py 9` 98 closed / 0 open / 1 oracle-verified; `verify_bodies.py` 175 bodies / 0 failures (the new row PASSes at 38 instructions, 6 absolute operands); `check_signature_parity.py --game ../game_refs` 132 measured / 0 mismatches / 0 loosenings; `check_oracle_verified.py` 3 rows / 0 failures; `zp_dump --ooc-selftest` PASS.
- `docs/parity.md`: both entries are now "reproduced" with the quoted instructions and the measurements -- the gate block in the `AddOptionsInMenu` section, the new "The `ZMenuIV.asi` `GetZMenuVersion` guard: reproduced" subsection, the pieces table row, the contracts-table gate row, a Preserved-faults row for the uninitialised version word, and the Status/measured counts (175 bodies, 84 checks). `tests/signatures.tsv` needed no row: the guard scans nothing.

## 2026-11-09 (readable-fusion, documentation pass: README + six-document docs shape)
- Rewrote `README.md` and added `docs/architecture.md`, `docs/building.md`, `docs/features.md`, `docs/hooking.md`, `docs/testing.md` in the shape of `../readable/README.md` and its five docs. The four evidence documents (`docs/parity.md`, `docs/matrix.md`, `docs/rebuilt.md`, `docs/review.md`) were not moved, renamed or edited, and no heading in `docs/parity.md` changed.
- Figures re-measured before writing, not copied from the old prose: `python tools/parity_report.py 9` -> `98 key(s) closed, 0 open, 1 oracle-verified`; `python tools/verify_bodies.py --original ../game_refs/ZolikaPatch_original.asi.off --candidate build/win-x86/Release/ZolikaPatch.asi --bodies tests/bodies.tsv --map build/win-x86/Release/ZolikaPatch.map` -> `175 body(ies), 0 failure(s)` (manifest: 184 non-comment rows = 175 body + 9 table); `python tools/check_oracle_verified.py` -> `3 oracle-verified row(s), 0 failure(s)`; `docs/matrix.md` read as generated, full-cell `differing 0` on all nine images, 18 cells, build 9 full cell 947 bytes / extra 57 all `SkipLauncher`. Test count 22 from `CMakeLists.txt` (`add_test`) and `build/win-x86/CTestTestfile.cmake`; ctest 22/22 was last recorded in the preceding entry. Build scopes in `docs/features.md` are decoded from the masks in `source/options.inc` (99 keys: stability 18, vehicles 21, graphics 16, loading 18, audio 13, misc 13).
- Stale figures left in place and reported rather than silently edited, per the pass's rule: `docs/review.md` (151 bodies, 20 tests, "20/20"), `docs/rebuilt.md` (164 bodies, 5 places), `docs/parity.md` line 288 (164 bodies; its final status already says 175), and `AGENTS.md` (164 bodies). No claim was added that those files are newer than they are.
- Claims removed as unsubstantiated while writing: `data/useless_checks.inc`'s header names `tools/extract_useless_checks.py`, which is not in `tools/` (the doc now says only that the hashes are pinned and the extractor is not in this tree); no clone URL was invented for `docs/building.md` (the old README had none either), and no CI badge was added because the tree has no `.github/` workflow to point at.
- Verification: `python tools/check_oracle_verified.py` -> `3 oracle-verified row(s), 0 failure(s)` after all edits (exit 0); a scratch link/anchor check (`.scratch/check_doc_links.py`) reports `links ok`; every new document is ASCII. No build, test run or git command was executed; the tree is left dirty for review.
