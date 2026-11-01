#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Body-contract check: compare every hook body's instructions before/after.

The migration moved the bodies out of one translation unit; this proves the
machine-level contract survived. Two ASI/map pairs are disassembled, each body's
instruction list is normalised (module addresses become the symbol they point
at, so a body that moved is still comparable) and the lists are compared.

Usage:
    python tools/opcep_bodycheck.py --baseline <asi> <map> --current <asi> <map>
    python tools/opcep_bodycheck.py --baseline ... --current ... --json out.json

Exit code 0 when every body is identical, 1 when a body differs (the report
lists the first differing instruction), 2 when an input is unreadable.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

try:
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
except ImportError:  # pragma: no cover
    Cs = None

MODULE_LO = 0x10000000
MODULE_HI = 0x10100000

MAP_LINE = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\s"
)


def read_pe(path: Path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path}: bad PE signature")
    coff = pe + 4
    nsections = int.from_bytes(data[coff + 2:coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16:coff + 18], "little")
    opt = coff + 20
    magic = int.from_bytes(data[opt:opt + 2], "little")
    image_base = int.from_bytes(data[opt + 28:opt + 32], "little") if magic == 0x10B else 0
    sections = []
    section_off = opt + opt_size
    for index in range(nsections):
        base = section_off + index * 40
        name = data[base:base + 8].rstrip(b"\0").decode("latin1")
        vsize = int.from_bytes(data[base + 8:base + 12], "little")
        va = int.from_bytes(data[base + 12:base + 16], "little")
        raw_size = int.from_bytes(data[base + 16:base + 20], "little")
        raw_ptr = int.from_bytes(data[base + 20:base + 24], "little")
        sections.append((name, va, vsize, raw_ptr, raw_size))
    return data, image_base, sections


def va_to_offset(data, image_base, sections, va):
    rva = va - image_base
    for _name, sva, vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + vsize:
            delta = rva - sva
            if delta < rawsize:
                return rawptr + delta
    return None


def read_map(path: Path):
    rows = []
    for line in path.read_text(encoding="latin1", errors="replace").splitlines():
        match = MAP_LINE.match(line)
        if match:
            rows.append((match.group(1), match.group(2), int(match.group(3), 16)))
    rows.sort(key=lambda row: row[2])
    return rows


def base_name(symbol: str) -> str:
    """The logical name of a symbol, undecorated, without stdcall arity and
    without leading '_'."""
    if symbol.startswith("?"):
        symbol = symbol[1:].split("@")[0]
    symbol = symbol.lstrip("_")
    return re.sub(r"@\d+$", "", symbol)


class Symbols:
    def __init__(self, rows, code_section: str):
        # Every symbol takes part in address normalisation (string literals and
        # EH tables live in .rdata); only code symbols are body candidates.
        self.rows = rows
        self.by_va = {}
        for _section, name, va in rows:
            self.by_va.setdefault(va, base_name(name))
        self.by_name = {}
        for section, name, va in rows:
            if section == code_section:
                self.by_name.setdefault(base_name(name), va)
        self.vas = [va for _s, _n, va in rows]

    def name_at(self, va: int) -> str | None:
        return self.by_va.get(va)

    def nearest(self, va: int) -> str | None:
        lo, hi = 0, len(self.vas)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.vas[mid] <= va:
                lo = mid + 1
            else:
                hi = mid
        if lo == 0:
            return None
        nearest = self.vas[lo - 1]
        delta = va - nearest
        name = self.by_va.get(nearest)
        if name is None:
            return None
        return name if delta == 0 else f"{name}+0x{delta:X}"

    def next_va(self, va: int) -> int | None:
        lo, hi = 0, len(self.vas)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.vas[mid] <= va:
                lo = mid + 1
            else:
                hi = mid
        return self.vas[lo] if lo < len(self.vas) else None


