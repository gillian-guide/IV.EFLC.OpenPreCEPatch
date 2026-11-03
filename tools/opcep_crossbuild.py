#!/usr/bin/env python3
"""Capture or diff per-build write sets for the cross-build proofs.

The committed matrix (tools/opcep_matrix.py) exercises one executable (1.0.8.0)
over 96 INI cells. The address->pattern conversion also touches arms gated to
1.0.6.0 / 1.0.7.0 / EFLC builds, so this tool re-runs the same dump on every
executable in game_refs and compares before/after captures.

    python tools/opcep_crossbuild.py --capture .scratch/cross-before
    python tools/opcep_crossbuild.py --check   .scratch/cross-before

Each capture is one file per (build, ini) with the dump's canonical
"RVA byte" lines.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_DIR = ROOT.parent / "game_refs"
EXE = ROOT / "build" / "x64" / "Release" / "opcep_parity_dump.exe"
TEMPLATE = ROOT / "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini"

# Executable -> compact version. The dump classifies nothing, so the build is
# named explicitly (the same arm set the ASI would select on that executable).
BUILDS = [
    ("GTAIV_1000.exe", 1000),
    ("GTAIV_1004.exe", 1004),
    ("GTAIV_1040.exe", 1040),
    ("GTAIV_1060.exe", 1060),
    ("GTAIV_1070.exe", 1070),
    ("GTAIV.exe", 1080),
    ("EFLC_1110.exe", 1110),
    ("EFLC_1120.exe", 1120),
    ("EFLC_1130.exe", 1130),
]


def render_ini(value: str) -> str:
    lines = TEMPLATE.read_text(encoding="utf-8").splitlines()
    out = []
    section = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and "]" in stripped:
            section = stripped[1 : stripped.index("]")]
            out.append(line)
            continue
        if section in {"PopupLightAngles", "HeadlightConeOffsets"} or "=" not in line:
            out.append(line)
            continue
        if stripped.startswith(";"):
            out.append(line)
            continue
        key, _, rest = line.partition("=")
        comment = ""
        if "//" in rest:
            body, _, tail = rest.partition("//")
            comment = "//" + tail
            rest = body
        if value is not None:
            rest = f" {value} "
        out.append(f"{key}={rest}{comment}")
    return "\n".join(out) + "\n"


def run(exe: Path, game: Path, ini: Path, out: Path, build: int) -> None:
    env = os.environ.copy()
    # The launcher write is gated by a process probe the harness cannot satisfy.
    env["OPCEP_LAUNCHER_PROBE"] = "1"
    result = subprocess.run(
        [str(exe), str(game), str(ini), str(out), "--build", str(build)],
        cwd=str(ROOT),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{game.name} build {build}: {result.stderr.decode(errors='replace').strip()}"
        )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--capture", type=Path)
    group.add_argument("--check", type=Path)
    args = parser.parse_args(argv)

    if not EXE.exists():
        print(f"error: {EXE} not built (cmake --build --preset x64-release)", file=sys.stderr)
        return 2

    work = ROOT / ".scratch" / "crossbuild"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    all_on = work / "all-on.ini"
    all_on.write_text(render_ini("1"), encoding="utf-8")
    defaults = TEMPLATE

    captures = []
    for name, build in BUILDS:
        game = GAME_DIR / name
        if not game.exists():
            print(f"skip: {name} not present")
            continue
        for ini_name, ini in (("defaults", defaults), ("all-on", all_on)):
            out = work / f"{name}.{ini_name}.writes.txt"
            run(EXE, game, ini, out, build)
            captures.append((name, ini_name, out))

    if args.capture:
        args.capture.mkdir(parents=True, exist_ok=True)
        for name, ini_name, out in captures:
            shutil.copyfile(out, args.capture / out.name)
        print(f"captured {len(captures)} write sets in {args.capture}")
        return 0

    failures = 0
    for name, ini_name, out in captures:
        before = args.check / out.name
        if not before.exists():
            print(f"error: no baseline for {out.name}", file=sys.stderr)
            failures += 1
            continue
        old = before.read_text(encoding="utf-8").splitlines()
        new = out.read_text(encoding="utf-8").splitlines()
        if old == new:
            continue
        failures += 1
        print(f"CHANGED {name} {ini_name}: {len(old)} -> {len(new)} writes", file=sys.stderr)
        old_map = dict(line.split() for line in old if line.strip())
        new_map = dict(line.split() for line in new if line.strip())
        for rva in sorted(set(old_map) | set(new_map))[:20]:
            a = old_map.get(rva, "--")
            b = new_map.get(rva, "--")
            if a != b:
                print(f"  {rva}: {a} -> {b}", file=sys.stderr)
    if failures:
        print(f"opcep_crossbuild: FAIL ({failures} changed write sets)", file=sys.stderr)
        return 1
    print(f"opcep_crossbuild: PASS ({len(captures)} write sets identical)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
