#!/usr/bin/env python3
"""The launcher path, end to end.

`LaunchWithPlayGTAIV` rewrites the game's two `LaunchGTAIV.exe` name slots to the
launcher's own name so GTA IV 1.0.8.0's SecuROM initialisation (sub_48FC30)
accepts a game started through the launcher executable. Without it the game
leaves through `lmsgboxExit("SRLC10")` -- "Game needs to be launched using
LaunchGTAIV.exe" -- and its own exit path faults.

This runs the dump tool on each real image with every key off except the
launcher's, and compares the write set with the recovered sites:

  * 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0 - the handler's five switch
    cases, two slots each, at the recovered RVAs;
  * 1.0.0.4 and the EFLC builds - no case in the handler, so no write, even
    though 1.0.0.4's image carries the same two slots and the EFLC images one.

The dump is asked for the handler's process gate's answer explicitly
(`--launcher-probe`, `Launcher::SetProbeOverride(1)`): the offline harness has no
PlayGTAIV.exe process to find, and the write set being measured is the one the game
produces when it *was* started through the launcher. The default dump cells keep
the gate off, which is what the stored baselines record.

    python tools/check_launcher_dump.py <zp_dump.exe> [game_refs]
"""

import subprocess
import sys
from pathlib import Path

# The text both slots hold before the write, `LaunchGTAIV.exe\0`.
OLD = b"LaunchGTAIV.exe\x00"
# The payload: the oracle's `PlayGTAIV.exe\0e\0` (its two `mov dword ptr [..],
# imm32` rows at game_refs/ZolikaPatch_original.asi.off file offset 0x43650).
NEW = b"PlayGTAIV.exe\x00e\x00"

# build id -> (image, recovered slot RVAs or None when the switch has no case)
EXPECTED = {
    1: ("GTAIV_1000.exe", (0x9F043C, 0x98FA0C)),
    5: ("GTAIV_1040.exe", (0x9EA184, 0x9DD8A0)),
    6: ("GTAIV_1004.exe", None),
    7: ("GTAIV_1060.exe", (0x9F8F40, 0xA9D0D8)),
    8: ("GTAIV_1070.exe", (0x9F9F70, 0xA9E118)),
    9: ("GTAIV.exe", (0xA508F8, 0xA5A5E4)),
    11: ("EFLC_1110.exe", None),
    12: ("EFLC_1120.exe", None),
    13: ("EFLC_1130.exe", None),
}


def expected_rows(sites):
    """The dump's rows: only the payload bytes that differ from the old text."""
    rows = {}
    for site in sites:
        for index in range(len(NEW)):
            if NEW[index] != OLD[index]:
                rows[site + index] = NEW[index]
    return rows


def read_rows(path: Path):
    rows = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.strip():
            continue
        rva, byte = line.split()
        rows[int(rva, 16)] = int(byte, 16)
    return rows


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    dump = Path(sys.argv[1]).resolve()
    game_refs = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else dump.parents[3] / ".." / "game_refs"

    failures = 0
    for build, (image, sites) in EXPECTED.items():
        exe = game_refs / image
        if not exe.exists():
            print(f"SKIP build {build}: {exe} is missing")
            continue

        out = dump.parent / f"launcher-b{build}.txt"
        result = subprocess.run(
            [str(dump), "--exe", str(exe), "--build", str(build), "--only",
             "LaunchWithPlayGTAIV", "--launcher-probe", str(out)],
            check=False, capture_output=True)
        if not out.exists():
            failures += 1
            print(f"FAIL build {build}: the dump wrote no file "
                  f"({result.returncode}: {result.stderr.decode(errors='replace').strip()})")
            continue
        rows = read_rows(out)
        out.unlink(missing_ok=True)

        expected = expected_rows(sites) if sites else {}
        if rows != expected:
            failures += 1
            missing = sorted(set(expected) - set(rows))
            extra = sorted(set(rows) - set(expected))
            wrong = sorted(rva for rva in set(rows) & set(expected) if rows[rva] != expected[rva])
            print(f"FAIL build {build}: {len(rows)} byte(s) changed, expected {len(expected)} "
                  f"({len(missing)} missing, {len(extra)} extra, {len(wrong)} wrong)")
            for rva in (missing + extra + wrong)[:8]:
                print(f"       RVA {rva:06X}: got {rows.get(rva)} want {expected.get(rva)}")
            continue

        if sites:
            print(f"PASS build {build}: both recovered slots carry PlayGTAIV.exe")
        else:
            print(f"PASS build {build}: no write (the handler's switch has no case)")

    print(f"{len(EXPECTED)} image(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
