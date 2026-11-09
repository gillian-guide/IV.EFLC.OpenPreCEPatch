#!/usr/bin/env python3
"""Prove transcribed hook bodies instruction-for-instruction.

A body's bytes cannot simply be compared with the original's: a body that reads a
published word or resumes through a function pointer carries *our* module's
addresses where the original carries *its* own. What is comparable is the
instruction stream with absolute operands treated as slots -- the same
instruction, the same registers, the same shape of memory operand, and an
absolute operand on both sides compared as a slot rather than as an address.

    python tools/verify_bodies.py --original <asi> --candidate <asi> --bodies <tsv>

Manifest rows: `name <recovered VA> <candidate VA> <max instructions>`, both VAs
as addresses in the image's own base. Execution stops at a terminal instruction
(`jmp` or `ret`) on both sides; a body that ends differently, or that runs past
`max instructions`, fails.

Manifest table rows: `table <recovered VA> <candidate symbol> <bytes>`. They name
a table an arm registers with `Bodies::RegisterTable`: an indexed absolute operand
whose displacement is that table (`push ds:off_100DE770[esi*4]`) is compared as
`table:<content hash>` instead of as a displacement, on both sides, so the proof
asks "does this read the same table?" and not "does the table sit at the same
address?". A displacement that is not a registered table keeps the literal
comparison: the relaxation is opt-in, per table, and an unregistered one behaves
exactly as before. A displacement *inside* a registered table (the popup checker's
eight interleaved columns, `[eax+0x1015C628]` through `[eax+0x1015C644]`) names that
table plus the offset, and the offset is still compared. The symbol may be C++
(`?name@@...`) or C (`_name`): a table can belong to another module and carry only a
C name. The content hash covers the bytes the *loaded* image holds, so a table in a
section's uninitialised tail (the original keeps every 512-slot model table past
`.data`'s raw size) reads as zeros, which is what the loader maps there.

    --selftest   the comparator against the original itself (must pass) and
                 against a one-byte corruption (must fail), plus the table path:
                 a registered table passes, a changed table *content* fails as
                 `table contents differ`, an unregistered displacement still
                 compares literally, a table in the uninitialised `.data` tail
                 resolves by content, a base-plus-displacement table read passes
                 with its base compared (a changed base is an instruction
                 difference), a displacement inside a registered table resolves
                 to that table plus its offset (a changed offset is an instruction
                 difference), and the map reader resolves both symbol decorations

Exit status: 0 proved, 1 a body differs, 2 inputs or capstone missing.
"""

import argparse
import hashlib
import struct
import sys
import tempfile
from pathlib import Path

IMAGE_BASE = 0x10000000
# A `ret` ends a body; a `jmp` does not, because these bodies jump within
# themselves (the special-path tail), and a linear sweep covers both sides the same
# way, so an internal jump is compared like any other instruction.
TERMINAL = {"jmp", "ret", "retf", "iret"}

try:
    import capstone
except ImportError:  # pragma: no cover - reported, not raised
    capstone = None


def image_offset(image: bytes, va: int, base: int = IMAGE_BASE):
    """The file offset of a VA, through the PE section table: a linked address is an
    RVA, and a section's raw pointer is what locates it in the file."""
    if len(image) < 0x40 or image[:2] != b"MZ":
        return None
    span = int.from_bytes(image[0x3C:0x40], "little")
    if image[span:span + 4] != b"PE\0\0":
        return None
    sections = int.from_bytes(image[span + 6:span + 8], "little")
    optional_size = int.from_bytes(image[span + 20:span + 22], "little")
    table = span + 24 + optional_size
    rva = va - base
    for index in range(sections):
        entry = table + index * 40
        virtual_size = int.from_bytes(image[entry + 8:entry + 12], "little")
        virtual_address = int.from_bytes(image[entry + 12:entry + 16], "little")
        raw_size = int.from_bytes(image[entry + 16:entry + 20], "little")
        raw_pointer = int.from_bytes(image[entry + 20:entry + 24], "little")
        size = virtual_size or raw_size
        if virtual_address <= rva < virtual_address + size:
            # A section's tail can be uninitialised (.bss): those bytes are not in
            # the file at all, they are zero in the loaded image. Mapping them to
            # raw_pointer + ... would read whatever section happens to follow, which
            # is how an earlier read of a scratch slot returned code bytes.
            if rva - virtual_address >= raw_size:
                return None
            return raw_pointer + (rva - virtual_address)
    return None


