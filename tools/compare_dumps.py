#!/usr/bin/env python3
"""Diff a write-set dump against a baseline.

Both sides are `<RVA> <byte>` lines (full mode) or `<key> <RVA> <byte>` lines
(per-option mode). Every feature commit is closed by running this against the
stored baselines in ../readable/build/p3-final-dumps3; the empty baseline is what
`check_empty_dump.py` enforces on a tree with no arm installed.

    python tools/compare_dumps.py <baseline> <ours> [--key NAME] [--limit N]

`--key` restricts the comparison to one option's rows of a per-option pair.
Exit status: 0 when the two sides agree, 1 when they differ.
"""

import sys
from pathlib import Path


def read_rows(path, key=None):
    rows = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            rows[int(parts[0], 16)] = int(parts[1], 16)
        elif len(parts) == 3:
            if key is None or parts[0] == key:
                rows[int(parts[1], 16)] = int(parts[2], 16)
    return rows


def main() -> int:
    arguments = [argument for argument in sys.argv[1:]]
    limit = 20
    key = None
    paths = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--key" and index + 1 < len(arguments):
            key = arguments[index + 1]
            index += 2
        elif argument == "--limit" and index + 1 < len(arguments):
            limit = int(arguments[index + 1])
            index += 2
        else:
            paths.append(argument)
            index += 1

    if len(paths) != 2:
        raise SystemExit(__doc__)

    baseline = read_rows(paths[0], key)
    ours = read_rows(paths[1], key)
    if not baseline and not ours:
        print(f"both sides are empty ({'key ' + key if key else 'full dump'})")
        return 0

    missing = sorted(set(baseline) - set(ours))
    extra = sorted(set(ours) - set(baseline))
    differing = sorted(rva for rva in set(baseline) & set(ours) if baseline[rva] != ours[rva])

    print(f"baseline {len(baseline)} byte(s), ours {len(ours)} byte(s), "
          f"missing {len(missing)}, extra {len(extra)}, differing {len(differing)}")
    for rva in missing[:limit]:
        print(f"  missing  {rva:06X} baseline {baseline[rva]:02X}")
    for rva in extra[:limit]:
        print(f"  extra    {rva:06X} ours {ours[rva]:02X}")
    for rva in differing[:limit]:
        print(f"  differs  {rva:06X} baseline {baseline[rva]:02X} ours {ours[rva]:02X}")

    return 0 if not (missing or extra or differing) else 1


if __name__ == "__main__":
    sys.exit(main())
