#!/usr/bin/env python3
"""Per-arm parity check against the reference implementation.

Runs the reference dump once per INI option (every other key forced to 0) and
compares its write set with the readable port's per-arm set. Prints, per arm,
the bytes that only one side writes and the bytes both write differently.

    python tools/parity_arms.py <reference_exe> <ours_per_option> [ini]
"""

import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYS = ROOT / "tests" / "option_keys.inc"


def option_keys():
    keys = []
    for line in KEYS.read_text().splitlines():
        line = line.strip()
        if line.startswith('{"'):
            key = line.split('"')[1]
            keys.append(key)
    return keys


def read_dump(path):
    out = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            out[int(parts[0], 16)] = int(parts[1], 16)
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    reference_exe, ours_path = sys.argv[1], sys.argv[2]
    base_ini = Path(sys.argv[3]) if len(sys.argv) > 3 else ROOT.parent / "game_refs" / "ZolikaPatch.ini"
    text = base_ini.read_text(errors="replace")
    keys = option_keys()

    ours = defaultdict(dict)
    for line in Path(ours_path).read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3:
            ours[parts[0]][int(parts[1], 16)] = int(parts[2], 16)

    workdir = Path(tempfile.mkdtemp())
    rows = []
    for key in keys:
        ini = workdir / "single.ini"
        body = [text, "\n[Options]\n"]
        body += [f"{k}=0\n" for k in keys]
        body.append(f"{key}=1\n")
        ini.write_text("".join(body), errors="ignore")

        dump = workdir / "ref.txt"
        if dump.exists():
            dump.unlink()
        subprocess.run([reference_exe, "--dump-writes", str(dump), str(ini)],
                       cwd=str(ROOT.parent), capture_output=True)
        ref = read_dump(dump) if dump.exists() else {}
        mine = ours.get(key, {})

        only_ref = sorted(set(ref) - set(mine))
        only_ours = sorted(set(mine) - set(ref))
        different = [rva for rva in set(ref) & set(mine)
                     if ref[rva] != mine[rva] and ref[rva] not in (0xE8, 0xE9)]
        if only_ref or only_ours or different:
            rows.append((key, len(ref), len(mine), only_ref, only_ours, different))

    print(f"arms compared: {len(keys)}, arms with differences: {len(rows)}\n")
    for key, ref_count, our_count, only_ref, only_ours, different in rows:
        print(f"{key}: reference {ref_count} bytes, ours {our_count} bytes")
        if only_ref:
            print(f"  only reference ({len(only_ref)}): " +
                  ", ".join(f"{rva:06X}" for rva in only_ref[:8]))
        if only_ours:
            print(f"  only ours ({len(only_ours)}): " +
                  ", ".join(f"{rva:06X}" for rva in only_ours[:8]))
        if different:
            print(f"  different ({len(different)}): " +
                  ", ".join(f"{rva:06X}" for rva in different[:8]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
