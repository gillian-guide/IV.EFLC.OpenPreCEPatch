#!/usr/bin/env python3
"""Generate docs/version-matrix.md from the recovered gate evidence + the arms.

One row per INI option; one column per supported game build. The cell says how
the option reaches that build:

    R  RECOVERED: the original handler's own build gate admits the build
    S  SEMANTIC: the original gate admits it, but the original had no
       per-build row/data for it (a successor build)
    A  applies on every build: the original is build-blind (pattern scan) and
       the port's arm has no build gate
    G  the original is build-blind, but the port's arm gates the build
    .  excluded

Sources:
    docs/version-matrix.json  recovered per-option handler/gate evidence (IDA)
    source/*.ixx              the arm's own gate, patterns and bodies
    data/plugins/*.ini        the shipped option list and defaults

Run with --check to verify the committed document still matches.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
SOURCE = ROOT / "source"
INI = ROOT / "data" / "plugins" / "IV.EFLC.OpenpreCEPatch.ini"

BUILDS = [1000, 1004, 1040, 1060, 1061, 1070, 1080, 1100, 1110, 1120, 1130, 12043]
BUILD_ID = {1000: 1, 1040: 5, 1004: 6, 1060: 7, 1070: 8, 1080: 9,
            1100: 10, 1110: 11, 1120: 12, 1130: 13, 1061: 15, 12043: 18}
ENUM = {"v1000": 1000, "v1004": 1004, "v1040": 1040, "v1060": 1060,
        "v1061": 1061, "v1070": 1070, "v1080": 1080, "v1100": 1100,
        "v1110": 1110, "v1120": 1120, "v1130": 1130, "v12043": 12043}

ARM = re.compile(r"void\s+(Install|Publish)(\w+)\(PatchContext&")
# A build gate guards the install (`if (... && BuildIs(...))`, `return BuildIs`).
# A `const bool old = BuildIs(...)` only selects a body variant, so the option
# still installs on every build; those are filtered in gate_builds().
BUILDIS = re.compile(r"BuildIs\([^,]+,\s*\{([^}]*)\}", re.S)
EQUAL = re.compile(r"ctx\.build\s*==\s*GameBuild::(v\d+)")
FUNCTION = re.compile(r"^\s*(?:static\s+)?[\w:<>]+\s+(\w+)\([^;{]*\)\s*(?:noexcept)?\s*\{", re.M)
OPTION = re.compile(r'ctx\.Option\("([^"]+)"')
PATTERN = re.compile(r'Pattern(?:All)?\(image,')
ABI = re.compile(r'OPP_BODY\((\w+)\)')


def shipped_options() -> list[tuple[str, str, str]]:
    """(section, key, default) for every `Key = Value` line in the INI."""
    rows = []
    section = ""
    for line in (ROOT / "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini").read_text(
        encoding="utf-8"
    ).splitlines():
        title = re.match(r"^\[(.+)\]\s*$", line)
        if title:
            section = title.group(1)
            continue
        entry = re.match(r"^\s*([A-Za-z0-9_]+)\s*=\s*([^/]*)", line)
        if entry and not line.lstrip().startswith(";"):
            rows.append((section, entry.group(1), entry.group(2).strip()))
    return rows


def gate_builds(body: str) -> set[int]:
    """The builds a body's own gate admits (ignoring body-variant selectors)."""
    gates: set[int] = set()
    for match in BUILDIS.finditer(body):
        prefix = body[max(0, match.start() - 80):match.start()]
        prefix = prefix[max(prefix.rfind(";"), prefix.rfind("{"),
                            prefix.rfind("}")) + 1:]
        if "=" in prefix and "==" not in prefix and "if" not in prefix:
            continue  # `const bool old = BuildIs(...)`: a body selector
        gates.update(ENUM[name] for name in re.findall(r"v\d+", match.group(1)))
    gates.update(ENUM[name] for name in EQUAL.findall(body))
    return gates


def brace_body(text: str, start: int) -> str:
    """The text between the `{` at/after `start` and its matching `}`."""
    open_at = text.find("{", start)
    if open_at < 0:
        return ""
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:index]
    return text[open_at + 1:]


def helper_gates(text: str) -> dict[str, set[int]]:
    """Named functions in the file that carry a build gate (e.g. PrePatch6)."""
    gates: dict[str, set[int]] = {}
    for match in FUNCTION.finditer(text):
        found = gate_builds(brace_body(text, match.end() - 1))
        if found:
            gates.setdefault(match.group(1), set()).update(found)
    return gates


