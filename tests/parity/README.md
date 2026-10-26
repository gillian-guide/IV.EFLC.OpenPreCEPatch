# Parity matrix

`matrix.json` is the committed write-set baseline for the INI surface. It is
produced by `tools/opcep_matrix.py` and records, for every cell:

- `writes` / `sha256` — the complete `RVA byte` write set of that cell;
- `arms` — how many bytes each installer arm left changed;
- `delta_writes` / `delta_sha256` / `delta_arms` (option and section cells) —
  the difference against `all-off`, with the arm that produced each byte.

The per-byte data lives next to it:

- `writesets/<cell>.txt` — `RVA byte`, sorted;
- `deltas/<cell>.txt` — `RVA byte arm` for every option/section cell.

Cells are:

| cell | meaning |
| --- | --- |
| `defaults` | `data/plugins/IV.EFLC.OpenPreCEPatch.ini` verbatim |
| `all-off` | every boolean key explicitly `0` (the baseline: zero writes) |
| `all-on` | every boolean key explicitly `1` |
| `option:<key>` | `all-off` plus that one key |
| `section:<name>` | `all-off` plus every key in that section |
| `legacy` | the frozen `fixtures/ZolikaPatch.legacy.ini` run through the real first-run importer |

## Running

```bat
python tools\opcep_matrix.py --check      :: compare with the committed matrix
python tools\opcep_matrix.py --update     :: record a new known-good baseline
python tools\opcep_matrix.py --cells "option:NewSaves" --report out.json
python tools\opcep_matrix.py --env OPCEP_IMPROVEMENTS=none --report off.json
```

The folded improvements are part of the original options, so there are no
per-fix cells. To compare improved and legacy paths without editing the INI:

- set `OPCEP_IMPROVEMENTS=none` (or `only:...` / `except:...`) and pass it with
  `--env`; or
- build the legacy dump with
  `cmake -S . -B build-parity -A x64 -DOPCEP_PARITY_BASELINE=ON` and compare its
  output with the normal dump.

The tool needs `build64\Release\opcep_parity_dump.exe` and a game executable
(default `..\game_refs\GTAIV.exe`); no game files are committed. The game image
hash is part of the baseline, so a different executable is reported as a
mismatch instead of silently changing the numbers.

## Policy

- A pure refactor must produce **zero** diff in `--check`.
- The scanner is limited to the executable sections and stops at the first
  match, like the original `hook::pattern(module)`; arms that patch more than
  one match use the explicit multi-match helper. A signature that only matched
  outside `.text` would change a cell, so `--check` guards that too (none do on
  the 1080 image: the executable-range change is a zero-diff PASS).
- The improvements are folded into the original options, so there are no
  per-fix cells. A folded improvement must either produce **zero** write-set
  change (runtime branch) or change only the cell that owns the original option
  (the `SkipIntro` rel32 is the known example). Say which in
  `docs/improvements-log.md`.
- Use `--env OPCEP_IMPROVEMENTS=none` or a `-DOPCEP_PARITY_BASELINE=ON` build
  for the improved-vs-legacy comparison; do not add an INI key for a test.
- Never update the golden to hide an unexplained delta. Use the attribution
  files to trace the byte to an arm first.

Attribution is the *net* write set per arm: if one arm writes a byte and a later
arm restores it, the byte does not appear. The final image is what the game
sees, which is what parity is measured on.
