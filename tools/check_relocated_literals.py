#!/usr/bin/env python3
"""Reject a recovered signature whose match depends on the relocation delta.

Every supported executable is loaded relocated: its PE header carries
`DYNAMIC_BASE` (`DllCharacteristics` 0x140), so the Windows loader applies the
image's own `.reloc` table and every absolute VA baked into code, data or an
instruction operand is rewritten with `delta = actualBase - ImageBase`. The live
base is arbitrary on every launch, so a signature string whose concrete bytes
overlap a fixup's *high half* matches on some launches and not on others: the arm
silently fires or misses depending on ASLR. That is a real in-game defect, not
just a harness one.

This script measures it without loading any game:

  * every pattern-like string literal in `source/*.ixx` (direct `find_pattern`
    arguments **and** the table rows -- the `kModChecks*` tables, the
    launcher/audio tables -- which the old `find_pattern("...")`-only extraction
    could not see);
  * the hit set (count and every hit RVA) in each image's unrelocated bytes, and
    again in the image relocated by each delta in `DELTAS`;
  * a literal is relocation-safe when its hit set is identical at every delta. A
    literal whose hit set changes is reported with the build and the delta that
    breaks it, and classified against the key's recovered original strings:
      (a) the literal *is* the original handler's own string -> a preserved
          original fault (1:1 policy: do not widen it; the original itself fires
          only on some launches);
      (b) the literal differs from a recovered original string -> ours to fix,
          and the original's string is the authority;
      (c) no recovered original string -> unverified, reported as such.
  * the static half of the same rule is kept: a concrete byte of a base-image hit
    that sits in a fixup's high half is an address byte and must be a wildcard.

    python tools/check_relocated_literals.py [--game DIR] [--verbose] [--all]

Exit status 0 when every changed literal is a preserved original fault, 1 when
any literal is ours to fix (or unverified).
"""

import argparse
import os
import re
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_signature_parity import catalogue_keys, key_for, read_evidence  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAME = ROOT.parent / "game_refs"

# The images the per-key baselines and the 18-cell matrix are scored on.
BUILDS = ["GTAIV_1000", "GTAIV_1004", "GTAIV_1040", "GTAIV_1060", "GTAIV_1070",
          "GTAIV", "EFLC_1110", "EFLC_1120", "EFLC_1130"]

# The relocation deltas every literal is measured at. The hit set must be the
# same at each of them as it is in the unrelocated image, or the arm would fire on
# some launches and not others. The list covers a small high word (0x00010000,
# 0x01000000), the two deltas the harness itself pinned or observed (0x2FC00000,
# the dump's fixed delta; 0x05D90000, the third hash the old random-base dump
# produced once), the delta that makes the original MiscFixes literal hit
# (0x05600000), and a negative delta (0xFFFF0000).
DELTAS = [0x00010000, 0x01000000, 0x05600000, 0x05D90000, 0x2FC00000, 0xFFFF0000]

IMAGE_SCN_MEM_EXECUTE = 0x20000000


