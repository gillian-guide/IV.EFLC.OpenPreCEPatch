#!/usr/bin/env python3
"""Independent cross-check of readable-fusion against ZolikaPatch_reconstruction.

The question this answers: readable-fusion claims 1:1 behaviour with the original
ASI, but its evidence compares against baselines produced by ../readable/ (a
derived source).  ZolikaPatch_reconstruction is a third, separately produced
source with its own recovered tables and its own test suite.  This tool reads
those tables, builds the write set they predict for each INI key on each game
build, and compares it with what our zp_dump actually writes.

Three parts, reported separately so a disagreement can be attributed:

  1. install sets   -- per build, per key: predicted vs written bytes.
  2. body table     -- every body VA in tests/bodies.tsv vs the reconstruction's
                       own body table (recovered_hook_targets.inc).
  3. reference run  -- build and run ZolikaPatch_reconstruction/tests/test_main.cpp
                       (and its --dump-writes development aid) as a measurement,
                       and fold the CHECK assertions it makes into part 1.

Read-only with respect to ZolikaPatch_reconstruction/ and readable-fusion/source/.
Build artefacts go under readable-fusion/build/, temp files under
readable-fusion/.scratch/.

Usage:
  python tools/crosscheck_reconstruction.py [--only 9] [--no-suite]
                                            [--json PATH] [--keep]

Exit code is 0 when the tool ran, not when the trees agree: the cross-check is a
measurement, and the report says what it measured.
"""

import argparse
import json
import re
import struct
import subprocess
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                       # readable-fusion
DECOMP = ROOT.parent                     # decomp
RECON = DECOMP / "ZolikaPatch_reconstruction"
GAME = DECOMP / "game_refs"

BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120",
          13: "EFLC_1130"}
VERSIONS = {1: "1.0.0.0", 5: "1.0.4.0", 6: "1.0.0.4", 7: "1.0.6.0",
            8: "1.0.7.0", 9: "1.0.8.0", 11: "1.1.1.0", 12: "1.1.2.0",
            13: "1.1.3.0"}
VERSION_TO_ORDINAL = {1000: 1, 1004: 6, 1040: 5, 1060: 7, 1061: 15, 1070: 8,
                      1080: 9, 1100: 10, 1110: 11, 1120: 12, 1130: 13, 12043: 18}

# The reconstruction's module image is based at 0x10000000 and is ~0x1B0000
# bytes; a 32-bit literal in that range is a pointer into its own data.
RECON_MODULE_LO = 0x10000000
RECON_MODULE_HI = 0x101AFFFF
# The dump's stand-in for a published module/image pointer (see bodies.ixx).
PLACEHOLDER = 0xD0000000

EXCEPTIONS = {0xC0000005: "ACCESS_VIOLATION", 0xC000001D: "ILLEGAL_INSTRUCTION",
              0xC0000094: "INTEGER_DIVIDE_BY_ZERO", 0xC00000FD: "STACK_OVERFLOW",
              0xC0000374: "HEAP_CORRUPTION", 0xC0000409: "STACK_BUFFER_OVERRUN"}


# --------------------------------------------------------------------------
# C initializer parsing
# --------------------------------------------------------------------------

def _split_fields(body):
    """Split a C initializer body on top-level commas (quotes/braces aware)."""
    fields, cur, depth, quote, escape = [], [], 0, False, False
    for ch in body:
        if quote:
            cur.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                quote = False
            continue
        if ch == '"':
            quote = True
            cur.append(ch)
        elif ch in "({[":
            depth += 1
            cur.append(ch)
        elif ch in ")}]":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            fields.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        fields.append("".join(cur).strip())
    return fields


def _scalar(tok):
    tok = tok.strip()
    if tok.startswith('"') and tok.endswith('"'):
        return tok[1:-1]
    if tok in ("nullptr", "NULL"):
        return None
    if tok.endswith("u") or tok.endswith("U"):
        tok = tok[:-1]
    try:
        return int(tok, 0)
    except ValueError:
        return tok


def _rows(text, array_name):
    """Yield the top-level {..} rows of `array_name[] = { ... };`.

    Several generated .inc files carry only the initializer rows; the array
    itself is declared in the .cpp that includes them.  When the named array is
    absent, every top-level {..} group in the file is a row.
    """
    m = re.search(rf"{re.escape(array_name)}\s*\[\s*\]\s*=\s*\{{", text)
    if m:
        i, depth, start = m.end(), 1, m.end()
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = text[start:i]
    else:
        body = "\n".join(line for line in text.splitlines()
                         if not line.lstrip().startswith("//"))
    rows = []
    # Walk top-level {...} groups.
    j, depth = 0, 0
    row_start = None
    while j < len(body):
        ch = body[j]
        if ch == "{":
            if depth == 0:
                row_start = j
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and row_start is not None:
                rows.append(body[row_start:j + 1])
                row_start = None
        j += 1
    return rows


def _row_fields(row):
    row = row.strip()
    if row.startswith("{"):
        row = row[1:]
    if row.endswith("}"):
        row = row[:-1]
    return [_scalar(f) for f in _split_fields(row)]


def _read(path):
    return path.read_text(encoding="utf-8", errors="replace")


# --------------------------------------------------------------------------
# PE mapping + signature scan
# --------------------------------------------------------------------------

class Image:
    """A file-layout PE mapped the way the game's loader maps it (by RVA)."""

    def __init__(self, path):
        raw = path.read_bytes()
        e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
        self.machine, self.nsec = struct.unpack_from("<HH", raw, e_lfanew + 4)
        opt = e_lfanew + 24
        size_opt = struct.unpack_from("<H", raw, e_lfanew + 20)[0]
        self.image_base = struct.unpack_from("<I", raw, opt + 28)[0]
        self.size = struct.unpack_from("<I", raw, opt + 56)[0]
        size_headers = struct.unpack_from("<I", raw, opt + 60)[0]
        self.bytes = bytearray(self.size)
        self.bytes[:min(size_headers, len(raw))] = raw[:min(size_headers, len(raw))]
        self.sections = []
        sec = opt + size_opt
        for _ in range(self.nsec):
            name = raw[sec:sec + 8].rstrip(b"\0").decode("latin-1")
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, sec + 8)
            chars = struct.unpack_from("<I", raw, sec + 36)[0]
            self.sections.append((name, vaddr, vsize, rsize, raddr, chars))
            count = min(rsize, self.size - vaddr) if vaddr < self.size else 0
            if count and raddr + count <= len(raw):
                self.bytes[vaddr:vaddr + count] = raw[raddr:raddr + count]
            sec += 40
        self.path = path
        self.pristine = bytes(self.bytes)

    def executable_end(self):
        end = 0
        for _name, vaddr, vsize, rsize, _raddr, chars in self.sections:
            if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                end = vaddr + (rsize or vsize)
        return min(end or self.size, self.size)


def parse_hex_token(tok):
    """The reconstruction's strtoul(text, 16) token conversion, low byte."""
    if tok in ("?", "??"):
        return -1
    i = 0
    neg = False
    while i < len(tok) and tok[i] in " \t\r\n\f\v":
        i += 1
    if i < len(tok) and tok[i] in "+-":
        neg = tok[i] == "-"
        i += 1
    if i + 1 < len(tok) and tok[i] == "0" and tok[i + 1] in "xX":
        i += 2
    acc = 0
    for ch in tok[i:]:
        if ch.isdigit():
            d = ord(ch) - 48
        elif "a" <= ch <= "f":
            d = ord(ch) - 87
        elif "A" <= ch <= "F":
            d = ord(ch) - 55
        else:
            break
        acc = (acc * 16 + d) & 0xFFFFFFFF
    if neg:
        acc = (-acc) & 0xFFFFFFFF
    return acc & 0xFF


