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

    # SCOSignatureFix: the extended anchor writes the same byte as the -32
    # anchor on 1.0.8.0, so the write set must be identical.
    keys = {"SCOSignatureFix": 1}
    off_code, off_path = cell.run("sco.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("sco.on", image, 9, "sco", keys)
    check(off_code == 0 and on_code == 0, "sco: both runs complete")
    check(parse_dump(off_path) == parse_dump(on_path),
        "sco: the branch-anchored scan writes the same byte as the -32 arm")

    # GarageModelFix: the prologue entry moves the same 5-byte E9 from the
    # mid-body site (0x868BB0) to the function entry (0x868B90).
    keys = {"GarageModelFix": 1}
    off_code, off_path = cell.run("garage.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("garage.on", image, 9, "garage_entry", keys)
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    removed = set(off) - set(on)
    added = set(on) - set(off)
    check(off_code == 0 and on_code == 0, "garage: both runs complete")
    check(len(removed) == 5 and len(added) == 5,
        "garage: the same 5-byte jump moves", (sorted(removed), sorted(added)))
    check(added and removed and min(added) - min(removed) == -0x20,
        "garage: the new site is the prologue, 0x20 bytes before the anchor")
    check(off[min(removed)] == 0xE9 and on[min(added)] == 0xE9,
        "garage: both writes are E9 jumps")

    # CutsceneFixes: the 3 sites (two call displacements + the branch byte) are
    # replaced by one 5-byte E9 at the shared callee 0x89A640 on 1.0.8.0.
    keys = {"CutsceneFixes": 1}
    off_code, off_path = cell.run("cutscene.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("cutscene.on", image, 9, "cutscene_entry", keys)
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    removed = set(off) - set(on)
    added = set(on) - set(off)
    check(off_code == 0 and on_code == 0, "cutscene: both runs complete")
    check(len(added) == 5 and on.get(min(added, default=0)) == 0xE9,
        "cutscene: one 5-byte E9 at the callee", sorted(added))
    check(min(added, default=0) == 0x89A640,
        "cutscene: the hook is the shared callee", hex(min(added, default=0)))
    check(len(removed) == 9,
        "cutscene: the three original sites are gone", sorted(removed))
    check(0x89A840 in removed and off[0x89A840] == 0xEB,
        "cutscene: the branch byte is no longer rewritten")

    # LoadDLCs' path guard: with a normal install path the write set is identical.
    keys = {"LoadDLCs": 1}
    off_code, off_path = cell.run("dlc.off", image, 9, "except:failsafe", keys)
    on_code, on_path = cell.run("dlc.on", image, 9, "dlc_path", keys)
    check(off_code == 0 and on_code == 0, "dlc_path: both runs complete")
    check(parse_dump(off_path) == parse_dump(on_path),
        "dlc_path: a fitting module path leaves the write set identical")

    print(f"{checks} check(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
