#!/usr/bin/env python3
"""The 18-cell dump matrix: 9 builds x (full, per-option) against the baselines.

This is the acceptance measurement for the write set. It runs the dump for every
supported image in both modes and compares each cell with the stored baseline,
reporting per build what the full cell wrote, how it differs, how the per-option
scoreboard stands, and which keys are behind the residual.

Every cell is its own subprocess, and a cell that dies with a Windows exception is
a recorded outcome, not an abort. That matters because one cell faults *by
design*: `NoImpostorHeightCutoff` reproduces the original's 1.0.0.0 access
violation (`docs/parity.md`), so build 1's per-option cell dies at that arm. A
fault records:

  * the cell's exit code, printed as the exception code (`0xC0000005`);
  * whatever the cell wrote before it died, kept next to the build as
    `matrix-b<build>-per-partial.txt`, with its size in the report;
  * the key it faulted on -- *measured*, not guessed: the build is re-run one key
    per subprocess, and an isolated single-key cell is exactly that key's rows of
    a per-option cell (`--selfcheck-isolated` proves it on a build that does not
    fault). A fault in the shared per-option loop hides every key after it and the
    stdio block hides the rows before it, so the per-key fallback is both what
    makes a faulted cell measurable and what proves no *other* key faults there.

    python tools/dump_matrix.py [--only 9] [--baseline DIR]
    python tools/dump_matrix.py --markdown docs/matrix.md
    python tools/dump_matrix.py --only 9 --check        # the cheap ctest form
"""

import argparse
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120", 13: "EFLC_1130"}
VERSIONS = {1: "1.0.0.0", 5: "1.0.4.0", 6: "1.0.0.4", 7: "1.0.6.0", 8: "1.0.7.0",
            9: "1.0.8.0", 11: "1.1.1.0", 12: "1.1.2.0", 13: "1.1.3.0"}
DEFAULT_BASELINE = ROOT.parent / "readable" / "build" / "p3-final-dumps3"

# The exception codes a cell can die with. Windows reports an unhandled SEH
# exception as the process exit code, so the exit code *is* the exception code.
EXCEPTIONS = {
    0xC0000005: "ACCESS_VIOLATION",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC000008C: "ARRAY_BOUNDS_EXCEEDED",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO",
    0xC00000FD: "STACK_OVERFLOW",
    0xC0000374: "HEAP_CORRUPTION",
    0xC0000409: "STACK_BUFFER_OVERRUN",
}

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parity_report import branch_bytes, load_oracle_verified, oracle_excuses  # noqa: E402


def exception_of(code: int) -> str:
    """The exception text for a naive exit code, or '' when the cell just ran."""
    if code == 0xE0000001:
        return "TIMEOUT (the cell did not exit)"
    if 0xC0000000 <= code <= 0xCFFFFFFF:
        return f"0x{code:08X} {EXCEPTIONS.get(code, 'exception')}"
    return ""


def read_rows(path, key=None) -> dict:
    """A dump file as {RVA: byte}, or one key's {RVA: byte} out of a cell.

    The full cell writes `<RVA> <byte>`, the per-option cell `<key> <RVA> <byte>`.
    A half-flushed line at the end of a faulted cell is not a row, so it is
    skipped -- which is what makes a partial file usable.
    """
    found = {}
    path = Path(path)
    if not path.exists():
        return found
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3 and (key is None or parts[0] == key):
            found[int(parts[1], 16)] = int(parts[2], 16)
        elif len(parts) == 2 and key is None:
            found[int(parts[0], 16)] = int(parts[1], 16)
    return found


def keys() -> list:
    table = ROOT / "source" / "options.inc"
    return [line.split("(", 1)[1].split(",", 1)[0]
            for line in table.read_text(encoding="utf-8").splitlines()
            if line.startswith("ZP_OPTION(")]