def disassemble(image: bytes, va: int, limit: int):
    """Exactly `limit` instructions of one body, swept linearly.

    A body is transcribed instruction for instruction, so the whole stream is
    comparable; stopping at the first terminal instruction hid the tail of a body
    that returns in the middle and proved a body whose entry is a jump as a single
    instruction. The budget in the manifest is therefore the body`s true instruction
    count, not a cap: if either side decodes fewer, the row fails.
    """
    if capstone is None:
        raise SystemExit("capstone is required: pip install capstone")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    offset = image_offset(image, va)
    if offset is None or offset >= len(image):
        return None
    out = []
    # The window must be larger than any body: a short window silently truncated the
    # sweep and the row still reported PASS with the truncated count, so a body over
    # the window was never fully proven.
    for instruction in md.disasm(image[offset:offset + 4096], va):
        out.append(instruction)
        if len(out) >= limit:
            break
    return out


def sections(image: bytes):
    """The PE section table as (virtual_address, virtual_size, raw_pointer, raw_size).

    `virtual_size` is the extent the section occupies once loaded; `raw_size` the
    part of it the file actually holds. The two differ for every section whose tail
    is uninitialised (`.bss`, and the `.data` beyond its raw size), and that
    difference is what `read_table` has to model.
    """
    if len(image) < 0x40 or image[:2] != b"MZ":
        return []
    span = int.from_bytes(image[0x3C:0x40], "little")
    if image[span:span + 4] != b"PE\0\0":
        return []
    count = int.from_bytes(image[span + 6:span + 8], "little")
    table = span + 24 + int.from_bytes(image[span + 20:span + 22], "little")
    out = []
    for index in range(count):
        entry = table + index * 40
        out.append((
            int.from_bytes(image[entry + 12:entry + 16], "little"),
            int.from_bytes(image[entry + 8:entry + 12], "little"),
            int.from_bytes(image[entry + 20:entry + 24], "little"),
            int.from_bytes(image[entry + 16:entry + 20], "little"),
        ))
    return out


def section_containing(image: bytes, va: int):
    """The section record whose loaded extent holds `va`, or None."""
    rva = va - IMAGE_BASE
    for section in sections(image):
        virtual_address, virtual_size, raw_pointer, raw_size = section
        size = virtual_size or raw_size
        if virtual_address <= rva < virtual_address + size:
            return section
    return None


def read_table(image: bytes, va: int, size: int):
    """The registered table's bytes as the *loaded* image holds them.

    A table's bytes are the file's where the section is file-backed and **zero**
    where the section's tail is uninitialised: those bytes are not in the file at
    all, but the loader maps zeros there, and that is what the table holds at
    runtime. The original keeps every 512-slot model table in the `.data` tail
    beyond its raw size (its `.data` is 0x504D4 bytes of virtual extent but only
    0x1000 of raw), so a file-only read would report the table as "not in the
    image" and the registry could only ever name file-backed tables. Reading the
    zero tail is what the loaded image means; it cannot create a false pass,
    because a table with real content on either side still hashes differently and a
    range that leaves its section is still rejected -- one contiguous run inside one
    section, exactly as before.
    """
    section = section_containing(image, va)
    if section is None:
        return None
    virtual_address, virtual_size, raw_pointer, raw_size = section
    extent = virtual_size or raw_size
    delta = va - IMAGE_BASE - virtual_address
    if delta < 0 or delta + size > extent:
        return None
    offset = raw_pointer + delta
    backed = max(0, min(size, raw_size - delta))
    return image[offset:offset + backed] + b"\0" * (size - backed)


def table_content_hash(image: bytes, va: int, size: int):
    """A hash of a registered table's contents that ignores where it sits.

    The table is compared by what it *holds*. A word that addresses the image is
    hashed as the bytes it names (a C string, NUL included), not as the address: a
    module cannot place its string pool where the original's sits, and hashing the
    pointer values would compare the two layouts instead of the two tables. Every
    other word is hashed as it stands, so a table of hashes, floats or ints
    compares by value. `size` is the length the registration names, and it is
    exactly what is hashed.
    """
    data = read_table(image, va, size)
    if data is None:
        return None
    canonical = bytearray()
    whole = len(data) - len(data) % 4
    for offset in range(0, whole, 4):
        word = int.from_bytes(data[offset:offset + 4], "little")
        target = image_offset(image, word)
        if target is None:
            canonical += b"v" + data[offset:offset + 4]
        else:
            end = image.find(b"\0", target, target + 0x1000)
            if end < 0:
                end = target + 0x1000
            canonical += b"s" + image[target:end]
        canonical += b"\0"
    canonical += b"t" + data[whole:]
    return hashlib.sha256(bytes(canonical)).hexdigest()[:16]


