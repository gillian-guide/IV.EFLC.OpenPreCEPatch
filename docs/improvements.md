# Improvements over the original loader

This tree keeps every original write, gate and arm order **of the install-time
game-image options it implements**, proved by `tools/opcep_matrix.py --check`
(96 INI cells, 4004 writes) and `tools/opcep_crossbuild.py` (nine on-disk
builds). The oracles are install-time only; they do not execute hook bodies and
they do not map `xlive.dll`, so:

| oracle | covers | does not cover |
|---|---|---|
| `opcep_matrix.py` | install-time writes into the game image for 96 INI cells, including the launcher name slots (the harness pins the PlayGTAIV process probe; the cell has 26 writes) | runtime hook-body writes, `xlive.dll`, the process probe itself |
| `opcep_crossbuild.py` | the same write set on the nine on-disk executables | same limits |
| `native tests` | the launcher bytes/gate, the four `xlive.dll` flag RVAs, the config contract, the order golden | live game execution |
| `opcep_sigcheck.py` | 58 address rows: one hit at the intended RVA | behaviour |

The `RemoveGFWLUpdatePopup` writes into `xlive.dll` are covered by
`TestRemoveGFWLUpdatePopupSites` (the four recovered RVAs pinned to the original
decimal immediates) rather than by the image oracles. Options the original has
and this tree does not implement are listed in the README; the legacy import
reports each one instead of dropping it silently. The entries below are the
deliberate differences: each one only changes what happens when the image or the
INI does not match what the original assumed, and each leaves the original path
reachable or trivially revertable.

## Absent-key defaults: the loader fallback vs the shipped INI

Two defaults are legitimate and are not made equal:

* an **absent key** uses the arm fallback, which is the original loader's
  `get(key, default)`, recovered per read site and committed as
  `tests/parity/loader_defaults.tsv` (94 rows, kind A/B). CTest
  `opcep_option_defaults` fails if a call site drifts from it or if one key uses
  two defaults without a documented reason (`CarDynamicShadowFix`).
* a **present key** uses the shipped INI value. 22 options ship a value that
  differs from the loader fallback (`tests/parity/option_defaults.tsv`, status
  `ok-shipped-differs`), so deleting one of those lines intentionally restores
  the original fallback. No behaviour changed to produce this split; it is the
  original's own shape made explicit.

## Install-time failsafes (default on)

`source/helpers.hxx` funnels every arm write through `SiteWritable`,
`ModuleSiteWritable`, `JumpTo`, `CallTo`, `NopAt`, `WriteValue` and `WriteRaw`.
A site outside the game image, a write that would leave it, or a branch whose
body did not resolve is a logged skip instead of a fault. On a healthy image
every check passes, so the write set is byte-identical to the unchecked form.

## Config: the bundled IniReader, one parse, no exception, no allocation per read

`source/config.cpp` reads the file with the bundled IniReader
(`external/inireader`, the mINI reader the original loader used) exactly once,
then copies the parsed document into a NUL-terminated arena plus an open
addressed section/key index. `Config::Option` resolves in O(1) with no
allocation and never throws. The reader gained three read-only enumeration
methods (`GetSections`/`GetKeys`/`GetRawValue`, local submodule commit
`9a1c3fa`) so the facade can copy the document once instead of doing one map
lookup per key; `GetKeys` deduplicates and `GetRawValue` returns the last value,
which is mINI's last-duplicate-wins answer.

The custom mINI reimplementation is gone: the grammar is now the bundled
reader's by construction, including its inline-comment rule (a space-preceded
`;`, `//` or `#` starts a comment after the first `=`; without the space it is
part of the value). `TestConfigSemantics` pins the CRLF, duplicate-key,
duplicate-section, quote, escaped-`=`, whole-line and inline comment cases.

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
`"0x2"` is 0), quoting, BOM/CRLF, and the whole-line `;`/`#`/`//` comments.
Tests: `TestConfig`, `TestConfigSemantics` (including the CRLF and inline-comment
cases), `TestIntPrefix`, `TestShippedTemplate` in `tests/test_main.cpp`.

The first-run creation/legacy import (`EnsureConfigFile`, the legacy key tables)
and the FusionFix overlap gate live in the same `config.hxx`/`config.cpp` pair:
they answer the same "what does this INI say?" question, and folding them there
deleted `ini_import.*`, `fusionfix_compat.*` and the interim `ini_file.*`
wrapper (the bundled reader is used directly, no extra plain TU).

## Launcher slots: exactly the original's two

`ApplyLaunchWithPlayGTAIV` rewrites the two `LaunchGTAIV.exe\0` name slots the
original's handler patches. The arm now requires **exactly two** matches on a
gated build: zero, one or three-or-more matches log
`launcher: N LaunchGTAIV name slot(s), expected 2: skipped` and leave the image
untouched, so a modded image cannot make this tree write a slot the original
never wrote. The native test covers the two-hit case plus the 0/1/3-hit skips
(`TestLauncherNamePatch`); the 96-cell matrix is unchanged (26 launcher writes
in its launcher cell).

## Backlog (documented, not gates)

`tools/gen_version_matrix.py` renders `docs/version-matrix.md` from
`docs/version-matrix.json`, the recovered IDA applicability dump. That input is
not checked in, so the tool prints a clear missing-input message and exits
without writing anything; it is deliberately not a CTest. Regenerating the
matrix needs the dump, which is a separate evidence-collection step.

Revert: `git revert <this commit>` (tag before it: `good-steam-init-20261030`).
