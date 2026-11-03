#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ratchet for the readability contract in docs/readability.md.

The contract is mechanical: readable C++ first, assembly in dedicated body
units, no unnamed site offsets, no raw game VAs in code, no module hubs, no
oversized files.  This tool counts violations per file and rule and compares
them with tools/readability_baseline.json.  Cleanup lowers the baseline; the
check fails only when a count grows, so the tree can be fixed module by module
without a flag day.

    python tools/readability_check.py --report
    python tools/readability_check.py --check
    python tools/readability_check.py --update-baseline
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source"
BASELINE = ROOT / "tools" / "readability_baseline.json"

# Files whose contents are recovered data, not logic.
DATA_ALLOWLIST = {
    "data/model_catalogue.ixx",
    "data/ooc_payload.h",
    "rebased.h",
}
DEAD_DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)", re.M)
CONST_DECL = re.compile(
    r"^\s*(?:inline\s+)?constexpr\s+[^;=\n]+\s+([A-Za-z_]\w*)\s*(?:=|\{)", re.M)
ABI_DOUBLE_CAST = re.compile(
    r"reinterpret_cast<[^;\n]{0,80}?static_cast<uintptr_t>\(\s*zpatch_")
OFFSETOF_ASSERT = re.compile(r"static_assert\(\s*offsetof\(")
PAD_BY_CONSTANT = re.compile(r"std::byte\s+pad_\w+\[\s*k[A-Za-z_]")

COMMENT_BLOCK = re.compile(r"/\*.*?\*/", re.S)
COMMENT_LINE = re.compile(r"//.*")
HEX = re.compile(r"\b0x[0-9A-Fa-f]+\b")
MATCH_OFFSET = re.compile(r"\bmatch\s*\+\s*(?:0x[0-9A-Fa-f]+|\d+)")
RAW_VA = re.compile(r"\b0x1[0-9A-Fa-f]{7}\b")
OFFSET_FIELD = re.compile(r"\+\s*0x[0-9A-Fa-f]+\b")
HUB = re.compile(r'import\s+opp\.(?:settings|pipeline)\s*;|#include\s+"(?:ini_file|opcodes)\.h"')
PAD_EXTENT = re.compile(r"\bpad_[0-9A-Fa-f]+\s*\[")
NAKED_ASM = re.compile(r"__asm\b|__declspec\s*\(\s*naked\s*\)")
BANNER = re.compile(r"^//\s*(?:-{3,}|={3,})\s*$", re.M)
STATEMENT_LINE = re.compile(r"\S")


def strip_comments(text: str) -> str:
    text = COMMENT_BLOCK.sub("", text)
    return "\n".join(COMMENT_LINE.sub("", line) for line in text.splitlines())


def is_code_line(line: str) -> bool:
    stripped = line.strip()
    if not stripped:
        return False
    return not stripped.startswith(("#include", "import ", "module ", "export module"))


def count_file(path: Path) -> dict[str, int]:
    rel = path.relative_to(SOURCE).as_posix()
    raw = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(raw)
    counts: dict[str, int] = {}

    n = len(ABI_DOUBLE_CAST.findall(code))
    if n:
        counts["abi-double-cast"] = n

    n = len(OFFSETOF_ASSERT.findall(code))
    if n:
        counts["offsetof-assert-wall"] = n

    n = len(PAD_BY_CONSTANT.findall(code))
    if n:
        counts["pad-sized-by-constant"] = n

    n = len(RAW_VA.findall(code))
    if n:
        counts["raw-game-va"] = n

    n = len(HUB.findall(code))
    if n:
        counts["module-hub"] = n

    lines = raw.splitlines()
    n = sum(1 for line in lines if BANNER.match(line))
    if n:
        counts["banner-comment"] = n

    # Hex values in executable statements (named constexpr values are handled by
    # the per-module semantic pass, not by this mechanical ratchet).
    n = 0
    for line in code.splitlines():
        if not is_code_line(line):
            continue
        if "constexpr" in line or "static_assert" in line or "case " in line:
            continue
        if "//" in line:
            continue
        # A view struct's pad extent states the recovered layout at the
        # declaration (`std::byte pad_0030[0x0E18 - 0x0030];`), so it is not a
        # magic value in a statement.
        if PAD_EXTENT.search(line):
            continue
        n += len(HEX.findall(line))
    if n:
        counts["hex-in-statement"] = n

    # Unnamed field offsets in executable statements.
    n = 0
    for line in code.splitlines():
        if not is_code_line(line):
            continue
        if "constexpr" in line or "static_assert" in line:
            continue
        n += len(OFFSET_FIELD.findall(line))
    if n:
        counts["unnamed-field-offset"] = n

    return counts