def parse_arms() -> dict[str, dict]:
    """Option key -> {gate, patterns, bodies, arms} from the .ixx sources."""
    arms: dict[str, dict] = {}
    for path in sorted(SOURCE.glob("*.ixx")):
        text = path.read_text(encoding="utf-8")
        helpers = helper_gates(text)
        matches = list(ARM.finditer(text))
        for index, match in enumerate(matches):
            start = match.end()
            end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            body = text[start:end]
            gates = gate_builds(body)
            for name, found in helpers.items():
                if re.search(rf"\b{re.escape(name)}\(", body):
                    gates.update(found)
            keys = sorted(set(OPTION.findall(body)))
            # The arm's own name names the option too: an arm that delegates
            # its option check to a helper (RemoveModChecks) has no direct
            # ctx.Option call, and its build blocks must still count.
            keys.append(match.group(2))
            for key in sorted(set(keys)):
                entry = arms.setdefault(key, {"gate": set(), "patterns": 0,
                                              "bodies": set(), "arms": []})
                entry["gate"].update(gates)
                entry["patterns"] += len(PATTERN.findall(body))
                entry["bodies"].update(ABI.findall(body))
                entry["arms"].append(f"{match.group(1)}{match.group(2)}")
    return arms


def cell(option: str, build: int, recovered: dict, arm: dict | None) -> str:
    entry = recovered.get(option, {})
    ids = BUILD_ID[build]
    if entry.get("gate_kind") in {"head_compare", "switch", "rva_rows"}:
        # SEMANTIC builds are admitted by the option's mechanism but carry no
        # recovered row/compare of their own, so they are not in
        # recovered_builds; check them first.
        if ids in entry.get("semantic_builds", []):
            return "S"
        if ids in entry.get("recovered_builds", []):
            return "R"
        return "."
    if entry.get("gate_kind") == "fallback":
        # The build compare only picks the missing-signature fallback, so the
        # option applies wherever its pattern matches.
        return "A"
    if arm is None:
        return "?"
    if arm["gate"]:
        return "G" if build in arm["gate"] else "."
    return "A"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--gaps", action="store_true",
                        help="report arms whose gate is wider or narrower than the recovered gate")
    args = parser.parse_args()

    matrix_path = DOCS / "version-matrix.json"
    if not matrix_path.exists():
        print(
            f"error: {matrix_path} is missing. It is the recovered applicability "
            "dump the generator renders from (not checked in; see "
            "docs/improvements.md). Nothing was written.",
            file=sys.stderr,
        )
        return 2
    recovered = json.loads(matrix_path.read_text(encoding="utf-8"))
    arms = parse_arms()
    # The HeadlightConeOffsets tables and OverrideGTADatPathTo are config values,
    # not installs: they have no arm and no handler, so they are not options with
    # a build applicability.
    options = [
        row for row in shipped_options()
        if row[1] in arms or row[1] in recovered
    ]
    shipped = {row[1] for row in shipped_options()}

    if args.gaps:
        problems = 0
        for _section, key, _default in options:
            entry = recovered.get(key, {})
            arm = arms.get(key)
            if arm is None or entry.get("gate_kind") not in {"head_compare", "switch", "rva_rows"}:
                continue
            recovered_set = {compact for compact, ident in BUILD_ID.items()
                             if ident in entry.get("recovered_builds", []) or
                             ident in entry.get("semantic_builds", [])}
            port = arm["gate"] or set(BUILDS)
            wider = sorted(port - recovered_set)
            narrower = sorted(recovered_set - port)
            if wider:
                print(f"WIDER   {key}: port {wider} not in the recovered gate")
                problems += 1
            if narrower:
                print(f"NARROW  {key}: recovered {narrower} not in the port gate")
                problems += 1
        print(f"{problems} gate mismatch(es)")
        return 0 if problems == 0 else 1

    lines = [
        "# Version applicability matrix",
        "",
        "One row per shipped INI option, one column per supported game build.",
        "Generated by `tools/gen_version_matrix.py` from `docs/version-matrix.json`",
        "(recovered IDA evidence), the arms in `source/*.ixx` and the shipped INI;",
        "run it with `--check` to verify this file is current.",
        "",
        "Cells:",
        "",
        "| cell | meaning |",
        "| --- | --- |",
        "| `R` | **RECOVERED**: the original handler's own build compare / switch / RVA row admits the build. |",
        "| `S` | **SEMANTIC**: the recovered gate admits the build, but the original shipped no per-build data for it (a successor build). |",
        "| `A` | Applies on every build: the original is build-blind (its install is a pattern scan) and the port's arm has no build gate. |",
        "| `G` | The original is build-blind, but the port's arm gates the build (a deliberate narrowing). |",
        "| `.` | Excluded. |",
        "",
        "`gated` in the port column is the union of the arm's own build comparisons",
        "(a `BuildIs`/`ctx.build ==` in the arm or in a helper it calls); an arm that",
        "also has an ungated pattern path reports that union as well, so the cells",
        "above - not the port column - are the applicability.",
        "",
        "Only the 1.0.8.0 image is committed, so every cell other than 1080 is",
        "**untested at runtime**: it records what the recovered gate and the arm",
        "say, not a live result.",
        "",
        "## Recovered gate kinds",
        "",
        "`dword_1015BE04` is the original's build id, written once by `DllMain`.",
        "Only 16 functions read it: `DllMain`, the shared `sub_10004850` helper and",
        "14 option handlers. Everything else installs through a pattern scan and is",
        "build-blind. The full evidence is in `docs/version-matrix.json`.",
        "",
        "| kind | options | what it means |",
        "| --- | --- | --- |",
    ]
    by_kind: dict[str, list[str]] = {}
    for name, entry in recovered.items():
        if name.startswith("_") or name not in shipped:
            continue
        by_kind.setdefault(entry.get("gate_kind", "none"), []).append(name)
    meanings = {
        "head_compare": "a comparison of the build id in the handler head",
        "switch": "a jump table over the build id",
        "rva_rows": "a per-build RVA row table",
        "fallback": "a build compare that only selects the missing-signature fallback",
        "none": "no build check (pattern-gated only)",
    }
    for kind in ("head_compare", "switch", "rva_rows", "fallback", "none"):
        names = by_kind.get(kind, [])
        if names:
            lines.append(f"| `{kind}` | {', '.join(f'`{n}`' for n in names)} | "
                         f"{meanings[kind]} |")

    lines += ["", "## Matrix", "",
              "| option | handler | " + " | ".join(str(b) for b in BUILDS) + " | gate evidence | port |",
              "| --- | --- | " + " | ".join(":-:" for _ in BUILDS) + " | --- | --- |"]
    for section, key, _default in options:
        arm = arms.get(key)
        entry = recovered.get(key, {})
        handler = entry.get("handler", "")
        evidence = entry.get("evidence", "").replace("|", "\\|")
        kind = entry.get("gate_kind", "")
        if not handler:
            handler = "—"
        gate = f"`{kind}`" if kind else "—"
        if evidence:
            gate += f": {evidence}"
        if arm is None:
            port = "no .ixx arm (dllmain or dispatcher-inline)" if handler != "—" else "no arm"
        else:
            parts = []
            if arm["gate"]:
                parts.append("gated " + ",".join(str(b) for b in sorted(arm["gate"])))
            else:
                parts.append("ungated")
            if arm["patterns"]:
                parts.append(f"{arm['patterns']} pattern site(s)")
            if arm["bodies"]:
                parts.append(f"{len(arm['bodies'])} body variant(s)")
            port = "; ".join(parts)
        cells = " | ".join(cell(key, build, recovered, arm) for build in BUILDS)
        lines.append(f"| `{key}` | `{handler}` | {cells} | {gate} | {port} |")

    lines += ["", "## Options with no per-build gate", "",
              "These options carry no build compare in the original: they install",
              "whenever their signature matches, so their applicability is a",
              "property of the view, not of a recovered gate. A `G` cell above means",
              "the port narrowed the install to the builds the feature was designed",
              "for; that narrowing is a documented divergence, not recovered",
              "behaviour.", ""]
    ungated = [key for _s, key, _d in options
               if arms.get(key) and not arms[key]["gate"]
               and recovered.get(key, {}).get("gate_kind") in {"none", "fallback", None}]
    lines.append(f"{len(ungated)} of {len(options)} shipped options are in this group.")
    lines.append("")

    lines += ["## Deliberate narrowings (port gate where the original has none)", ""]
    narrowings = []
    for _section, key, _default in options:
        arm = arms.get(key)
        entry = recovered.get(key, {})
        if arm is None or not arm["gate"]:
            continue
        if entry.get("gate_kind") not in {"none", "fallback", None}:
            continue
        narrowings.append(f"| `{key}` | {', '.join(str(b) for b in sorted(arm['gate']))} "
                          f"| the original installs wherever its pattern matches |")
    if narrowings:
        lines += ["| option | port gate | why it is narrower |", "| --- | --- | --- |"]
        lines += narrowings
        lines.append("")
        lines.append("Each of these needs per-build evidence (a verified pattern or an RVA row)")
        lines.append("before it is safe to widen; widening a gate to \"make it work\" is not")
        lines.append("allowed. They are listed so the gap is visible, not accidental.")
        lines.append("")
        lines.append("A gate in this table can be branch-local: an arm may be pattern-based")
        lines.append("for most builds and carry a per-build fallback branch (for example")
        lines.append("`RemoveRGSCLoginRequirement`'s 1.0.8.0 RVA path). The cells above are")
        lines.append("authoritative for applicability; this table only names the arms whose")
        lines.append("code contains a narrowing.")
    else:
        lines.append("None: every arm with a build gate mirrors a recovered gate.")
    lines.append("")

    generated = "\n".join(lines)
    out = DOCS / "version-matrix.md"
    if args.check:
        current = out.read_text(encoding="utf-8") if out.exists() else ""
        if current != generated:
            print(f"stale: {out}")
            return 1
        print(f"ok: {out} is current")
        return 0
    out.write_text(generated, encoding="utf-8")
    print(f"wrote {out} ({len(options)} options)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
