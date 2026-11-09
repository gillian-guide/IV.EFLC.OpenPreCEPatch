#!/usr/bin/env python3
"""Check the launch slots the launcher arm rewrites.

The scan is one wildcard-free 16-byte text ("LaunchGTAIV.exe\\0") and the arm only
writes it on the five builds the original gated. This records, per image, how many
slots the scan resolves and whether the build is gated:

  * 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0 - two slots, written;
  * 1.0.0.4 - two slots, **not** written: the scan alone would rewrite them, so
    the build gate is load-bearing here, not decoration;
  * EFLC 1.1.1.0/1.1.2.0/1.1.3.0 - one slot, not written.

    python tools/verify_launcher_slots.py <game_refs>
"""

import sys
from pathlib import Path

SLOT = b"LaunchGTAIV.exe\x00"

# build id -> (image name, expected hits, gated)
EXPECTED = {
    1: ("GTAIV_1000", 2, True),
    5: ("GTAIV_1040", 2, True),
    7: ("GTAIV_1060", 2, True),
    8: ("GTAIV_1070", 2, True),
    9: ("GTAIV", 2, True),
    6: ("GTAIV_1004", 2, False),
    11: ("EFLC_1110", 1, False),
    12: ("EFLC_1120", 1, False),
    13: ("EFLC_1130", 1, False),
}


def hits(path: Path):
    data = path.read_bytes()
    found = []
    start = 0
    while True:
        index = data.find(SLOT, start)
        if index < 0:
            return found
        found.append(index)
        start = index + 1


def main() -> int:
    game_refs = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("..") / "game_refs"
    failures = 0
    for build, (name, expected, gated) in EXPECTED.items():
        image = game_refs / f"{name}.exe"
        if not image.exists():
            print(f"SKIP build {build}: {image} is missing")
            continue
        found = hits(image)
        ok = len(found) == expected
        note = "written" if gated else "not written"
        print(f"{'PASS' if ok else 'FAIL'} build {build}: {len(found)} slot(s) "
              f"(expected {expected}, {note})")
        if not ok:
            failures += 1
    print(f"{len(EXPECTED)} image(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