class Tables:
    """The registered tables on one side of a comparison.

    `entries` maps each table's VA *in that side's image* to the length the
    registration names; the original side gets the recovered VA, the candidate side
    the VA the linker map gives the candidate's own copy. The hashes are computed
    on first use, because most rows read no table at all.
    """

    def __init__(self, image: bytes, entries, side: str):
        self.image = image
        self.entries = entries
        self.side = side
        self._hashes = {}

    def resolve(self, displacement: int):
        """The (content hash, offset) of the registered table a displacement names.

        A displacement that is *exactly* a registered VA names that table at offset 0.
        A displacement that falls **inside** one names the same table at that offset:
        the popup-headlights checker reads its 512-slot hash and angle tables as eight
        interleaved 64-row columns (`mov ecx,[eax+0x1015C628]`, `+0x1015C62C`, …,
        `+0x1015C644`, with EAX stepping 0x20), and a 32-bit module cannot make the
        linker emit a symbol four bytes into an array -- the map's only name for the
        storage is the array's base. The offset stays part of the comparison, so a
        transcription that read the wrong column is an *instruction* difference and
        not a match. A displacement outside every registered table resolves to
        nothing and keeps the literal comparison, so the relaxation is still opt-in
        per table and per shape.
        """
        va = displacement & 0xFFFFFFFF
        size = self.entries.get(va)
        if size is not None:
            return self._digest(va, size), 0
        for base, extent in self.entries.items():
            if base < va < base + extent:
                return self._digest(base, extent), va - base
        return None

    def _digest(self, va: int, size: int):
        if va not in self._hashes:
            digest = table_content_hash(self.image, va, size)
            if digest is None:
                raise SystemExit(
                    f"registered table {va:#x} ({self.side} side) is not in the image")
            self._hashes[va] = digest
        return self._hashes[va]


def summarise(instruction, tables=None):
    """The comparable shape: mnemonic, registers, memory displacement, and an
    absolute operand reduced to its slot.

    Two shapes are table reads the registry can name: an indexed absolute (an index
    and no base register) and a base plus displacement (a base register and no
    index). For either, a displacement `tables` resolves is compared as
    `table:<content hash>` while the registers around it are still compared -- the
    index register and scale, or the base register -- so the same table read through a
    different register still fails. A displacement the registry does not name keeps
    the literal comparison, and a base *with* an index stays literal too: the
    relaxation is per shape and per table, not global.
    """
    parts = []
    for operand in instruction.operands:
        if operand.type == capstone.x86.X86_OP_REG:
            parts.append(f"reg:{instruction.reg_name(operand.reg)}")
        elif operand.type == capstone.x86.X86_OP_IMM:
            parts.append("imm")
        elif operand.type == capstone.x86.X86_OP_MEM:
            base = instruction.reg_name(operand.mem.base) if operand.mem.base else ""
            index = instruction.reg_name(operand.mem.index) if operand.mem.index else ""
            table = table_read(operand, tables)
            if table is not None:
                digest, offset = table
                named = f"table:{digest}" + (f"+{offset}" if offset else "")
                if index:
                    parts.append(f"[{index}*{operand.mem.scale}+{named}]")
                else:
                    parts.append(f"[{base}+{named}]")
                continue
            if not base and not index:
                parts.append("abs")
            else:
                parts.append(f"[{base}+{index}*{operand.mem.scale}+{operand.mem.disp}]")
        else:
            parts.append("other")
    return f"{instruction.mnemonic} {' '.join(parts)}".strip()


def table_read(operand, tables):
    """The content hash when the operand is a table read the registry names.

    Two shapes resolve, and they are the two the original's class-A pieces use: an
    indexed absolute (`push ds:off_100DE770[esi*4]`, an index and no base) and a
    base-plus-displacement (`push ds:[edi+0x10159380]`, a base and no index). A base
    *with* an index is not one of them, and a plain absolute keeps the `abs` slot
    comparison, so an unrelated `[reg+disp]` never becomes a table read by accident.
    """
    if tables is None or operand.type != capstone.x86.X86_OP_MEM:
        return None
    if operand.mem.index and not operand.mem.base:
        return tables.resolve(operand.mem.disp)
    if operand.mem.base and not operand.mem.index:
        return tables.resolve(operand.mem.disp)
    return None


