#!/usr/bin/env python3
"""Check the LaunchWithPlayGTAIV slot scan against the original per-build table.

The launcher arm only runs when a PlayGTAIV.exe process is present, so the
write-set dump never exercises it. This tool supplies the missing proof: it maps
each game executable the way the loader would and scans the recovered 16-byte
string, then checks that on every build the original table covered, the hits are
exactly the two RVAs that table named (research 11 §3.4).

Run from `readable/`; the images live in `../game_refs/`.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME_REFS = ROOT.parent / "game_refs"

# The original table (research 11): build -> the two launcher-slot RVAs.
EXPECTED = {
    "v1000": ("GTAIV_1000.exe", 0x9F043C, 0x98FA0C),
    "v1040": ("GTAIV_1040.exe", 0x9EA184, 0x9DD8A0),
    "v1060": ("GTAIV_1060.exe", 0x9F8F40, 0xA9D0D8),
    "v1070": ("GTAIV_1070.exe", 0x9F9F70, 0xA9E118),
    "v1080": ("GTAIV.exe", 0xA508F8, 0xA5A5E4),
}
SIGNATURE = bytes.fromhex("4C61756E636847544149562E65786500")  # "LaunchGTAIV.exe\0"
# Every gated build has exactly two slots; the other on-disk images are counted
# only, because the arm's build gate keeps them out.
OUT_OF_GATE = {
    "GTAIV_1004.exe": 2,
    "GTAIV_1010.exe": 2,
    "EFLC_1110.exe": 1,
    "EFLC_1120.exe": 1,
    "EFLC_1130.exe": 1,
    "GTAIV_12059.exe": 0,
}


def map_image(path: pathlib.Path):
    data = path.read_bytes()
    pe = int.from_bytes(data[0x3C:0x40], "little")
    coff = pe + 4
    nsections = int.from_bytes(data[coff + 2 : coff + 4], "little")
    opt = coff + 20
    size_of_image = int.from_bytes(data[opt + 56 : opt + 60], "little")
    size_of_headers = int.from_bytes(data[opt + 60 : opt + 64], "little")
    section_table = opt + int.from_bytes(data[coff + 16 : coff + 18], "little")
    image = bytearray(size_of_image)
    image[: min(size_of_headers, len(data))] = data[: size_of_headers]
    for i in range(nsections):
        base = section_table + i * 40
        vsize = int.from_bytes(data[base + 8 : base + 12], "little")
        va = int.from_bytes(data[base + 12 : base + 16], "little")
        rawsize = int.from_bytes(data[base + 16 : base + 20], "little")
        rawptr = int.from_bytes(data[base + 20 : base + 24], "little")
        chunk = data[rawptr : rawptr + min(vsize, rawsize)]
        image[va : va + len(chunk)] = chunk
    return bytes(image)


def scan(image: bytes, needle: bytes):
    hits = []
    start = 0
    while True:
        found = image.find(needle, start)
        if found < 0:
            return hits
        hits.append(found)
        start = found + 1


def main() -> int:
    if not GAME_REFS.is_dir():
        print(f"error: {GAME_REFS} is missing", file=sys.stderr)
        return 2
    failures = 0
    for build, (name, first, second) in EXPECTED.items():
        path = GAME_REFS / name
        if not path.is_file():
            print(f"SKIP {build}: {name} is not on disk")
            failures += 1
            continue
        hits = scan(map_image(path), SIGNATURE)
        want = sorted([first, second])
        if hits != want:
            failures += 1
            print(f"FAIL {build} ({name}): hits {[hex(v) for v in hits]} != "
                  f"{[hex(v) for v in want]}")
        else:
            print(f"PASS {build} ({name}): {len(hits)} hits at "
                  f"{', '.join(hex(v) for v in hits)}")
    for name, count in OUT_OF_GATE.items():
        path = GAME_REFS / name
        if not path.is_file():
            continue
        hits = scan(map_image(path), SIGNATURE)
        state = "ok" if len(hits) == count else f"expected {count}"
        print(f"INFO {name}: {len(hits)} hit(s) outside the gate ({state})")
    if failures:
        print(f"\nFAIL: {failures} gated build(s) do not match the original table")
        return 1
    print("\nPASS: the launcher scan resolves exactly the original table's slots "
          "on every gated build")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
