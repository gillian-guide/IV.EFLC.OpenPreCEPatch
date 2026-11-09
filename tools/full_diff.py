#!/usr/bin/env python3
"""Explain a full-cell difference by attributing it to keys.

The per-key scoreboard can be clean while the full (shipped-INI) cell differs,
because an arm's behaviour can depend on the order or on another arm having written
the same byte first. This lists every differing RVA, both bytes, and which keys'
per-option cells touch that RVA - so the difference has a name.

    python tools/full_diff.py <build> [--limit N]
"""

import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT.parent / "readable" / "build" / "p3-final-dumps3"
BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120", 13: "EFLC_1130"}


from parity_report import branch_bytes


def rows(path, key=None):
    out = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            out[int(parts[0], 16)] = int(parts[1], 16)
        elif len(parts) == 3 and (key is None or parts[0] == key):
            out[int(parts[1], 16)] = int(parts[2], 16)
    return out


def keys():
    table = ROOT / "source" / "options.inc"
    return [line.split("(", 1)[1].split(",", 1)[0]
            for line in table.read_text(encoding="utf-8").splitlines()
            if line.startswith("ZP_OPTION(")]


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    build = sys.argv[1]
    limit = int(sys.argv[sys.argv.index("--limit") + 1]) if "--limit" in sys.argv else 40

    dump = ROOT / "build" / "win-x86" / "Release" / "zp_dump.exe"
    exe = ROOT.parent / "game_refs" / f"{BUILDS[int(build)]}.exe"
    ini = ROOT / "ZolikaPatch.ini"
    ours_path = ROOT / "build" / "win-x86" / f"full-b{build}.txt"
    subprocess.run([str(dump), "--exe", str(exe), "--build", build, str(ours_path), str(ini)],
                   check=True, capture_output=True)
    ours = rows(ours_path)
    want = rows(BASELINE / f"b{build}-full.txt")

    # RVA -> the keys whose per-option cell touched it.
    by_key = defaultdict(list)
    per = BASELINE / f"b{build}-per.txt"
    for line in per.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3:
            by_key[int(parts[1], 16)].append(parts[0])

    pristine = exe.read_bytes()
    branch = branch_bytes(want, ours, pristine)
    differing = sorted(rva for rva in (set(want) & set(ours)) - branch if want[rva] != ours[rva])
    missing = sorted((set(want) - set(ours)) - branch)
    extra = sorted((set(ours) - set(want)) - branch)
    print(f"build {build}: {len(differing)} differing, {len(missing)} missing, {len(extra)} extra")
    for rva in extra[:5]:
        holders = ", ".join(sorted(set(by_key.get(rva, [])))) or "no per-key cell"
        print(f"  EXTRA {rva:06X}  ours {ours[rva]:02X}  <- {holders}")
    for rva in differing[:limit]:
        holders = ", ".join(sorted(set(by_key.get(rva, [])))) or "no per-key cell"
        print(f"  {rva:06X}  baseline {want[rva]:02X}  ours {ours[rva]:02X}  <- {holders}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