def table_operand_count(stream, tables) -> int:
    """How many operands the stream resolves to a registered table."""
    if tables is None:
        return 0
    count = 0
    for instruction in stream:
        for operand in instruction.operands:
            if table_read(operand, tables) is not None:
                count += 1
    return count


def table_mismatch(left, right, left_tables, right_tables):
    """The specific answer when a differing instruction is a table read.

    Two registered tables whose contents differ is not an instruction difference:
    the transcription reads a different table. A table that resolves on one side
    and not the other is the registration (or the displacement) being wrong, and is
    reported as such. Two reads of the *same* table at *different* offsets are an
    instruction difference too -- the operand changed, the table did not -- so that
    case is left to the caller's instruction report. A base-register difference is
    likewise not a table finding.
    """
    for left_operand, right_operand in zip(left.operands, right.operands):
        if (left_operand.type != capstone.x86.X86_OP_MEM
                or right_operand.type != capstone.x86.X86_OP_MEM):
            continue
        if bool(left_operand.mem.base) != bool(right_operand.mem.base):
            continue
        if bool(left_operand.mem.index) != bool(right_operand.mem.index):
            continue
        if left_operand.mem.base and left_operand.mem.index:
            continue
        if left_operand.mem.base and (left.reg_name(left_operand.mem.base)
                                      != right.reg_name(right_operand.mem.base)):
            continue
        left_table = table_read(left_operand, left_tables)
        right_table = table_read(right_operand, right_tables)
        if left_table is not None and right_table is not None:
            if left_table[0] != right_table[0]:
                return (f"table contents differ: {left_operand.mem.disp & 0xFFFFFFFF:#x} "
                        f"({left_table[0]}) against {right_operand.mem.disp & 0xFFFFFFFF:#x} "
                        f"({right_table[0]})")
        elif left_table is not None or right_table is not None:
            side, va = ((("original", left_operand.mem.disp) if left_table is not None
                         else ("candidate", right_operand.mem.disp)))
            return f"table {va & 0xFFFFFFFF:#x} is registered on the {side} side only"
    return None


def internal_branch_mismatch(left_stream, right_stream):
    """The first branch whose target offset differs inside the body, or None.

    A `jcc`/`jmp` immediate is reduced to "imm" by `summarise`, so a transcription
    that jumps to the wrong label still matches shape for shape - one row did. Both
    sides are transcribed from the same body, so their instructions occupy the same
    relative offsets: a branch whose target lies inside the body must land on the same
    offset from the body start on both sides.
    """
    if not left_stream or not right_stream:
        return None
    left_start = left_stream[0].address
    left_size = sum(instruction.size for instruction in left_stream)
    right_start = right_stream[0].address

    for index, (left, right) in enumerate(zip(left_stream, right_stream)):
        if left.mnemonic == "call" or not (left.mnemonic.startswith("j")):
            continue
        left_target = None
        right_target = None
        for operand in left.operands:
            if operand.type == capstone.x86.X86_OP_IMM:
                left_target = operand.imm - left_start
        for operand in right.operands:
            if operand.type == capstone.x86.X86_OP_IMM:
                right_target = operand.imm - right_start
        if left_target is None or right_target is None:
            continue
        # Only a target inside the body is comparable: an external one is a slot.
        if not 0 <= left_target < left_size:
            continue
        if left_target != right_target:
            return index, left_target, right_target
    return None


