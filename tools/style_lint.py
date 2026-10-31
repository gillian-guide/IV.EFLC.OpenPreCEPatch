#!/usr/bin/env python3
"""Style contract checker for the readable port.

The rules are the ones in CONTRIBUTING.md, encoded so "did we get all of them?"
is a machine answer instead of a review opinion. Vendored code and generated
data are out of scope; see SCOPE below.

    python tools/style_lint.py                    # human summary
    python tools/style_lint.py --json out.json    # machine-readable findings
    python tools/style_lint.py --fail-on error    # non-zero on any error
    python tools/style_lint.py --file src/dllmain.cpp --fail-on error
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # readable/
SRC = ROOT / "src"

# Vendored libraries (git submodules, never edited here) and generated data keep
# their upstream shape. Vendored code lives under external/, outside the walked
# roots below; the entries are kept explicit so the scope stays readable.
EXCLUDED = (
    "external/",
    "src/jitasm.h",
    "src/Pool.h",
    "src/StdInc.h",
    "src/INI.h",
    "src/catalogue_data.cpp",
    "src/patch_data.cpp",
    "src/ooc_payload.h",
)


@dataclass
class Finding:
    rule: str
    severity: str
    path: str
    line: int
    message: str
    text: str


# ---------------------------------------------------------------- helpers
def strip_comments(lines: list[str]) -> list[str]:
    """Blank out // and /* */ comments, keeping line numbering and strings."""
    out: list[str] = []
    in_block = False
    for line in lines:
        kept = []
        i = 0
        in_string = False
        while i < len(line):
            c = line[i]
            if in_block:
                if c == "*" and i + 1 < len(line) and line[i + 1] == "/":
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_string:
                kept.append(c)
                if c == "\\" and i + 1 < len(line):
                    kept.append(line[i + 1])
                    i += 2
                    continue
                if c == '"':
                    in_string = False
                i += 1
                continue
            if c == '"':
                in_string = True
                kept.append(c)
                i += 1
                continue
            if c == "/" and i + 1 < len(line):
                if line[i + 1] == "/":
                    break
                if line[i + 1] == "*":
                    in_block = True
                    i += 2
                    continue
            kept.append(c)
            i += 1
        out.append("".join(kept))
    return out


def function_end(cleaned: list[str], decl_line: int) -> int:
    """Last line of the innermost block containing decl_line (0-based)."""
    depth = 0
    start = decl_line
    for i in range(decl_line, -1, -1):
        depth += cleaned[i].count("}") - cleaned[i].count("{")
        if depth < 0:
            start = i
            break
    depth = 0
    for i in range(start, len(cleaned)):
        depth += cleaned[i].count("{") - cleaned[i].count("}")
        if depth == 0 and "}" in cleaned[i]:
            return i
    return len(cleaned) - 1


def scope_kinds(cleaned: list[str]) -> list[list[str]]:
    """Active scope kinds per line: type / init / function / block."""
    kinds: list[str] = []
    active: list[list[str]] = []
    for i, line in enumerate(cleaned):
        active.append(list(kinds))
        prefix = line.split("{")[0].strip()
        if line.strip() == "{" and i:
            for j in range(i - 1, -1, -1):
                if cleaned[j].strip():
                    prefix = cleaned[j].strip()
                    break
        if re.search(r"\b(namespace|struct|class|union|enum)\b", prefix):
            kind = "type"
        elif "=" in prefix or prefix.endswith(","):
            kind = "init"
        elif "(" in prefix or ")" in prefix:
            kind = "function"
        else:
            kind = "block"
        opens, closes = line.count("{"), line.count("}")
        for _ in range(max(0, opens - closes)):
            kinds.append(kind)
        for _ in range(max(0, closes - opens)):
            if kinds:
                kinds.pop()
    return active


def is_local(active: list[str]) -> bool:
    return bool(active) and "function" in active and active[-1] != "type"


