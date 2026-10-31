#!/usr/bin/env python3
"""Live parity check: our ASI's write log against the reference's.

Both logs name the arm, the site and the bytes written. Jump and call sites are
compared by opcode only: the displacement points at the body, and the two builds
place their bodies at different addresses.

    python tools/compare_live.py <reference log> <ours log>
"""

import re
import sys
from pathlib import Path

REFERENCE = re.compile(r"write option=(\S+) va=([0-9A-Fa-f]+) len=(\d+) bytes=((?:[0-9A-Fa-f]{2} ?)+)")
OURS = re.compile(r"(\S+) ([0-9A-Fa-f]{8}) (\d+) ([0-9A-Fa-f]+)")


def parse_reference(path):
    out = []
    for line in Path(path).read_text(errors="replace").splitlines():
        match = REFERENCE.search(line)
        if not match:
            continue
        option, va, _length, hex_bytes = match.groups()
        out.append((option, int(va, 16), bytes.fromhex(hex_bytes)))
    return out


def parse_ours(path):
    out = []
    for line in Path(path).read_text(errors="replace").splitlines():
        match = OURS.match(line.strip())
        if not match:
            continue
        arm, va, _length, hex_bytes = match.groups()
        out.append((arm, int(va, 16), bytes.fromhex(hex_bytes)))
    return out


def summarize(writes):
    """(site, length, opcode-or-bytes) -> arm, last write wins."""
    final = {}
    for arm, site, data in writes:
        final[site] = (arm, data)
    return final


def base_delta(reference, ours):
    """The two runs may load the module at different bases (ASLR). The offset
    that most pairs of writes share is the base difference."""
    from collections import Counter
    counts = Counter()
    for site in list(reference)[:400]:
        for other in list(ours)[:400]:
            counts[other - site] += 1
    return counts.most_common(1)[0][0] if counts else 0


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    reference = summarize(parse_reference(sys.argv[1]))
    ours = summarize(parse_ours(sys.argv[2]))
    delta = base_delta(reference, ours)
    if delta:
        print(f"module base differs between the runs by {delta:#x}; normalising")
        ours = {site - delta: value for site, value in ours.items()}

    print(f"reference writes: {len(reference)} sites, ours: {len(ours)} sites")

    only_ref = sorted(set(reference) - set(ours))
    only_ours = sorted(set(ours) - set(reference))
    both = sorted(set(reference) & set(ours))

    real = []
    displacements = 0
    for site in both:
        ref_arm, ref_bytes = reference[site]
        our_arm, our_bytes = ours[site]
        if ref_bytes == our_bytes:
            continue
        if ref_bytes[:1] in (b"\xE8", b"\xE9") and our_bytes[:1] == ref_bytes[:1]:
            displacements += 1
            continue
        real.append((site, ref_arm, ref_bytes, our_arm, our_bytes))

    print(f"only in the reference: {len(only_ref)}")
    for site in only_ref[:40]:
        arm, data = reference[site]
        print(f"  {site:08X} {arm} {data.hex().upper()}")
    print(f"only in ours: {len(only_ours)}")
    for site in only_ours[:40]:
        arm, data = ours[site]
        print(f"  {site:08X} {arm} {data.hex().upper()}")
    print(f"jump/call displacements (expected to differ): {displacements}")
    print(f"other differences: {len(real)}")
    for site, ref_arm, ref_bytes, our_arm, our_bytes in real[:40]:
        print(f"  {site:08X} ref {ref_arm} {ref_bytes.hex().upper()} | ours {our_arm} {our_bytes.hex().upper()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
