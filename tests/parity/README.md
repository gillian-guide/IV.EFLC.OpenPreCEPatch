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

There is no separate improved/legacy mode: the improvements are the code, and
the matrix is the record of what the installer writes. Changing an
implementation shows up as a cell diff, which is exactly the review signal.

The tool needs `build\x64\Release\opcep_parity_dump.exe` (created by
`cmake --preset x64 && cmake --build --preset x64-release`) and a game executable
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
- An improvement that changes the write set must be attributed to one arm and
  the delta stated in `docs/improvements.md`.

The cross-build oracle is `tests/parity/crossbuild-baseline/`: one write set per
on-disk executable (nine builds) for the shipped defaults and an all-options-on
INI, produced by `python tools/opcep_crossbuild.py --capture <dir>` at a known
commit. `--check <dir>` diffs the current tree against it. Both oracles pin the
PlayGTAIV process probe (`OPCEP_LAUNCHER_PROBE=1`) so the launcher write is
observable; they cover install-time game-image writes only (not hook bodies and
not `xlive.dll`, which `TestRemoveGFWLUpdatePopupSites` covers).

`address_sites.tsv` is the address-proof table: each row is an install site or a
published target, validated against the on-disk images by
`tools/opcep_sigcheck.py`. It is maintained beside the source, not generated from
it; the RemoveModChecks NOP rows use per-byte anchors while the arm uses one
instruction anchor plus offsets, so a row must be re-derived together with the
arm when either changes.
- Never update the golden to hide an unexplained delta. Use the attribution
  files to trace the byte to an arm first.

Attribution is the *net* write set per arm: if one arm writes a byte and a later
arm restores it, the byte does not appear. The final image is what the game
sees, which is what parity is measured on.
