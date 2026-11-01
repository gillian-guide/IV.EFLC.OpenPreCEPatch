#!/usr/bin/env python3
"""Prove the rebuilt 32-bit hook bodies against the original ZolikaPatch ASI.

The write-set dump (`tests/dump_main.cpp`) applies the installers only; it never
executes or compares a hook body. This tool closes that gap offline: it reads
`kAbiBodies` from `source/hook_bodies.cpp`, finds every body in the linker map of the
built ASI, disassembles it with capstone and compares it instruction for
instruction with the original body at the reference VA in
`game_refs/ZolikaPatch_original.asi.off` (image base 0x10000000).

Three results per body:

  PASS  an `@exact` transcription: every mnemonic and operand matches after
        relocating module addresses. Operand addresses are not blanked out
        blindly: internal branch targets are compared as body-relative deltas,
        every other module address is bound original<->port and the binding is
        required to be a bijection over all bodies, and bindings the source
        already documents (`src/rebased.h` slot comments and the hook-bodies
        unit's `kGlobalSlots` rows) must agree by symbol name.
  INFO  a compiled C++ replacement. Its contract is the documented instruction
        range: the original function's extent is disassembled and printed next
        to the source comment that documents the replacement. The tool never
        claims byte equality for it.
  FAIL  an `@exact` body that is missing, differs, or whose operand binding
        contradicts the documented one.

Usage (after building the ASI with ZP_LINK_MAP=ON):

    python tools/verify_bodies.py
    python tools/verify_bodies.py --candidate build/agent-x86/Release/ZolikaPatch.asi \
        --map build/agent-x86/Release/ZolikaPatch.map

Exit codes: 0 everything proved, 1 a FAIL, 2 missing inputs/tooling.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

try:
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
except ImportError:  # pragma: no cover - environment without capstone
    print("error: capstone is required (pip install capstone)", file=sys.stderr)
    raise SystemExit(2)

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_SOURCE = ROOT / "source/hook_bodies.cpp"
DEFAULT_REBASED = ROOT / "src/rebased.h"
DEFAULT_ASI = ROOT / "build/agent-x86/Release/ZolikaPatch.asi"
DEFAULT_MAP = ROOT / "build/agent-x86/Release/ZolikaPatch.map"
DEFAULT_ORIGINAL = ROOT.parent / "game_refs/ZolikaPatch_original.asi.off"

# The original ASI is linked /BASE:0x10000000 and so is the rebuild. Any operand
# inside this window is a module address (global, helper, branch target) and is
# relocated, never compared as a number.
MODULE_LO = 0x10000000
MODULE_HI = 0x10200000

# The documented slot bindings the source carries: the `kGlobalSlots` table rows
# and the older `if (slot_name == ...)` publishes the table replaced.
TABLE_SLOT = re.compile(r'\{\s*"(?P<name>\w+)"\s*,\s*&(?P<sym>\w+)\s*\}')
SLOT_PUBLISH = re.compile(
    r'if\s*\(\s*slot_name\s*==\s*"(?P<name>\w+)"\s*\)\s*'
    r"return\s+reinterpret_cast<std::uintptr_t>\(\s*&(?P<sym>\w+)\s*\)"
)

# Original trampolines that MSVC serves through a private per-caller copy in the
# rebuild: one original VA maps to several identical port helpers. The rebuild's
# operand is still proved by the stream comparison; only the reverse mapping is
# many-to-one.
FANOUT_HELPERS = {
    0x1005FE7B: "zpatch_cutscene_cookie_check",  # per-caller private copies
}

# Converted bodies the audit named (reports/readable-audit.md O-2): compiled
# replacements that are not in kAbiBodies, so the registry alone would not cover
# them. Each row is (symbol, original VA, source marker that must be present,
# source file, documented contract). The VA is the original function the source
# comment names; the tool disassembles its extent and reports it as the body's
# documented instruction range. These bodies are NOT byte-compared: their only
# contract is that range plus the behaviour the native tests exercise.
CONVERTED_BODIES = [
    ("abi_new_saves_convert", 0x10051B30, "sub_10051B30", "source/hook_bodies.cpp",
     "compiled port of sub_10051B30: read the 0x110-byte save header, walk 18 chunks of "
     "9-byte header plus payload, record the new-format chunk sizes, then patch game "
     "0x872185/0x7D8C50/0x8DA090"),
    ("abi_menu_focus_set", 0x1001EDC0, "sub_1001EDB0/C0", "source/hook_bodies.cpp",
     "compiled port of sub_1001EDC0: set the focus-loss flag, then NOP or restore the "
     "2-byte branch sub_1001ECB0 patches"),
    ("zpatch_mf_advance_c", 0x100241E0, "sub_100241E0", "source/hook_bodies.cpp",
     "compiled port of sub_100241E0: write the three 8-byte resume windows "
     "(kResumeWindows) selected by the both-axes-zero predicate; the window bytes are "
     "named and static_assert-pinned in the source"),
    ("ApplySkipLauncher", 0x10045CE0, "0x10045CE0", "source/xliveless.cpp",
     "compiled port of the original launcher pass 0x10045CE0 plus the DFA (0x10004850) "
     "and ROM (0x10004620) installers; the recovered rows are byte arrays in the source "
     "and the stages are covered by the native tests"),
]

_HEX = re.compile(r"0x[0-9a-fA-F]+")
_ROW = re.compile(
    r"\{\s*(?:BodyId::(?P<id>\w+)|(?P<va>0x[0-9A-Fa-f]+)u?)\s*,\s*"
    r"(?:reinterpret_cast<[^>]*>\(\s*)?&(?P<sym>\w+)\)?\s*\}\s*,?"
)
_ENUM_ENTRY = re.compile(r"^\s*(?P<id>\w+)\s*=\s*(?P<va>0x[0-9A-Fa-f]+)u?\s*,?")
_EXACT = re.compile(r"@exact\s+(0x[0-9A-Fa-f]+)(?:\s*:?\s+(?P<name>[A-Za-z_]\w*))?")


# --------------------------------------------------------------------------- PE


def read_sections(path: pathlib.Path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError(f"{path}: bad PE signature")
    coff = pe + 4
    nsections = int.from_bytes(data[coff + 2 : coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    opt = coff + 20
    magic = int.from_bytes(data[opt : opt + 2], "little")
    if magic != 0x10B:
        raise ValueError(f"{path}: not a PE32 image")
    image_base = int.from_bytes(data[opt + 28 : opt + 32], "little")
    sections = []
    sec_off = opt + opt_size
    for i in range(nsections):
        base = sec_off + i * 40
        name = data[base : base + 8].rstrip(b"\0").decode("latin1")
        vsize = int.from_bytes(data[base + 8 : base + 12], "little")
        va = int.from_bytes(data[base + 12 : base + 16], "little")
        rawsize = int.from_bytes(data[base + 16 : base + 20], "little")
        rawptr = int.from_bytes(data[base + 20 : base + 24], "little")
        sections.append((name, va, vsize, rawptr, rawsize))
    return data, image_base, sections


def va_to_offset(data: bytes, image_base: int, sections, va: int):
    rva = va - image_base
    for _name, sva, vsize, rawptr, _rawsize in sections:
        if sva <= rva < sva + vsize:
            return rawptr + (rva - sva)
    return None


# --------------------------------------------------------------------- source


def parse_source(source: pathlib.Path):
    text = source.read_text(encoding="utf-8")
    body_ids: dict[str, int] = {}
    id_block = re.search(r"enum\s+class\s+BodyId[^{]*\{(?P<body>.*?)\};", text, re.S)
    if id_block:
        for line in id_block.group("body").splitlines():
            match = _ENUM_ENTRY.match(line)
            if match:
                body_ids[match.group("id")] = int(match.group("va"), 16)

    entries: list[tuple[int, str]] = []
    table = re.search(r"const AbiBody kAbiBodies\[\]\s*=\s*\{(?P<body>.*?)\n\};", text, re.S)
    if not table:
        raise ValueError(f"{source}: kAbiBodies table not found")
    table_text = table.group("body")
    for match in _ROW.finditer(table_text):
        if match.group("id"):
            va = body_ids.get(match.group("id"))
            if va is None:
                raise ValueError(f"{source}: BodyId::{match.group('id')} is not in the enum")
        else:
            va = int(match.group("va"), 16)
        entries.append((va, match.group("sym")))
    # Every table row must be parsed: a formatting change must not silently
    # shrink the proof set (this is exactly how a two-line row hid two bodies
    # before the parse became multiline).
    rows = len(re.findall(r"\{\s*(?:BodyId::\w+|0x[0-9A-Fa-f]+u)\s*,", table_text))
    if rows != len(entries):
        raise ValueError(f"{source}: parsed {len(entries)} of {rows} kAbiBodies rows")

    exact: dict[int, str | None] = {}
    for match in _EXACT.finditer(text):
        exact[int(match.group(1), 16)] = match.group("name")
    return text, body_ids, entries, exact


def comment_block_above(lines: list[str], symbol: str) -> tuple[int, str]:
    """The body definition's line index and its leading comment (documentation)."""
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\s*\(")
    for index, line in enumerate(lines):
        if pattern.search(line) and "(" in line:
            collected = []
            probe = index - 1
            skipped = 0
            while probe >= 0:
                stripped = lines[probe].strip()
                if stripped.startswith("//"):
                    collected.append(stripped[2:].strip())
                elif stripped == "":
                    if collected:
                        break
                elif re.fullmatch(r"(namespace\s*\{?|\{|\}|extern \"C\")", stripped) and skipped < 4:
                    # A declaration often follows its documentation across a
                    # namespace/brace line (compiled replacements do this).
                    skipped += 1
                else:
                    break
                probe -= 1
            return index, " ".join(reversed(collected))
    return -1, ""


def demangle(name: str | None) -> str:
    if not name:
        return ""
    if name.startswith("?"):
        return name[1:].split("@", 1)[0]
    if name.startswith("_"):
        return name[1:]
    return name


def symbol_for_va(symbols: dict[str, int], va: int, prefer: str | None = None):
    hits = [name for name, value in symbols.items() if value == va]
    if prefer:
        for name in hits:
            if demangle(name) == prefer:
                return name
    return min(hits, key=len) if hits else None


# ---------------------------------------------------------------------- parse


def parse_map(path: pathlib.Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    line_re = re.compile(r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\s")
    for line in path.read_text(encoding="latin1", errors="replace").splitlines():
        match = line_re.match(line)
        if match:
            symbols.setdefault(match.group(1), int(match.group(2), 16))
    return symbols


def find_symbol(symbols: dict[str, int], name: str):
    # Exact decorated names first (`_abi_x`, `?abi_x@@...`), then the shortest
    # substring match (a plain function rather than an annotation).
    for candidate in (f"_{name}", name):
        if candidate in symbols:
            return symbols[candidate]
    hits = [(symbol, va) for symbol, va in symbols.items() if name in symbol]
    if not hits:
        return None
    hits.sort(key=lambda item: len(item[0]))
    return hits[0][1]


def parse_documented_bindings(source_text: str, rebased_text: str) -> dict[int, str]:
    """original global VA -> port symbol, as documented by the source itself.

    Three forms are parsed: `src/rebased.h`'s `extern "C" T symbol; // dword_1015..`
    declarations, the hook-bodies unit's `kGlobalSlots` rows
    (`{"dword_1015..", &symbol}`), and the older
    `if (slot_name == "dword_1015..") return &symbol;` publishes. A learned
    binding that contradicts any form is a FAIL.
    """
    documented: dict[int, str] = {}

    def bind(name: str, symbol: str) -> None:
        hex_tail = re.search(r"_([0-9A-Fa-f]{8})$", name)
        if hex_tail:
            documented.setdefault(int(hex_tail.group(1), 16), symbol)

    decl = re.compile(
        r'extern\s+"C"\s+[^;]*?\b(?P<sym>\w+)\s*(?:\[[^\]]*\])?\s*;\s*'
        r"//\s*(?:\w+\s+)?(?:dword|byte|word|unk|off|Src)_(?P<va>[0-9A-Fa-f]{8})\b"
    )
    for match in decl.finditer(rebased_text):
        documented.setdefault(int(match.group("va"), 16), match.group("sym"))
    for table in TABLE_SLOT.finditer(source_text):
        bind(table.group("name"), table.group("sym"))
    for match in SLOT_PUBLISH.finditer(source_text):
        bind(match.group("name"), match.group("sym"))
    return documented


# -------------------------------------------------------------------- disasm


class Disassembler:
    def __init__(self, path: pathlib.Path):
        self.data, self.base, self.sections = read_sections(path)
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)

    def body(self, va: int, max_bytes: int = 4096):
        offset = va_to_offset(self.data, self.base, self.sections, va)
        if offset is None:
            raise ValueError(f"{va:#x} is not inside any section")
        out = []
        for insn in self.md.disasm(self.data[offset : offset + max_bytes], va):
            if insn.mnemonic == "int3":
                break
            out.append((insn.address, insn.mnemonic, insn.op_str))
        return out


def module_addrs(operand: str):
    return [int(match.group(0), 16) for match in _HEX.finditer(operand)
            if MODULE_LO <= int(match.group(0), 16) < MODULE_HI]


def tokenize(insn, body_lo: int, body_hi: int, canon) -> str:
    """Operand text with internal targets as deltas and module addrs canonicalised."""
    _addr, mnemonic, operand = insn
    out = []
    last = 0
    for match in _HEX.finditer(operand):
        value = int(match.group(0), 16)
        if not (MODULE_LO <= value < MODULE_HI):
            continue
        out.append(operand[last : match.start()])
        if body_lo <= value < body_hi:
            out.append(f"D:{value - body_lo:#x}")
        else:
            out.append(canon(value))
        last = match.end()
    out.append(operand[last:])
    return "".join(out)


def external_addrs(insn, body_lo: int, body_hi: int):
    return [value for value in module_addrs(insn[2]) if not (body_lo <= value < body_hi)]


# ---------------------------------------------------------------------- proof


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=pathlib.Path, default=DEFAULT_SOURCE)
    parser.add_argument("--rebased", type=pathlib.Path, default=DEFAULT_REBASED)
    parser.add_argument("--candidate", type=pathlib.Path, default=DEFAULT_ASI)
    parser.add_argument("--map", dest="map_path", type=pathlib.Path, default=DEFAULT_MAP)
    parser.add_argument("--original", type=pathlib.Path, default=DEFAULT_ORIGINAL)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    for required in (args.source, args.rebased, args.candidate, args.map_path, args.original):
        if not required.is_file():
            print(f"error: missing {required}", file=sys.stderr)
            return 2

    text, _body_ids, entries, exact = parse_source(args.source)
    lines = text.splitlines()
    symbols = parse_map(args.map_path)
    documented = parse_documented_bindings(text, args.rebased.read_text(encoding="utf-8"))
    new = Disassembler(args.candidate)
    old = Disassembler(args.original)

    registered = {va for va, _symbol in entries}
    failures: list[str] = []
    for va, name in exact.items():
        if va not in registered:
            failures.append(f"@exact {va:#x} ({name}) has no kAbiBodies entry: the proof "
                            f"would never compare it")

    # ---------------------------------------------------------------- learn
    learn: dict[int, set[int]] = {}
    inverse: dict[int, set[int]] = {}
    paired = 0
    bodies = []
    for reference_va, symbol in entries:
        port_va = find_symbol(symbols, symbol)
        if port_va is None:
            failures.append(f"FAIL {symbol}: not found in {args.map_path.name}")
            bodies.append((reference_va, symbol, port_va, None))
            continue
        bodies.append((reference_va, symbol, port_va, None))
        if reference_va not in exact:
            continue
        try:
            old_body = old.body(reference_va)
            new_body = new.body(port_va)
        except ValueError as exc:
            failures.append(f"FAIL {symbol}: {exc}")
            continue
        if len(old_body) != len(new_body):
            failures.append(f"FAIL {symbol}: {len(old_body)} original instructions, "
                            f"{len(new_body)} in the rebuild")
        old_lo, old_hi = reference_va, old_body[-1][0] + 16 if old_body else reference_va
        new_lo, new_hi = port_va, new_body[-1][0] + 16 if new_body else port_va
        for old_insn, new_insn in zip(old_body, new_body):
            if old_insn[1] != new_insn[1]:
                failures.append(
                    f"FAIL {symbol} at {old_insn[0]:#x}: {old_insn[1]} vs {new_insn[1]}")
                continue
            old_ext = external_addrs(old_insn, old_lo, old_hi)
            new_ext = external_addrs(new_insn, new_lo, new_hi)
            if len(old_ext) != len(new_ext):
                failures.append(
                    f"FAIL {symbol} at {old_insn[0]:#x}: {len(old_ext)} bound operand(s) "
                    f"in the original, {len(new_ext)} in the rebuild "
                    f"({old_insn[1]} {old_insn[2]} vs {new_insn[2]})")
                continue
            for original_va, port_addr in zip(old_ext, new_ext):
                learn.setdefault(original_va, set()).add(port_addr)
                inverse.setdefault(port_addr, set()).add(original_va)
                paired += 1

    # A port address that serves two different original slots would silently
    # cross-wire two bodies, so that direction must stay single-valued.
    for port_addr, original_v_as in sorted(inverse.items()):
        if len(original_v_as) > 1:
            failures.append(
                f"FAIL port {port_addr:#x} binds to {len(original_v_as)} original slots: "
                + ", ".join(f"{value:#x}" for value in sorted(original_v_as)))

    # Original slots that fan out are only the documented private helpers.
    for original_va, port_addrs in sorted(learn.items()):
        if len(port_addrs) > 1 and original_va not in FANOUT_HELPERS:
            failures.append(
                f"FAIL original {original_va:#x} binds to {len(port_addrs)} rebuild "
                f"addresses with no documented private copy: "
                + ", ".join(f"{value:#x}" for value in sorted(port_addrs)))

    for original_va, port_addrs in learn.items():
        expected = documented.get(original_va)
        if expected is None:
            continue
        for port_addr in sorted(port_addrs):
            got = demangle(symbol_for_va(symbols, port_addr))
            if got != expected:
                failures.append(
                    f"FAIL original {original_va:#x} is documented as `{expected}` but the "
                    f"body operand binds to `{got or hex(port_addr)}` ({port_addr:#x})")

    # ---------------------------------------------------------------- compare
    def canon_original(value: int) -> str:
        return f"g:{value:#x}"

    def canon_rebuild(value: int) -> str:
        originals = inverse.get(value)
        if originals and len(originals) == 1:
            return f"g:{next(iter(originals)):#x}"
        return f"u:{value:#x}"

    counts = {"PASS": 0, "INFO": 0, "FAIL": 0}
    for reference_va, symbol, port_va, _extra in bodies:
        line, contract = comment_block_above(lines, symbol)
        contract = contract or "(no leading comment)"
        if reference_va not in exact:
            counts["INFO"] += 1
            try:
                old_body = old.body(reference_va)
                extent = f"{reference_va:#x}..{old_body[-1][0] + 16:#x} ({len(old_body)} insns)"
            except ValueError as exc:
                extent = str(exc)
            if not args.quiet:
                print(f"INFO {symbol} ref={reference_va:#x} at {args.source.name}:{line + 1}\n"
                      f"     original range {extent}; compiled C++ replacement, not byte-compared\n"
                      f"     contract: {contract}")
            if port_va is None:
                counts["FAIL"] += 1
            continue
        if port_va is None:
            counts["FAIL"] += 1
            continue
        try:
            old_body = old.body(reference_va)
            new_body = new.body(port_va)
        except ValueError as exc:
            counts["FAIL"] += 1
            print(f"FAIL {symbol}: {exc}")
            continue
        old_lo, old_hi = reference_va, old_body[-1][0] + 16 if old_body else reference_va
        new_lo, new_hi = port_va, new_body[-1][0] + 16 if new_body else port_va
        diffs = []
        for index in range(max(len(old_body), len(new_body))):
            old_insn = old_body[index] if index < len(old_body) else (0, "<end>", "")
            new_insn = new_body[index] if index < len(new_body) else (0, "<end>", "")
            if old_insn[1] != new_insn[1]:
                diffs.append((index, f"{old_insn[1]} {old_insn[2]}", f"{new_insn[1]} {new_insn[2]}"))
                continue
            old_token = tokenize(old_insn, old_lo, old_hi, canon_original)
            new_token = tokenize(new_insn, new_lo, new_hi, canon_rebuild)
            if old_token != new_token:
                diffs.append((index, f"{old_insn[1]} {old_token}", f"{new_insn[1]} {new_token}"))
        if diffs or len(old_body) != len(new_body):
            counts["FAIL"] += 1
            print(f"FAIL {symbol} ref={reference_va:#x} ({len(old_body)} vs {len(new_body)} insns)")
            for index, old_text, new_text in diffs[:8]:
                print(f"   {index:3d} orig {old_text:44s} | new {new_text}")
            if len(diffs) > 8:
                print(f"   ... {len(diffs) - 8} more")
        else:
            counts["PASS"] += 1
            if not args.quiet:
                print(f"PASS {symbol} ref={reference_va:#x} ({len(old_body)} insns)")

    # ------------------------------------------------- named converted bodies
    for symbol, original_va, marker, source_name, body_contract in CONVERTED_BODIES:
        path = ROOT / source_name
        if not path.is_file():
            path = args.source
        if marker not in path.read_text(encoding="utf-8"):
            failures.append(f"FAIL {symbol}: the source no longer documents the original "
                            f"body ({marker} is absent from {source_name})")
            continue
        try:
            old_body = old.body(original_va)
            extent = f"{original_va:#x}..{old_body[-1][0] + 16:#x} ({len(old_body)} insns)"
        except ValueError as exc:
            failures.append(f"FAIL {symbol}: {exc}")
            continue
        source_lines = path.read_text(encoding="utf-8").splitlines()
        line, source_comment = comment_block_above(source_lines, symbol)
        counts["INFO"] += 1
        if not args.quiet:
            contract = body_contract
            if source_comment:
                contract += f"\n     source comment: {source_comment}"
            print(f"INFO {symbol} ref={original_va:#x} at {source_name}:{line + 1}\n"
                  f"     original range {extent}; compiled C++ replacement, not byte-compared\n"
                  f"     contract: {contract}")

    for failure in failures:
        print(failure)

    print(f"\nbodies: {counts['PASS']} PASS, {counts['INFO']} INFO, {counts['FAIL']} FAIL; "
          f"{len(learn)} original slots bound to {len(inverse)} rebuild addresses "
          f"({paired} operand pairings), {len(documented)} bindings documented")
    if failures or counts["FAIL"]:
        print(f"FAIL: {len(failures) + counts['FAIL']} proof problem(s)")
        return 1
    print("PASS: every @exact body matches the original; every compiled body's range is "
          "documented")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