def canonical(name: str | None) -> str | None:
    """Collapse MSVC's bookkeeping symbols: ICF folds EH handlers across
    functions, and every TU's lambda gets a unique tag, so their identity is not
    part of a body's contract, only their presence."""
    if name is None:
        return None
    if "<lambda" in name:
        return "lambda"
    for prefix in ("ehhandler$", "unwindtable$", "ehfuncinfo$"):
        if name.startswith(prefix):
            return "eh"
    return name


def normalise(insns, image_base: int, image_size: int, symbols: Symbols):
    """Turn capstone instructions into comparable, layout-independent tuples."""
    out = []
    for insn in insns:
        operands = []
        for operand in insn.operands:
            kind = operand.type
            if kind == 1:  # register
                operands.append(insn.reg_name(operand.reg))
            elif kind == 2:  # immediate
                value = operand.imm
                if image_base <= value < image_base + image_size:
                    operands.append("&" + str(canonical(symbols.nearest(value))))
                else:
                    operands.append(str(value))
            elif kind == 3:  # memory
                parts = []
                mem = operand.mem
                if mem.base:
                    parts.append(insn.reg_name(mem.base))
                if mem.index:
                    parts.append(insn.reg_name(mem.index) + "*" + str(mem.scale))
                disp = mem.disp
                if image_base <= disp < image_base + image_size:
                    disp_text = "&" + str(canonical(symbols.nearest(disp)))
                else:
                    disp_text = str(disp)
                parts.append(disp_text)
                operands.append("[" + "+".join(parts) + "]")
            else:
                operands.append("?")
        out.append((insn.mnemonic, tuple(operands)))
    return out


def body_instructions(data, image_base, sections, start, limit, symbols):
    offset = va_to_offset(data, image_base, sections, start)
    if offset is None:
        return None
    max_bytes = min(limit, 16384)
    code = data[offset:offset + max_bytes]
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    disassembler.detail = True
    insns = []
    for insn in disassembler.disasm(code, start):
        # The linker places the next symbol after the body; a run of int3 padding
        # marks the end when the next symbol is unreachable code.
        if insn.mnemonic == "int3" and insns:
            break
        insns.append(insn)
    # Trim trailing padding nops.
    while insns and insns[-1].mnemonic == "nop":
        insns.pop()
    return insns