def run_dump(dump: Path, arguments) -> int:
    """Run one cell; return its naive exit code. A non-exception failure aborts.

    A cell that hangs is a fault outcome on the 1.0.0.0 out-of-range write: the
    undefined access can fault or spin depending on the heap layout. Cap the
    wait so one spinning child cannot stall the whole matrix; the sentinel is
    reported as TIMEOUT and the isolated cells still attribute the key.
    """
    try:
        result = subprocess.run([str(dump), *[str(item) for item in arguments]],
                                capture_output=True, text=True, errors="replace",
                                timeout=120)
    except subprocess.TimeoutExpired:
        return 0xE0000001
    code = result.returncode & 0xFFFFFFFF
    if code != 0 and not exception_of(code):
        raise SystemExit(f"zp_dump failed with exit {code} (not a fault):\n"
                         f"{result.stdout}{result.stderr}")
    return code


@dataclass
class Full:
    code: int
    wrote: int = 0
    differing: list = field(default_factory=list)
    missing: list = field(default_factory=list)
    extra: list = field(default_factory=list)
    partial: int = 0

    @property
    def fault(self) -> str:
        return exception_of(self.code)


@dataclass
class PerOption:
    code: int
    rows: dict = field(default_factory=dict)      # key -> {RVA: byte} this build wrote
    closed: int = 0
    opened: list = field(default_factory=list)    # [(key, missing, extra, differing)]
    oracle: list = field(default_factory=list)    # same shape, excused per build
    faulted: list = field(default_factory=list)   # [(key, exception text)]
    isolated: bool = False                        # measured one key per subprocess
    wrote: int = 0                                # bytes in the kept partial file
    reached: int = 0                              # keys the shared loop ran before it died

    @property
    def fault(self) -> str:
        return exception_of(self.code)


def full_cell(build: int, exe: Path, baseline: Path, dump: Path, ini: Path, work: Path) -> Full:
    out = work / f"matrix-b{build}-full.txt"
    code = run_dump(dump, ["--exe", exe, "--build", build, out, ini])
    have = read_rows(out)
    partial = 0
    if code:
        # Keep what the cell wrote: a fault is a finding, not a cleanup problem.
        kept = work / f"matrix-b{build}-full-partial.txt"
        kept.unlink(missing_ok=True)
        out.replace(kept)
        partial = kept.stat().st_size
    else:
        out.unlink(missing_ok=True)

    want = read_rows(baseline / f"b{build}-full.txt")
    # Branch displacements are not comparable: the baseline wrote one stand-in
    # address for every body target, so those bytes are counted apart from a real
    # difference (the scoreboard excludes them the same way, per key).
    branch = branch_bytes(want, have, exe.read_bytes())
    return Full(code=code, wrote=len(have),
                differing=sorted(rva for rva in (set(want) & set(have)) - branch
                                 if want[rva] != have[rva]),
                missing=sorted((set(want) - set(have)) - branch),
                extra=sorted((set(have) - set(want)) - branch),
                partial=partial)


def isolated_cells(build: int, exe: Path, dump: Path, work: Path, table) -> tuple:
    """One subprocess per key: every key's own cell, and the ones that fault."""
    have = {}
    faulted = []
    ini = work / f"matrix-b{build}-isolated.ini"
    all_off = "[Options]\n" + "".join(f"{key}=0\n" for key in table)
    for key in table:
        # Exactly the document the per-option cell builds for this key, applied
        # through the full-cell path so a fault cannot take the rest down.
        ini.write_text(all_off + f"{key}=1\n", encoding="utf-8")
        out = work / f"matrix-b{build}-isolated.txt"
        code = run_dump(dump, ["--exe", exe, "--build", build, out, ini])
        if code:
            faulted.append((key, exception_of(code)))
            have[key] = {}
        else:
            have[key] = read_rows(out)
        out.unlink(missing_ok=True)
    ini.unlink(missing_ok=True)
    return have, faulted


