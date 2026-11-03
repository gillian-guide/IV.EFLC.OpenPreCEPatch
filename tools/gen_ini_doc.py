#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate docs/ini.md and docs/features.md from the shipped INI.

Keeping the documentation generated from the file the loader actually reads
means the tables cannot drift when a key or default changes.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INI = ROOT / "data/plugins/IV.EFLC.OpenPreCEPatch.ini"
DOCS = ROOT / "docs"

SECTION_RE = re.compile(r"^\[([^\]]+)\]\s*(?://\s*(.*))?$")
KEY_RE = re.compile(r"^([A-Za-z0-9_]+)\s*=\s*(.*?)\s*(?://\s*(.*))?$")


def parse():
    sections = []
    current = None
    for raw in INI.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        section = SECTION_RE.match(line)
        if section:
            current = {"name": section.group(1), "comment": section.group(2) or "", "keys": []}
            sections.append(current)
            continue
        key = KEY_RE.match(line)
        if key and current is not None:
            current["keys"].append(
                {
                    "key": key.group(1),
                    "default": key.group(2),
                    "comment": key.group(3) or "",
                }
            )
    return sections


def main():
    sections = parse()
    DOCS.mkdir(exist_ok=True)

    ini = [
        "# INI reference",
        "",
        "Generated from `data/plugins/IV.EFLC.OpenPreCEPatch.ini`.",
        "",
        "Tags: `[all]` / `[1060+]` / `[all-1080,1130]` list the game builds an option applies to.",
        "`FF` means the option is automatically disabled when a FusionFix ASI is present.",
        "",
        "The tag is the applicability the shipped arm implements. For the recovered",
        "per-build evidence behind it (handler, gate instruction, RVA row) and for",
        "the builds the original is merely pattern-gated on, see",
        "[version-matrix.md](version-matrix.md).",
        "",
    ]
    features = [
        "# Features",
        "",
        "One subsection per INI key. A key is enabled when its value parses to",
        "exactly `1`; the default is used when the key is absent.",
        "",
    ]

    for section in sections:
        if not section["keys"]:
            continue
        ini.append(f"## {section['name']}")
        ini.append("")
        if section["comment"]:
            ini.append(section["comment"])
            ini.append("")
        ini.append("| key | default | notes |")
        ini.append("| --- | --- | --- |")
        for entry in section["keys"]:
            ini.append(
                f"| `{entry['key']}` | `{entry['default']}` | {entry['comment']} |"
            )
        ini.append("")

        features.append(f"## {section['name']}")
        features.append("")
        if section["comment"]:
            features.append(section["comment"])
            features.append("")
        for entry in section["keys"]:
            features.append(f"### {entry['key']}")
            features.append("")
            features.append(entry["comment"] or "No description in the shipped INI.")
            features.append("")
            features.append(f"- **Default:** `{entry['default']}`")
            features.append("")

    (DOCS / "ini.md").write_text("\n".join(ini).rstrip() + "\n", encoding="utf-8")
    (DOCS / "features.md").write_text("\n".join(features).rstrip() + "\n", encoding="utf-8")
    print(f"generated {DOCS / 'ini.md'} and {DOCS / 'features.md'} ({len(sections)} sections)")


if __name__ == "__main__":
    main()
