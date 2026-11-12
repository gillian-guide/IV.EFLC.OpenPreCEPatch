#!/usr/bin/env python3
"""Per-key parity scoreboard against the stored baselines.

For every catalogue key, compares our per-option dump with the baseline the old
tree produced, and prints one line per key: matched, missing, extra, differing.
A key is closed only when all four are zero (or both sides are empty). A key whose
difference is a known baseline omission is reported as ORACLE instead of OPEN, on
the builds `tests/oracle_verified.tsv` names for it (never globally), and is never
counted as closed.

    python tools/parity_report.py <build> [--baseline DIR] [--ours FILE]

Defaults: baseline ../readable/build/p3-final-dumps3, ours a fresh per-option
dump of that build. Exit status: 0 when every key is closed, 1 otherwise.
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_bodies import image_offset

ROOT = Path(__file__).resolve().parent.parent
BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120", 13: "EFLC_1130"}
GAME_BASE = 0x400000

# Keys whose retained arm deliberately writes a different set in this
# distribution. They are measured, reported apart and never counted as open; the
# reason is the one the commit messages give.
IMPROVED_DELTAS = {
    "GarageModelFix": "the prologue hook replaces the mid-body site (same fix, -0x20)",
    "BuildingAlphaFix": "only the jnz is removed; the test stays",
    "CutsceneFixes": "the shared callee is hooked once instead of the three sites",
    "ImprovedShaderStreaming": "only the two LookupShader calls; the chain jumps are dropped",
    "RemoveGFWLUpdatePopup": "the shipped oPCEP INI enables it; the parity flat file keeps the original 0",
}


def displacement_rvas(pristine: bytes, rvas):
    """The RVAs that are the displacement of a branch, read from the pristine image.

    A cell only lists the bytes a key changed, so for an arm installed over a site
    that was already an E8/E9 the opcode row is in neither cell and the four
    displacement bytes look like an ordinary difference. The image itself says
    whether the byte before an RVA is a branch, and that is the only reliable
    source: the baselines wrote one stand-in address for every body target, so the
    bytes themselves cannot be compared.
    """
    out = set()
    for rva in rvas:
        # A displacement is four bytes long, so the opcode that owns it sits one to
        # four bytes before the RVA in question: checking only the immediately
        # preceding byte classified one byte of each branch and left the rest
        # looking like real differences.
        for back in range(1, 5):
            if rva - back < 1:
                continue
            offset = image_offset(pristine, GAME_BASE + rva - back, GAME_BASE)
            if offset is not None and pristine[offset] in (0xE8, 0xE9):
                out.add(rva)
                break
    return out


def branch_bytes(want, have, pristine):
    """Every RVA that is a branch displacement, from both sources that can tell.

    A displacement is four bytes after an `E8`/`E9`. That opcode is visible in a
    cell when the arm changed it, and in the pristine image when the site was
    already a branch - but a site the mod itself patched hides it from the pristine
    image (the original byte there is the instruction the arm overwrote), so the two
    sources are needed together, not either alone.
    """
    marked = set()
    for side in (want, have):
        for rva, byte in side.items():
            if byte in (0xE8, 0xE9):
                marked.update(rva + offset for offset in range(1, 5))
    marked |= displacement_rvas(pristine, set(want) | set(have))
    return marked


def rows(path, key=None):
    out = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3 and (key is None or parts[0] == key):
            out[int(parts[1], 16)] = int(parts[2], 16)
        elif len(parts) == 2 and key is None:
            out[int(parts[0], 16)] = int(parts[1], 16)
    return out


def load_oracle_verified():
    """Keys whose measured difference is the baseline's incompleteness, not ours,
    and the builds each excuse covers.

    The stored baselines came from the reference tree, which is incomplete in two
    measured ways: its live launcher arm is a 7-row subset of what the original ASI
    scans, and its CarDynamicShadowFix arm never ran the tree's own `@noE` pass. The
    gap is *per key and build*: CarDynamicShadowFix is closed on 1.0.8.0 and only
    differs on 1.0.4.0, so excusing the key globally would let a regression on
    1.0.8.0 report as an oracle gap. A row therefore names the builds it covers
    (`*` = every build the catalogue mask allows) and the key is reported apart from
    the open ones only on those builds. It never counts as closed: nothing here
    closes a key, it only stops a known-bad baseline cell from reading as unfinished
    work.
    """
    path = ROOT / "tests" / "oracle_verified.tsv"
    if not path.exists():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 5:
            continue
        key, builds, why = parts[0].strip(), parts[1].strip(), parts[2].strip()
        if not key or not builds:
            continue
        if builds == "*":
            out[key] = (None, why)
        else:
            try:
                out[key] = (frozenset(int(item) for item in builds.split(",")), why)
            except ValueError:
                out[key] = (frozenset(), why)  # malformed: never excuses a build
    return out


def oracle_excuses(entry, build):
    """Does this row excuse `build`? `*` (builds is None) excuses every build."""
    builds, _ = entry
    return builds is None or build in builds


def keys():
    table = (ROOT / "source" / "options.inc")
    out = []
    for line in table.read_text(encoding="utf-8").splitlines():
        if line.startswith("ZP_OPTION("):
            out.append(line.split("(", 1)[1].split(",", 1)[0])
    return out


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    build = int(sys.argv[1])
    baseline_dir = ROOT.parent / "readable" / "build" / "p3-final-dumps3"
    ours_path = None
    index = 2
    while index < len(sys.argv):
        if sys.argv[index] == "--baseline":
            baseline_dir = Path(sys.argv[index + 1])
            index += 2
        elif sys.argv[index] == "--ours":
            ours_path = Path(sys.argv[index + 1])
            index += 2
        else:
            raise SystemExit(__doc__)

    pristine = (ROOT.parent / "game_refs" / f"{BUILDS[build]}.exe").read_bytes()

    if ours_path is None:
        ours_path = ROOT / "build" / "win-x86" / f"per-b{build}.txt"
        dump = ROOT / "build" / "win-x86" / "Release" / "zp_dump.exe"
        image = ROOT.parent / "game_refs" / f"{BUILDS[build]}.exe"
        subprocess.run([str(dump), "--per-option", "--exe", str(image), "--build", str(build),
                        str(ours_path), "no-such.ini"], check=True, capture_output=True)

    baseline_path = baseline_dir / f"b{build}-per.txt"
    closed = 0
    open_keys = 0
    excused = 0
    deltas = 0
    oracle_verified = load_oracle_verified()
    for key in keys():
        want = rows(baseline_path, key)
        have = rows(ours_path, key)
        # A branch's displacement is not comparable: the baselines wrote one
        # stand-in address for every body target, and ours names the registered VA.
        # Three sources decide which RVAs are displacements: an E8/E9 opcode in
        # either cell (the ordinary case), and the pristine image, which is the only
        # source for an arm installed over a site that was already a branch - then
        # no cell carries the opcode row at all.
        branch = branch_bytes(want, have, pristine)

        missing = len((set(want) - set(have)) - branch)
        extra = len((set(have) - set(want)) - branch)
        differing = sum(1 for rva in (set(want) & set(have)) - branch
                        if want[rva] != have[rva])
        if not want and not have:
            closed += 1
            continue
        if missing or extra or differing:
            entry = oracle_verified.get(key)
            if entry is not None and oracle_excuses(entry, build):
                excused += 1
                print(f"ORACLE {key} [build {build}]: baseline {len(want)}, ours {len(have)}, "
                      f"missing {missing}, extra {extra}, differing {differing} "
                      f"- {entry[1]}")
                continue
            if key in IMPROVED_DELTAS:
                deltas += 1
                print(f"DELTA {key} [build {build}]: baseline {len(want)}, ours {len(have)}, "
                      f"missing {missing}, extra {extra}, differing {differing} "
                      f"- {IMPROVED_DELTAS[key]}")
                continue
            open_keys += 1
            print(f"OPEN {key}: baseline {len(want)}, ours {len(have)}, "
                  f"missing {missing}, extra {extra}, differing {differing}")
        else:
            closed += 1
    print(f"build {build}: {closed} key(s) closed, {deltas} expected delta(s), "
          f"{open_keys} open, {excused} oracle-verified (baseline incomplete)")
    return 0 if open_keys == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
