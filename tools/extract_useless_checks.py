#!/usr/bin/env python3
"""Generate (or verify) src/useless_checks_data.inc for readable.

RemoveUselessChecks is the original ASI's sub_10044BD0: five uint32 tables of
game RVAs in .rdata plus 22 single writes. The tables are the original bytes, so
this tool reads them straight out of the shipped ASI and refuses to write a file
whose per-array SHA-256 differs from the pinned value below.

Usage (from the readable repository root):
  python tools/extract_useless_checks.py --write       regenerate the .inc
  python tools/extract_useless_checks.py --verify      re-extract and diff (CI)
  python tools/extract_useless_checks.py --print-sha   verify the checked-in
                                                      header's hashes only

The ASI is located at ../game_refs/ZolikaPatch_original.asi.off, game_refs/... or
the path given with --asi. --verify --print-sha work without the ASI.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INC = REPO / "src" / "useless_checks_data.inc"
MANIFEST = REPO / "data" / "useless_checks.json"

ASI_SHA256 = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
IMAGE_BASE = 0x10000000

# name, build, VA, element count, shape, sha256 of the array bytes
ARRAYS = [
    ("kUselessBuild9Nop5", 9, 0x10104090, 2230, "NOP5",
     "e92eb0c352daee36f255a52d946f99de43d6a9a3dd584be2af54b496c035373e"),
    ("kUselessBuild9MovAl1", 9, 0x10106448, 45, "MOV_AL_1",
     "9844fbfa3318acaa07080a9585893d32768f45cac4743e3a593fe9112f65af68"),
    ("kUselessBuild9XorAlAl", 9, 0x10106368, 56, "XOR_AL_AL",
     "aa7f4bfd10d299ec10a1e286fb3a49110b833afd0b099ce5accbd7a290414221"),
    ("kUselessBuild8Nop5", 8, 0x10108B88, 2288, "NOP5",
     "33b5a773610baf43e0f7599b2c4526b929a089274087441b924c1f8aaa558c3e"),
    ("kUselessBuild5Nop5", 5, 0x10106500, 2466, "NOP5",
     "a7c1e7341541b0a11f2220e2519d445ada66c3dfed46350186e14a6a0d2f12cb"),
]

# The handler's singles, in sub_10044BD0's own order (0x10044C05 first,
# 0x10044F82 last). handler_va is the instruction that writes the site; it is
# used to check that the RVA really is an immediate in the handler.
SINGLES = {
    9: [
        (0x559127, 38, 0x90, 0x10044C05), (0x5592A7, 38, 0x90, 0x10044C43),
        (0x559013, 6, 0x90, 0x10044C81), (0x55F5B5, 7, 0x90, 0x10044CAF),
        (0x55F5BD, 6, 0x90, 0x10044CDE), (0x55FC21, 7, 0x90, 0x10044D0E),
        (0x55FC2E, 6, 0x90, 0x10044D3F), (0x55EACA, 7, 0x90, 0x10044D6D),
        (0x55EADF, 6, 0x90, 0x10044D9E), (0x55EB83, 13, 0x90, 0x10044DC4),
        (0x5594CA, 7, 0x90, 0x10044E07), (0x5594A2, 7, 0x90, 0x10044E38),
        (0x5594B7, 2, 0x90, 0x10044E69), (0x559CC0, 9, 0x90, 0x10044E92),
        (0x77027A, 9, 0x90, 0x10044EC8), (0x559591, 7, 0x90, 0x10044EFC),
        (0x559598, 1, 0xEB, 0x10044F2D), (0x625556, 7, 0x90, 0x10044F53),
        (0x62555D, 1, 0xEB, 0x10044F82),
    ],
    8: [(0x3DA1B7, 38, 0x90, 0x100451B3), (0x3DA337, 38, 0x90, 0x100451F1)],
    5: [(0x46CCE7, 38, 0x90, 0x10045284), (0x46CE67, 38, 0x90, 0x100452C2)],
}

SHAPE_BYTES = {
    "NOP5": "five 0x90 bytes, one 5-byte protected write",
    "MOV_AL_1": "0xB0 0x01 0x90 0x90 0x90, five 1-byte protected writes",
    "XOR_AL_AL": "0x32 0xC0 0x90 0x90 0x90, five 1-byte protected writes",
}


def find_asi(explicit):
    if explicit:
        return Path(explicit)
    for candidate in (REPO / "game_refs", REPO.parent / "game_refs"):
        path = candidate / "ZolikaPatch_original.asi.off"
        if path.exists():
            return path
    return None


def parse_pe(blob):
    if blob[:2] != b"MZ":
        raise SystemExit("not a PE file")
    e_lfanew = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise SystemExit("missing PE signature")
    coff = e_lfanew + 4
    section_count, = struct.unpack_from("<H", blob, coff + 2)
    optional_size, = struct.unpack_from("<H", blob, coff + 16)
    optional = coff + 20
    magic, = struct.unpack_from("<H", blob, optional)
    if magic != 0x10B:
        raise SystemExit(f"expected PE32 (0x10B), got {magic:#x}")
    image_base, = struct.unpack_from("<I", blob, optional + 28)
    sections = []
    table = optional + optional_size
    for i in range(section_count):
        entry = table + i * 40
        name = blob[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, entry + 8)
        sections.append((name, vaddr, vsize, rawptr, rawsize))
    return image_base, sections


class PeData:
    def __init__(self, blob):
        self.blob = blob
        self.image_base, self.sections = parse_pe(blob)

    def read(self, rva, size):
        for name, vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva and rva + size <= vaddr + max(vsize, rawsize):
                offset = rawptr + (rva - vaddr)
                return self.blob[offset:offset + size]
        raise SystemExit(f"rva {rva:#x} (+{size}) is not mapped by any section")


def extract_arrays(pe):
    out = []
    for name, build, va, count, shape, sha in ARRAYS:
        raw = pe.read(va - IMAGE_BASE, count * 4)
        actual = hashlib.sha256(raw).hexdigest()
        if actual != sha:
            raise SystemExit(f"{name}: sha256 {actual} != pinned {sha}")
        values = struct.unpack(f"<{count}I", raw)
        out.append(dict(name=name, build=build, va=va, count=count, shape=shape,
                        sha256=sha, raw=raw, values=values))
    return out


def check_singles(pe):
    """Each single's RVA must appear as an immediate next to its handler VA."""
    for build, rows in SINGLES.items():
        for rva, length, byte, handler_va in rows:
            window = pe.read(handler_va - 8 - IMAGE_BASE, 32)
            needle = struct.pack("<I", rva)
            if needle not in window:
                raise SystemExit(
                    f"build {build} single {rva:#x}: not encoded at handler {handler_va:#x}")
            if length not in window:
                raise SystemExit(
                    f"build {build} single {rva:#x}: length {length} not encoded at "
                    f"handler {handler_va:#x}")


