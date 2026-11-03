# Improvements over the original loader

This tree keeps every original write, gate and arm order **of the install-time
game-image options it implements**, proved by `tools/opcep_matrix.py --check`
(96 INI cells, 3964 writes) and `tools/opcep_crossbuild.py` (nine on-disk
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

## Arm writes

Arm writes are direct `injector::MakeJMP` / `MakeCALL` / `MakeNOP` / `WriteMemory`
calls, as in FusionFix: there is no wrapper between an arm and the injector. A
missing signature is the arm's own clean skip (the original's behaviour), and the
run-time body guards (`zpatch_failsafe_skip`, the `OPCEP_IMPROVEMENTS` group) keep
a body from jumping or calling through an unpublished slot. The 96-cell matrix
proves the write set is unchanged.

## ImprovedShaderStreaming and NewMemorySystem

`docs/performance.md` describes both hot-path replacements: the game function or
allocator they replace, the exact write contract, and the deliberate changes.
The two compatibility/robustness changes are:

* `ImprovedShaderStreaming` keeps LibertyCityPlates' address signature intact.
  The earlier patch overwrote the bytes after the vehicle lookup with a jump to
  the function end; LCP scans that block and aborts its whole address setup when
  the signature stops matching (so its shaders never load). The option now
  replaces only the two first `LookupShader` calls; a miss falls through the
  game's original name chain.
* `NewMemorySystem` checks that the allocator singleton was published before it
  replaces the constructor call, rejects a 32-bit allocation-size overflow, and
  hands pointers it did not allocate back to the original allocator instead of
  reading its own base-offset header from them. It allocates from a private
  `HeapCreate` heap, separating its traffic from the CRT heap used by FusionFix
  and LibertyCityPlates.

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

## The install tiers

`opp.common` owns FusionFix's `Event<Args...>` (a mutex-guarded `std::function` list
whose `operator+=` returns the unsubscribe token) and the three `Event<>` tiers
(`PublishEvent`, `InstallEvent`, `PostEvent`); `opp.game` runs them from `PatchGame`.
Each feature is a class whose constructor only subscribes its body to a tier, and a
file-scope instance performs the subscription. Handlers take no arguments: they read
the `Game` singleton (`Game::Get()` for the image/build, `Game::Option(...)` for the
INI), which `PatchGame` initializes once. There is no `Phase` enum, per-arm schedule
table or dependency graph.

The publish tier is load-bearing (a site's original bytes and resume are read before
any install overwrites them). Inside a tier the handlers run in subscription order,
which is the executable's static-init/link order; the 96-cell matrix proves that
order is write-equivalent. Attribution is `TraceArm`: the arm names every handler at
its start (`InstallX` / `PublishX`, the original loader's names) and the write
observer brackets the arm's bytes, so `tests/parity/matrix.json` keeps its per-arm
counts.

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

Four plain `__cdecl` bodies are **not** claimed byte-identical because the TU
split made the compiler re-decide helper inlining: `abi_bpm_check`,
`abi_death_music_install`, `abi_gfwl_patch`, `abi_new_saves_convert`. They have
no `@exact` byte contract and are not mid-function shims; their compiled form
differs by stack frame, register allocation or helper inlining, and behavioural
equivalence is **not asserted or verified** (there is no runtime oracle for
them). The write-set oracles only prove the install path is unchanged.

## Startup warnings: task dialogs, loader probe, single instance

The original popped plain message boxes (and, for an unsupported version, opened
the ZolikaPatch downgrading page and killed the process without a dialog). The
warnings are now FusionFix-style task dialogs, and each is gated by an actual
probe instead of a proxy:

* **Presentation.** `source/warnings.ixx` shows `TaskDialogIndirect` with the
  command-link buttons, hyperlink support and information/error icon that
  `GTAIV.EFLC.FusionFix/source/compat.ixx` uses. The game executable has no
  `Microsoft.Windows.Common-Controls` dependency, so comctl32 v6 does not exist
  in the process: the ASI embeds an `RT_MANIFEST` (`source/warnings.rc`) and
  `CreateActCtx`/`ActivateActCtx` are held while the dialog is created. When
  that fails the code falls back to a plain message box. Every dialog is posted
  to a worker thread because the entry point can run under the loader lock, and
  `TaskDialogIndirect` maps comctl32.
* **Old ASI Loader.** The loader is identified from the caller's stack (and, as
  a fallback, by scanning the loaded modules) for the `IsUltimateASILoader`
  export. That export was added to Ultimate ASI Loader on 2023-10-24 and first
  shipped in **v6.9.0** (2024-01-21), the same release that carries the extended
  version resource, so 6.9.0 is the minimum: it is the marker FusionFix's own
  compatibility check trusts, and no module installed next to the game needs a
  newer export (FusionFix 5.1.1's `GetOverloadPathW`, v10.0.0, is optional and
  degrades to no file overload). A loader without the marker, or one whose
  version resource is older, gets "Old ASI Loader detected!" with a
  *Download Ultimate ASI Loader* button and *Continue*. The `dsound.dll`
  file-size check is gone (it warned about a setup that the probe now judges).
* **Unsupported game version.** 1.2.0.1–1.2.9.99 are refused except **1.2.0.43**
  (`kSupportedCompleteEdition`), which this tree has recovered gates for. The
  dialog states the downgrade/accidental-install case and the process exits with
  the original's `0x8000FFFF`; the browser launch is gone.
* **Loaded twice.** A process-wide named mutex
  (`Local\IV.EFLC.OpenPreCEPatch-<pid>`) is claimed before anything is
  installed; a second copy of the ASI (a renamed duplicate, or two loaders with
  their own copies) gets the "loaded twice" dialog and exits. The old check
  inferred a double load from a `CarDecalsFix` signature miss, which also fires
  on a different game image; that case is now only logged.
* **FusionFix notice.** `IV.EFLC.OpenPreCEPatch.fusionfix-notice.txt` is
  rewritten on every launch with the enabled-but-gated options and their
  reasons; the dialog is shown only while the file does not exist, so it stays
  one-time and deleting the file restores it.

The ZMenuIV version guard is unchanged: it keeps the original's message box and
exit code.

## Backlog (documented, not gates)

`tools/gen_version_matrix.py` renders `docs/version-matrix.md` from
`docs/version-matrix.json`, the recovered IDA applicability dump. That input is
not checked in, so the tool prints a clear missing-input message and exits
without writing anything; it is deliberately not a CTest. Regenerating the
matrix needs the dump, which is a separate evidence-collection step.