DECL = re.compile(
    r"^\s*(?:const\s+)?(auto|[A-Za-z_][\w:<>,\s\*&]*?)\s+([A-Za-z_]\w*)\s*=\s*(.+?);\s*$"
)
EXCLUDED_TYPES = (
    "std::string",
    "std::vector",
    "std::array",
    "std::initializer_list",
    "std::FILE",
    "HMODULE",
    "HANDLE",
    "ModuleView",
    "GameImage",
    "PatchContext",
    "ConfigDocument",
    "Match",
    "hook::pattern",
    "LARGE_INTEGER",
    "FILETIME",
    "SYSTEM_INFO",
    "MEMORY_BASIC_INFORMATION",
    "OVERLAPPED",
)
NAME_ALLOWLIST = {"image", "ctx", "pattern", "config", "match", "out", "value"}


def iter_files(only: list[str] | None, include_tests: bool = True):
    roots = [SRC]
    if include_tests:
        roots.append(ROOT / "tests")
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".hpp"):
                continue
            rel = path.relative_to(ROOT).as_posix()
            if any(rel.startswith(x) or rel == x for x in EXCLUDED):
                continue
            if rel.startswith("tests/") and "injector" in rel:
                continue
            if only and not any(rel == f or rel.endswith("/" + f) for f in only):
                continue
            yield path, rel


# ---------------------------------------------------------------- rules
BANNED_CALLS = re.compile(
    r"\b(WriteHex|WriteValue|WritePointer|PointerAt|SiteAddress|RelativeTarget|Nop)\s*\("
)
SCAN_CALL = re.compile(r"\b(?:Scan|ScanAll)\s*\(|FindAllPatterns\s*\(\s*\{image")
SCAN_DEF = re.compile(r"\b(?:Scan|ScanAll)\s*\(")


