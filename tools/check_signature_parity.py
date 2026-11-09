#!/usr/bin/env python3
"""Fail when one of our signature scans would select a different site than the
original ASI's own signature does.

The bug class this closes: an arm rescans a *shortened* (or loosened) copy of the
original handler's signature. On the build the baselines were taken from, the
shortened string happens to pick the original's first match, so nothing looks
wrong; on another build it picks a different function, and the arm patches a site
the original never touches (or misses the one it does).

    python tools/check_signature_parity.py [--game DIR] [--verbose] [--strict]

What the check does, per `find_pattern` literal in `source/*.ixx`:

  * the arm's installer/publisher name names the catalogue key
    (`InstallAimProjectileCrashFix` / `PublishAimProjectileCrashFix` -> the key);
  * the key's *original* signature strings come from the reconstruction's recovered
    tables (`ZolikaPatch_reconstruction/evidence/handler_table.json`, the strings
    its handler decompilation shows it passing to the scanner;
    `option_signatures.json`; our own recovered `tests/signatures.tsv`), and, for a
    key none of them covers, from the original ASI's own stored strings (every
    pattern-like ASCII string in `game_refs/ZolikaPatch_original.asi.off`);
  * our literal is *associated* with one of those strings when it is an exact copy
    or a loosening of it (a prefix truncation, or a wildcard where the original has
    a concrete byte);
  * measured on all nine supported images, an associated literal's hit set (count
    and every hit RVA, hence the first-hit RVA) must equal the original string's.

A recovered-signature association that differs is a failure: that is the bug class.
A pool-only association is reported, never fatal (the string may belong to another
handler). A literal that merely loosens a recovered signature while selecting the
same sites on every image is reported as a loosening: harmless today, and the
original's own string is the faithful form. `--strict` turns both of those into
failures, so a tree with every literal traced can pin the stronger rule.

Exit status 0 when no associated literal selects a different site, 1 otherwise.
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT.parent
DEFAULT_GAME = DECOMP / "game_refs"
EVIDENCE = DECOMP / "ZolikaPatch_reconstruction" / "evidence"

# The nine images the per-key baselines and the 18-cell matrix are scored on.
BUILDS = ["GTAIV_1000", "GTAIV_1004", "GTAIV_1040", "GTAIV_1060", "GTAIV_1070",
          "GTAIV", "EFLC_1110", "EFLC_1120", "EFLC_1130"]

IMAGE_SCN_MEM_EXECUTE = 0x20000000
PATTERN_STRING = re.compile(
    rb"(?<![0-9A-F?])(?:[0-9A-F?]{2})(?: [0-9A-F?]{2}){1,}(?![0-9A-F? ])")


class Image:
    """The executable mapped at its RVAs, with the scanner's own scan range."""

    def __init__(self, path):
        data = path.read_bytes()
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        coff = e_lfanew + 4
        count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        self.size = struct.unpack_from("<I", data, optional + 56)[0]
        self.sections = []
        for index in range(count):
            entry = optional + optional_size + index * 40
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, entry + 8)
            characteristics = struct.unpack_from("<I", data, entry + 36)[0]
            self.sections.append((virtual_address, virtual_size, raw_size, raw_offset,
                                  characteristics))
        self.bytes = bytearray(self.size)
        for virtual_address, virtual_size, raw_size, raw_offset, _ in self.sections:
            blob = data[raw_offset:raw_offset + raw_size]
            self.bytes[virtual_address:virtual_address + len(blob)] = blob
        headers = min(struct.unpack_from("<I", data, optional + 60)[0], len(data))
        self.bytes[0:headers] = data[0:headers]
        self.scan_end = 0
        for virtual_address, virtual_size, raw_size, _, characteristics in self.sections:
            if characteristics & IMAGE_SCN_MEM_EXECUTE:
                self.scan_end = virtual_address + (raw_size or virtual_size)
        if self.scan_end == 0:
            self.scan_end = self.size
        self.cache = {}

    def find(self, tokens):
        """Every hit RVA of the pattern, in order (cached per signature string)."""
        key = tuple(tokens)
        if key in self.cache:
            return self.cache[key]
        pattern = b"".join(b"." if token == "?" else re.escape(bytes([int(token, 16)]))
                           for token in tokens)
        expression = re.compile(pattern, re.DOTALL)
        # `self.bytes` is indexed by RVA, and the scan range game.ixx uses is
        # [0, end of the last executable section): the first section starts at RVA
        # 0x1000, so the range is RVAs, not VAs.
        hits = [match.start() for match in
                expression.finditer(self.bytes, 0, self.scan_end)]
        self.cache[key] = hits
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


def compatible(short, long):
    """Every byte both strings spell out agrees (a wildcard agrees with anything)."""
    for left, right in zip(short, long):
        if left != "?" and right != "?" and left != right:
            return False
    return True


