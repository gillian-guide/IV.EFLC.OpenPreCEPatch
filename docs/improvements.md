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

**The TSV is the option catalogue (the OA-3 decision).** There is no second
typed `OptionSpec` table in `opp.config`: the defaults live at the arm's own
`Config::Option("Key", "default")` call site, and
`tests/parity/option_defaults.tsv` is generated from those call sites and
checked by CTest. A typed table would duplicate the defaults and could drift from
the code that actually reads them; the generated TSV cannot. The shipped INI is
the user-facing surface, and `loader_defaults.tsv` is the recovered-loader
answer. `opp.config` carries this decision in the `Config::Option` comment.

## Install-time failsafes (default on)

`opp.patch` funnels every arm write through `SiteWritable`,
`ModuleSiteWritable`, `JumpTo`, `CallTo`, `NopAt`, `WriteValue` and `WriteRaw`.
A site outside the game image, a write that would leave it, or a branch whose
body did not resolve is a logged skip instead of a fault. On a healthy image
every check passes, so the write set is byte-identical to the unchecked form.

## Config: the bundled IniReader, one parse, no exception, no allocation per read

`opp.config` reads the file with the bundled IniReader
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

## Multi-hit arms: expected site counts (P13)

The same guard now covers every multi-match arm whose original read or patched a
**fixed** set of sites: `PatternAll` result counts are compared with the
recovered count through `opp::ExpectMatchCount`, and a different count logs
`arm <name>: <what>: N match(es), expected M: skipped` and installs nothing (a
partial multi-site install is worse than no install). Guarded arms:

| arm | expected sites | evidence |
| --- | --- | --- |
| `PublishBuildingDynamicShadows` hash pair | 2 | recovered `dword_10159C1C` index 0 / `dword_10159C24` index 1 |
| `InstallNoImpostorHeightCutoff` | 2 | the two recovered literal sites |
| `InstallBenchmarkFix` | 2 | the two benchmark entries (bodies `abi_benchmark_a/b`) |
| `InstallEpisodicVehicleSupport` (component window) | 2 | the two recovered NOP sites |
| `InstallEpisodicVehicleSupport` (episode test) | 2 | the two recovered NOP sites |
| `ApplyLaunchWithPlayGTAIV` | 2 | the two launcher name slots (see above) |

`InstallStartupTimeFix` is deliberately **not** guarded: the original walks every
match of its literal (`for_each_result`) and patches each one, so the hit count is
image data, not a recovered per-build constant. All guards are verified by the
96-cell matrix and the 18 crossbuild write sets, which are unchanged.

## The install tiers and their deterministic order

`opp.pipeline` owns three FusionFix-style tiers (`PublishEvent`, `InstallEvent`,
`PostEvent`). Each feature module subscribes its arms at static-init time and the
pipeline runs the tiers in order; the publish tier is load-bearing (a site's
original bytes and resume are read before any install overwrites them). There is
no `Phase` enum, per-arm schedule table or dependency graph.

Inside a tier the arms run in **name** order. Subscription order is the
executable's static-init/link order, which differs between the ASI, the dump and
the tests; name order is identical in every binary and keeps two arms that patch
the same site independent of the link. The 96-cell matrix (96 cells / 4004
writes) and the 18 crossbuild write sets are byte-identical to the old shipping
schedule. The only visible difference is attribution: `MovingPlaneParts` and
`MovingSteeringWheelSupport` both write the same five VEHICLES bytes with the same
value, and the refresh credits the one that now runs first (`MovingPlaneParts`);
`tests/parity/matrix.json` was regenerated for that cell only (the write count and
hash are unchanged; the dropped `delta_sha256` change is the arm label inside the
delta file).

## Hook-body fidelity after the TU split

The hook bodies used to share one translation unit; they now live with the
modules that install them. The transcribed machine-code bodies keep their
`@exact` byte contract: `tools/opcep_bodycheck.py` compares a baseline and a
current ASI after normalising module addresses, lambda tags and EH handlers, and
requires every `@exact` body (`source/*.ixx` and `source/*.cpp`) to be
instruction-identical. At the audit commit: **67 required, 64 emitted and
compared, 3 uninstalled, 0 differing**. The three uninstalled bodies
(`abi_garage_body_a`, `abi_local_settings_detour`,
`abi_local_settings_fallback_detour`) have no installer in this tree, are
referenced by no arm and drop out of the build.

The old 134-row ABI table maps to: **103** rows with a recovered handler VA (all
in `tests/dump_abi.inc`), **30** name-only helper rows, and `abi_garage_entry_a`,
which has no recovered VA and gets the synthetic dump-only VA `0x0F000001`. All
134 names still have a body definition or an `OPP_BODY` install site.

Four plain C bodies are **not** claimed byte-identical because the TU split made
the compiler re-decide helper inlining: `abi_bpm_check`,
`abi_death_music_install`, `abi_gfwl_patch`, `abi_new_saves_convert`. They carry
no `@exact` contract (ordinary `__cdecl` bodies, not mid-function shims); stack
frame, register allocation or helper inlining differ, the calling convention and
observable behaviour do not, and the write-set oracles prove the install path is
unchanged.

## Backlog (documented, not gates)

`tools/gen_version_matrix.py` renders `docs/version-matrix.md` from
`docs/version-matrix.json`, the recovered IDA applicability dump. That input is
not checked in, so the tool prints a clear missing-input message and exits
without writing anything; it is deliberately not a CTest. Regenerating the
matrix needs the dump, which is a separate evidence-collection step.