def lint(path: Path, rel: str) -> list[Finding]:
    raw = path.read_text(encoding="utf-8", errors="replace").splitlines()
    code = strip_comments(raw)
    scopes = scope_kinds(code)
    is_installers = rel in ("src/dllmain.cpp", "src/patches.cpp")
    findings: list[Finding] = []

    def add(rule: str, severity: str, line: int, message: str) -> None:
        findings.append(
            Finding(rule, severity, rel, line + 1, message, raw[line].strip())
        )

    for i, text in enumerate(code):
        raw_line = raw[i]
        stripped = text.strip()

        # E001 zero offsets
        if re.search(r"\bmatch \+ 0\b|\bmatches\[\d+\] \+ 0\b", text):
            add("E001", "error", i, "zero offset: use the match itself")
        # E002 negative-offset spellings
        if re.search(r" \+ -\d+|\+ 429496\d+", text):
            add("E002", "error", i, "spell negative offsets as subtraction")
        # E003 bespoke write helpers
        for m in BANNED_CALLS.finditer(text):
            add("E003", "error", i, f"bespoke write helper {m.group(1)}(); use injector::")
        # E004 NOP fills
        if re.search(r"\bFill\s*\([^;]*0x90", text):
            add("E004", "error", i, "Fill(..., 0x90): use injector::MakeNOP")
        # E005 old scan entry points (installers only)
        if is_installers and SCAN_CALL.search(text) and not SCAN_DEF.search(text):
            add("E005", "error", i, "installers must scan with Pattern(image, ...)")
        # E006 vector of matches (installers only)
        if is_installers and re.search(r"std::vector<std::uint8_t\*> matches", text):
            add("E006", "error", i, "use hook::pattern, not a matches vector")
        # E007 doubled address cast
        if "reinterpret_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>" in text:
            add("E007", "error", i, "use AddressOf() for a 32-bit pointer value")
        # E008 pointer from Field
        if re.search(
            r"reinterpret_cast<std::uint8_t\*>\s*\(\s*static_cast<std::uintptr_t>\s*\(\s*Field",
            text,
        ):
            add("E008", "error", i, "pointer from a field: use PointerAt()")
        # E015 literal NOP byte (installers only; other files may hold byte tables)
        if is_installers and re.search(r"\b0x90\b", text) and "kNop" not in text:
            add("E015", "error", i, "literal 0x90: use kNop / injector::MakeNOP")
        # E016 huge wrap literal
        if "429496" in text:
            add("E016", "error", i, "32-bit wrap literal: spell the negative offset")
        # W002 long line (a long recovered signature string is data, not style)
        if len(raw_line) > 110 and not re.search(r'"[0-9A-F? ]{40,}"', raw_line):
            add("W002", "warn", i, f"line is {len(raw_line)} columns")
        # W004 comment that is only a name and a dangling colon
        if re.match(r"^\s*// [\w@]+:\s*$", raw_line):
            add("W004", "warn", i, "comment ends with a dangling colon")
        # W003 write without a trailing comment (the call may wrap)
        if re.search(
            r"injector::(WriteMemory<[^>]+>|MakeNOP|MakeJMP|MakeCALL)\s*\(", text
        ):
            has_comment = "//" in raw_line
            if not has_comment:
                for j in range(i + 1, min(i + 5, len(raw))):
                    if "//" in raw[j]:
                        has_comment = True
                        break
                    if raw[j].rstrip().endswith(");"):
                        break
            if not has_comment:
                add("W003", "warn", i, "write has no trailing // instruction comment")

    # E010 duplicate adjacent statements
    for i in range(1, len(code)):
        a, b = code[i - 1].strip(), code[i].strip()
        if a == b and a.endswith(";") and not a.startswith("//"):
            add("E010", "error", i, "duplicate adjacent statement")

    # E009/W001 locals: unused and single-use
    for i, text in enumerate(code):
        if not is_local(scopes[i]):
            continue
        m = DECL.match(text)
        if not m:
            continue
        type_name, name, rhs = m.group(1).strip(), m.group(2), m.group(3)
        if type_name.startswith("const "):
            type_name = type_name[6:].strip()
        if name.upper() == name or name.startswith("k"):
            continue
        end = function_end(code, i)
        uses = 0
        for j in range(i + 1, end + 1):
            uses += len(re.findall(rf"\b{re.escape(name)}\b", code[j]))
        if uses == 0:
            add("E009", "error", i, f"unused local {name}")
            continue
        if name in NAME_ALLOWLIST:
            continue
        if type_name.endswith("&") or type_name.endswith("*"):
            continue
        if any(type_name.startswith(t) for t in EXCLUDED_TYPES):
            continue
        if uses == 1 and "static" not in text:
            add("W001", "warn", i, f"{name} is used once; inline it or name why")

    # E011 empty publish arms
    if is_installers:
        for i, text in enumerate(code):
            m = re.match(r"^\s*void (Publish\w+)\(const PatchContext& ctx\)\s*$", text)
            if not m:
                continue
            end = function_end(code, i + 1)
            body = "\n".join(code[i + 2 : end])
            if re.search(
                r"\b(Publish\s*\(|injector::|Pattern\s*\(|AddressOf\s*\(|\w+\(ctx)",
                body,
            ):
                continue
            if "Option(" not in body:
                continue
            add("E011", "error", i,
                f"{m.group(1)} only gates and returns; remove the empty arm")

    return findings


# ---------------------------------------------------------------- main
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", action="append", default=None,
                        help="limit to a path relative to readable/ (repeatable)")
    parser.add_argument("--json", metavar="PATH", help="write findings as JSON")
    parser.add_argument("--fail-on", choices=("error", "warn", "none"), default="none")
    parser.add_argument("--no-tests", action="store_true", help="skip readable/tests")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    findings: list[Finding] = []
    for path, rel in iter_files(args.file, include_tests=not args.no_tests):
        findings.extend(lint(path, rel))

    findings.sort(key=lambda f: (f.path, f.line, f.rule))
    errors = sum(f.severity == "error" for f in findings)
    warns = sum(f.severity == "warn" for f in findings)

    if args.json:
        Path(args.json).write_text(json.dumps([asdict(f) for f in findings], indent=1),
                                   encoding="utf-8")
    if not args.quiet:
        by_rule: dict[str, int] = {}
        for f in findings:
            by_rule[f.rule] = by_rule.get(f.rule, 0) + 1
        for f in findings:
            if f.severity == "error":
                print(f"{f.path}:{f.line}: {f.rule} {f.message}")
                print(f"    {f.text}")
        print()
        print(f">{errors} errors, {warns} warnings")
        for rule in sorted(by_rule):
            print(f"  {rule}: {by_rule[rule]}")

    if args.fail_on == "error" and errors:
        return 1
    if args.fail_on == "warn" and (errors or warns):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
