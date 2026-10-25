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
```

The tool needs `build64\Release\opcep_parity_dump.exe` and a game executable
(default `..\game_refs\GTAIV.exe`); no game files are committed. The game image
hash is part of the baseline, so a different executable is reported as a
mismatch instead of silently changing the numbers.

## Policy

- A pure refactor must produce **zero** diff in `--check`.
- A new opt-in option must only add its own cell, and that cell's `delta_arms`
  must name the expected installer arm(s). A key that only flips a runtime
  branch (no new code bytes) legitimately has a zero-write cell; say so in
  `docs/improvements-log.md`.
- Never update the golden to hide an unexplained delta. Use the attribution
  files to trace the byte to an arm first.

Attribution is the *net* write set per arm: if one arm writes a byte and a later
arm restores it, the byte does not appear. The final image is what the game
sees, which is what parity is measured on.
