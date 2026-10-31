#!/usr/bin/env python3
"""Diff the readable port's applied write set against the reference run's log.

`ZolikaPatch.log` is the trace of the last run of the reference implementation
(the reconstruction). Its `write option=... va=... len=... bytes=...` lines are
the writes that run performed inside the game module. This script replays them
in order to get the final byte at every modified RVA, reads the same from a
write-set dump, and reports every difference.

    python tools/compare_writes.py <log> <dump> [--image-size HEX]
"""

import re
import sys
from pathlib import Path

MODULE_BASE = 0x00400000
WRITE = re.compile(r"write option=(\S+) va=([0-9A-Fa-f]+) len=(\d+) bytes=((?:[0-9A-Fa-f]{2} ?)+)")


def reference_writes(log_path, image_size):
    """Accepts the reference run's log or an already-dumped `RVA byte` file."""
    final = {}
    options = {}
    text = Path(log_path).read_text(errors="replace")
    if "write option=" not in text:
        return dump_writes(log_path), {}
    for line in text.splitlines():
        match = WRITE.search(line)
        if not match:
            continue
        option, va, _length, hex_bytes = match.groups()
        address = int(va, 16)
        if not (MODULE_BASE <= address < MODULE_BASE + image_size):
            continue
        for offset, byte in enumerate(bytes.fromhex(hex_bytes)):
            rva = address - MODULE_BASE + offset
            final[rva] = byte
            options[rva] = option
    return final, options


def dump_writes(dump_path):
    final = {}
    for line in Path(dump_path).read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            final[int(parts[0], 16)] = int(parts[1], 16)
    return final


def is_jump_opcode(byte):
    return byte in (0xE8, 0xE9)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    log_path, dump_path = sys.argv[1], sys.argv[2]
    image_size = 0x18B2000
    for argument in sys.argv[3:]:
        if argument.startswith("--image-size="):
            image_size = int(argument.split("=", 1)[1], 16)

    reference, options = reference_writes(log_path, image_size)
    ours = dump_writes(dump_path)
    print(f"reference writes: {len(reference)} bytes, ours: {len(ours)} bytes")

    missing = sorted(set(reference) - set(ours))
    extra = sorted(set(ours) - set(reference))
    differing = sorted(rva for rva in set(reference) & set(ours) if reference[rva] != ours[rva])

    # A jump's displacement points at a body: ours and the reference place
    # different bodies, so only the opcode is comparable.
    displacement = [rva for rva in differing if is_jump_opcode(reference[rva])]
    real = [rva for rva in differing if rva not in set(displacement)]

    print(f"sites only in the reference: {len(missing)}")
    for rva in missing[:40]:
        print(f"  RVA {rva:06X} {options.get(rva, '?')} (reference byte {reference[rva]:02X})")
    print(f"sites only in ours: {len(extra)}")
    for rva in extra[:40]:
        print(f"  RVA {rva:06X} (our byte {ours[rva]:02X})")
    print(f"jump displacements (expected to differ): {len(displacement)}")
    print(f"other byte differences: {len(real)}")
    for rva in real[:40]:
        print(f"  RVA {rva:06X} {options.get(rva, '?')} reference {reference[rva]:02X} ours {ours[rva]:02X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