class Image:
    """The executable mapped at its RVAs, plus what `.reloc` rewrites."""

    def __init__(self, path):
        self.path = path
        data = path.read_bytes()
        if len(data) < 0x40:
            raise ValueError(f"{path} is not a PE file")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError(f"{path} has no PE signature")
        coff = e_lfanew + 4
        self.sections = []
        count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        self.size = struct.unpack_from("<I", data, optional + 56)[0]
        reloc_rva, reloc_size = struct.unpack_from("<II", data, optional + 96 + 5 * 8)
        for index in range(count):
            entry = optional + optional_size + index * 40
            name = data[entry:entry + 8].rstrip(b"\0").decode(errors="replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, entry + 8)
            characteristics = struct.unpack_from("<I", data, entry + 36)[0]
            self.sections.append((name, virtual_address, virtual_size, raw_size,
                                  raw_offset, characteristics))

        # The image as the loader would map it before relocating anything.
        self.bytes = bytearray(self.size)
        for name, virtual_address, virtual_size, raw_size, raw_offset, _ in self.sections:
            blob = data[raw_offset:raw_offset + raw_size]
            self.bytes[virtual_address:virtual_address + len(blob)] = blob
        headers = min(struct.unpack_from("<I", data, optional + 60)[0], len(data))
        self.bytes[0:headers] = data[0:headers]

        # The scan range game.ixx uses: base to the end of the last executable
        # section.
        self.scan_end = 0
        for name, virtual_address, virtual_size, raw_size, _, characteristics in self.sections:
            if characteristics & IMAGE_SCN_MEM_EXECUTE:
                self.scan_end = virtual_address + (raw_size or virtual_size)
        if self.scan_end == 0:
            self.scan_end = self.size

        self.relocated = bytearray(self.size)   # 1 = this byte is relocated
        self.fields = set()                     # the first byte of each HIGHLOW dword
        if reloc_rva == 0 or reloc_size == 0:
            raise ValueError(f"{path} has no .reloc directory")
        offset = self._file_offset(data, reloc_rva)
        if offset is None:
            raise ValueError(f"{path}: .reloc is not backed by file data")
        end = offset + reloc_size
        while offset + 8 <= end:
            page, block_size = struct.unpack_from("<II", data, offset)
            if block_size == 0:
                break
            if block_size < 8 or offset + block_size > end:
                raise ValueError(f"{path}: malformed .reloc block")
            cursor = offset + 8
            while cursor + 2 <= offset + block_size:
                entry = struct.unpack_from("<H", data, cursor)[0]
                cursor += 2
                kind = entry >> 12
                if kind == 3:  # IMAGE_REL_BASED_HIGHLOW
                    field = page + (entry & 0x0FFF)
                    self.relocated[field:field + 4] = b"\1\1\1\1"
                    self.fields.add(field)
                elif kind != 0:
                    raise ValueError(f"{path}: unsupported relocation type {kind}")
            offset += block_size
        self.field_list = sorted(self.fields)
        self._moved = None
        self._moved_delta = None
        self._hits = {}

    def _file_offset(self, data, rva):
        for name, virtual_address, virtual_size, raw_size, raw_offset, _ in self.sections:
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                delta = rva - virtual_address
                if delta < raw_size:
                    return raw_offset + delta
        return None

    def field_covering(self, rva):
        """The fixup dword that actually contains `rva`, or None."""
        low, high = 0, len(self.field_list)
        while low < high:
            middle = (low + high) // 2
            if self.field_list[middle] <= rva:
                low = middle + 1
            else:
                high = middle
        index = low - 1
        if index >= 0 and rva < self.field_list[index] + 4:
            return self.field_list[index]
        return None

    def relocate(self, delta):
        """The image with every HIGHLOW fixup moved by `delta` (cached per delta)."""
        if delta == 0:
            return self.bytes
        if self._moved_delta != delta:
            moved = bytearray(self.bytes)
            for field in self.fields:
                value = struct.unpack_from("<I", moved, field)[0]
                struct.pack_into("<I", moved, field, (value + delta) & 0xFFFFFFFF)
            self._moved = moved
            self._moved_delta = delta
        return self._moved

    def find(self, tokens, delta=0):
        """Every non-overlapping hit RVA of the pattern in the range, at `delta`.

        The longest run of concrete bytes anchors a `bytes.find` scan, which is
        much faster than a regex over a 10 MB `.text`; each candidate is then
        verified byte by byte.
        """
        key = (delta, tuple(tokens))
        if key in self._hits:
            return self._hits[key]
        data = self.relocate(delta)
        runs = []
        cursor = 0
        while cursor < len(tokens):
            if tokens[cursor] == "?":
                cursor += 1
                continue
            start = cursor
            while cursor < len(tokens) and tokens[cursor] != "?":
                cursor += 1
            runs.append((start, bytes(int(token, 16) for token in tokens[start:cursor])))
        if not runs:
            self._hits[key] = list(range(0, self.scan_end))
            return self._hits[key]
        anchor_start, anchor = max(runs, key=lambda run: len(run[1]))
        hits = []
        position = data.find(anchor, 0, self.scan_end)
        while position >= 0:
            start = position - anchor_start
            if start >= 0 and start + len(tokens) <= self.scan_end:
                if all(token == "?" or data[start + offset] == int(token, 16)
                       for offset, token in enumerate(tokens)):
                    hits.append(start)
                    position = data.find(anchor, start + len(tokens), self.scan_end)
                    continue
            position = data.find(anchor, position + 1, self.scan_end)
        self._hits[key] = hits
        return hits


def normalise(text):
    """IDA's one-token-per-byte string as the scanner's `?` form, or None."""
    tokens = []
    for token in text.split():
        token = token.upper()
        if set(token) == {"?"}:
            tokens.append("?")
        elif re.fullmatch(r"[0-9A-F]{1,2}", token):
            tokens.append(f"{int(token, 16):02X}")
        else:
            return None
    return tokens


FUNCTION = re.compile(
    r"^\s*(?:void|bool|std::byte\s*\*|uint32_t|int|static\s+\w+)\s+(\w+)\s*\(", re.MULTILINE)
FUNCTION_DEF = re.compile(
    r"^\s*(?:static\s+)?(?:void|bool|std::byte\s*\*|uint32_t|int)\s+(\w+)\s*\([^;{}]*\)\s*\{",
    re.MULTILINE)
TABLE = re.compile(r"constexpr\s+[^;{=]*?\b(\w+)\s*(?:\[\s*\])?\s*=\s*\{")


def strip_comments(text):
    """The source with comments blanked, newlines kept so line numbers stay true."""
    out = []
    index = 0
    while index < len(text):
        if text[index] == '"':
            end = index + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == '"':
                    end += 1
                    break
                end += 1
            out.append(text[index:end])
            index = end
        elif text.startswith("//", index):
            while index < len(text) and text[index] != "\n":
                out.append(" ")
                index += 1
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) if end < 0 else end + 2
            out.extend("\n" if character == "\n" else " " for character in text[index:end])
            index = end
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def enclosing_function(text, position):
    arm = "(file scope)"
    for definition in FUNCTION.finditer(text[:position]):
        arm = definition.group(1)
    return arm