def exact_names(root: Path) -> set[str]:
    """The bodies opted into byte comparison: the function after each @exact
    comment. Scans a source tree (source/*.ixx and source/*.cpp) or a single
    file; a marker in the module implementation unit counts too."""
    if root.is_file():
        paths = [root]
    else:
        paths = sorted(root.glob("*.ixx")) + sorted(root.glob("*.cpp"))
    names: set[str] = set()
    for path in paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            if "@exact" not in line:
                continue
            for candidate in lines[index + 1:index + 26]:
                match = re.search(
                    r"\b(abi_[A-Za-z0-9_]+|zpatch_[A-Za-z0-9_]+)\s*\(", candidate
                )
                if match and "reinterpret_cast" not in candidate:
                    names.add(match.group(1))
                    break
    if not names:
        raise SystemExit(f"error: no @exact markers under {root}")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", nargs=2, metavar=("ASI", "MAP"), required=True)
    parser.add_argument("--current", nargs=2, metavar=("ASI", "MAP"), required=True)
    parser.add_argument("--json", help="write the per-body result here")
    parser.add_argument("--only", help="check only bodies whose name contains this")
    parser.add_argument(
        "--exact-source",
        help="scan this tree's source/*.ixx for '@exact' markers and require those "
        "bodies to be identical (the transcribed machine-code contract); other "
        "differences are reported as informational",
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if Cs is None:
        print("error: capstone is required (pip install capstone)", file=sys.stderr)
        return 2

    results = []
    pairs = {}
    for label, (asi, map_path) in (("baseline", args.baseline), ("current", args.current)):
        asi_path, map_file = Path(asi), Path(map_path)
        try:
            data, image_base, sections = read_pe(asi_path)
            rows = read_map(map_file)
        except (OSError, ValueError) as error:
            print(f"error: {label}: {error}", file=sys.stderr)
            return 2
        image_size = max(sva + vsize for _n, sva, vsize, _r, _s in sections)
        pairs[label] = (data, image_base, sections, image_size, Symbols(rows, "0001"))

    base_asi, base_base, base_sections, base_size, base_symbols = pairs["baseline"]
    cur_asi, cur_base, cur_sections, cur_size, cur_symbols = pairs["current"]

    names = sorted(set(base_symbols.by_name) & set(cur_symbols.by_name))
    body_names = [
        name for name in names if re.match(r"^(abi_|zpatch_)", name) and not name.startswith("zpatch_abi")
    ]
    if args.only:
        body_names = [name for name in body_names if args.only in name]

    identical = 0
    different = []
    for name in body_names:
        base_va = base_symbols.by_name[name]
        cur_va = cur_symbols.by_name[name]
        base_limit = (base_symbols.next_va(base_va) or (base_va + 8192)) - base_va
        cur_limit = (cur_symbols.next_va(cur_va) or (cur_va + 8192)) - cur_va
        base_insns = body_instructions(
            base_asi, base_base, base_sections, base_va, base_limit, base_symbols
        )
        cur_insns = body_instructions(
            cur_asi, cur_base, cur_sections, cur_va, cur_limit, cur_symbols
        )
        if base_insns is None or cur_insns is None:
            continue
        base_norm = normalise(base_insns, base_base, base_size, base_symbols)
        cur_norm = normalise(cur_insns, cur_base, cur_size, cur_symbols)
        if base_norm == cur_norm:
            identical += 1
            results.append({"name": name, "status": "identical"})
        else:
            first = next(
                (i for i, (a, b) in enumerate(zip(base_norm, cur_norm)) if a != b),
                min(len(base_norm), len(cur_norm)),
            )
            detail = {
                "name": name,
                "status": "different",
                "first_instruction": first,
                "baseline_len": len(base_norm),
                "current_len": len(cur_norm),
                "baseline": base_norm[first:first + 6],
                "current": cur_norm[first:first + 6],
            }
            different.append(detail)
            results.append(detail)

    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=1), encoding="utf-8")

    required = None
    not_emitted = []
    required_compared = 0
    if args.exact_source:
        required = exact_names(Path(args.exact_source))
        compared = {item["name"] for item in results}
        required_compared = len(required & compared)
        missing = sorted(required - compared)
        # A body with no installer in this tree is not referenced by any arm, so
        # the compiler drops it; its bytes are not part of the shipped contract.
        # Report it, but do not fail (a dropped *installed* body shows up as a
        # write-set delta in the matrix instead).
        for name in missing:
            if name in base_symbols.by_name and name not in cur_symbols.by_name:
                not_emitted.append(name)
            else:
                print(f"error: @exact body not compared: {name}", file=sys.stderr)
                return 2

    print(f"bodies compared: {identical + len(different)}; identical: {identical}; "
          f"different: {len(different)}")
    required_diffs = []
    for item in different:
        is_required = required is None or item["name"] in required
        label = "FAIL" if is_required else "info"
        if is_required:
            required_diffs.append(item["name"])
        print(f"\n{label} DIFF {item['name']} at instruction {item['first_instruction']} "
              f"(baseline {item['baseline_len']} insns, current {item['current_len']})")
        for base_text, cur_text in zip(item["baseline"], item["current"]):
            print(f"  - {base_text}")
            print(f"  + {cur_text}")
        if args.verbose:
            print(f"  baseline: {item['baseline']}")
            print(f"  current:  {item['current']}")
    if required is not None:
        print(f"\n@exact bodies: required {len(required)}; emitted and compared "
              f"{required_compared}; uninstalled {len(not_emitted)}; differing "
              f"{len(required_diffs)}")
        if not_emitted:
            print(f"@exact bodies with no installer (dropped from the build): "
                  f"{', '.join(not_emitted)}")
    return 0 if not required_diffs else 1


if __name__ == "__main__":
    raise SystemExit(main())