def emit(arrays, asi_sha):
    lines = []
    add = lines.append
    add("// GENERATED FILE - do not edit.")
    add("//")
    add("// RemoveUselessChecks write data: the original handler sub_10044BD0's five")
    add("// game-RVA tables and its 23 single writes (19 build 9, 2 build 8, 2 build 5),")
    add("// transcribed from the shipped ASI.")
    add("//")
    add("// Source: game_refs/ZolikaPatch_original.asi.off (PE32, image base 0x10000000)")
    add(f"//   sha256 {asi_sha}")
    add("// Generator: tools/extract_useless_checks.py (--verify re-extracts and diffs).")
    add("//")
    add("// Each table is applied as game_base + rva, in array order, one entry at a time:")
    for entry in arrays:
        add(f"//   build {entry['build']}, VA {entry['va']:#010x}, {entry['count']} entries: "
            f"{SHAPE_BYTES[entry['shape']]}")
        add(f"//     sha256 {entry['sha256']}")
    add("")
    for entry in arrays:
        values = entry["values"]
        add(f"constexpr std::uint32_t {entry['name']}[] = {{")
        for i in range(0, len(values), 10):
            row = ", ".join(f"0x{v:06X}u" for v in values[i:i + 10])
            add("\t" + row + ",")
        add("};")
        add("")
    add("// The handler applies the singles first (one protected write of `length` bytes")
    add("// each), then the build's tables. Order is the handler's own.")
    for build in (9, 8, 5):
        add(f"constexpr UselessCheckSingle kUselessSingles{build}[] = {{")
        for rva, length, byte, _ in SINGLES[build]:
            add(f"\t{{0x{rva:06X}u, {length}u, 0x{byte:02X}u}},")
        add("};")
        add("")
    return "\n".join(lines)


