#!/usr/bin/env python3
"""Source-level deltas OpenPreCEPatch makes to shared options, measured.

Two arms change the *write set* deliberately, so they are opt-in groups rather
than the 1:1 code:

* `shader`: ImprovedShaderStreaming replaces only the two LookupShader calls and
  drops the two jumps over the remaining name chain (the bytes LibertyCityPlates
  scans). 1:1 writes four sites; the group writes two.
* `alpha_nop`: BuildingAlphaFix site 2 removes only the `jnz` (6 bytes) instead
  of the `test`+`jnz` pair (13 bytes).

Each cell runs `zp_dump` twice on the same isolated INI (every key off, the one
under test on) and compares the byte maps.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
checks = 0
failures = 0


def check(condition, what, detail=""):
    global checks, failures
    checks += 1
    if condition:
        print(f"PASS {what}")
        return
    failures += 1
    print(f"FAIL {what}" + (f" ({detail})" if detail else ""))


def parse_dump(path):
    values = {}
    for line in Path(path).read_text(encoding="ascii", errors="replace").splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        try:
            values[int(parts[0], 16)] = int(parts[1], 16)
        except ValueError:
            continue
    return values


def runs(rvas):
    out = []
    for rva in sorted(rvas):
        if out and rva == out[-1][-1] + 1:
            out[-1].append(rva)
        else:
            out.append([rva])
    return out


class Cell:
    def __init__(self, dump, game, work):
        self.dump = dump
        self.game = game
        self.work = work
        self.work.mkdir(parents=True, exist_ok=True)
        catalogue = (ROOT / "source" / "options.inc").read_text(encoding="utf-8")
        self.keys = re.findall(r"^ZP_OPTION\(([A-Za-z0-9_]+),", catalogue, re.MULTILINE)

    def run(self, name, image, build, improvements, keys):
        ini = self.work / f"{name}.ini"
        ini.write_text(
            "[Options]\n" + "".join(f"{key}=0\n" for key in self.keys)
            + "".join(f"{key}={value}\n" for key, value in keys.items()),
            encoding="ascii")
        output = self.work / f"{name}.txt"
        if output.exists():
            output.unlink()
        environment = dict(os.environ)
        environment["OPCEP_IMPROVEMENTS"] = improvements
        result = subprocess.run(
            [str(self.dump), "--exe", str(image), "--build", str(build), str(output), str(ini)],
            env=environment, capture_output=True, text=True)
        return result.returncode, output


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--dump", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--work", default=None)
    args = parser.parse_args()

    cell = Cell(Path(args.dump).resolve(), Path(args.game).resolve(),
                Path(args.work) if args.work else Path.cwd() / "opcep_deltas")
    image = Path(args.game).resolve() / "GTAIV.exe"
    if not image.exists():
        print("skip: GTAIV.exe is absent")
        return 0

    # ImprovedShaderStreaming: four writes 1:1, two with the shader group.
    keys = {"ImprovedShaderStreaming": 1}
    off_code, off_path = cell.run("shader.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("shader.on", image, 9, "shader", keys)
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    removed = set(off) - set(on)
    check(off_code == 0 and on_code == 0, "shader: both runs complete")
    check(not (set(on) - set(off)), "shader: the group adds no write")
    check(len(removed) == 10, "shader: the group removes the two 5-byte jumps", sorted(removed))
    check(len(runs(removed)) == 2 and all(len(run) == 5 for run in runs(removed)),
        "shader: the removed bytes are two 5-byte runs", runs(removed))
    check(all(off[run[0]] == 0xE9 for run in runs(removed)),
        "shader: each removed write is an E9", [(run, hex(off[run[0]])) for run in runs(removed)])

    # BuildingAlphaFix site 2: 13 bytes -> 6 bytes, the remaining six identical.
    keys = {"BuildingAlphaFix": 1}
    off_code, off_path = cell.run("alpha.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("alpha.on", image, 9, "alpha_nop", keys)
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    removed = set(off) - set(on)
    check(off_code == 0 and on_code == 0, "alpha: both runs complete")
    check(not (set(on) - set(off)), "alpha: the group adds no write")
    check(len(removed) == 7, "alpha: the group leaves six of the thirteen bytes", sorted(removed))
    check(len(runs(removed)) == 1 and len(runs(removed)[0]) == 7,
        "alpha: the removed run is the test (7 bytes)", runs(removed))
    nop_runs = [run for run in runs(on) if all(on[rva] == 0x90 for rva in run)]
    check(any(len(run) == 6 for run in nop_runs),
        "alpha: the kept six bytes are NOPs", [(run, hex(on[run[0]])) for run in runs(on)])

    print(f"{checks} check(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
