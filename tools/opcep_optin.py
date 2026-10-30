#!/usr/bin/env python3
"""Check the write set of every entry-point rewrite.

The three rewrites that are part of the default set (garage_entry,
cutscene_entry, alpha_nop) are pinned by running the dump with that group
*excluded* (`OPCEP_IMPROVEMENTS=except:<group>`): the fixture then records what
each group contributes relative to the default. The allocator rewrite is not in
the default set, so `nms_thunk` is pinned with `all,nms_thunk` (the default plus
that group).

Each fixture is compared byte-for-byte with the dump's write set:

    tests/parity/optin/<group>.txt      "RVA byte" lines, sorted

Run with --update after an intended change and read the diff; the expected delta
per group is documented in docs/entry-points-audit.md and docs/improvements-log.md.

The per-group INI and dump output live under `.scratch/optin/` so the tree only
gains the committed fixtures. `nms_thunk` needs NewMemorySystem enabled (it
defaults off), so that group's INI is the shipped one with the key set to 1.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DUMP = ROOT / "build64" / "Release" / "opcep_parity_dump.exe"
GAME = ROOT.parent / "game_refs" / "GTAIV.exe"
INI = ROOT / "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini"
FIXTURES = ROOT / "tests" / "parity" / "optin"

# group -> (OPCEP_IMPROVEMENTS value, option the group's arm needs enabled)
GROUPS = {
    "garage_entry": ("except:garage_entry", None),
    "cutscene_entry": ("except:cutscene_entry", None),
    "alpha_nop": ("except:alpha_nop", None),
    "nms_thunk": ("all,nms_thunk", ("NewMemorySystem", "0")),
}


def build_ini(path: pathlib.Path, key: str, old: str) -> None:
    text = INI.read_text(encoding="utf-8")
    needle = f"{key} = {old}"
    if needle not in text:
        raise SystemExit(f"cannot find {needle!r} in the shipped INI")
    path.write_text(text.replace(needle, f"{key} = 1", 1), encoding="utf-8")


def run_dump(ini_path: pathlib.Path, spec: str, group: str) -> list[str]:
    import os

    merged = dict(os.environ)
    merged["OPCEP_IMPROVEMENTS"] = spec
    # Work inside the repository: the sandbox denies writes under %TEMP%, and the
    # dump opens the output path itself (a name, not an open handle).
    work = ROOT / ".scratch" / "optin"
    work.mkdir(parents=True, exist_ok=True)
    out_path = work / f"{group}.writes.txt"
    subprocess.run(
        [str(DUMP), str(GAME), str(ini_path), str(out_path)],
        check=True, env=merged, capture_output=True, text=True,
    )
    return sorted(out_path.read_text(encoding="utf-8").splitlines())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="compare with the fixtures (the default action)")
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    if not DUMP.exists():
        print(f"missing {DUMP} (configure a 64-bit build first)")
        return 1
    FIXTURES.mkdir(parents=True, exist_ok=True)
    failures = 0
    work = ROOT / ".scratch" / "optin"
    work.mkdir(parents=True, exist_ok=True)
    for group, (spec, override) in GROUPS.items():
        ini_path = work / f"{group}.ini"
        if override is None:
            ini_path.write_bytes(INI.read_bytes())
        else:
            build_ini(ini_path, *override)
        writes = run_dump(ini_path, spec, group)
        fixture = FIXTURES / f"{group}.txt"
        expected = (
            fixture.read_text(encoding="utf-8").splitlines() if fixture.exists() else []
        )
        if args.update:
            fixture.write_text("\n".join(writes) + "\n", encoding="utf-8")
            print(f"wrote {fixture} ({len(writes)} writes, {spec})")
            continue
        if writes != expected:
            added = [line for line in writes if line not in set(expected)]
            removed = [line for line in expected if line not in set(writes)]
            print(f"{group}: MISMATCH (+{len(added)} -{len(removed)})")
            for line in (added + removed)[:10]:
                print(f"  {line}")
            failures += 1
        else:
            print(f"{group}: ok ({len(writes)} writes, {spec})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
