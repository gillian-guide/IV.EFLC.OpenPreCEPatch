#!/usr/bin/env python3
"""Validate a candidate signature against the on-disk game images.

A pattern is only safe when it has exactly one hit, at the intended RVA, on every
build its arm is gated for. This tool maps a PE32 image the way the loader does
(section virtual addresses), scans a signature over the executable sections and
prints the hit RVAs, so a conversion can be checked against every executable in
game_refs without launching the game.

    python tools/opcep_sigcheck.py --build 1060 --rva 3A14 \
        --sig "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 0F"

    python tools/opcep_sigcheck.py --candidates .scratch/candidates.tsv

The TSV form is: arm<TAB>compact<TAB>rva(hex)<TAB>signature. Exit code 1 when a
row has zero or more than one hit, or when the only hit is not at the RVA.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_DIR = ROOT.parent / "game_refs"
IMAGES = {
    1000: "GTAIV_1000.exe",
    1004: "GTAIV_1004.exe",
    1040: "GTAIV_1040.exe",
    1060: "GTAIV_1060.exe",
    1070: "GTAIV_1070.exe",
    1080: "GTAIV.exe",
    1110: "EFLC_1110.exe",
    1120: "EFLC_1120.exe",
    1130: "EFLC_1130.exe",
}


def map_image(path: Path) -> tuple[bytearray, int]:
    """Return the image laid out at its virtual addresses and SizeOfImage."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise ValueError(f"{path}: no PE signature")
    coff = e_lfanew + 4
    sections, = struct.unpack_from("<H", data, coff + 2)
    size_of_optional, = struct.unpack_from("<H", data, coff + 16)
    optional = coff + 20
    size_of_image, = struct.unpack_from("<I", data, optional + 56)
    size_of_headers, = struct.unpack_from("<I", data, optional + 60)
    image = bytearray(size_of_image)
    image[:size_of_headers] = data[:size_of_headers]
    table = optional + size_of_optional
    for index in range(sections):
        off = table + index * 40
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, off + 8)
        count = min(rsize, vsize)
        if count and vaddr + count <= size_of_image and rptr + count <= len(data):
            image[vaddr : vaddr + count] = data[rptr : rptr + count]
    return image, size_of_image


def parse_signature(text: str) -> tuple[bytes | None, bytes]:
    pattern = []
    mask = []
    for token in text.split():
        if token in {"?", "??"}:
            pattern.append(0)
            mask.append(0)
            continue
        if token.startswith("0x"):
            token = token[2:]
        if not re.fullmatch(r"[0-9A-Fa-f]{1,2}", token):
            raise ValueError(f"bad token {token!r}")
        pattern.append(int(token, 16))
        mask.append(0xFF)
    return bytes(pattern), bytes(mask)


def find_hits(image: bytearray, text: str, executable_end: int) -> list[int]:
    pattern, mask = parse_signature(text)
    hits = []
    first = pattern[: len(pattern)]
    start = 0
    end = executable_end
    while True:
        index = image.find(first[:1], start, end)
        if index < 0 or index >= end:
            return hits
        if all((image[index + i] & mask[i]) == (pattern[i] & mask[i]) for i in range(len(pattern))):
            hits.append(index)
        start = index + 1


def executable_end(image: bytearray, size_of_image: int) -> int:
    e_lfanew = struct.unpack_from("<I", image, 0x3C)[0]
    optional = e_lfanew + 4 + 20
    table = optional + struct.unpack_from("<H", image, e_lfanew + 4 + 16)[0]
    sections, = struct.unpack_from("<H", image, e_lfanew + 4 + 2)
    end = 0
    for index in range(sections):
        off = table + index * 40
        vsize, vaddr = struct.unpack_from("<II", image, off + 8)
        characteristics, = struct.unpack_from("<I", image, off + 36)
        if characteristics & 0x20000000:
            end = max(end, vaddr + vsize)
    return min(end or size_of_image, size_of_image)


def check_row(images, arm: str, build: int, rva: int, signature: str) -> list[str]:
    problems = []
    path = GAME_DIR / IMAGES[build]
    if not path.exists():
        return [f"{arm} {build}: image {path.name} missing"]
    image, size = map_image(path)
    end = executable_end(image, size)
    hits = find_hits(image, signature, end)
    if hits != [rva]:
        problems.append(
            f"{arm} {build}: expected one hit at {rva:06X}, got {[f'{h:06X}' for h in hits][:8]}"
            f" ({len(hits)} hits)"
        )
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=int)
    parser.add_argument("--rva", type=lambda text: int(text, 16))
    parser.add_argument("--sig")
    parser.add_argument("--candidates", type=Path)
    args = parser.parse_args(argv)

    failures = 0
    if args.candidates:
        rows = []
        for line in args.candidates.read_text(encoding="utf-8").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            arm, build, rva, signature = line.split("\t")
            rows.append((arm, int(build), int(rva, 16), signature))
        print(f"opcep_sigcheck: {len(rows)} candidate rows")
        for arm, build, rva, signature in rows:
            problems = check_row(None, arm, build, rva, signature)
            for problem in problems:
                print(f"  FAIL {problem}", file=sys.stderr)
            failures += len(problems)
        if failures == 0:
            print(f"opcep_sigcheck: PASS ({len(rows)} rows: one hit at the intended RVA)")
        return 1 if failures else 0

    if args.build is None or args.rva is None or not args.sig:
        parser.error("either --candidates, or --build/--rva/--sig")
    problems = check_row(None, "sig", args.build, args.rva, args.sig)
    image, size = map_image(GAME_DIR / IMAGES[args.build])
    end = executable_end(image, size)
    hits = find_hits(image, args.sig, end)
    print(f"{args.build}: {len(hits)} hit(s) at {[f'{h:06X}' for h in hits][:20]}")
    window = image[args.rva : args.rva + 32]
    print(f"bytes at {args.rva:06X}: {' '.join(f'{b:02X}' for b in window)}")
    for problem in problems:
        print(f"  FAIL {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