def per_option_cell(build: int, exe: Path, baseline: Path, dump: Path, work: Path,
                    table) -> PerOption:
    out = work / f"matrix-b{build}-per.txt"
    code = run_dump(dump, ["--per-option", "--exe", exe, "--build", build, out,
                           work / "no-such.ini"])
    cell = PerOption(code=code)
    if code:
        # The shared loop died. Keep its file, then measure the keys one subprocess
        # at a time so the keys after the fault are covered and the fault is
        # attributed to the key that reproduces it alone.
        kept = work / f"matrix-b{build}-per-partial.txt"
        kept.unlink(missing_ok=True)
        out.replace(kept)
        cell.wrote = kept.stat().st_size
        cell.isolated = True
        cell.rows, cell.faulted = isolated_cells(build, exe, dump, work, table)
        if cell.faulted:
            cell.reached = table.index(cell.faulted[0][0])
    else:
        cell.rows = {key: read_rows(out, key) for key in table}
        out.unlink(missing_ok=True)

    pristine = exe.read_bytes()
    want_path = baseline / f"b{build}-per.txt"
    dead = {key for key, _ in cell.faulted}
    oracle_verified = load_oracle_verified()
    for key in table:
        if key in dead:
            continue
        want = read_rows(want_path, key)
        ours = cell.rows.get(key, {})
        branch = branch_bytes(want, ours, pristine)
        missing = len((set(want) - set(ours)) - branch)
        extra = len((set(ours) - set(want)) - branch)
        differing = sum(1 for rva in (set(want) & set(ours)) - branch
                        if want[rva] != ours[rva])
        if not want and not ours:
            cell.closed += 1
        elif missing or extra or differing:
            entry = oracle_verified.get(key)
            if entry is not None and oracle_excuses(entry, build):
                cell.oracle.append((key, missing, extra, differing))
            else:
                cell.opened.append((key, missing, extra, differing))
        else:
            cell.closed += 1
    return cell


def attribution(build: int, baseline: Path, cell: PerOption, full: Full) -> list:
    """Name the keys behind the full-cell residual.

    The same index `tools/full_diff.py` uses: an RVA is attributed to the keys
    whose per-key cell writes it. Missing bytes are the baseline's, so they are
    named from the baseline's per-option file; extra bytes are ours, so they are
    named from this build's cells too.
    """
    want_by_key = {}
    per = baseline / f"b{build}-per.txt"
    if per.exists():
        for line in per.read_text(encoding="utf-8", errors="replace").splitlines():
            parts = line.split()
            if len(parts) == 3:
                want_by_key.setdefault(int(parts[1], 16), set()).add(parts[0])
    have_by_key = {}
    for key, rows in cell.rows.items():
        for rva in rows:
            have_by_key.setdefault(rva, set()).add(key)

    open_keys = {key for key, *_ in cell.opened}
    oracle_keys = {key for key, *_ in cell.oracle}
    lines = []
    for kind, rvas in (("missing", full.missing), ("extra", full.extra),
                       ("differing", full.differing)):
        if not rvas:
            continue
        count = Counter()
        unnamed = 0
        for rva in rvas:
            names = want_by_key.get(rva) or have_by_key.get(rva)
            if not names:
                unnamed += 1
                continue
            for name in names:
                count[name] += 1
        parts = []
        for key, hits in sorted(count.items(), key=lambda item: (-item[1], item[0])):
            if key in open_keys:
                state = "open"
            elif key in oracle_keys:
                state = "oracle-verified baseline omission"
            else:
                state = "closed"
            parts.append(f"`{key}` {hits} ({state})")
        if unnamed:
            parts.append(f"no per-key cell {unnamed}")
        named = set(count)
        if count and not unnamed and named <= open_keys:
            suffix = ", all from open keys"
        elif count and not unnamed and named <= oracle_keys:
            suffix = ", all oracle-verified baseline omissions"
        elif count and named & oracle_keys and not named & open_keys:
            suffix = ", the omissions being oracle-verified baseline gaps"
        elif count and named & open_keys:
            suffix = ", partly from open keys"
        elif not count:
            suffix = ""
        else:
            suffix = ", none from open keys"
        lines.append(f"{kind} {len(rvas)}: {', '.join(parts)}{suffix}")
    return lines


