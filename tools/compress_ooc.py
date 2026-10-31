#!/usr/bin/env python3
"""Compress data/finale2.sco into the blob embedded by OutOfCommissionFix.

The mod's loader expects the same LZ stream the original resource uses:
a sequence of literal and back-reference opcodes, terminated by 05 FA and the
big-endian Adler-32 of the decoded table. The normal build uses the checked-in original compressed resource in
ooc_payload.h; this script is the optional regeneration/research path.

  python tools/compress_ooc.py --input data/finale2.sco --output build/generated/ooc_payload.h
  python tools/compress_ooc.py --check
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCO = ROOT / "data" / "finale2.sco"
SCO_SIZE = 179536
SCO_SHA256 = "86d2bb26100061f6af79ed9dc359a8e82c47a0a37f06d6a1b02c81ec6d0bcce0"


def adler32(data: bytes) -> int:
    a, b = 1, 0
    for start in range(0, len(data), 1872):
        for value in data[start:start + 1872]:
            a += value
            b += a
        a %= 0xFFF1
        b %= 0xFFF1
    return (b << 16) | a


def literal(out: bytearray, data: bytes, start: int, end: int) -> None:
    remaining = end - start
    while remaining:
        count = min(remaining, 65536)
        if count <= 32:
            out.append(31 + count)
            out += data[start:start + count]
        elif count <= 2048:
            coded = count - 1
            out.append(8 + (coded >> 8))
            out.append(coded & 0xFF)
            out += data[start:start + count]
        else:
            coded = count - 1
            out.append(7)
            out.append((coded >> 8) & 0xFF)  # the stream stores this high byte first
            out.append(coded & 0xFF)
            out += data[start:start + count]
        start += count
        remaining -= count


def backref(out: bytearray, distance: int, count: int) -> None:
    while count:
        chunk = min(count, 256)
        if distance <= 256 and chunk <= 128:
            out.append(127 + chunk)
            out.append(distance - 1)
        else:
            coded = distance - 1
            out.append(0x40 + (coded >> 8))
            out.append(coded & 0xFF)
            out.append(chunk - 1)
        count -= chunk


def compress(data: bytes) -> bytes:
    """Greedy LZ77 using the opcodes the loader's decoder understands."""
    size = len(data)
    out = bytearray()
    heads: dict[bytes, list[int]] = {}
    last_literal = 0
    pos = 0

    def remember(at: int) -> None:
        if at + 3 <= size:
            bucket = heads.setdefault(data[at:at + 3], [])
            bucket.append(at)
            if len(bucket) > 32:
                del bucket[0]

    while pos < size:
        best_length = 0
        best_distance = 0
        if pos + 3 <= size:
            for candidate in reversed(heads.get(data[pos:pos + 3], ())):
                distance = pos - candidate
                if distance > 16384:
                    break
                limit = min(256, size - pos)
                length = 3
                while length < limit and data[candidate + length] == data[pos + length]:
                    length += 1
                if length > best_length:
                    best_length = length
                    best_distance = distance
                    if length == limit:
                        break
        if best_length >= 3:
            if pos > last_literal:
                literal(out, data, last_literal, pos)
            backref(out, best_distance, best_length)
            for at in range(pos, pos + best_length):
                remember(at)
            pos += best_length
            last_literal = pos
        else:
            remember(pos)
            pos += 1
    if last_literal < size:
        literal(out, data, last_literal, size)
    out.append(5)  # terminator, followed by the Adler trailer
    out.append(0xFA)
    out += struct.pack(">I", adler32(data))
    return bytes(out)


def decode(blob: bytes, expected_size: int) -> bytes:
    """Mirror of zpatch_ooc_decode, used by --check."""
    out = bytearray(expected_size)
    cursor = 0
    pc = 0

    def copy_literal(count: int, source: int) -> None:
        nonlocal cursor
        if cursor + count <= expected_size:
            out[cursor:cursor + count] = blob[source:source + count]
            cursor += count
        else:
            cursor = expected_size + 1

    def copy_backref(distance: int, count: int) -> None:
        nonlocal cursor
        if cursor + count > expected_size or distance > cursor:
            cursor = expected_size + 1
            return
        for i in range(count):
            out[cursor + i] = out[cursor - distance + i]
        cursor += count

    while True:
        op = blob[pc]
        previous = pc
        if op < 0x20:
            if op >= 0x18:
                copy_backref(blob[pc + 2] + 256 * blob[pc + 1] + 65536 * op - 1572863,
                             blob[pc + 3] + 1)
                pc += 4
            elif op >= 0x10:
                copy_backref(blob[pc + 2] + 256 * blob[pc + 1] + 65536 * op - 0xFFFFF,
                             (blob[pc + 3] << 8) + blob[pc + 4] + 1)
                pc += 5
            elif op >= 8:
                count = 256 * op + blob[pc + 1] - 2047
                copy_literal(count, pc + 2)
                pc += 2 + count
            elif op == 7:
                count = 256 * blob[pc + 1] + blob[pc + 2] + 1
                copy_literal(count, pc + 3)
                pc += 3 + count
            elif op == 6:
                copy_backref(blob[pc + 3] + 256 * blob[pc + 2] + 65536 * blob[pc + 1] + 1,
                             blob[pc + 4] + 1)
                pc += 5
            elif op == 4:
                copy_backref(blob[pc + 3] + 256 * blob[pc + 2] + 65536 * blob[pc + 1] + 1,
                             (blob[pc + 4] << 8) + blob[pc + 5] + 1)
                pc += 6
            else:
                break
        elif op < 0x80:
            if op >= 0x40:
                copy_backref(blob[pc + 1] + 256 * op - 0x3FFF, blob[pc + 2] + 1)
                pc += 3
            else:
                count = op - 31
                copy_literal(count, pc + 1)
                pc += op - 30
        else:
            copy_backref(blob[pc + 1] + 1, op - 127)
            pc += 2
        if pc == previous:
            break
    if cursor != expected_size or blob[pc:pc + 2] != b"\x05\xFA":
        raise ValueError("compressed stream does not decode to the expected table")
    trailer = struct.unpack(">I", blob[pc + 2:pc + 6])[0]
    if adler32(out) != trailer:
        raise ValueError("Adler-32 trailer mismatch")
    return bytes(out)


def write_header(blob: bytes, output: Path) -> None:
    lines = [
        "// Generated by tools/compress_ooc.py from data/finale2.sco.",
        "// Do not edit; regenerate at build time.",
        "",
        'extern "C" const unsigned char zpatch_ooc_blob[] = {',
    ]
    for start in range(0, len(blob), 12):
        chunk = blob[start:start + 12]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines += ["};", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", default=str(SCO))
    parser.add_argument("--output", default=str(ROOT / "build" / "generated" / "ooc_payload.h"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    data = Path(args.input).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != SCO_SIZE or digest != SCO_SHA256:
        print(f"error: {args.input} is {len(data)} bytes / {digest}; expected "
              f"{SCO_SIZE} / {SCO_SHA256}")
        return 2
    blob = compress(data)
    if decode(blob, SCO_SIZE) != data:
        print("error: compressed stream failed its round-trip check")
        return 2
    if args.check:
        print(f"finale2.sco: {len(data)} bytes, sha256={digest}")
        print(f"compressed:  {len(blob)} bytes, round-trip OK")
        return 0
    write_header(blob, Path(args.output))
    print(f"wrote {args.output} ({len(blob)} bytes from {len(data)} raw)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
