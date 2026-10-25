# Improvements log

One entry per behaviour-changing checkpoint. Everything here is opt-in: a `0`
(the shipped default) selects the original path.

## 2026-10-25 — safety net and first opt-in pass

**Baseline:** `baseline-openprece-20261025` (commit `175cb56`), tag
`good-matrix-20261025` after the matrix harness.

### No-runtime changes

- `tools/opcep_matrix.py` + `tests/parity/`: 126 cells (defaults, all-off,
  all-on, each option, each section, the legacy fixture, and one dependent
  combo), with per-cell write sets and per-byte arm attribution.
- `tools/gen_ini_template.py`: regenerates `source/ini_template.hxx` from the
  shipped INI. The template is emitted as two adjacent raw literals because
  MSVC truncates a single literal above 16,380 bytes.
- `tests/dump_main.cpp` canonicalises module-relative operands
  (`AddressOf`, `FindGlobalValue`, `FindGlobalSymbol`): each 4-byte window is
  replaced with a site-derived synthetic address. A 4 KB unrelated global was
  added, the dump rebuilt, and the defaults write set stayed byte-identical —
  proving refactor checks are not fooled by module layout.
- `tools/gen_dump_abi.py` keeps mapping hook names to the *original* destination
  VAs for the 64-bit dump, so body addresses never enter the comparison.

### Stability keys (all default `0`)

| key | change | original path |
| --- | --- | --- |
| `NewSavesGuards` | validates header/chunk `fread`, `size < 9`, `malloc` and payload reads; malformed save returns before the P1–P3 patches | unchanged loop when `0` |
| `OutOfCommissionSafeInit` | returns `0` on failed decode/allocation instead of faulting or mounting garbage | unchanged when `0` |
| `VehicleComponentGuards` | null-checks the registry/entry/bone chain and the script-resolver result | unchanged unguarded walk when `0` |
| `ModelLookupGuards` | null-checks the shared lookup, FCHS parameter and popup lookup results | unchanged unguarded calls when `0` |
| `TraceInstallArms` | one `arm <name>` log line per installer arm | no logging when `0` |

`zpatch_new_saves_scan` was split out of the converter so the native tests can
drive valid, truncated and `size < 9` files with guard/seek combinations; the
converter keeps the same side effects.

### Performance keys (all default `0`)

| key | change | expected delta |
| --- | --- | --- |
| `NewSavesSeekPayloads` | `fseek` over chunk payloads (guarded/valid sizes only) | none (runtime path) |
| `OutOfCommissionCache` | decode/RPF build/mount once, reuse archive | none (runtime path) |
| `SteerLookupCache` | 64-slot direct-mapped model→tilt cache | none (runtime path) |
| `PopupLookupCache` | 512-slot direct-mapped hash→index cache (popup + FCHS) | none (runtime path) |
| `ModelNameIndex` | one-time sorted `(hash, first index)` index, binary search | none (runtime path) |

### FusionFix-borrowed key (default `0`)

| key | change | original path |
| --- | --- | --- |
| `SkipIntroShowScreens` | `abi_skip_intro_screens`: same site/resume, `edx = 0x12C` (300 ms) below the 0x1F40 threshold, following FusionFix `skipintro.ixx` | `abi_skip_intro_body` (`xor edx,edx`) when `0` |

`SkipIntroShowScreens` is in the FusionFix gate table and marked `FF`. It needs
`SkipIntro=1`; the standalone cell is therefore 0 writes, and the dedicated
`combo:SkipIntroShowScreens` cell measures against `option:SkipIntro`:

- `combo:SkipIntroShowScreens`: **4 bytes**, all `InstallSkipIntro` — the rel32
  target changes from the original body to the 300 ms body at the same site.

### Expected write-set delta summary

| cell | before | after | explanation |
| --- | ---: | ---: | --- |
| `all-off` | 0 | 0 | no code writes |
| `defaults` | 908 | 909 | same sites; canonicalised pointer windows now always emit all 4 bytes, and the new default-0 keys write nothing |
| `all-on` | 12 789 | 12 802 | canonicalised pointer windows + the 4-byte SkipIntro variant at the same site |
| `legacy` | 899 | 900 | same as defaults (imported file selects `SkipIntro=1`) |
| each new option alone | — | 0 | runtime branch only |
| `combo:SkipIntroShowScreens` | — | 4 | deliberately the variant rel32 |

The defaults count changed by one byte only because a module pointer's low byte
used to equal the pristine byte; the canonical representation emits the whole
4-byte operand unconditionally. No new game site is touched with the shipped
INI.

### Tests run

- `cmake --build build --config Release` (x86) — pass.
- `cmake --build build64 --config Release --target opcep_parity_dump` — pass.
- `ctest --test-dir build -C Release` — pass (includes the new
  `zpatch_new_saves_scan` and `ModelNameIndex` tests).
- `python tools/opcep_matrix.py --update` then `--check` — pass, 126 cells,
  40,272 writes.
- Layout probe: unrelated 4 KB global added → defaults dump byte-identical.
- `llvm-objdump` on `build/Release/IV.EFLC.OpenPreCEPatch.asi`: the original
  `SkipIntro` body is still present (`imul ecx,ecx,0x190; cmp edx,0x1F40;
  jge; xor edx,edx; jmp [slot]`) and the variant is separate
  (`...; mov edx,0x12C; jmp [slot]`). Both tail-jump the same published slot.
- No live game test in this pass; the new paths are runtime-branch-only and the
  INI defaults leave every original path selected.

**Known-good checkpoint:** tag `good-optin-20261025` (this commit).