class Scanner:
    """Signature scans over one image, cached per signature string."""

    def __init__(self, image):
        self.image = image
        self._regex = {}
        self._hits = {}

    def _compiled(self, signature):
        rx = self._regex.get(signature)
        if rx is None:
            parts = []
            for tok in signature.split():
                value = parse_hex_token(tok)
                parts.append(b"." if value < 0 else re.escape(bytes([value])))
            rx = re.compile(b"".join(parts), re.S)
            self._regex[signature] = rx
        return rx

    def all(self, signature):
        hits = self._hits.get(signature)
        if hits is None:
            hits = [m.start() for m in self._compiled(signature).finditer(self.image.pristine)]
            self._hits[signature] = hits
        return hits

    def first(self, signature):
        hits = self.all(signature)
        return hits[0] if hits else None


# --------------------------------------------------------------------------
# Reconstruction tables
# --------------------------------------------------------------------------

def base_key(option):
    """A per-build variant row (`Key@new`, `Key@old`, `Key@noE`) belongs to Key."""
    return str(option).split("@", 1)[0]


@dataclass
class OpRow:
    option: str
    signature: str
    offset: int
    length: int
    replacement: bytes
    module_rva: int
    handler: int


@dataclass
class RvaInstall:
    option: str
    site_rva: int
    opcode: int
    kind: int
    build: int
    operand: int
    slot: str


@dataclass
class SigInstall:
    option: str
    signature: str
    offset: int
    opcode: int
    dest_kind: int
    dest: int
    deref: int
    symbol: str
    literal: str


@dataclass
class SigWrite:
    option: str
    signature: str
    offset: int
    data: bytes
    match_index: int
    fallback: str


@dataclass
class RvaJump:
    option: str
    rva: int
    opcode: int
    destination: int
    handler: int


@dataclass
class SigJump:
    option: str
    signature: str
    offset: int
    destination: int


@dataclass
class LiteralOp:
    option: str
    signature: str
    offset: int
    width: int
    value: int
    kind: str


@dataclass
class MatchJump:
    option: str
    signature: str
    match_index: int
    offset: int
    destination: int
    opcode: int


@dataclass
class UselessSite:
    build: int
    rva: int
    length: int
    data: bytes


