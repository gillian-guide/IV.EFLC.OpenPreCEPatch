#!/usr/bin/env python3
"""Generate the build-applicability table from the catalogue.

`source/options.inc` carries each key's recovered build mask. This script joins
it with `data/applicability_evidence.json` (which keys have a recovered build
comparison in the original handler) and emits:

  tests/parity/applicability.tsv    one row per key, one column per build

Cells: R = recovered gate admits the build; A = the original is pattern-gated
and admits it; G = a port narrowing (none today); . = refused. The loader's
refusal is `Catalogue::AppliesTo`/`Loader::Enabled`, so the table is what those
functions implement, and CTest `zp_applicability_drift` fails when the committed
file drifts from the catalogue.

    python tools/gen_applicability.py [--check]
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OPTIONS = ROOT / "source" / "options.inc"
EVIDENCE = ROOT / "data" / "applicability_evidence.json"
TABLE = ROOT / "tests" / "parity" / "applicability.tsv"

ROW = re.compile(
    r"^ZP_OPTION\(([A-Za-z0-9_]+), (-?\d+), (-?\d+), 0x([0-9A-Fa-f]+)u, (\w+), (\w+)\)"
)


def load():
    evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))
    builds = evidence["builds"]
    recovered = set(evidence["recovered_gate"])
    semantic = set(evidence.get("semantic", []))

    rows = []
    for line in OPTIONS.read_text(encoding="utf-8").splitlines():
        match = ROW.match(line)
        if not match:
            continue
        key, shipped, absent, mask, module, kind = match.groups()
        mask = int(mask, 16)
        cells = []
        for build in builds:
            if not (mask & (1 << build["id"])):
                cells.append(".")
            elif key in semantic:
                cells.append("G")
            elif key in recovered:
                cells.append("R")
            else:
                cells.append("A")
        evidence_class = (
            "semantic" if key in semantic else
            "recovered" if key in recovered else
            "pattern"
        )
        rows.append({
            "key": key,
            "shipped": int(shipped),
            "absent": int(absent),
            "mask": mask,
            "module": module,
            "kind": kind,
            "evidence": evidence_class,
            "cells": cells,
        })
    return builds, rows


def render_tsv(builds, rows):
    header = ["key", "module", "evidence", "mask"] + [str(b["compact"]) for b in builds]
    lines = ["\t".join(header)]
    for row in rows:
        lines.append("\t".join([
            row["key"], row["module"], row["evidence"], f"0x{row['mask']:08X}",
            *row["cells"],
        ]))
    return "\n".join(lines) + "\n"


def main():
    builds, rows = load()
    # The table covers exactly the catalogue: a floor would let a removed key
    # (or a parser regression) slip through as long as the count stayed high.
    catalogue = [line for line in OPTIONS.read_text(encoding="utf-8").splitlines()
                 if line.startswith("ZP_OPTION(")]
    if len(rows) != len(catalogue):
        raise SystemExit(f"{len(rows)} option rows parsed, "
                         f"{len(catalogue)} in the catalogue")
    tsv = render_tsv(builds, rows)

    if "--check" in sys.argv:
        if not TABLE.exists() or TABLE.read_text(encoding="utf-8") != tsv:
            raise SystemExit(f"applicability out of date: {TABLE.relative_to(ROOT)}")
        print(f"applicability up to date ({len(rows)} keys x {len(builds)} builds)")
        return 0

    TABLE.parent.mkdir(parents=True, exist_ok=True)
    TABLE.write_text(tsv, encoding="utf-8")
    print(f"wrote {TABLE.relative_to(ROOT)} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