def build_manifest(arrays, asi_sha):
    return {
        "source": "game_refs/ZolikaPatch_original.asi.off",
        "source_sha256": asi_sha,
        "image_base": "0x10000000",
        "generator": "tools/extract_useless_checks.py",
        "arrays": [
            {"name": e["name"], "build": e["build"], "va": f"{e['va']:#010x}",
             "count": e["count"], "shape": e["shape"], "sha256": e["sha256"]}
            for e in arrays
        ],
        "singles": [
            {"build": build, "rva": f"{rva:#08x}", "length": length, "byte": f"{byte:#04x}",
             "handler_va": f"{handler:#010x}"}
            for build, rows in SINGLES.items()
            for rva, length, byte, handler in rows
        ],
    }


def header_hashes(text):
    return {(build, va): sha for build, va, sha in re.findall(
        r"build (\d+), VA (0x[0-9a-f]+), \d+ entries: [^\n]*\n//\s+sha256 ([0-9a-f]{64})", text)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asi", help="path to ZolikaPatch_original.asi.off")
    parser.add_argument("--write", action="store_true", help="regenerate the .inc")
    parser.add_argument("--verify", action="store_true", help="re-extract and diff")
    parser.add_argument("--print-sha", action="store_true",
                        help="check the checked-in header hashes only")
    args = parser.parse_args()
    if not (args.write or args.verify or args.print_sha):
        parser.error("choose --write, --verify or --print-sha")

    if args.print_sha:
        text = INC.read_text(encoding="utf-8")
        found = header_hashes(text)
        pinned = {(str(e[1]), f"{e[2]:#010x}"): e[5] for e in ARRAYS}
        for key, sha in pinned.items():
            if found.get(key) != sha:
                print(f"FAIL {key[0]}/{key[1]}: header {found.get(key)} != pinned {sha}")
                return 1
        print(f"ok: {len(pinned)} array hashes match the header")
        return 0

    asi = find_asi(args.asi)
    if asi is None or not asi.exists():
        print("skip: ZolikaPatch_original.asi.off not found (pass --asi)", file=sys.stderr)
        return 2
    blob = asi.read_bytes()
    asi_sha = hashlib.sha256(blob).hexdigest()
    if asi_sha != ASI_SHA256:
        print(f"FAIL: ASI sha256 {asi_sha} != pinned {ASI_SHA256}", file=sys.stderr)
        return 1
    pe = PeData(blob)
    if pe.image_base != IMAGE_BASE:
        print(f"FAIL: image base {pe.image_base:#x} != {IMAGE_BASE:#x}", file=sys.stderr)
        return 1
    arrays = extract_arrays(pe)
    check_singles(pe)
    text = emit(arrays, asi_sha)

    if args.write:
        INC.write_text(text, encoding="utf-8", newline="\n")
        MANIFEST.write_text(json.dumps(build_manifest(arrays, asi_sha), indent=2) + "\n",
                            encoding="utf-8", newline="\n")
        print(f"wrote {INC.relative_to(REPO)} ({len(text)} bytes, "
              f"{sum(e['count'] for e in arrays)} RVAs + 23 singles)")
        return 0

    current = INC.read_text(encoding="utf-8")
    if current != text:
        print(f"FAIL: {INC.relative_to(REPO)} is stale; run --write", file=sys.stderr)
        return 1
    print("ok: data file matches the ASI")
    return 0


if __name__ == "__main__":
    sys.exit(main())