def loosens(literal, original):
    """literal is a truncation or a wildcarding of the original signature."""
    if not compatible(literal, original):
        return False
    if len(literal) < len(original) and compatible(literal, original[:len(literal)]):
        return True
    # A wildcard over a concrete original byte only counts when the two strings
    # still agree on a run of concrete bytes: without that, a short unrelated
    # string in the ASI's own pool "loosens" by coincidence.
    wildcarded = any(left == "?" and right != "?" for left, right in zip(literal, original))
    return wildcarded and longest_agreement(literal, original) >= 2


def longest_agreement(literal, original):
    """The longest run of bytes both strings spell out identically."""
    best = run = 0
    for left, right in zip(literal, original):
        if left != "?" and left == right:
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


FUNCTION = re.compile(
    r"^\s*(?:void|bool|std::byte\s*\*|uint32_t|int|static\s+\w+)\s+(\w+)\s*\(", re.MULTILINE)


def source_patterns():
    """(file, line, enclosing function, [literal, ...]) for every find_pattern call."""
    out = []
    for path in sorted((ROOT / "source").glob("*.ixx")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"find_pattern\s*(?:<\s*\d+\s*>)?\s*\(", text):
            line = text.count("\n", 0, match.start()) + 1
            arm = "(file scope)"
            for definition in FUNCTION.finditer(text[:match.start()]):
                arm = definition.group(1)
            cursor = match.end()
            depth = 1
            literals = []
            while cursor < len(text) and depth > 0:
                character = text[cursor]
                if character == '"':
                    end = cursor + 1
                    value = []
                    while end < len(text) and text[end] != '"':
                        if text[end] == "\\":
                            end += 1
                        value.append(text[end])
                        end += 1
                    literals.append("".join(value))
                    cursor = end + 1
                    continue
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
                cursor += 1
            literals = [item for item in literals if normalise(item) is not None]
            if literals:
                out.append((path.name, line, arm, literals))
    return out


def catalogue_keys():
    text = (ROOT / "source" / "options.inc").read_text(encoding="utf-8", errors="replace")
    return [line.split("(", 1)[1].split(",", 1)[0]
            for line in text.splitlines() if line.startswith("ZP_OPTION(")]


def read_evidence():
    """key -> [original signature tokens], and our recorded table's rows."""
    signatures = {}

    def add(target, key, signature):
        tokens = normalise(signature)
        if not tokens:
            return
        target.setdefault(key, [])
        if tokens not in target[key]:
            target[key].append(tokens)

    # The strings the original's handlers themselves pass to their scanner.
    table = EVIDENCE / "handler_table.json"
    if table.exists():
        for row in json.loads(table.read_text(encoding="utf-8")):
            for signature in row.get("signatures") or []:
                add(signatures, row["option"], signature)

    # The reconstruction's recovered per-option signature rows.
    recovered = EVIDENCE / "option_signatures.json"
    if recovered.exists():
        for key, values in json.loads(recovered.read_text(encoding="utf-8")).items():
            for signature in values:
                add(signatures, key, signature)

    # Our own recovered rows. They do not drive the association -- a row here can be
    # the shortened string the bug class produced, so using it to check our source
    # would be circular -- but a row that loosens the original is a stale record and
    # is reported. A row's key may carry a variant suffix (`ForceCarHeadlightShadows.a`).
    own = {}
    path = ROOT / "tests" / "signatures.tsv"
    if path.exists():
        for number, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) >= 4:
                add(own, parts[0].split(".")[0], parts[2])
    return signatures, own


def asi_strings(path):
    """Every pattern-like string the original ASI itself carries."""
    if not path.exists():
        return []
    out = []
    seen = set()
    for match in PATTERN_STRING.finditer(path.read_bytes()):
        tokens = normalise(match.group(0).decode("ascii", "replace"))
        if tokens and len(tokens) >= 3:
            key = tuple(tokens)
            if key not in seen:
                seen.add(key)
                out.append(tokens)
    return out


def key_for(arm, keys):
    for prefix in ("Install", "Publish"):
        if arm.startswith(prefix) and arm[len(prefix):] in keys:
            return arm[len(prefix):]
    if arm in keys:
        return arm
    return None


def describe(hits):
    if not hits:
        return "no hit"
    return f"{len(hits)} hit(s), first RVA {hits[0]:#x}"