def _matching_brace(text, open_brace):
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def table_containing(text, position):
    """The table variable whose initializer contains `position`, or None."""
    best = None
    for match in TABLE.finditer(text[:position]):
        open_brace = text.find("{", match.end() - 1)
        if open_brace < 0 or open_brace > position:
            continue
        if _matching_brace(text, open_brace) > position:
            best = match.group(1)
    return best


def consuming_arm(text, table):
    """The function that consumes `table`, following table-to-table aliases."""
    for _ in range(4):
        outer = None
        for match in TABLE.finditer(text):
            name = match.group(1)
            if name == table:
                continue
            open_brace = text.find("{", match.end() - 1)
            close = _matching_brace(text, open_brace)
            if close > 0 and re.search(rf"\b{re.escape(table)}\b", text[open_brace:close]):
                outer = name
                break
        if outer is None:
            break
        table = outer
    for definition in FUNCTION_DEF.finditer(text):
        open_brace = text.rfind("{", definition.start(), definition.end())
        close = _matching_brace(text, open_brace)
        if close > 0 and re.search(rf"\b{re.escape(table)}\b", text[open_brace:close]):
            return definition.group(1)
    return table


def source_patterns():
    """(file, line, arm, literal, tokens) for every pattern-like literal."""
    out = []
    for path in sorted((ROOT / "source").glob("*.ixx")):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in re.finditer(r'"([^"\\]*(?:\\.[^"\\]*)*)"', text):
            value = match.group(1)
            tokens = normalise(value)
            if not tokens or len(tokens) < 2:
                continue
            table = table_containing(text, match.start())
            arm = consuming_arm(text, table) if table is not None else \
                enclosing_function(text, match.start())
            line = text.count("\n", 0, match.start()) + 1
            out.append((path.name, line, arm, value, tokens))
    seen = set()
    unique = []
    for item in out:
        key = (item[0], item[1], item[3])
        if key in seen:
            continue
        seen.add(key)
        unique.append(item)
    return unique


def concrete_high_half(image, tokens, hit):
    """The concrete offsets of a hit that sit in a fixup's high half."""
    out = []
    for offset, token in enumerate(tokens):
        if token == "?":
            continue
        rva = hit + offset
        field = image.field_covering(rva)
        if field is not None and rva - field >= 2:
            out.append((offset, field))
    return out


def describe(hits):
    if not hits:
        return "0"
    shown = ", ".join(f"{hit:#x}" for hit in hits[:4])
    more = "" if len(hits) <= 4 else ", ..."
    return f"{len(hits)} [{shown}{more}]"


