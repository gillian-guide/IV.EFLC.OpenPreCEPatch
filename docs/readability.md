# Readability contract

This is the mechanical definition of "readable" for `source/`. It replaces
ad-hoc cleanup prompts: `tools/readability_check.py` counts violations and
`ctest` ratchets them down. A module is done when its counts reach zero (or only
documented recovered tables remain) and the 96-cell matrix is unchanged.

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
   `WriteValue<uint32_t>` / `WriteRaw` (or `injector::WriteMemory*`) with a
   one-line comment naming the mnemonic; do not add `InstructionWindow`,
   `PackedLowDword` or similar wrappers around bytes.
4. **No game VAs in code.** Scan a signature, then name the site offset
   (`kModelIndex`, `kCallBeforeSite`). Original-body addresses belong in an
   `@exact` comment, not in an install.
5. **No hubs.** One owner per concept: `opp.config` owns the INI, `opp.game`
   owns process/path probes and the install driver, `opp.patch` owns writes and
   opcodes, `opp.common` owns the event tiers. There is no
   `settings`/`opcodes.hxx`/`ini_file.*` indirection.
6. **Length is earned.** Delete dead code, unused defines, variables, parameters
   and one-caller helpers that do not name a real concept. Split a file when it
   has two responsibilities, not to hit a line count; there is no line limit.
7. **Comments state invariants, once.** No banner blocks, no restating the code,
   no session history.

```bat
python tools/readability_check.py --report          :: current counts
python tools/readability_check.py --check           :: fail on any regression
python tools/readability_check.py --update-baseline :: after a cleanup pass
```