def top_level_constants(text: str) -> list[str]:
    """Names declared at namespace/class scope, not inside a function body."""
    names: list[str] = []
    scopes: list[str] = []
    pending_function = False
    for line in strip_comments(text).splitlines():
        for _ in range(line.count("}")):
            if scopes:
                scopes.pop()
        match = CONST_DECL.match(line)
        if match:
            nearest = next((scope for scope in reversed(scopes) if scope != "block"),
                           "global")
            if nearest not in ("function", "export-namespace"):
                names.append(match.group(1))

        stripped = line.strip()
        if pending_function and stripped.startswith("{"):
            scopes.append("function")
            pending_function = False
            continue
        if re.search(r"\)\s*(?:noexcept|const)?\s*$", line) and ";" not in line:
            pending_function = True
        if "{" not in line:
            continue
        kind = "block"
        if re.search(r"\bexport\s+namespace\b", line):
            kind = "export-namespace"
        elif re.search(r"\bnamespace\b", line):
            kind = "namespace"
        elif re.search(r"\b(class|struct|enum)\b", line):
            kind = "type"
        elif "(" in line and not re.match(
                r"\s*(if|for|while|switch|catch)\b", line):
            kind = "function"
        scopes.extend(kind for _ in range(line.count("{")))
    return names


def one_use_constants(all_text: dict[str, str]) -> dict[str, int]:
    """Top-level constexpr names with only a declaration and one reference."""
    out: dict[str, int] = {}
    combined = "\n".join(all_text.values())
    for rel, text in all_text.items():
        for match in CONST_DECL.finditer(strip_comments(text)):
            name = match.group(1)
            if len(re.findall(rf"\b{re.escape(name)}\b", combined)) <= 2:
                out[rel] = out.get(rel, 0) + 1
    return out


def dead_defines(all_text: dict[str, str]) -> dict[str, int]:
    """Macros whose name occurs only at their own definition."""
    out: dict[str, int] = {}
    combined = "\n".join(all_text.values())
    for rel, text in all_text.items():
        for match in DEAD_DEFINE.finditer(strip_comments(text)):
            name = match.group(1)
            if len(re.findall(rf"\b{re.escape(name)}\b", combined)) <= 1:
                out[rel] = out.get(rel, 0) + 1
    return out


def collect() -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    texts: dict[str, str] = {}
    for path in sorted(SOURCE.rglob("*")):
        if not path.is_file() or path.suffix not in {".ixx", ".cpp", ".hxx", ".h"}:
            continue
        rel = path.relative_to(SOURCE).as_posix()
        if rel in DATA_ALLOWLIST:
            continue
        texts[rel] = path.read_text(encoding="utf-8", errors="replace")
        counts = count_file(path)
        if counts:
            result[rel] = counts
    usage = dict(texts)
    for extra in (ROOT / "tests").rglob("*.cpp"):
        usage[extra.as_posix()] = extra.read_text(encoding="utf-8", errors="replace")
    for source, rule in ((dead_defines(usage), "dead-define"),
                         (one_use_constants(usage), "one-use-constant")):
        for rel, count in source.items():
            if rel in texts:
                result.setdefault(rel, {})[rule] = count
    return result


def totals(current: dict[str, dict[str, int]]) -> dict[str, int]:
    out: dict[str, int] = {}
    for rules in current.values():
        for rule, count in rules.items():
            out[rule] = out.get(rule, 0) + count
    return dict(sorted(out.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()

    current = collect()
    summary = totals(current)
    if args.update_baseline or (args.check and not BASELINE.exists()):
        BASELINE.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"readability: wrote {BASELINE} ({sum(summary.values())} violations)")
        return 0

    if args.report or not args.check:
        for rule, count in summary.items():
            print(f"{rule:24} {count:6}")
        top = sorted(current.items(), key=lambda kv: sum(kv[1].values()), reverse=True)[:20]
        print("\nworst files")
        for rel, rules in top:
            print(f"{sum(rules.values()):5}  {rel}")
        return 0

    baseline = json.loads(BASELINE.read_text(encoding="utf-8")) if BASELINE.exists() else {}
    regressions: list[str] = []
    improvements: list[str] = []
    for rel, rules in current.items():
        for rule, count in rules.items():
            old = baseline.get(rel, {}).get(rule, 0)
            if count > old:
                regressions.append(f"{rel}: {rule} {old} -> {count}")
            elif count < old:
                improvements.append(f"{rel}: {rule} {old} -> {count}")
    for rel, rules in baseline.items():
        for rule, old in rules.items():
            if current.get(rel, {}).get(rule, 0) < old:
                improvements.append(f"{rel}: {rule} {old} -> 0")

    if regressions:
        print("readability: FAIL")
        for line in regressions:
            print("  " + line)
        return 1
    if improvements:
        print(f"readability: PASS ({len(improvements)} counts can be ratcheted down)")
    else:
        print("readability: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
