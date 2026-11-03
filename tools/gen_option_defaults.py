#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit option defaults against the original loader and the shipped INI.

Two different defaults are legitimate and must not be conflated:

* the **arm fallback** used when a key is absent from the INI. This tree keeps
  the original loader's default, recovered per read site from the 7.65 ASI
  (`tests/parity/loader_defaults.tsv`); that is the 1:1 contract for a deleted
  key.
* the **shipped INI** value, which is the configuration a user gets out of the
  box. It may differ from the loader fallback; the difference is recorded here
  (column `shipped`) and explained in `docs/improvements.md`.

This tool extracts every `Option("Key", "D")` / `RawOption(...)` call site,
checks that a key always uses one default, compares it with the recovered
loader default and writes (or verifies) `tests/parity/option_defaults.tsv`.

    python tools/gen_option_defaults.py           # rewrite the table
    python tools/gen_option_defaults.py --check   # fail on drift or divergence
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source"
TEMPLATE = ROOT / "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini"
LOADER = ROOT / "tests" / "parity" / "loader_defaults.tsv"
OUT = ROOT / "tests" / "parity" / "option_defaults.tsv"

CALL = re.compile(r'\b(?:Option|RawOption)\(\s*"([A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*\)')
KEY_LINE = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*([^/\s]*)")
SECTION_LINE = re.compile(r"^\s*\[([^\]]+)\]")

# Keys whose read site is not in the recovered loader table: either the original
# composes the key at run time (CarDecalsFix / CarDynamicShadowFix / the two LOD
# cutoffs are built with sprintf fragments) or the option is a successor
# extension. Their fallback is documented here instead of being checked.
NO_LOADER_ROW = {
    "CarDecalsFix": "original composes the key with sprintf fragments",
    "CarDynamicShadowFix": "original composes the key with sprintf fragments",
    "NoImpostorHeightCutoff": "not a literal read in the recovered loader table",
    "NoLODLightHeightCutoff": "not a literal read in the recovered loader table",
    "DrivebyFOVFix": "successor extension, no original read site",
    "GFWLProtectionDisabler": "standalone patch, no original loader read site",
    "SteamAchievements": "successor feature, no original loader read site",
}

# Successor / alias keys mapped onto the original key they replace.
ALIASES = {
    "RemoveModChecks": "Patch6and7RemoveModChecks",
    "Patch7RemoveModChecks": "Patch6and7RemoveModChecks",
}

# Options that deliberately use two absent-key defaults. CarDynamicShadowFix is
# the recovered shape: publish/install read "0" while the NoE arm reads "1"
# (research 06 R11), so the NoE NOP still installs when the key is missing.
KNOWN_MULTI_DEFAULT = {
    "CarDynamicShadowFix": "publish/install use 0; the NoE arm uses 1 (recovered)",
}


def template_values() -> dict[str, str]:
    values: dict[str, str] = {}
    section = None
    for raw in TEMPLATE.read_text(encoding="utf-8").splitlines():
        match = SECTION_LINE.match(raw)
        if match:
            section = match.group(1)
            continue
        if section in {"PopupLightAngles", "HeadlightConeOffsets"}:
            continue
        line = raw.split("//", 1)[0]
        match = KEY_LINE.match(line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def loader_defaults() -> dict[str, str]:
    defaults: dict[str, str] = {}
    for line in LOADER.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        _address, key, default, _kind = line.split("\t")
        defaults[key] = default
    return defaults


def call_sites() -> dict[str, set[str]]:
    defaults: dict[str, set[str]] = {}
    for path in sorted(SOURCE.glob("*.ixx")) + sorted(SOURCE.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for match in CALL.finditer(text):
            defaults.setdefault(match.group(1), set()).add(match.group(2))
    return defaults


def build_rows() -> tuple[list[tuple[str, str, str, str, str]], list[str]]:
    template = template_values()
    loader = loader_defaults()
    rows: list[tuple[str, str, str, str, str]] = []
    problems: list[str] = []
    calls = call_sites()
    for key in sorted(calls):
        values = calls[key]
        shipped = template.get(key, "-")
        if len(values) != 1:
            if key in KNOWN_MULTI_DEFAULT:
                rows.append((key, "|".join(sorted(values)), "-", shipped,
                             "multi-default-by-design"))
                continue
            problems.append(f"{key}: call sites disagree: {sorted(values)}")
            continue
        value = next(iter(values))
        original = loader.get(ALIASES.get(key, key))
        if shipped == "-":
            status = "no-template-row"
        elif original is not None and original != "-":
            if original == value:
                status = "ok" if shipped == value else "ok-shipped-differs"
            else:
                status = "DIFFERS-FROM-ORIGINAL"
                problems.append(
                    f"{key}: arm default {value} != original loader default {original}")
        else:
            status = "no-loader-row"
        rows.append((key, value, original if original is not None else "-",
                     shipped, status))
    return rows, problems


def render(rows: list[tuple[str, str, str, str, str]]) -> str:
    lines = [
        "# option\tarm default\toriginal loader default\tshipped INI\tstatus",
        "# Generated by tools/gen_option_defaults.py; do not edit by hand.",
        "# ok = fallback equals the original loader default;",
        "# ok-shipped-differs = same fallback, different shipped default;",
        "# no-loader-row = no recovered read site (see the tool's comment);",
        "# multi-default-by-design = documented per-arm defaults.",
    ]
    for key, value, original, shipped, status in rows:
        lines.append(f"{key}\t{value}\t{original}\t{shipped}\t{status}")
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify the table instead of writing it")
    args = parser.parse_args(argv)

    rows, problems = build_rows()
    text = render(rows)
    status = 0
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
        status = 1
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print(f"error: {OUT} is stale; run tools/gen_option_defaults.py",
                  file=sys.stderr)
            status = 1
        else:
            differs = sum(1 for row in rows if row[4] == "ok-shipped-differs")
            print(f"ok: {OUT} matches the source ({len(rows)} options, "
                  f"{differs} shipped-INI differences documented)")
    else:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(text, encoding="utf-8")
        print(f"wrote {OUT} ({len(rows)} options)")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