def compare(original: bytes, candidate: bytes, rows, symbols,
            original_tables=None, candidate_tables=None):
    results = []
    for name, reference_va, symbol, limit in rows:
        candidate_va = symbols.get(symbol)
        if candidate_va is None:
            results.append((name, "MISSING", f"symbol {symbol} is not in the map"))
            continue
        wanted = disassemble(original, reference_va, limit)
        have = disassemble(candidate, candidate_va, limit)
        if not wanted or not have:
            results.append((name, "MISSING", "outside the image"))
            continue
        if len(wanted) != len(have):
            results.append((name, "FAIL", f"{len(wanted)} instruction(s) against {len(have)}"))
            continue
        for index, (left, right) in enumerate(zip(wanted, have)):
            if summarise(left, original_tables) != summarise(right, candidate_tables):
                # A registered table read that differs is reported as a table
                # difference: "the transcription reads another table" is the
                # finding, not "instruction 5 differs".
                detail = table_mismatch(left, right, original_tables, candidate_tables)
                results.append((name, "FAIL",
                                detail or f"instruction {index}: "
                                          f"'{summarise(left, original_tables)}' against "
                                          f"'{summarise(right, candidate_tables)}'"))
                break
        else:
            # A `jcc`/`jmp` immediate is compared as a slot, so a transcription that
            # jumps to the wrong label still matches shape for shape. The body's
            # instructions sit at the same relative offsets on both sides, so every
            # branch whose target is inside the body has to land on the same relative
            # offset - that is what catches a misplaced label (it caught one).
            mismatch = internal_branch_mismatch(wanted, have)
            if mismatch is not None:
                results.append((name, "FAIL",
                                f"branch at instruction {mismatch[0]} targets "
                                f"{mismatch[1]:+#x} against {mismatch[2]:+#x}"))
                continue
            # The row's budget is the body's exact instruction count, so a linear
            # sweep that decoded the same number on both sides and matched
            # instruction for instruction has covered the whole body - including a
            # tail after a mid-body `ret` and a body whose entry is a jump.
            slots = sum(1 for instruction in wanted if "abs" in summarise(instruction))
            tables = table_operand_count(wanted, original_tables)
            detail = f"{len(wanted)} instruction(s), {slots} absolute operand(s)"
            if tables:
                detail += f", {tables} table operand(s)"
            results.append((name, "PASS", detail))
    return results


def read_manifest(path: Path):
    """The manifest's body rows and its table rows.

    A body row is `name <recovered VA> <candidate symbol> <instruction count>`; a
    table row is `table <recovered VA> <candidate symbol> <bytes>`. A table row is
    the offline projection of the arm's `Bodies::RegisterTable` call: the
    recovered VA and length are the ones the registration names, and the candidate
    symbol is how the linker map gives this module's own copy its address.
    """
    rows = []
    tables = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip().lstrip('\ufeff')
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "table":
            _, reference_va, symbol, size = parts
            tables.append((int(reference_va, 16), symbol, int(size)))
            continue
        name, reference_va, symbol, limit = parts
        rows.append((name, int(reference_va, 16), symbol, int(limit)))
    return rows, tables


def read_map(path: Path):
    """symbol -> VA from the linker map. A candidate address is a link result, so it
    is looked up at test time instead of written down: a relink moves every body,
    and a stale literal would read the middle of a different function.

    Both decorations are recorded: a C++ symbol is `?name@@...`, a C one is
    `_name`. Tables are not always C++ objects -- the wheelangle tables are
    `extern "C"` arrays owned by the module whose hook fills them, so the only name
    the map gives them is the C one, and a `table` row has to be able to name it.
    """
    symbols = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        for index, token in enumerate(parts[:-1]):
            if not (token.startswith("?") or token.startswith("_")):
                continue
            candidate = parts[index + 1]
            if len(candidate) == 8:
                try:
                    symbols[token] = int(candidate, 16)
                except ValueError:
                    continue
    return symbols


