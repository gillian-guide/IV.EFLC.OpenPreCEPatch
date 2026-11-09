#!/usr/bin/env python3
"""Every test that faults on purpose must suppress the fault dialog.

A deliberate access violation is a result in this project: the guard test proves the
SEH frame answers 0 by faulting inside it, the save-walk test re-runs itself as a
faulting child, and the arena test reaches the heap's own failure paths. If such a
process does not set `SEM_NOGPFAULTERRORBOX`, Windows puts an "Application Error"
dialog on the machine running the harness - which happened twice, in front of the
person waiting for the result. This check makes that mistake a test failure instead
of an interruption.

    python tools/check_fault_dialogs.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"

# A source is treated as faulting-on-purpose when it does any of these.
FAULT_EVIDENCE = ("--child", "0xC0000005", "STATUS_ACCESS_VIOLATION",
                  "EXCEPTION_EXECUTE_HANDLER", "SEH")
SUPPRESSION = "SetErrorMode"


def main() -> int:
    failures = 0
    checked = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if not re.search(r"\bint main\s*\(", text):
            continue
        checked += 1
        faults = [marker for marker in FAULT_EVIDENCE if marker in text]
        if faults and SUPPRESSION not in text:
            print(f"{path.name}: faults on purpose ({', '.join(faults)}) "
                  f"but never calls {SUPPRESSION}")
            failures += 1

    print(f"{checked} test source(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
