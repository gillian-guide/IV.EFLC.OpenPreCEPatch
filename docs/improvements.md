# Improvements over the original loader

This tree keeps every original write, gate and arm order **of the options it
implements**, proved by `tools/opcep_matrix.py --check` (96 INI cells, 3874
writes) and by `tools/opcep_crossbuild.py` (nine on-disk builds). Options the
original has and this tree does not implement are listed in the README; the
legacy import reports each one instead of dropping it silently. The entries below
are the deliberate differences: each one only changes what happens when the image
or the INI does not match what the original assumed, and each leaves the original
path reachable or trivially revertable.

## Install-time failsafes (default on)

`source/helpers.hxx` funnels every arm write through `SiteWritable`,
`ModuleSiteWritable`, `JumpTo`, `CallTo`, `NopAt`, `WriteValue` and `WriteRaw`.
A site outside the game image, a write that would leave it, or a branch whose
body did not resolve is a logged skip instead of a fault. On a healthy image
every check passes, so the write set is byte-identical to the unchecked form.

## Config: one parse, no exception, no allocation per read

`source/config.cpp` parses the INI once into a NUL-terminated arena plus an open
addressed index. `Config::Option` resolves in O(1) with no allocation and never
throws.

The original (and this tree before the change) did
`std::stoi(value, nullptr, 10)` inside `DllMain`, so a bare `Key=` or
`Key=abc` threw `std::invalid_argument` out of `DllMain` and terminated the
process (138 installer read sites, no handler). Now an unparseable value is
logged as `config: invalid value for <key>: "<value>", using default <default>`
and falls back to the same default an absent key uses; `config: N value(s) did
not parse and used the option default` summarises it once per run.

Everything a legal INI can observe is unchanged: the section search order
(`Options` first, then the subsystem groups), last-duplicate-wins, the
exactly-`1` gate with `std::stoi`'s base-10 prefix grammar (`"1abc"` is 1,
`"0x2"` is 0), quoting, and the space-preceded inline-comment rule of the mINI
reader this replaces. Tests: `TestConfig`, `TestConfigSemantics`, `TestIntPrefix`,
`TestShippedTemplate` in `tests/test_main.cpp`.

Revert: `git revert <this commit>` (tag before it: `good-steam-init-20261030`).