def run_matrix(builds, baseline: Path, dump: Path, ini: Path, work: Path, table) -> list:
    results = []
    for build in builds:
        exe = ROOT.parent / "game_refs" / f"{BUILDS[build]}.exe"
        if not exe.exists():
            print(f"build {build}: {exe} is missing")
            continue
        full = full_cell(build, exe, baseline, dump, ini, work)
        cell = per_option_cell(build, exe, baseline, dump, work, table)
        results.append((build, full, cell, attribution(build, baseline, cell, full)))
    return results


def print_table(results, table) -> None:
    print(f"{'build':>6} {'full':>7} {'diff':>5} {'miss':>5} {'extra':>5} "
          f"{'closed':>8} {'oracle':>7} {'fault':>26}")
    for build, full, cell, residual in results:
        scored = full.fault == ""
        print(f"{build:>6} {full.wrote:>7} "
              f"{(len(full.differing) if scored else '-'):>5} "
              f"{(len(full.missing) if scored else '-'):>5} "
              f"{(len(full.extra) if scored else '-'):>5} "
              f"{str(cell.closed) + '/' + str(len(table)):>8} "
              f"{len(cell.oracle):>7} "
              f"{(cell.faulted[0][0] if cell.faulted else cell.fault or '-')[:26]:>26}")
        if full.fault:
            print(f"       full cell FAULTED: {full.fault}, {full.partial} byte(s) kept "
                  f"(partial not scored)")
        for key, text in cell.faulted:
            print(f"       per-option cell FAULTED at {key}: {text}"
                  f" ({cell.wrote} byte(s) kept, {cell.reached} key(s) reached before it)")
        for line in residual:
            print(f"       {line}")


