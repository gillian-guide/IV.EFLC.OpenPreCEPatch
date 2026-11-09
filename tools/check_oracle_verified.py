#!/usr/bin/env python3
"""Guard the oracle-verified class.

The scoreboard reports these keys apart from the open ones so that a baseline known to
be incomplete does not read as unfinished work. That is only honest while every entry
is real, documented, and no broader than the evidence - so this check fails if:

  * an entry names a key that is not in the catalogue;
  * its build list is empty, names a build the catalogue mask does not allow, or
    names a build the project has no measured omission for (the list is pinned
    below: `SkipLauncher` on every build, `CarDynamicShadowFix` on 1.0.0.0 and
    1.0.4.0, `Patch7RemoveModChecks` on 1.0.6.0 and 1.0.7.0 - each key is closed
    on the builds the row does not name, so a global row would hide a real
    regression);
  * its documentation anchor does not exist (the file, and the `#fragment` when
    given, must match a heading in that file);
  * its justification or measurement is missing or vacuous;
  * there are more entries than measured omissions.

    python tools/check_oracle_verified.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "tests" / "oracle_verified.tsv"
CATALOGUE = ROOT / "source" / "options.inc"

# The builds `tools/parity_report.py` scores.
SUPPORTED_BUILDS = frozenset({1, 5, 6, 7, 8, 9, 11, 12, 13})

# The omissions this project has measured, and the builds each one covers. A row may
# excuse a subset of these builds, never more: this is what has been proven, not what
# would be convenient. `None` means every build the key's catalogue mask allows.
#
#   SkipLauncher: the stored baseline is the reference tree's uncalled 7-row arm;
#       the original's 25 rows replay to our write set on all nine images.
#   CarDynamicShadowFix: the reference's `ApplyCarDynamicShadowFixNoE` pass never ran
#       in the dump that produced the stored baseline, which therefore lacks the
#       six-byte NOP fill at the C anchor's +0x3E on 1.0.0.0 and 1.0.4.0 (the builds
#       where pattern E is absent). The key is otherwise closed on 1.0.8.0, so the
#       row must not go global.
EVIDENCE = {
    "SkipLauncher": None,
    "CarDynamicShadowFix": frozenset({1, 5}),
    # The original has no handler of its own for Patch7: "Patch7RemoveModChecks" is
    # ORed into Patch6and7's handler 0x10044710, so a Patch7-only INI runs that
    # 16/21-byte write set, while the stored cell records the reference tree's own
    # smaller 5/10-byte arm. Only builds 7 and 8 are gated to that handler.
    "Patch7RemoveModChecks": frozenset({7, 8}),
}
LIMIT = len(EVIDENCE)


def catalogue():
    """key -> build mask, from the generated catalogue."""
    masks = {}
    for line in CATALOGUE.read_text(encoding="utf-8").splitlines():
        if not line.startswith("ZP_OPTION("):
            continue
        fields = [field.strip() for field in line.split("(", 1)[1].rstrip(")").split(",")]
        if len(fields) < 4:
            continue
        masks[fields[0]] = int(fields[3].rstrip("uU"), 0)
    return masks


def slug(title):
    """GitHub's heading anchor: lowercase, punctuation dropped, spaces to hyphens."""
    return re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")


def anchor_exists(anchor):
    path_text, _, fragment = anchor.partition("#")
    path = ROOT / path_text
    if not path.exists():
        return False, "the document does not exist"
    if not fragment:
        return True, ""
    headings = [slug(line.lstrip("#").strip())
                for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
                if line.startswith("#")]
    if fragment not in headings:
        return False, f"no heading in {path_text} anchors to #{fragment}"
    return True, ""


def parse_builds(text):
    """`*` -> every supported build; otherwise the comma-separated ids."""
    if text == "*":
        return set(SUPPORTED_BUILDS)
    return {int(item) for item in text.split(",")}


def main() -> int:
    if not TSV.exists():
        print("no oracle_verified.tsv: the scoreboard has no excused keys")
        return 0

    known = catalogue()
    failures = 0
    entries = 0
    seen = set()
    for number, line in enumerate(TSV.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        entries += 1
        parts = [part.strip() for part in line.split("\t")]
        if len(parts) < 5 or not all(parts[:5]):
            print(f"line {number}: needs key, builds, why, doc anchor and measurement")
            failures += 1
            continue
        key, builds, why, anchor, measured = parts[:5]

        if key in seen:
            print(f"line {number}: {key} already has a row")
            failures += 1
        seen.add(key)

        if key not in known:
            print(f"line {number}: {key} is not in the catalogue")
            failures += 1

        try:
            named = parse_builds(builds)
        except ValueError:
            print(f"line {number}: {key} has a malformed build list '{builds}'")
            failures += 1
            named = set()
        if not named:
            print(f"line {number}: {key} names no build")
            failures += 1
        else:
            mask = known.get(key, 0)
            outside = sorted(build for build in named if build not in SUPPORTED_BUILDS)
            disallowed = sorted(build for build in named - set(outside)
                                if not mask & (1 << build))
            if outside:
                print(f"line {number}: {key} names build(s) {outside} the scoreboard "
                      f"does not measure")
                failures += 1
            if disallowed:
                print(f"line {number}: {key} names build(s) {disallowed} the catalogue "
                      f"mask 0x{mask:X} does not allow")
                failures += 1
            if not (named & {build for build in SUPPORTED_BUILDS if mask & (1 << build)}):
                print(f"line {number}: {key} names no build the catalogue mask allows")
                failures += 1

        ok, reason = anchor_exists(anchor)
        if not ok:
            print(f"line {number}: {key} points at a bad anchor: {reason}")
            failures += 1

        if len(why) < 40 or len(measured) < 20:
            print(f"line {number}: {key} needs a real justification and measurement")
            failures += 1

        evidence = EVIDENCE.get(key)
        if evidence is None and key not in EVIDENCE:
            print(f"line {number}: {key} names an omission the project has no evidence for")
            failures += 1
        elif evidence is not None:
            extra = sorted(named - evidence)
            if extra:
                print(f"line {number}: {key} excuses build(s) {extra}; the measured "
                      f"omission covers only {sorted(evidence)}")
                failures += 1

    if entries > LIMIT:
        print(f"{entries} excused rows, but only {LIMIT} omissions have evidence")
        failures += 1

    print(f"{entries} oracle-verified row(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