class Tables:
    def __init__(self, root):
        src = root / "src"
        self.root = root
        self.op_rows = self._op_rows(src / "recovered_patch_ops.inc")
        self.rva_installs = self._rva_installs(src / "recovered_rva_installs.inc")
        self.anchors, self.suppressions, self.sig_installs, self.sig_writes, \
            self.slot_installs = self._corrections(src / "recovered_op_corrections.inc")
        self.rva_jumps = self._rva_jumps(src / "recovered_rva_jumps.inc")
        self.sig_jumps = self._sig_jumps(src / "recovered_sig_jumps.inc")
        self.indirect = self._literal_ops(src / "recovered_indirect_writes.inc",
                                          "recovered_indirect_writes", "indirect")
        self.literal_stores = self._literal_ops(src / "recovered_literal_stores.inc",
                                                "recovered_literal_stores", "literal")
        self.match_jumps = self._match_jumps(src / "recovered_match_jumps.inc")
        self.match_literals = self._match_literals(src / "recovered_match_literals.inc")
        self.build_gates = self._build_gates(src / "recovered_build_gates.inc")
        self.rva_builds = self._rva_builds(src / "recovered_rva_builds.inc")
        self.feature_masks = self._feature_masks(src / "recovered_feature_coverage.inc")
        self.useless = self._useless(src / "recovered_useless_checks.inc")
        self.hook_targets = self._hook_targets(src / "recovered_hook_targets.inc")
        self.skip = self._skip_launcher(src / "recovered_skip_launcher.inc")

    @staticmethod
    def _json(path):
        try:
            return json.loads(_read(path))
        except Exception:
            return None

    @staticmethod
    def _bytes(text):
        out = []
        for tok in str(text).split():
            tok = tok.strip()
            if len(tok) == 2:
                out.append(int(tok, 16))
            elif tok:
                out.append(int(tok, 16) & 0xFF)
        return bytes(out)

    def _op_rows(self, path):
        rows = []
        for row in _rows(_read(path), "kOps"):
            f = _row_fields(row)
            if len(f) < 7:
                continue
            rows.append(OpRow(str(f[0]), str(f[1]), int(f[2]), int(f[3]),
                              self._bytes(f[4]), int(f[5]), int(f[6])))
        return rows

    def _rva_installs(self, path):
        rows = []
        for row in _rows(_read(path), "kRvaInstalls"):
            f = _row_fields(row)
            if len(f) < 8:
                continue
            rows.append(RvaInstall(str(f[0]), int(f[1]), int(f[2]), int(f[3]),
                                   int(f[4]), int(f[5]), str(f[7])))
        return rows

    def _corrections(self, path):
        text = _read(path)
        anchors = [(str(f[0]), str(f[1])) for f in
                   (_row_fields(r) for r in _rows(text, "kOpAnchors")) if len(f) >= 2]
        suppressions = [(int(f[0]), str(f[1]), int(f[2]), str(f[3]))
                        for f in (_row_fields(r) for r in _rows(text, "kOpSuppressions"))
                        if len(f) >= 4]
        sig_installs = []
        for f in (_row_fields(r) for r in _rows(text, "kSigInstalls")):
            if len(f) < 8:
                continue
            sig_installs.append(SigInstall(str(f[0]), str(f[1]), int(f[2]), int(f[3]),
                                           int(f[4]), int(f[5]), int(f[6]), str(f[7]),
                                           str(f[8]) if len(f) > 8 else ""))
        sig_writes = []
        for f in (_row_fields(r) for r in _rows(text, "kSigWrites")):
            if len(f) < 4:
                continue
            sig_writes.append(SigWrite(str(f[0]), str(f[1]), int(f[2]), self._bytes(f[3]),
                                       int(f[4]), str(f[5]) if len(f) > 5 else ""))
        slot_installs = []
        for f in (_row_fields(r) for r in _rows(text, "kSlotInstalls")):
            if len(f) < 9:
                continue
            slot_installs.append(tuple(f))
        return anchors, suppressions, sig_installs, sig_writes, slot_installs

    def _rva_jumps(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kRvaJumps")):
            if len(f) < 5:
                continue
            rows.append(RvaJump(str(f[0]), int(f[1]), int(f[2]), int(f[3]), int(f[4])))
        return rows

    def _sig_jumps(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kSigJumps")):
            if len(f) < 4:
                continue
            rows.append(SigJump(str(f[0]), str(f[1]), int(f[2]), int(f[3])))
        return rows

    def _literal_ops(self, path, array, kind):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), array)):
            if len(f) < 5:
                continue
            rows.append(LiteralOp(str(f[0]), str(f[1]), int(f[2]), int(f[3]),
                                  int(f[4]), kind))
        return rows

    def _match_jumps(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kMatchJumps")):
            if len(f) < 6:
                continue
            rows.append(MatchJump(str(f[0]), str(f[1]), int(f[2]), int(f[3]),
                                  int(f[4]), int(f[5])))
        return rows

    def _match_literals(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kMatchLiterals")):
            if len(f) < 6:
                continue
            rows.append((str(f[0]), str(f[1]), int(f[2]), int(f[3]), int(f[4]),
                         int(f[5])))
        return rows

    def _build_gates(self, path):
        gates = {}
        for f in (_row_fields(r) for r in _rows(_read(path), "kGates")):
            if len(f) >= 2:
                gates[str(f[0])] = int(f[1])
        return gates

    def _rva_builds(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kRvaBuildGates")):
            if len(f) >= 4:
                rows.append((int(f[0]), int(f[1]), int(f[2]), int(f[3])))
        return rows

    def _feature_masks(self, path):
        masks = {}
        for f in (_row_fields(r) for r in _rows(_read(path), "kRecovered")):
            if len(f) >= 10:
                masks[str(f[0])] = (int(f[5]), bool(f[4]))
        return masks

    def _useless(self, path):
        rows = []
        for f in (_row_fields(r) for r in _rows(_read(path), "kUselessChecks")):
            if len(f) < 4:
                continue
            data = f[3].strip()
            if data.startswith("{"):
                data = data[1:-1]
            rows.append(UselessSite(int(f[0]), int(f[1]), int(f[2]),
                                    self._bytes(data.replace(",", " "))))
        return rows

    def _hook_targets(self, path):
        """{reference VA: [option, ...]} from the reconstruction's body tables."""
        out = defaultdict(list)
        text = _read(path)
        for m in re.finditer(r"\{\s*(0x[0-9a-fA-F]+)u\s*,.*?\"([^\"]+)\"\s*\}", text):
            out[int(m.group(1), 16)].append(m.group(2))
        # The JSON is the same map with the full option list per body (the .inc
        # keeps only the first attributed option).
        data = self._json(self.root / "evidence" / "hook_reimplementation_map.json")
        for row in data or []:
            try:
                va = int(str(row.get("target")), 0)
            except (TypeError, ValueError):
                continue
            for option in row.get("options", []):
                if option not in out[va]:
                    out[va].append(option)
        return dict(out)

    def _skip_launcher(self, path):
        text = _read(path)
        arrays = {}
        for m in re.finditer(r"kSkipBytes_(\d+)_(\d+)\[\]\s*=\s*\{([^}]*)\}", text):
            arrays[(int(m.group(1)), int(m.group(2)))] = self._bytes(
                m.group(3).replace(",", " "))
        collector_arrays = {}
        for m in re.finditer(r"kSkipCollectorBytes_(\d+)\[\]\s*=\s*\{([^}]*)\}", text):
            collector_arrays[int(m.group(1))] = self._bytes(m.group(2).replace(",", " "))
        writes = {}
        for m in re.finditer(r"kWrites_(\d+)\[\]\s*=\s*\{(.*?)\n\};", text, re.S):
            index = int(m.group(1))
            rows = []
            for j, f in enumerate(_row_fields(r) for r in _rows(m.group(0),
                                                                f"kWrites_{index}")):
                if len(f) < 5:
                    continue
                offset, length, arr, fill, target = f[:5]
                if arr is None:
                    data = bytes([int(fill) & 0xFF]) * int(length)
                else:
                    data = arrays.get((index, j), b"")
                target = (0xFFFFFFFF if target in (None, "kNoJumpTarget")
                          else int(target))
                rows.append((int(offset), int(length), data, target))
            writes[index] = rows
        entry_rows = []
        for index, f in enumerate(_row_fields(r) for r in
                                  _rows(text, "kSkipLauncherEntries")):
            if len(f) >= 2:
                entry_rows.append((str(f[0]), writes.get(index, [])))
        collectors = []
        for f in (_row_fields(r) for r in _rows(text, "kSkipLauncherCollectors")):
            if len(f) >= 4:
                target = str(f[1]).split("::")[-1]
                collectors.append((str(f[0]), target, collector_arrays.get(
                    int(str(f[2]).replace("kSkipCollectorBytes_", "")), b""),
                    int(f[3])))
        dfa = [(str(f[0]), int(f[1]), int(f[2]), str(f[3]).split("::")[-1])
               for f in (_row_fields(r) for r in _rows(text, "kSkipLauncherDfaInstalls"))
               if len(f) >= 4]
        rom = [(str(f[0]), int(f[1]), int(f[2]), str(f[3]).split("::")[-1])
               for f in (_row_fields(r) for r in _rows(text, "kSkipLauncherRomInstalls"))
               if len(f) >= 4]
        collected = []
        for f in (_row_fields(r) for r in _rows(text, "kSkipLauncherDfaCollected")):
            if len(f) >= 3:
                collected.append((str(f[0]), str(f[1]), int(f[2])))
        return {"entries": entry_rows, "collectors": collectors, "dfa": dfa,
                "rom": rom, "collected": collected}


# The reconstruction's SkipHook enum -> the original body VA it stands for
# (skip_launcher.hpp documents each one; recovered_hook_targets.inc confirms).
SKIP_HOOK_VA = {
    "dfa_init": 0x10004780, "create_file": 0x10004820, "close_handle": 0x10004790,
    "set_file_pointer": 0x10004760, "get_file_size_ex": 0x10004720,
    "read_file": 0x10004740, "rom_reset": 0x10004570, "rom_set": 0x10004610,
}


# --------------------------------------------------------------------------
# The replayer: what the reconstruction's tables predict
# --------------------------------------------------------------------------

@dataclass
class Note:
    table: str
    text: str


class Predictor:
    """Applies the reconstruction's tables to one image for one build/key."""

    def __init__(self, tables, image, build, scanner=None):
        self.t = tables
        self.image = image
        self.build = build
        self.scan = scanner or (Scanner(image) if image is not None else None)
        self.bytes = bytearray(image.pristine) if image is not None else bytearray()
        self.notes = []
        self.skipped = []           # (rva, table, reason) for a guarded-out row
        self.writes = []            # (rva, data, table, kind)
        self._sig_cache = {}

    # ---- gates -----------------------------------------------------------
    def option_gate(self, option):
        mask = self.t.build_gates.get(option)
        if mask is None:
            return True
        return (mask >> self.build) & 1 == 1

    def feature_gate(self, option):
        entry = self.t.feature_masks.get(option)
        if entry is None:
            return True
        mask, gated = entry
        return True if not gated else ((mask >> self.build) & 1) == 1

    def rva_build_allows(self, handler, rva):
        for h, r, kind, value in self.t.rva_builds:
            if h != handler or r != rva:
                continue
            if kind == 0:
                return self.build == value
            if kind == 1:
                return self.build >= value
            if kind == 2:
                return self.build > value
            if kind == 3:
                return self.build <= value
            if kind == 4:
                return self.build < value
            return True
        return False

    def anchor_ok(self, option):
        for name, signature in self.t.anchors:
            if name == option:
                return self.scan.first(signature) is not None
        return True

    # ---- matching --------------------------------------------------------
    def match_now(self, signature, index=0):
        """The index-th match in the image as it is *now* (the original scans
        once per handler and the handler's earlier writes are already in)."""
        rx = self.scan._compiled(signature)
        fresh = [m.start() for m in rx.finditer(bytes(self.bytes))]
        return fresh[index] if index < len(fresh) else None

    def all_matches_now(self, signature):
        rx = self.scan._compiled(signature)
        return [m.start() for m in rx.finditer(bytes(self.bytes))]

    def match_cached(self, signature, index=0):
        """The first match of a pass, reused for every later row of the same
        signature (the original handlers write relative to one `ebx` match, so a
        row inside its own signature does not hide the next row)."""
        hits = self._sig_cache.get(signature)
        if hits is None:
            hits = self.scan.all(signature)
            self._sig_cache[signature] = hits
        return hits[index] if index < len(hits) else None

    # ---- writes ----------------------------------------------------------
    def put(self, rva, data, table, kind="literal"):
        if rva < 0 or rva + len(data) > len(self.bytes):
            return False
        self.bytes[rva:rva + len(data)] = data
        self.writes.append((rva, bytes(data), table, kind))
        return True

    def branch(self, site, opcode, destination, table):
        disp = (destination - (self.image.image_base + site + 5)) & 0xFFFFFFFF
        data = bytes([opcode]) + struct.pack("<I", disp)
        kind = "branch-module" if RECON_MODULE_LO <= destination <= RECON_MODULE_HI \
            else "branch-game"
        return self.put(site, data, table, kind)

    # ---- stages ----------------------------------------------------------
    def run(self, key, include_skip_launcher=True):
        # SkipLauncher's handler runs before the per-option op table in the
        # original's load (dllmain: apply_skip_launcher_if_enabled, then
        # apply_recovered_patches), and its entry 9 signature is the same bytes
        # the SkipLauncher op row writes, so the order is load-bearing.
        if include_skip_launcher and key == "SkipLauncher":
            self.stage_skip_launcher()
        self.stage_useless(key)
        self.stage_ops(key)
        self.stage_rva_installs(key)
        self.stage_sig_installs(key)
        self.stage_sig_writes(key)
        self.stage_rva_jumps(key)
        self.stage_sig_jumps(key)
        self.stage_indirect(key)
        self.stage_match_jumps(key)
        self.stage_match_literals(key)
        self.stage_literal_stores(key)
        return self

    def stage_useless(self, key):
        if key != "RemoveUselessChecks":
            return
        for site in self.t.useless:
            if site.build != self.build:
                continue
            self.put(site.rva, site.data, "recovered_useless_checks.inc")

    def _suppressed(self, op):
        for handler, signature, offset, values in self.t.suppressions:
            if handler != op.handler or offset != op.offset:
                continue
            if signature != op.signature:
                continue
            if values.replace(" ", "").upper() != op.replacement.hex().upper():
                continue
            return True
        return False

    def _covered_by_jump(self, op):
        for jump in self.t.sig_jumps:
            if (base_key(jump.option) == base_key(op.option)
                    and jump.signature == op.signature
                    and jump.offset == op.offset):
                return True
        for jump in self.t.match_jumps:
            if (base_key(jump.option) == base_key(op.option)
                    and jump.signature == op.signature
                    and jump.offset == op.offset):
                return True
        # A curated sig install / sig write is the handler's real, full write;
        # the op row is the partial capture of the same site.
        for install in self.t.sig_installs:
            if (base_key(install.option) == base_key(op.option)
                    and install.signature == op.signature
                    and install.offset == op.offset):
                return True
        for write in self.t.sig_writes:
            if (base_key(write.option) == base_key(op.option)
                    and write.signature == op.signature
                    and write.offset == op.offset):
                return True
        return False

    def _rva_install_covers(self, rva):
        for install in self.t.rva_installs:
            if install.build not in (0, self.build):
                continue
            if install.kind not in (3, 5) and install.site_rva == rva:
                return True
        return False

    def stage_ops(self, key):
        for op in self.t.op_rows:
            if base_key(op.option) != key:
                continue
            if not self.option_gate(op.option):
                continue
            if not self.anchor_ok(op.option):
                continue
            if op.module_rva and not self.rva_build_allows(op.handler, op.module_rva):
                continue
            if op.module_rva and self._rva_install_covers(op.module_rva):
                continue
            if self._suppressed(op):
                continue
            if self._covered_by_jump(op):
                continue
            if op.length == 1 and op.replacement in (b"\xE9", b"\xE8"):
                covered = any(j.module_rva == op.module_rva
                              for j in self.t.rva_jumps) if op.module_rva else False
                if not covered:
                    self.notes.append(Note("recovered_patch_ops.inc",
                                           f"{op.option}: lone {op.replacement.hex().upper()} row skipped "
                                           f"(no recovered jump)"))
                    continue
                continue
            if op.module_rva:
                self.put(op.module_rva, op.replacement, "recovered_patch_ops.inc")
            else:
                hit = self.match_cached(op.signature)
                if hit is None:
                    self.notes.append(Note("recovered_patch_ops.inc",
                                           f"{op.option}: signature absent"))
                    continue
                self.put(hit + op.offset, op.replacement, "recovered_patch_ops.inc")

    def stage_rva_installs(self, key):
        for install in self.t.rva_installs:
            if base_key(install.option) != key:
                continue
            if install.build not in (0, self.build):
                continue
            if not self.anchor_ok(install.option):
                continue
            if install.kind == 3 or install.kind == 4:
                # Slot publish into the plugin's own global: not an image write.
                self.notes.append(Note("recovered_rva_installs.inc",
                                       f"{install.option}: slot {install.slot} publish is a "
                                       f"plugin-global write, not an image write"))
                continue
            if install.kind == 5:
                # A pointer into the plugin's data, written over a game operand.
                self.put(install.site_rva, struct.pack("<I", PLACEHOLDER),
                         "recovered_rva_installs.inc", "module-pointer")
                continue
            if install.kind == 2:
                self.put(install.site_rva, bytes([install.opcode]) * install.operand,
                         "recovered_rva_installs.inc")
                continue
            destination = (self.image.image_base + install.operand
                           if install.kind == 0 else install.operand)
            self.branch(install.site_rva, install.opcode, destination,
                        "recovered_rva_installs.inc")

    def stage_sig_installs(self, key):
        # A handler's installs for one signature form a patch set: one scan, then
        # every member writes relative to that single match.  Re-scanning per row
        # would miss the rows after one that overwrites a byte of its own
        # signature (PopupHeadlightsFlag's +10 EB, ForceCarHeadlightShadows' +6 EB).
        groups = {}
        for install in self.t.sig_installs:
            groups.setdefault(install.signature, []).append(install)
        for signature, members in groups.items():
            enabled = [m for m in members
                       if base_key(m.option) == key
                       and self.option_gate(m.option)
                       and self.anchor_ok(m.option)]
            if not enabled:
                continue
            hit = self.match_now(signature)
            if hit is None:
                continue
            for install in enabled:
                site = hit + install.offset
                if install.deref:
                    # The site is a pointer stored at match + offset (CrashMsgFix's
                    # `*(match-4)`); the write lands on that pointer's target.
                    try:
                        site = struct.unpack_from("<I", self.bytes, site)[0]
                    except struct.error:
                        continue
                    site -= self.image.image_base
                if install.dest_kind == 6:
                    self.put(site, Tables._bytes(install.literal),
                             "recovered_op_corrections.inc")
                    continue
                if install.dest_kind == 3:
                    self.put(site, struct.pack("<I", PLACEHOLDER),
                             "recovered_op_corrections.inc", "module-pointer")
                    continue
                if install.dest_kind == 4:
                    self.put(site, bytes([install.dest & 0xFF]),
                             "recovered_op_corrections.inc")
                    continue
                if install.dest_kind == 5:
                    self.put(site, struct.pack("<I", PLACEHOLDER),
                             "recovered_op_corrections.inc", "module-pointer")
                    continue
                if install.dest_kind == 2:
                    rel = struct.unpack_from("<i", self.bytes,
                                             hit + install.offset + 2)[0]
                    destination = self.image.image_base + hit + install.offset + 6 + rel
                elif install.dest_kind == 0:
                    destination = install.dest
                else:
                    destination = self.image.image_base + hit + install.dest
                self.branch(site, install.opcode, destination,
                            "recovered_op_corrections.inc")

    def stage_sig_writes(self, key):
        # The handler scans once per signature and writes every row relative to
        # that one match vector; a row inside the signature must not hide the
        # rows after it.
        groups = {}
        for write in self.t.sig_writes:
            groups.setdefault(write.signature, []).append(write)
        for signature, members in groups.items():
            enabled = [w for w in members
                       if base_key(w.option) == key
                       and self.option_gate(w.option)
                       and self.anchor_ok(w.option)]
            if not enabled:
                continue
            hits = self.all_matches_now(signature)
            fallback_hits = None
            for write in enabled:
                index = write.match_index
                hit = hits[index] if index < len(hits) else None
                if hit is None and write.fallback:
                    if fallback_hits is None:
                        fallback_hits = self.all_matches_now(write.fallback)
                    hit = (fallback_hits[index]
                           if index < len(fallback_hits) else None)
                if hit is None:
                    continue
                self.put(hit + write.offset, write.data,
                         "recovered_op_corrections.inc")

    def stage_rva_jumps(self, key):
        for jump in self.t.rva_jumps:
            if base_key(jump.option) != key:
                continue
            if not self.feature_gate(jump.option):
                continue
            if not self.rva_build_allows(jump.handler, jump.rva):
                continue
            if not self.anchor_ok(jump.option):
                continue
            self.branch(jump.rva, jump.opcode, jump.destination,
                        "recovered_rva_jumps.inc")

    def stage_sig_jumps(self, key):
        for jump in self.t.sig_jumps:
            if base_key(jump.option) != key:
                continue
            if not self.feature_gate(jump.option):
                continue
            if not self.anchor_ok(jump.option):
                continue
            hit = self.match_now(jump.signature)
            if hit is None:
                continue
            self.branch(hit + jump.offset, 0xE9, jump.destination,
                        "recovered_sig_jumps.inc")

    def stage_indirect(self, key):
        for op in self.t.indirect:
            if base_key(op.option) != key:
                continue
            if not self.feature_gate(op.option) or not self.anchor_ok(op.option):
                continue
            hit = self.match_now(op.signature)
            if hit is None:
                continue
            target = struct.unpack_from("<I", self.bytes, hit + op.offset)[0]
            if target < self.image.image_base or \
                    target + op.width > self.image.image_base + len(self.bytes):
                continue
            self.put(target - self.image.image_base,
                     op.value.to_bytes(op.width, "little"),
                     "recovered_indirect_writes.inc")

    def stage_match_jumps(self, key):
        # The multi-hit collector scans once per signature and the rows write
        # relative to that one match vector, so the vector is taken before any
        # of the signature's own writes land.
        cache = {}
        for op in self.t.match_jumps:
            if base_key(op.option) != key:
                continue
            if not self.feature_gate(op.option) or not self.anchor_ok(op.option):
                continue
            hits = cache.get(op.signature)
            if hits is None:
                hits = self.all_matches_now(op.signature)
                cache[op.signature] = hits
            if op.match_index >= len(hits):
                continue
            self.branch(hits[op.match_index] + op.offset, op.opcode,
                        op.destination, "recovered_match_jumps.inc")

    def stage_match_literals(self, key):
        cache = {}
        for option, signature, index, offset, width, value in self.t.match_literals:
            if base_key(option) != key:
                continue
            if not self.feature_gate(option) or not self.anchor_ok(option):
                continue
            hits = cache.get(signature)
            if hits is None:
                hits = self.all_matches_now(signature)
                cache[signature] = hits
            targets = hits if index < 0 else (hits[index:index + 1])
            for hit in targets:
                kind = "literal"
                data = value.to_bytes(width, "little")
                if width == 4 and RECON_MODULE_LO <= value <= RECON_MODULE_HI:
                    data = struct.pack("<I", PLACEHOLDER)
                    kind = "module-pointer"
                self.put(hit + offset, data, "recovered_match_literals.inc", kind)

    def stage_literal_stores(self, key):
        for op in self.t.literal_stores:
            if base_key(op.option) != key:
                continue
            if not self.feature_gate(op.option) or not self.anchor_ok(op.option):
                continue
            hit = self.match_now(op.signature)
            if hit is None:
                continue
            kind = "literal"
            data = op.value.to_bytes(op.width, "little")
            if op.width == 4 and RECON_MODULE_LO <= op.value <= RECON_MODULE_HI:
                data = struct.pack("<I", PLACEHOLDER)
                kind = "module-pointer"
            self.put(hit + op.offset, data, "recovered_literal_stores.inc", kind)

    def stage_skip_launcher(self):
        skip = self.t.skip
        matches = {}
        for index, (signature, writes) in enumerate(skip["entries"]):
            hit = self.match_now(signature)
            matches[index] = hit
            if hit is None:
                continue
            if any(target != 0xFFFFFFFF and matches.get(target) is None
                   for _off, _len, _data, target in writes):
                # The original skips the whole block when the jump's target entry
                # was absent (the xor and the jump share one guard).  Record the
                # jump site so a write of ours there can be attributed.
                for offset, _length, _data, target in writes:
                    if target != 0xFFFFFFFF:
                        self.skipped.append((hit + offset, "recovered_skip_launcher.inc",
                                             f"target entry {target} absent"))
                continue
            for offset, length, data, target in writes:
                if target != 0xFFFFFFFF:
                    dest = matches.get(target)
                    if dest is None:
                        continue
                    disp = (self.image.image_base + dest -
                            (self.image.image_base + hit + offset + 4)) & 0xFFFFFFFF
                    self.put(hit + offset, struct.pack("<I", disp),
                             "recovered_skip_launcher.inc", "branch-game")
                    continue
                self.put(hit + offset, data[:length], "recovered_skip_launcher.inc")
        for signature, target, data, length in skip["collectors"]:
            hits = self.all_matches_now(signature)
            if not hits:
                continue
            chosen = hits[:2] if target == "first_two" else hits[-2:]
            for hit in chosen:
                self.put(hit, data[:length], "recovered_skip_launcher.inc")
        for signature, offset, opcode, hook in skip["dfa"] + skip["rom"]:
            hit = self.match_now(signature)
            if hit is None:
                continue
            destination = SKIP_HOOK_VA.get(hook)
            if destination is None:
                continue
            self.branch(hit + offset, opcode, destination,
                        "recovered_skip_launcher.inc")
        for collector, target_name, count in skip["collected"]:
            hits = self.all_matches_now(collector)
            if not hits:
                continue
            targets = []
            m = re.search(rf"{re.escape(target_name)}\[\]\s*=\s*\{{(.*?)\n\}};",
                          _read(self.t.root / "src" / "recovered_skip_launcher.inc"),
                          re.S)
            if m:
                for f in (_row_fields(r) for r in _rows(m.group(0), target_name)):
                    if len(f) >= 2:
                        targets.append((str(f[0]), int(f[1])))
            for signature, index in targets:
                if index >= len(hits):
                    continue
                dest = self.match_now(signature)
                if dest is None:
                    continue
                disp = (self.image.image_base + dest -
                        (self.image.image_base + hits[index] + 5)) & 0xFFFFFFFF
                self.put(hits[index], b"\xE9" + struct.pack("<I", disp),
                         "recovered_skip_launcher.inc", "branch-game")

    # ---- result ----------------------------------------------------------
    def changed(self):
        """{rva: byte} of bytes that differ from the pristine image.

        Only the ranges this pass actually wrote are examined; a full 15 MB
        compare per key would dominate the run time.
        """
        out = {}
        if self.image is None:
            return out
        for rva, data, _table, _kind in self.writes:
            for i in range(len(data)):
                if (rva + i < len(self.bytes) and
                        self.bytes[rva + i] != self.image.pristine[rva + i]):
                    out[rva + i] = self.bytes[rva + i]
        return out

    def write_spans(self):
        """The writes as (rva, data, table, kind) in application order."""
        return list(self.writes)


# --------------------------------------------------------------------------
# Our side: run zp_dump per-option and parse it
# --------------------------------------------------------------------------

def catalogue_keys():
    text = _read(ROOT / "source" / "options.inc")
    return [m.group(1) for m in re.finditer(r"ZP_OPTION\((\w+),", text)]


def run_ours(dump, exe, build, out, work, keys):
    """Our per-option cell as {key: {rva: byte}}, plus the keys whose cell faults.

    Build 1.0.0.0's per-option cell faults by design (NoImpostorHeightCutoff
    reproduces the original's access violation). A shared loop that dies hides
    every key after the fault, so those keys are measured one subprocess at a
    time, exactly like tools/dump_matrix.py does.
    """
    out.parent.mkdir(parents=True, exist_ok=True)
    faulted = set()
    sidecar = out.with_suffix(".faults")
    if out.exists() and out.stat().st_mtime >= dump.stat().st_mtime:
        if sidecar.exists():
            faulted = set(json.loads(_read(sidecar)))
        return read_ours(out), faulted
    ini = work / "no-such.ini"
    proc = subprocess.run([str(dump), "--per-option", "--exe", str(exe),
                           "--build", str(build), str(out), str(ini)],
                          capture_output=True, text=True, errors="replace")
    code = proc.returncode & 0xFFFFFFFF
    if code and not (0xC0000000 <= code <= 0xCFFFFFFF):
        raise SystemExit(f"zp_dump failed for build {build}: exit {code}\n"
                         f"{proc.stdout}{proc.stderr}")
    rows = read_ours(out)
    if not code:
        sidecar.unlink(missing_ok=True)
        return rows, faulted

    print(f"  build {build}: our per-option cell faulted with "
          f"0x{code:08X} {EXCEPTIONS.get(code, 'exception')}; "
          f"measuring the uncovered keys one at a time")
    all_off = "[Options]\n" + "".join(f"{key}=0\n" for key in keys)
    merged = []
    for key in keys:
        if rows.get(key):
            continue
        single = work / f"isolated-b{build}.ini"
        single.write_text(all_off + f"{key}=1\n", encoding="utf-8")
        single_out = work / f"isolated-b{build}.txt"
        single_out.unlink(missing_ok=True)
        proc = subprocess.run([str(dump), "--exe", str(exe), "--build", str(build),
                               str(single_out), str(single)],
                              capture_output=True, text=True, errors="replace")
        single_code = proc.returncode & 0xFFFFFFFF
        if single_code:
            faulted.add(key)
            continue
        if single_out.exists():
            for line in _read(single_out).splitlines():
                parts = line.split()
                if len(parts) == 2:
                    merged.append(f"{key} {parts[0]} {parts[1]}")
            single_out.unlink(missing_ok=True)
    if merged:
        existing = _read(out) if out.exists() else ""
        out.write_text(existing + "\n".join(merged) + "\n", encoding="utf-8")
    sidecar.write_text(json.dumps(sorted(faulted)), encoding="utf-8")
    return read_ours(out), faulted


def read_ours(path):
    rows = defaultdict(dict)
    if not path.exists():
        return rows
    for line in _read(path).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                rows[parts[0]][int(parts[1], 16)] = int(parts[2], 16)
            except ValueError:
                pass
    return rows


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

@dataclass
class KeyResult:
    key: str
    status: str                       # confirmed | different | recon_only | ours_only | uncovered
    recon: dict = field(default_factory=dict)
    ours: dict = field(default_factory=dict)
    differing: list = field(default_factory=list)   # (rva, recon byte, our byte, kind)
    branch_only: list = field(default_factory=list)
    missing: list = field(default_factory=list)     # recon bytes we do not write
    extra: list = field(default_factory=list)       # our bytes no recon table lists
    shared: list = field(default_factory=list)      # our bytes another key's recon set lists
    gating: list = field(default_factory=list)      # (rva, reason) we write where recon skipped
    notes: list = field(default_factory=list)
    tables: set = field(default_factory=set)


def compare_key(key, recon_bytes, ours_bytes, writes, skipped=(), shared=None):
    """Classify one key's predicted write set against our dump.

    `shared` maps an RVA to the set of keys whose recon set writes it; a byte of
    ours that no row of *this* key lists but another key's rows do is a shared
    install, not a divergence (the two trees attribute it to different keys).
    `skipped` is this key's guarded-out rows: writing where the reconstruction's
    own table says the original skips is a gating disagreement.
    """
    shared = shared or {}
    recon = dict(recon_bytes)
    ours = dict(ours_bytes)
    result = KeyResult(key=key, status="confirmed", recon=recon, ours=ours)
    result.tables = {w[2] for w in writes}
    if not recon and not ours:
        result.status = "uncovered"
        return result
    if not recon:
        result.status = "ours_only"
        return result
    if not ours:
        result.status = "recon_only"
        return result

    # Map a recon byte to the write kind that produced it.
    kinds = {}
    for rva, data, _table, kind in writes:
        for i in range(len(data)):
            kinds[rva + i] = kind

    overlap = set(recon) & set(ours)
    for rva in sorted(overlap):
        if recon[rva] == ours[rva]:
            continue
        kind = kinds.get(rva, "literal")
        if kind == "branch-module":
            # Both sides jump to a body in their own module. If our dump's
            # target is a registered transcribed body, its displacement was
            # canonicalised to the original ASI VA and should equal the
            # reconstruction's; a difference means the two tables name
            # different body VAs (or ours is a rebuilt body outside the
            # registry). Keep it apart from a literal byte divergence.
            result.branch_only.append((rva, recon[rva], ours[rva]))
            continue
        if kind == "module-pointer":
            continue        # both sides write a module pointer; the value differs by design
        result.differing.append((rva, recon[rva], ours[rva], kind))

    result.missing = sorted(set(recon) - set(ours))
    extra = sorted(set(ours) - set(recon))
    skipped_sites = {}
    for rva, table, reason in skipped:
        for i in range(8):
            skipped_sites.setdefault(rva + i, (table, reason))
    for rva in extra:
        if rva in skipped_sites:
            result.gating.append((rva, skipped_sites[rva][1]))
        elif shared.get(rva) and key not in shared[rva]:
            result.shared.append((rva, sorted(shared[rva])))
        else:
            result.extra.append(rva)

    if result.differing or result.missing or result.gating:
        result.status = "different"
    elif result.branch_only:
        # Sites and opcodes agree; only module-relative displacements differ.
        result.status = "confirmed"
    return result


# --------------------------------------------------------------------------
# Part 2: body table
# --------------------------------------------------------------------------

def parse_bodies():
    bodies = []
    for line in _read(ROOT / "tests" / "bodies.tsv").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "table":
            if len(parts) >= 4:
                bodies.append({"name": parts[1], "va": int(parts[1], 16),
                               "symbol": parts[2], "kind": "table"})
            continue
        if len(parts) >= 4:
            try:
                va = int(parts[1], 16)
            except ValueError:
                continue
            bodies.append({"name": parts[0], "va": va, "symbol": parts[2],
                           "kind": "body"})
    return bodies


def parse_body_targets():
    """Our key -> body VAs (tests/body_targets.tsv)."""
    out = defaultdict(set)
    for line in _read(ROOT / "tests" / "body_targets.tsv").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 3:
            out[parts[0]].add(int(parts[2], 16))
    return out


# --------------------------------------------------------------------------
# Part 3: the reconstruction's own suite
# --------------------------------------------------------------------------

def build_recon_suite(work):
    """Compile tests/test_main.cpp with g++ (MinGW) into work/."""
    exe = work / "zpatch_tests.exe"
    sources = [
        "config.cpp", "game.cpp", "log.cpp", "model_catalog.cpp", "vehicle_tables.cpp",
        "ide_flags.cpp", "vehicle_animation.cpp", "hook_bodies.cpp", "hook_install.cpp",
        "skip_launcher.cpp", "skip_launcher_win.cpp", "rebased_values.cpp",
        "rva_installs.cpp", "op_corrections.cpp", "hook_bodies_win.cpp", "patch.cpp",
        "patch_catalog.cpp", "patch_manifest.cpp", "pattern.cpp", "option_flags.cpp",
        "recovered_sites.cpp", "recovered_ops.cpp", "recovered_extra_signatures.cpp",
        "recovered_rva_jumps.cpp", "recovered_sig_jumps.cpp",
        "recovered_indirect_writes.cpp", "recovered_match_jumps.cpp",
        "recovered_literal_stores.cpp", "recovered_match_literals.cpp",
        "recovered_build_gates.cpp", "recovered_preamble.cpp", "preamble_apply.cpp",
        "recovered_apply.cpp", "game_image.cpp", "menu_integration.cpp",
        "number_format.cpp", "zmenu_guard.cpp", "useless_checks.cpp", "runtime.cpp",
    ]
    cmd = ["g++", "-Iinclude", "-std=c++17", "-O1", "-w"]
    cmd += [f"src/{name}" for name in sources]
    cmd += ["tests/test_main.cpp", "-lversion", "-o", str(exe)]
    proc = subprocess.run(cmd, cwd=str(RECON), capture_output=True, text=True,
                          errors="replace")
    return exe, proc


def run_suite(exe):
    proc = subprocess.run([str(exe)], cwd=str(RECON), capture_output=True,
                          text=True, errors="replace")
    return proc


def run_dump_writes(exe, ini, out):
    proc = subprocess.run([str(exe), "--dump-writes", str(out), str(ini)],
                          cwd=str(RECON), capture_output=True, text=True,
                          errors="replace")
    rows = {}
    if out.exists():
        for line in _read(out).splitlines():
            parts = line.split()
            if len(parts) == 2:
                rows[int(parts[0], 16)] = int(parts[1], 16)
    return proc, rows


GATE_CALL_RE = re.compile(
    r"CHECK\((?P<neg>!)?(?:zpatch::)?build_allows_option\((?:zpatch::)?GameBuild::"
    r"(?P<build>v\d+),\s*\"(?P<key>[^\"]+)\",\s*known\)\)")
RVA_GATE_RE = re.compile(
    r"CHECK\((?P<neg>!)?(?:zpatch::)?rva_build_allows\((?P<handler>0x[0-9a-fA-F]+)u?,\s*"
    r"(?P<rva>\d+)u?,\s*(?P<build>\d+)\)\)")
COVERS_RE = re.compile(
    r"CHECK\((?P<neg>!)?(?:zpatch::)?rva_install_covers\((?P<rva>\d+)u?,\s*(?P<build>\d+)\)\)")
GATE_COUNT_RE = re.compile(r"CHECK\(zpatch::recovered_build_gates\(gate_count\)\s*&&\s*"
                           r"gate_count==(\d+)\)")


def extract_assertions(text):
    """The cross-checkable CHECKs: gates, rva attribution and table counts."""
    assertions = {"option_gates": [], "rva_gates": [], "covers": [],
                  "gate_count": None, "lines": []}
    for m in GATE_CALL_RE.finditer(text):
        assertions["option_gates"].append(
            (m.group("key"), m.group("build"), not m.group("neg")))
        assertions["lines"].append(m.group(0))
    for m in RVA_GATE_RE.finditer(text):
        assertions["rva_gates"].append(
            (int(m.group("handler"), 16), int(m.group("rva")),
             int(m.group("build")), not m.group("neg")))
        assertions["lines"].append(m.group(0))
    for m in COVERS_RE.finditer(text):
        assertions["covers"].append(
            (int(m.group("rva")), int(m.group("build")), not m.group("neg")))
        assertions["lines"].append(m.group(0))
    m = GATE_COUNT_RE.search(text)
    if m:
        assertions["gate_count"] = int(m.group(1))
        assertions["lines"].append(m.group(0))
    return assertions


# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------

def build_report(only_builds, dump, work, skip_suite):
    keys = catalogue_keys()
    tables = Tables(RECON)
    report = {"builds": {}, "part2": {}, "part3": {}}

    print("part 1: install sets")
    for build in only_builds:
        exe = GAME / f"{BUILDS[build]}.exe"
        if not exe.exists():
            print(f"  build {build}: {exe.name} is missing")
            continue
        print(f"  build {build} ({VERSIONS[build]}): mapping {exe.name}")
        image = Image(exe)
        ours, faulted = run_ours(dump, exe, build,
                                 work / f"ours-b{build}-per.txt", work, keys)
        report.setdefault("faulted", {})[build] = sorted(faulted)
        scanner = Scanner(image)
        predictions = {}
        for key in keys:
            predictor = Predictor(tables, image, build, scanner).run(key)
            predictions[key] = predictor
        shared = {}
        for key, predictor in predictions.items():
            for rva in predictor.changed():
                shared.setdefault(rva, set()).add(key)
        per_key = {}
        for key in keys:
            predictor = predictions[key]
            recon_bytes = predictor.changed()
            per_key[key] = compare_key(key, recon_bytes, ours.get(key, {}),
                                       predictor.write_spans(),
                                       predictor.skipped, shared)
            per_key[key].notes = predictor.notes
        report["builds"][build] = per_key
        counts = Counter(r.status for r in per_key.values())
        print(f"    {len(keys)} keys: " + ", ".join(
            f"{name} {counts.get(name, 0)}" for name in
            ("confirmed", "different", "recon_only", "ours_only", "uncovered")))

    print("part 2: body table")
    bodies = parse_bodies()
    our_targets = parse_body_targets()
    va_to_ours = defaultdict(set)
    for key, vas in our_targets.items():
        for va in vas:
            va_to_ours[va].add(key)
    recon_bodies = tables.hook_targets
    part2 = {"rows": [], "disagreements": [], "ours_silent": [], "uncovered": []}
    for body in bodies:
        va = body["va"]
        recon_keys = sorted(set(recon_bodies.get(va, [])))
        ours_keys = sorted(va_to_ours.get(va, []))
        if body["kind"] == "table":
            agree = "not a body"
        elif not recon_keys and not ours_keys:
            agree = "both silent"
        elif not recon_keys:
            agree = "reconstruction silent"
        elif not ours_keys:
            agree = "ours silent"
        elif set(recon_keys) & set(ours_keys):
            agree = "agree"
        else:
            agree = "disagree"
        row = {"name": body["name"], "va": va, "kind": body["kind"],
               "ours": ours_keys, "recon": recon_keys, "agree": agree}
        part2["rows"].append(row)
        if agree == "disagree":
            part2["disagreements"].append(row)
        elif agree == "ours silent":
            part2["ours_silent"].append(row)
        elif agree == "reconstruction silent":
            part2["uncovered"].append(row)
    report["part2"] = part2
    print(f"    {len(part2['rows'])} row(s): "
          f"{len(part2['disagreements'])} disagreement(s), "
          f"{len(part2['ours_silent'])} with no key in our body_targets.tsv, "
          f"{len(part2['uncovered'])} not in the reconstruction's body map")

    print("part 3: reconstruction suite")
    part3 = {"built": False, "suite": None, "dump_writes": None,
             "assertions": None, "assertion_results": []}
    if not skip_suite:
        work.mkdir(parents=True, exist_ok=True)
        exe, proc = build_recon_suite(work)
        part3["build_ok"] = proc.returncode == 0
        part3["build_output"] = (proc.stdout + proc.stderr)[-4000:]
        if proc.returncode == 0:
            suite = run_suite(exe)
            part3["built"] = True
            part3["suite"] = {"exit": suite.returncode,
                              "stderr": suite.stderr[-4000:],
                              "stdout": suite.stdout[-4000:]}
            print(f"    suite exit {suite.returncode}"
                  + (f": {suite.stderr.strip().splitlines()[-1]}"
                     if suite.stderr.strip() else ""))
            # --dump-writes on the shipped INI (build 9 reference).
            ini = GAME / "ZolikaPatch.ini"
            if ini.exists():
                proc, rows = run_dump_writes(exe, ini, work / "recon-b9-full.txt")
                part3["dump_writes"] = {
                    "exit": proc.returncode,
                    "stderr": proc.stderr[-2000:],
                    "rows": len(rows),
                }
                print(f"    --dump-writes: exit {proc.returncode}, {len(rows)} byte(s)")
        text = _read(RECON / "tests" / "test_main.cpp")
        assertions = extract_assertions(text)
        part3["assertions"] = assertions
        # Evaluate the extracted assertions against the tables directly.
        for key, build_name, expected in assertions["option_gates"]:
            build = VERSION_TO_ORDINAL.get(
                int(build_name[1:]) if build_name.startswith("v") else 0, 0)
            actual = Predictor(tables, None, build).option_gate(key)
            part3["assertion_results"].append(
                ("option_gate", key, build, expected, actual, expected == actual))
        for handler, rva, build, expected in assertions["rva_gates"]:
            actual = Predictor(tables, None, build).rva_build_allows(handler, rva)
            part3["assertion_results"].append(
                ("rva_gate", hex(handler), rva, build, expected, actual,
                 expected == actual))
        for rva, build, expected in assertions["covers"]:
            actual = any(i.build in (0, build) and i.kind not in (3, 5)
                         and i.site_rva == rva for i in tables.rva_installs)
            part3["assertion_results"].append(
                ("covers", rva, build, expected, actual, expected == actual))
        if assertions["gate_count"] is not None:
            actual = len(tables.build_gates)
            part3["assertion_results"].append(
                ("gate_count", "-", assertions["gate_count"], actual,
                 assertions["gate_count"] == actual))
    report["part3"] = part3
    return report


def print_details(report):
    for build, per_key in report["builds"].items():
        print(f"\n=== build {build} ({VERSIONS[build]}) ===")
        for key, result in per_key.items():
            if result.status == "confirmed":
                continue
            if result.status == "uncovered":
                continue
            if result.status == "different":
                print(f"  {key}: DIFFERENT ({len(result.differing)} differing byte(s), "
                      f"{len(result.missing)} recon byte(s) we do not write, "
                      f"{len(result.gating)} gating)")
                for rva, want, have, kind in result.differing[:6]:
                    print(f"    RVA {rva:06X}: recon {want:02X}, ours {have:02X} ({kind})")
                for rva in result.missing[:4]:
                    print(f"    RVA {rva:06X}: recon {result.recon[rva]:02X}, ours --")
                for rva, reason in result.gating[:4]:
                    print(f"    RVA {rva:06X}: recon skips ({reason}), ours {result.ours[rva]:02X}")
            elif result.status == "recon_only":
                print(f"  {key}: recon lists {len(result.recon)} byte(s), we write none")
            elif result.status == "ours_only":
                print(f"  {key}: we write {len(result.ours)} byte(s), recon lists none")
            if result.extra:
                print(f"    (+{len(result.extra)} byte(s) no recon table lists)")
            if result.shared:
                print(f"    (+{len(result.shared)} byte(s) another key's recon rows list)")
    print("\n=== part 2 disagreements ===")
    for row in report["part2"]["disagreements"]:
        print(f"  {row['name']} {row['va']:08X}: ours={row['ours']} "
              f"recon={row['recon']} ({row['agree']})")
    print("\n=== part 3 assertions ===")
    for item in report["part3"].get("assertion_results", []):
        if not item[-1]:
            print(f"  MISMATCH {item}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", type=int, help="one build ordinal")
    parser.add_argument("--no-suite", action="store_true",
                        help="skip part 3 (the reconstruction build)")
    parser.add_argument("--json", help="write the structured result here")
    parser.add_argument("--work", default=str(ROOT / ".scratch" / "crosscheck"))
    parser.add_argument("--dump", default=str(ROOT / "build" / "win-x86" /
                                              "Release" / "zp_dump.exe"))
    args = parser.parse_args()
    dump = Path(args.dump)
    if not dump.exists():
        raise SystemExit(f"zp_dump is missing: {dump}")
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    builds = [args.only] if args.only else sorted(BUILDS)
    started = time.time()
    report = build_report(builds, dump, work, args.no_suite)
    print_details(report)
    print(f"\ncrosscheck finished in {time.time() - started:.1f}s")
    if args.json:
        def clean(value):
            if isinstance(value, dict):
                return {str(k): clean(v) for k, v in value.items()}
            if isinstance(value, (list, tuple)):
                return [clean(v) for v in value]
            if isinstance(value, set):
                return sorted(clean(v) for v in value)
            if hasattr(value, "__dict__"):
                return clean(vars(value))
            return value
        Path(args.json).write_text(json.dumps(clean(report), indent=1),
                                   encoding="utf-8")
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
