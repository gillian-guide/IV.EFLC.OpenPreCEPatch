#!/usr/bin/env python3
"""Per-key parity across every supported build.

Runs tools/parity_report.py for each build in turn and prints one row per build,
so a change that closes a key on 1.0.8.0 but breaks another build is visible.

    python tools/parity_matrix.py [--builds 1,5,6,...]

Exit status: 0 when every build is clean, 1 otherwise (the current state: the
feature modules are not finished, so open keys are expected).
"""

import subprocess
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILDS = [1, 5, 6, 7, 8, 9, 11, 12, 13]


def main() -> int:
    builds = BUILDS
    if "--builds" in sys.argv:
        builds = [int(part) for part in sys.argv[sys.argv.index("--builds") + 1].split(",")]

    rows = []
    failures = 0
    for build in builds:
        result = subprocess.run([sys.executable, str(ROOT / "tools" / "parity_report.py"), str(build)],
                                capture_output=True, text=True)
        summary = [line for line in result.stdout.splitlines() if "key(s) closed" in line]
        if not summary:
            print(f"build {build}: no summary (exit {result.returncode})")
            failures += 1
            continue
        parts = re.findall(r"(\d+) key\(s\) closed, (\d+) open", summary[0])
        closed, opened = (int(parts[0][0]), int(parts[0][1])) if parts else (0, 99)
        rows.append((build, closed, opened))
        failures += 0 if opened == 0 else 1

    print(f"{'build':>6} {'closed':>7} {'open':>5}")
    for build, closed, opened in rows:
        print(f"{build:>6} {closed:>7} {opened:>5}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