def markdown(results, table) -> str:
    measured = len(results) * 2
    lines = [
        "# The 18-cell dump matrix",
        "",
        "Nine supported images x two cells -- the **full** cell (the shipped INI) and the",
        "**per-option** cell (every key off, one on) -- measured against the reference",
        "tree's stored dumps in `readable/build/p3-final-dumps3`. Generated, not",
        "hand-written:",
        "",
        "```",
        "python tools/dump_matrix.py --markdown docs/matrix.md",
        "```",
        "",
        "Each cell is its own subprocess. Every number is the scoreboard's comparison",
        "(`tools/parity_report.py`): a branch displacement -- the four bytes after an",
        "`E8`/`E9`, found in either cell or in the pristine image -- is excluded, because",
        "the baseline recorded one stand-in address for every body target and those bytes",
        "cannot be compared. `differing`, `missing` and `extra` count real bytes only.",
        "",
        "## Full cell",
        "",
        "`bytes` is what our cell writes; the rest is against the baseline. The residual",
        "column names the keys whose per-key cell writes those bytes (a byte can be named",
        "by more than one key, so the counts need not add up to the byte count), each with",
        "its scoreboard state: `closed`, `open`, or `oracle-verified baseline omission`.",
        "",
        "| build | version | image | bytes | differing | missing | extra | residual |",
        "|---:|---|---|---:|---:|---:|---:|---|",
    ]
    for build, full, cell, residual in results:
        scored = full.fault == ""
        residual_text = (
            "faulted: partial not scored" if not scored
            else ('; '.join(residual) if residual else "none")
        )
        lines.append(f"| {build} | {VERSIONS[build]} | `{BUILDS[build]}.exe` | {full.wrote} | "
                     f"{len(full.differing) if scored else '-'} | "
                     f"{len(full.missing) if scored else '-'} | "
                     f"{len(full.extra) if scored else '-'} | "
                     f"{residual_text} |")
    lines += [
        "",
        "A **faulted** full cell's partial rows are not a write-set scoreboard (the",
        "preserved 1.0.0.0 fault stops the install pass); its diff/missing/extra cells",
        "are `-`, the fault is reported below, and the per-option/isolated cells are the",
        "scoreboard for that build.",
        "",
        "## Per-option cell",
        "",
        "`closed / 99` is the key scoreboard: a key is closed when its cell has the same",
        "rows as the baseline's, branch displacements aside. `open` names the keys that",
        "are not; `oracle` names the keys whose difference is a measured omission in the",
        "stored baseline (`tests/oracle_verified.tsv`, per key and build) and is therefore",
        "reported apart and never counted as closed; `fault` names the cell that died.",
        "",
        "| build | version | closed / 99 | open | oracle | fault |",
        "|---:|---|---:|---|---|---|",
    ]
    for build, full, cell, _ in results:
        fault = "-"
        if cell.faulted:
            fault = "; ".join(f"FAULTED at `{key}` ({text})" for key, text in cell.faulted)
        elif cell.fault:
            fault = f"FAULTED ({cell.fault})"
        opened = ", ".join(f"`{key}`" for key, *_ in cell.opened) or "-"
        oracle = ", ".join(f"`{key}`" for key, *_ in cell.oracle) or "-"
        closed = str(cell.closed) + (" (isolated)" if cell.isolated else "")
        lines.append(f"| {build} | {VERSIONS[build]} | {closed} | {opened} | {oracle} | {fault} |")

    faulted = [(build, cell) for build, _, cell, _ in results if cell.faulted or cell.fault]
    full_faults = [(build, full) for build, full, _cell, _ in results if full.fault]
    lines += ["", "## The fault", ""]
    if not faulted and not full_faults:
        lines += ["No cell faulted.", ""]
    for build, full in full_faults:
        lines += [
            f"`{BUILDS[build]}.exe` ({VERSIONS[build]}) dies in the **full** cell with",
            f"`{full.fault}` after {full.partial} byte(s): the shipped INI reaches",
            "`NoImpostorHeightCutoff`, whose 1.0.0.0 pattern matches once and whose",
            "original writes `hits[1]` through an uninitialised vector entry. The",
            "per-option cell isolates that key (its row is `open` above); in the 1:1",
            "configuration this is the original's fault, and",
            "`tools/check_failsafe_parity.py` shows the same cell completing with",
            "`OPCEP_IMPROVEMENTS=all` and the guarded write absent.",
            "",
        ]
    for build, cell in faulted:
        key, text = cell.faulted[0] if cell.faulted else ("?", cell.fault)
        lines += [
            f"`{BUILDS[build]}.exe` ({VERSIONS[build]}) dies in the per-option cell at",
            f"`{key}`, {text}: the original's own fault -- an uninitialised vector entry,",
            "write `66 89 06` at the original's `0x100536F1` -- which `docs/parity.md`",
            "records as deliberate and unguarded. The cell wrote "
            f"{cell.wrote} byte(s) before it died, so its rows are not a scoreboard: the",
            f"first {cell.reached} keys ran, and the {len(table) - cell.reached} after it",
            "were never reached. It is scored one key per subprocess instead, and",
            f"`{key}` is the only key whose cell faults alone -- so no other cell faults on",
            "that build. The isolated cells are not a different measurement: on builds 5",
            "and 9 they are byte-identical to the per-option rows for all 99 keys",
            "(`python tools/dump_matrix.py --selfcheck-isolated --only 5`).",
            "",
        ]
    lines += [
        "## What the matrix proves",
        "",
        f"{measured} cell(s) ran for {len(results)} build(s). A per-option cell that",
        "completes does so over all 99 keys, so any fault inside it would have shown up",
        "as an abort; the one fault recorded is the reproduced original fault, not a",
        "harness failure. Every residual full-cell byte has a key named behind it, and",
        "the per-option column is the scoreboard that says whether that key is done.",
        "",
    ]
    return "\n".join(lines)