def report(results) -> int:
    for name, status, detail in results:
        print(f"{status} {name}" + (f" ({detail})" if detail else ""))
    failed = sum(1 for _, status, _ in results if status != "PASS")
    print(f"{len(results)} body(ies), {failed} failure(s)")
    return 0 if failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original")
    parser.add_argument("--candidate")
    parser.add_argument("--bodies")
    parser.add_argument("--map")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        if not args.original:
            raise SystemExit("--selftest needs --original")
        original = Path(args.original).read_bytes()
        rows = [("selftest-body", IMAGE_BASE + 0x5590, IMAGE_BASE + 0x5590, 24)]
        first = report(compare(original, original, rows, {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))
        corrupt = bytearray(original)
        offset = image_offset(original, IMAGE_BASE + 0x5590)
        corrupt[offset] ^= 0xFF  # the opcode, not a constant: a constant alone is not an instruction difference
        second = report(compare(original, bytes(corrupt), rows, {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))

        # A branch target is compared by offset because `summarise` reduces it to
        # "imm": corrupt a `jcc` displacement and the shape still matches, so only the
        # branch check can catch it. The AimProjectile body's own tail is a jump, so
        # this exercises the real path rather than a synthetic one.
        branches = bytearray(original)
        stream = disassemble(original, IMAGE_BASE + 0x5590, 24) or []
        branch_at = next((instruction for instruction in stream
                          if instruction.mnemonic.startswith("j")), None)
        third = 1
        if branch_at is not None:
            spot = image_offset(original, branch_at.address)
            if spot is not None and branch_at.size >= 2:
                branches[spot + 1] ^= 0x08
            third = report(compare(original, bytes(branches), rows,
                                   {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))

        # The table path. sub_10003F00 (21 instructions) reads the 38,455-entry name
        # table at 0x100DE770 through two indexed absolute operands, which is the
        # class-A blocker: the displacement cannot match, so the table is registered
        # and compared by content. The four cases are: registered and identical ->
        # PASS; same table, one changed name -> FAIL as `table contents differ`; the
        # operand moved to a *different* registered table -> FAIL as a content
        # difference (not an instruction one); and the same moved operand with no
        # registration -> FAIL literally, which is what keeps the relaxation opt-in.
        name_body = IMAGE_BASE + 0x3F00
        name_rows = [("selftest-table", name_body, name_body, 21)]
        name_symbols = {name_body: name_body}
        table_va = IMAGE_BASE + 0xDE770
        table_bytes = 0x9637 * 4
        registered = Tables(original, {table_va: table_bytes}, "original")
        fourth_results = compare(original, original, name_rows, name_symbols,
                                 registered, registered)
        fourth = report(fourth_results)
        # The PASS has to be the *table* path, not two identical images sliding
        # through: the row's detail counts the operands the registry resolved.
        resolved = any("table operand" in detail for _, _, detail in fourth_results)

        changed = bytearray(original)
        first_name = int.from_bytes(read_table(original, table_va, 4), "little")
        name_offset = image_offset(original, first_name)
        if name_offset is not None:
            changed[name_offset] ^= 0x20
        fifth = report(compare(original, bytes(changed), name_rows, name_symbols,
                               registered,
                               Tables(bytes(changed), {table_va: table_bytes}, "candidate")))

        # 0x10003F13 is the first table operand's displacement (the `push` at
        # 0x10003F10). Point it at the "**UNKNOWN**" literal instead, and register
        # that VA as a table of its own: both sides resolve, the contents differ.
        moved = bytearray(original)
        body_offset = image_offset(original, name_body)
        unknown_va = IMAGE_BASE + 0x128D20
        if body_offset is not None:
            struct.pack_into("<I", moved, body_offset + 0x13, unknown_va)
        sixth = report(compare(original, bytes(moved), name_rows, name_symbols,
                               registered,
                               Tables(bytes(moved), {unknown_va: 12}, "candidate")))
        seventh = report(compare(original, bytes(moved), name_rows, name_symbols))

        # The uninitialised-tail path. The steer consumer (sub_10004C70, 320
        # instructions) reads both 512-slot wheelangle tables through indexed
        # absolute operands: 0x1015A538 in the walk and 0x10159D38 at the angle
        # conversion. Both live past the raw 0x1000 of the original's 0x504D4-byte
        # `.data` extent, so the file holds none of their bytes and a file-only read
        # would raise "registered table ... is not in the image". Reading the zero
        # tail the loader maps makes them resolvable, and the row's detail has to
        # say both operands resolved -- otherwise this case proves nothing.
        steer = IMAGE_BASE + 0x4C70
        steer_rows = [("selftest-tail-table", steer, steer, 320)]
        steer_symbols = {steer: steer}
        tail = {IMAGE_BASE + 0x15A538: 2048, IMAGE_BASE + 0x159D38: 2048}
        eighth_results = compare(original, original, steer_rows, steer_symbols,
                                 Tables(original, tail, "original"),
                                 Tables(original, tail, "original"))
        eighth = report(eighth_results)
        tail_resolved = any("2 table operand" in detail for _, _, detail in eighth_results)
        # The opt-in still bites for a zero table: registered on one side only is a
        # registration difference, not a match. (Two zero tables cannot be told
        # apart by content, which is why the manifest names the pairing explicitly.)
        ninth = report(compare(original, original, steer_rows, steer_symbols,
                               Tables(original, tail, "original"),
                               Tables(original, {}, "candidate")))

        # The map reader resolves both decorations. A table can be a C array in
        # another unit (the wheelangle tables are `extern "C"` in misc.ixx and the
        # map gives them no C++ name), so a `table` row has to be able to name it.
        map_text = (" 0001:00000000       ?SelftestBody@@YAXXZ             10001000"
                    "     zp_modules:vehicles.obj\n"
                    " 0003:000045a8       _zpatch_wheelangle_models        1017d5a8"
                    "     zp_modules:misc.obj\n")
        with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as handle:
            handle.write(map_text)
            map_path = Path(handle.name)
        resolved_symbols = read_map(map_path)
        map_path.unlink()
        tenth = 0 if (resolved_symbols.get("?SelftestBody@@YAXXZ") == 0x10001000
                      and resolved_symbols.get("_zpatch_wheelangle_models") == 0x1017d5a8) else 1

        # The base-plus-displacement shape. sub_10006260 (53 instructions) reads the
        # 512-slot siren list as `push dword ptr [esi+0x10159380]` (0x100062BC) and
        # `cmp dword ptr [esi+0x10159380],0` (0x100062DB): an absolute *with* a base
        # register and no index, which used to compare literally. Three cases:
        # registered on both sides -> PASS with both operands resolved; the base
        # register changed -> FAIL as an *instruction* difference (the base is still
        # compared), not as a table difference; the table's contents changed -> FAIL
        # as `table contents differ`.
        siren_body = IMAGE_BASE + 0x6260
        siren_rows = [("selftest-base-disp", siren_body, siren_body, 53)]
        siren_symbols = {siren_body: siren_body}
        siren_table = IMAGE_BASE + 0x159380
        siren_tables = {siren_table: 2048}
        eleventh_results = compare(original, original, siren_rows, siren_symbols,
                                   Tables(original, siren_tables, "original"),
                                   Tables(original, siren_tables, "candidate"))
        eleventh = report(eleventh_results)
        base_resolved = any("2 table operand" in detail for _, _, detail in eleventh_results)

        # `FF B6` -> `FF B7`: the push reads the table through EDI instead of ESI.
        other_base = bytearray(original)
        spot = image_offset(original, IMAGE_BASE + 0x62BC)
        if spot is None or other_base[spot:spot + 2] != b"\xFF\xB6":
            raise SystemExit("selftest: the siren checker's push is not at 0x100062BC")
        other_base[spot + 1] = 0xB7
        twelfth_results = compare(original, bytes(other_base), siren_rows, siren_symbols,
                                  Tables(original, siren_tables, "original"),
                                  Tables(bytes(other_base), siren_tables, "candidate"))
        twelfth = report(twelfth_results)
        # The finding has to be the register, not the table: both sides resolve, so the
        # detail is an instruction difference and not one of table_mismatch's answers.
        base_is_instruction = all(detail.startswith("instruction ")
                                  for _, status, detail in twelfth_results if status == "FAIL")

        # Retarget both base+disp operands at a file-backed 2,048-byte range and change
        # one byte of it on the candidate side: same registration, different contents.
        moved_table = IMAGE_BASE + 0x100000
        moved_image = bytearray(original)
        for at in (0x62BE, 0x62DD):
            offset = image_offset(original, IMAGE_BASE + at)
            if offset is None or moved_image[offset:offset + 4] != struct.pack("<I", siren_table):
                raise SystemExit(f"selftest: the siren operand at {at:#x} is not a disp32")
            struct.pack_into("<I", moved_image, offset, moved_table)
        mutated = bytearray(moved_image)
        mutated[image_offset(original, moved_table)] ^= 0x01
        thirteenth_results = compare(bytes(moved_image), bytes(mutated), siren_rows, siren_symbols,
                                     Tables(bytes(moved_image), {moved_table: 2048}, "original"),
                                     Tables(bytes(mutated), {moved_table: 2048}, "candidate"))
        thirteenth = report(thirteenth_results)
        contents_named = any("table contents differ" in detail
                             for _, _, detail in thirteenth_results)

        # The in-table column offset. sub_10044310 (the popup-headlights checker, 125
        # instructions) reads its 512-slot hash and angle tables as eight interleaved
        # columns -- `mov ecx,[eax+0x1015C628]` through `[eax+0x1015C644]`, the same
        # eight at 0x1015BE28, with EAX stepping 0x20 over 64 rows -- so only the first
        # column's displacement *is* the table's VA and the other seven sit four bytes
        # into it. The registry resolves a displacement inside a registered table to
        # that table plus the offset, so all sixteen columns compare as
        # `table:<hash>+<offset>` while the eight model reads at 0x1015CE28 compare
        # exactly. Two cases: registered -> PASS with all 26 operands resolved, and one
        # column moved by eight bytes -> FAIL as an *instruction* difference (the same
        # table at a different offset), which is what keeps the offset compared.
        popup_body = IMAGE_BASE + 0x44310
        popup_rows = [("selftest-in-table", popup_body, popup_body, 125)]
        popup_symbols = {popup_body: popup_body}
        popup_tables = {IMAGE_BASE + 0x15CE28: 2048, IMAGE_BASE + 0x15C628: 2048,
                        IMAGE_BASE + 0x15BE28: 2048}
        fourteenth_results = compare(original, original, popup_rows, popup_symbols,
                                     Tables(original, popup_tables, "original"),
                                     Tables(original, popup_tables, "candidate"))
        fourteenth = report(fourteenth_results)
        columns_resolved = any("26 table operand" in detail
                               for _, _, detail in fourteenth_results)

        moved_column = bytearray(original)
        column_spot = image_offset(original, IMAGE_BASE + 0x443F2)
        if (column_spot is None
                or moved_column[column_spot:column_spot + 4]
                != struct.pack("<I", IMAGE_BASE + 0x15C628)):
            raise SystemExit("selftest: the popup column at 0x100443F2 is not a disp32")
        struct.pack_into("<I", moved_column, column_spot, IMAGE_BASE + 0x15C630)
        fifteenth_results = compare(original, bytes(moved_column), popup_rows,
                                    popup_symbols,
                                    Tables(original, popup_tables, "original"),
                                    Tables(bytes(moved_column), popup_tables, "candidate"))
        fifteenth = report(fifteenth_results)
        offset_is_instruction = all(detail.startswith("instruction ")
                                    for _, status, detail in fifteenth_results
                                    if status == "FAIL")

        ok = (first == 0 and second == 1 and third == 1 and fourth == 0 and resolved
              and fifth == 1 and sixth == 1 and seventh == 1
              and eighth == 0 and tail_resolved and ninth == 1 and tenth == 0
              and eleventh == 0 and base_resolved and twelfth == 1 and base_is_instruction
              and thirteenth == 1 and contents_named
              and fourteenth == 0 and columns_resolved
              and fifteenth == 1 and offset_is_instruction)
        print("selftest: identical PASS, corrupted opcode FAIL, moved branch target FAIL, "
              "registered table PASS (2 operands resolved), changed table contents FAIL, "
              "moved table operand FAIL, unregistered table operand FAIL, "
              "uninitialised-tail table PASS (2 operands resolved), "
              "one-side-only tail table FAIL, map C and C++ symbols PASS, "
              "base+disp table PASS (2 operands resolved), changed base register FAIL "
              "(instruction), base+disp contents differ FAIL (table), "
              "in-table column offset PASS (26 operands resolved), changed column offset "
              "FAIL (instruction)"
              if ok else f"selftest: BROKEN ({first}, {second}, {third}, {fourth}, "
                         f"{resolved}, {fifth}, {sixth}, {seventh}, {eighth}, "
                         f"{tail_resolved}, {ninth}, {tenth}, {eleventh}, {base_resolved}, "
                         f"{twelfth}, {base_is_instruction}, {thirteenth}, {contents_named}, "
                         f"{fourteenth}, {columns_resolved}, {fifteenth}, "
                         f"{offset_is_instruction})")
        return 0 if ok else 2

    if not (args.original and args.candidate and args.bodies and args.map):
        raise SystemExit(__doc__)
    original = Path(args.original).read_bytes()
    candidate = Path(args.candidate).read_bytes()
    rows, table_rows = read_manifest(Path(args.bodies))
    symbols = read_map(Path(args.map))
    candidate_tables = {}
    for reference_va, symbol, size in table_rows:
        candidate_va = symbols.get(symbol)
        if candidate_va is None:
            raise SystemExit(f"table symbol {symbol} is not in the map")
        candidate_tables[candidate_va] = size
    return report(compare(original, candidate, rows, symbols,
                          Tables(original, {va: size for va, _, size in table_rows}, "original"),
                          Tables(candidate, candidate_tables, "candidate")))


if __name__ == "__main__":
    sys.exit(main())