def measure_image(task):
    """One image's whole sweep; runs in a worker process."""
    build, path, patterns = task
    image = Image(Path(path))
    found = {}
    counts = {}
    for name, line, arm, literal, tokens in patterns:
        base = image.find(tokens, 0)
        per_delta = [base]
        observations = {}
        for delta in DELTAS:
            moved = image.find(tokens, delta)
            per_delta.append(moved)
            if moved != base:
                observations[delta] = (base, moved)
        counts[(name, line, literal)] = [len(hits) for hits in per_delta]
        static = [(hit, concrete_high_half(image, tokens, hit)) for hit in base]
        static = [(hit, offsets) for hit, offsets in static if offsets]
        if observations or static:
            found[(name, line, literal)] = {
                "arm": arm, "tokens": tokens,
                "builds": {build: {"base": base, "deltas": observations, "static": static}}}
    return build, found, counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=str(DEFAULT_GAME))
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--all", action="store_true",
                        help="print every pattern's hit set, not only the changed ones")
    args = parser.parse_args()

    game = Path(args.game)
    keys = catalogue_keys()
    key_set = set(keys)
    signatures, _ = read_evidence()
    patterns = source_patterns()
    print(f"{len(patterns)} pattern literal(s); {len(DELTAS)} delta(s): "
          + ", ".join(f"{delta:#010x}" for delta in DELTAS))

    # Only the paths are kept in the parent while the workers run: an Image is a
    # 26 MB buffer, and nine of them in the parent on top of the nine worker copies
    # is needless memory pressure.
    image_paths = {}
    for build in BUILDS:
        path = game / f"{build}.exe"
        if path.exists():
            image_paths[build] = path
        else:
            print(f"{build}: image missing, skipped")
    if not image_paths:
        print("no image to measure")
        return 0

    # (file, line, literal) -> arm/tokens plus the per-build observations. Each
    # image is a worker process: the sweep is ~13,000 regex-free searches over a
    # 10 MB .text, and the images are independent.
    issues = {}
    counts_by_build = {}
    tasks = [(build, str(path), patterns) for build, path in image_paths.items()]
    workers = max(1, min(len(tasks), os.cpu_count() or 1))
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for build, found, counts in pool.map(measure_image, tasks):
            counts_by_build[build] = counts
            for key, entry in found.items():
                target = issues.setdefault(key, {"arm": entry["arm"],
                                                 "tokens": entry["tokens"],
                                                 "builds": {}})
                target["builds"].update(entry["builds"])
            print(f"{build}: done", flush=True)
    if args.verbose:
        for name, line, arm, literal, tokens in patterns:
            if (name, line, literal) not in issues:
                print(f"  OK  {name}:{line} ({arm}) {literal}: stable at every delta")

    if args.all:
        reference_build = "GTAIV" if "GTAIV" in counts_by_build else next(iter(counts_by_build))
        counts = counts_by_build[reference_build]
        print(f"\nEvery pattern on {reference_build}.exe: hit count at each delta")
        print(f"{'file:line':24} {'key':24} {'literal':44} {'base':>10} "
              + " ".join(f"{delta:#010x}" for delta in DELTAS))
        for name, line, arm, literal, tokens in patterns:
            key = key_for(arm, key_set) or arm
            values = counts[(name, line, literal)]
            print(f"{name + ':' + str(line):24} {key[:24]:24} {literal[:44]:44} "
                  + " ".join(f"{count:>10}" for count in values))

    if not issues:
        print(f"{len(patterns)} literal(s) measured on {len(image_paths)} image(s): every "
              f"hit set is identical at every delta")
        return 0

    preserved = []
    ours = []
    unverified = []
    for (name, line, literal), entry in sorted(issues.items()):
        arm = entry["arm"]
        key = key_for(arm, key_set)
        originals = signatures.get(key, []) if key is not None else []
        if any(entry["tokens"] == original for original in originals):
            preserved.append((name, line, arm, key, literal, entry))
        elif originals:
            ours.append((name, line, arm, key, literal, entry, originals))
        else:
            unverified.append((name, line, arm, key, literal, entry))

    def print_entry(name, line, arm, key, literal, entry, verdict):
        label = f" -> {key}" if key else ""
        print(f"\n{name}:{line} ({arm}{label})  {literal}")
        print(f"  verdict: {verdict}")
        for build, observed in sorted(entry["builds"].items()):
            base = describe(observed["base"])
            parts = [f"base {base}"]
            for delta in DELTAS:
                if delta in observed["deltas"]:
                    parts.append(f"{delta:#010x} {describe(observed['deltas'][delta][1])}")
            print(f"  {build}: " + "; ".join(parts))
            for hit, offsets in observed["static"]:
                fields = ", ".join(f"{field:#x}" for _, field in offsets)
                print(f"    concrete byte(s) of the hit at {hit:#x} sit in the high half "
                      f"of fixup(s) {fields}")

    if preserved:
        print(f"\n{len(preserved)} preserved original fault(s): the literal is the "
              f"original handler's own string, so the base-dependence is the original's "
              f"(1:1 policy: keep it, record it in the docs):")
        for name, line, arm, key, literal, entry in preserved:
            print_entry(name, line, arm, key, literal, entry,
                        "preserved original fault")
    if ours:
        print(f"\n{len(ours)} literal(s) OURS TO FIX: the literal is not the original's "
              f"own string and its hit set changes with the delta:")
        for name, line, arm, key, literal, entry, originals in ours:
            print_entry(name, line, arm, key, literal, entry,
                        "ours to fix: use the original's string")
            for original in originals:
                print(f"  original: {' '.join(original)}")
    if unverified:
        print(f"\n{len(unverified)} literal(s) with no recovered original string to "
              f"compare against:")
        for name, line, arm, key, literal, entry in unverified:
            print_entry(name, line, arm, key, literal, entry,
                        "unverified: no recovered original string")

    print(f"\n{len(patterns)} literal(s) measured on {len(image_paths)} image(s) at "
          f"{len(DELTAS)} delta(s): {len(preserved)} preserved original fault(s), "
          f"{len(ours)} ours to fix, {len(unverified)} unverified")
    if ours or unverified:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
