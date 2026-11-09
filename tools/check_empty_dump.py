#!/usr/bin/env python3
"""The empty-dump baseline.

With no arm installed, the pipeline must change nothing: the dump has to be
empty on every supported image. That is the precondition every later arm is
diffed against, so it is a test rather than a habit.

    python tools/check_empty_dump.py <zp_dump.exe> [game_refs]
"""

import subprocess
import sys
from pathlib import Path

IMAGES = {
    1: "GTAIV_1000.exe",
    5: "GTAIV_1040.exe",
    6: "GTAIV_1004.exe",
    7: "GTAIV_1060.exe",
    8: "GTAIV_1070.exe",
    9: "GTAIV.exe",
    11: "EFLC_1110.exe",
    12: "EFLC_1120.exe",
    13: "EFLC_1130.exe",
}


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    dump = Path(sys.argv[1]).resolve()
    # Called from the source tree, where ../game_refs is the live game directory.
    game_refs = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else dump.parents[3] / ".." / "game_refs"
    failures = 0
    # "No arm installed" means every option off, not "no INI": an arm's default is
    # on for most keys, so the baseline needs an explicit all-off document.
    table = Path(__file__).resolve().parent.parent / "source" / "options.inc"
    keys = [line.split("(", 1)[1].split(",", 1)[0]
            for line in table.read_text(encoding="utf-8").splitlines()
            if line.startswith("ZP_OPTION(")]
    ini = dump.parent / "all-off.ini"
    ini.write_text("[Options]\n" + "".join(f"{key}=0\n" for key in keys), encoding="utf-8")
    for build, image in IMAGES.items():
        exe = game_refs / image
        if not exe.exists():
            print(f"SKIP build {build}: {exe} is missing")
            continue
        out = dump.parent / f"empty-b{build}.txt"
        subprocess.run(
            [str(dump), "--exe", str(exe), "--build", str(build), str(out), str(ini)],
            check=False, capture_output=True)
        rows = [line for line in out.read_text(errors="replace").splitlines() if line.strip()]
        out.unlink(missing_ok=True)
        if rows:
            failures += 1
            print(f"FAIL build {build}: {len(rows)} byte(s) changed with no arm installed")
        else:
            print(f"PASS build {build}: the pipeline changes nothing")
    print(f"{len(IMAGES)} image(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