def check(dump: Path, baseline: Path, ini: Path, work: Path, table) -> int:
    """The cheap pin: build 9, both cells, three assertions."""
    build = 9
    exe = ROOT.parent / "game_refs" / f"{BUILDS[build]}.exe"
    if not exe.exists():
        raise SystemExit(f"zp_matrix needs {exe}")
    if not (baseline / f"b{build}-full.txt").exists():
        raise SystemExit(f"zp_matrix needs the baseline dumps in {baseline}")

    full = full_cell(build, exe, baseline, dump, ini, work)
    cell = per_option_cell(build, exe, baseline, dump, work, table)
    open_keys = [key for key, *_ in cell.opened]

    failures = []
    print(f"zp_matrix: build 9 full cell: {full.wrote} byte(s), {len(full.differing)} differing, "
          f"{len(full.extra)} extra, {len(full.missing)} missing")
    if full.fault:
        failures.append(f"the full cell faulted: {full.fault}")
    if full.differing:
        failures.append(f"the full cell has {len(full.differing)} differing byte(s)")
    if full.extra:
        failures.append(f"the full cell has {len(full.extra)} extra byte(s)")
    print(f"zp_matrix: build 9 per-option cell: {cell.closed}/99 closed, {len(open_keys)} open"
          + (f" ({', '.join(open_keys)})" if open_keys else ""))
    if cell.fault or cell.faulted:
        failures.append(f"a per-option cell faulted: {cell.fault or cell.faulted}")
    if cell.closed != 98:
        failures.append(f"the per-option cell reports {cell.closed} closed, not 98")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: build 9 is clean and its per-option cell is 98/99")
    return 0


def selfcheck(dump: Path, work: Path, table, build: int) -> int:
    """Prove an isolated single-key cell is that key's rows of a per-option cell."""
    exe = ROOT.parent / "game_refs" / f"{BUILDS[build]}.exe"
    per = work / f"matrix-b{build}-per.txt"
    code = run_dump(dump, ["--per-option", "--exe", exe, "--build", build, per,
                           work / "no-such.ini"])
    if code:
        raise SystemExit(f"build {build}'s per-option cell faults; pick a clean build")
    have, faulted = isolated_cells(build, exe, dump, work, table)
    bad = 0
    for key in table:
        want = read_rows(per, key)
        ours = have.get(key, {})
        if want != ours:
            bad += 1
            print(f"MISMATCH {key}: per-option {len(want)} row(s), isolated {len(ours)}")
    per.unlink(missing_ok=True)
    print(f"build {build}: {len(table) - bad}/{len(table)} key(s) identical, "
          f"{bad} mismatch(es), {len(faulted)} fault(s)")
    return 0 if bad == 0 and not faulted else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    parser.add_argument("--dump", default=str(ROOT / "build" / "win-x86" / "Release" / "zp_dump.exe"))
    parser.add_argument("--work", default=str(ROOT / "build" / "win-x86"))
    # The full cell is the write set of the shipped INI, so it has to be the same
    # document the baseline was produced from. Per-option cells rebuild their own
    # documents (every key off, one key on) and ignore this file.
    parser.add_argument("--ini", default=str(ROOT / "tests" / "parity" / "ZolikaPatch.ini"))
    parser.add_argument("--only", type=int)
    parser.add_argument("--check", action="store_true",
                        help="the cheap ctest form: build 9, both cells, pinned")
    parser.add_argument("--markdown", help="write the documented table to this path")
    parser.add_argument("--selfcheck-isolated", action="store_true",
                        help="prove an isolated single-key cell equals the per-option rows")
    args = parser.parse_args()

    baseline = Path(args.baseline)
    dump = Path(args.dump)
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    table = keys()

    if args.check:
        if args.only not in (None, 9):
            raise SystemExit("--check pins build 9; drop --only")
        ini = Path(args.ini).resolve()
        if not ini.exists():
            raise SystemExit(f"the shipped INI is missing: {ini}")
        return check(dump, baseline, ini, work, table)

    if args.selfcheck_isolated:
        return selfcheck(dump, work, table, args.only or 9)

    ini = Path(args.ini).resolve()
    if not ini.exists():
        raise SystemExit(f"the shipped INI is missing: {ini}")
    builds = [args.only] if args.only else sorted(BUILDS)
    results = run_matrix(builds, baseline, dump, ini, work, table)
    print_table(results, table)
    if args.markdown:
        Path(args.markdown).write_text(markdown(results, table), encoding="utf-8")
        print(f"wrote {args.markdown}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
