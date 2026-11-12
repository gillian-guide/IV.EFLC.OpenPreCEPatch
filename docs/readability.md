# Readability contract

This is the mechanical definition of "readable" for `source/`. A module is done
when it follows this list (or only documented recovered tables remain) and the
18-cell dump matrix is unchanged.

1. **Code before assembly.** `__asm` and `__declspec(naked)` stay in the same
   module file, in one clearly marked tail section after every readable
   installer. Do not create a separate body TU. Inside `__asm`, a displacement
   must be an `enum` value or a literal: MSVC assembles a `constexpr` object as
   its address, not its value.
2. **No one-use variables.** A value with exactly one use is written at that
   use; a local name is introduced only when the value is read more than once.
   Never introduce a name that only restates the literal (`kFooOffset = -6`).
   A field is a struct member (`vehicle->modelIndex`), not `+ 0x2E`; a float is
   a `float`/`double` literal (`1.0f`, `0.3333`), not its bit pattern. Use
   `std::bit_cast` only when the decimal literal is not bit-exact, and pin it
   with `static_assert`.
3. **Use injector, don't wrap it.** Raw instruction writes are
   `injector::WriteMemory<uint32_t>` / `WriteMemoryRaw` (or the `MakeJMP` /
   `MakeCALL` / `MakeNOP` helpers) with a one-line comment naming the mnemonic;
   do not add `InstructionWindow`, `PackedLowDword` or similar wrappers around
   bytes. `source/patch.ixx` keeps only `raw_mem` and `IATHook`, because a
   patch-and-restore byte run and an import-table replacement are not a single
   write.
4. **No game VAs in code.** Scan a signature, then name the site offset
   (`kModelIndex`, `kCallBeforeSite`). Original-body addresses belong in the
   body's own comment, not in an install.
5. **No hubs.** One owner per concept: `source/config.ixx` owns the INI,
   `source/game.ixx` owns process/path probes and the image range,
   `source/patch.ixx` owns writes and opcodes, `source/common.ixx` owns the event
   tiers, and `source/loader.ixx` owns the three tiers and the option gate. There
   is no `settings`/`opcodes.hxx`/`ini_file.*` indirection.
6. **Length is earned.** Delete dead code, unused defines, variables, parameters
   and one-caller helpers that do not name a real concept. Split a file when it
   has two responsibilities, not to hit a line count; there is no line limit.
7. **Comments state invariants, once.** No banner blocks, no restating the code,
   no session history.