def measure(image, tokens, original):
    """None when the two hit sets agree, else the difference, named."""
    ours = image.find(tokens)
    theirs = image.find(original)
    if ours == theirs:
        return None
    if len(ours) != len(theirs):
        difference = f"hit count {len(ours)} vs {len(theirs)}"
    elif ours and ours[0] != theirs[0]:
        difference = f"first RVA {ours[0]:#x} vs {theirs[0]:#x}"
    else:
        index = next(position for position, (left, right) in enumerate(zip(ours, theirs))
                     if left != right)
        difference = f"hit {index} at RVA {ours[index]:#x} vs {theirs[index]:#x}"
    return f"{difference}; ours {describe(ours)}, original {describe(theirs)}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=str(DEFAULT_GAME))
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    game = Path(args.game)
    keys = catalogue_keys()
    key_set = set(keys)
    signatures, recorded = read_evidence()
    pool = asi_strings(DEFAULT_GAME / "ZolikaPatch_original.asi.off")
    patterns = source_patterns()

    images = {}
    for build in BUILDS:
        path = game / f"{build}.exe"
        if path.exists():
            images[build] = Image(path)
    missing = [build for build in BUILDS if build not in images]
    print(f"{len(patterns)} find_pattern call(s); {len(signatures)} key(s) with a "
          f"recovered signature; {len(pool)} string(s) in the ASI's own pool; "
          f"{len(images)} image(s)")
    if missing:
        print(f"images missing, skipped: {', '.join(missing)}")

    failures = []      # recovered association, hit sets differ: the bug class
    pool_notes = []    # pool-only association and it differs (not fatal)
    loosenings = []    # recovered association, same sites, but a looser string
    loose_rows = []    # tests/signatures.tsv records a looser string than the original
    unverified = []    # no original string to compare against

    def differences_for(tokens, original):
        out = [(build, measure(image, tokens, original))
               for build, image in images.items()]
        return [item for item in out if item[1] is not None]

    def match_any(tokens, candidates):
        """The first candidate whose hit set equals ours on every image, or None."""
        for candidate in sorted(candidates, key=len, reverse=True):
            if not differences_for(tokens, candidate):
                return candidate
        return None

    checked = 0
    for name, line, arm, literals in patterns:
        key = key_for(arm, key_set)
        for literal in literals:
            tokens = normalise(literal)
            recovered = signatures.get(key, []) if key is not None else []
            if tokens in recovered:
                checked += 1
                if args.verbose:
                    print(f"  OK  {name}:{line} ({arm}): the original's own string")
                continue

            associated = [candidate for candidate in recovered
                          if loosens(tokens, candidate)]
            if associated:
                checked += 1
                matching = match_any(tokens, associated)
                if matching is not None:
                    loosenings.append((name, line, arm, key, literal, matching))
                else:
                    failures.append((name, line, arm, key, literal,
                                     max(associated, key=len),
                                     differences_for(tokens, max(associated, key=len))))
                continue

            weak = [candidate for candidate in pool if loosens(tokens, candidate)]
            if weak:
                matching = match_any(tokens, weak)
                if matching is None:
                    pool_notes.append((name, line, arm, literal,
                                       max(weak, key=len),
                                       differences_for(tokens, max(weak, key=len))))
                elif args.verbose:
                    print(f"  note {name}:{line} ({arm}): loosens an ASI string "
                          f"({len(matching)} bytes) but selects the same sites")
                continue

            unverified.append((name, line, arm, literal,
                               "not the key's recovered signature"
                               if key is not None else "arm name names no catalogue key"))

    # Our recorded table must itself carry the original's own strings.
    for key, rows in recorded.items():
        originals = signatures.get(key, [])
        if not originals:
            continue
        for row in rows:
            if row in originals:
                continue
            associated = [candidate for candidate in originals if loosens(row, candidate)]
            if associated:
                loose_rows.append((key, row, max(associated, key=len)))

    for name, line, arm, key, literal, original, differences in failures:
        print(f"FAIL {name}:{line} ({arm}" + (f" -> {key}" if key else "") + ")")
        print(f"  ours    : {literal}")
        print(f"  original: {' '.join(original)}")
        for build, difference in differences:
            print(f"    {build}: {difference}")

    if pool_notes:
        print()
        print(f"{len(pool_notes)} literal(s) loosen a string in the ASI's own pool and "
              f"differ from it (the string may belong to another handler):")
        for name, line, arm, literal, original, differences in pool_notes:
            print(f"  {name}:{line} ({arm})")
            print(f"    ours    : {literal}")
            print(f"    ASI     : {' '.join(original)}")
            for build, difference in differences:
                print(f"      {build}: {difference}")

    if loosenings:
        print()
        print(f"{len(loosenings)} literal(s) loosen the original signature but select "
              f"the same sites on every image; the original's own string is the "
              f"faithful form:")
        for name, line, arm, key, literal, original in loosenings:
            print(f"  {name}:{line} ({arm}" + (f" -> {key}" if key else "") + ")")
            print(f"    ours    : {literal}")
            print(f"    original: {' '.join(original)}")

    if loose_rows:
        print()
        print(f"{len(loose_rows)} row(s) of tests/signatures.tsv are a looser copy of "
              f"the original signature:")
        for key, row, original in loose_rows:
            print(f"  {key}")
            print(f"    row     : {' '.join(row)}")
            print(f"    original: {' '.join(original)}")

    if unverified:
        print()
        print(f"{len(unverified)} literal(s) have no recovered original signature to "
              f"compare against:")
        for name, line, arm, literal, reason in unverified:
            print(f"  {name}:{line} ({arm}): {reason}")

    print()
    print(f"{checked} signature(s) measured on {len(images)} image(s): "
          f"{len(failures)} mismatch(es), {len(loosenings)} loosening(s), "
          f"{len(unverified)} without an original signature")
    if failures:
        print("a shortened pattern selects a different site than the original ASI's "
              "own signature on at least one image: use the original's full string")
        return 1
    if args.strict and (loosenings or unverified or pool_notes or loose_rows):
        print("--strict: loosenings, stale table rows and unverifiable literals are "
              "failures")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
