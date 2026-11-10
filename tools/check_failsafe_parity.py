#!/usr/bin/env python3
"""Fault-parity cells for the failsafe group.

Each cell runs the real install pipeline (`zp_dump`) over a copy of a real game
image twice - once with the failsafe group off (`OPCEP_IMPROVEMENTS=except:
failsafe`), once with it on (`all`) - and compares the observable outcome:

* on a healthy image the two write sets must be byte-identical (the guard may
  not change a successful install);
* for a forced-missing preamble pointer, the on run must be the off run minus
  exactly the guarded arm's 5-byte jump, and nothing else;
* on the 1.0.0.0 image, `NoImpostorHeightCutoff` must fault (0xC0000005) with
  the group off and complete with it on. That is the same input, two outcomes,
  and it is the fault-parity contract.

The forced-missing seam is `--failsafe-missing <name>`, which names the exact
`Failsafe::Missing` string the arm passes.
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
    """`<RVA> <byte>` lines -> {rva: value}."""
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


class Cell:
    def __init__(self, dump, game, work):
        self.dump = dump
        self.game = game
        self.work = work
        self.work.mkdir(parents=True, exist_ok=True)
        # Every cell is isolated like the matrix's: every key off, the target on.
        # A default-on neighbour would change the heap layout and hide the
        # 1.0.0.0 out-of-bounds write the fault cell is measuring.
        catalogue = (ROOT / "source" / "options.inc").read_text(encoding="utf-8")
        self.keys = re.findall(r"^ZP_OPTION\(([A-Za-z0-9_]+),", catalogue, re.MULTILINE)

    def ini(self, name, keys):
        path = self.work / name
        text = "[Options]\n" + "".join(f"{key}=0\n" for key in self.keys)
        text += "".join(f"{key}={value}\n" for key, value in keys.items())
        path.write_text(text, encoding="ascii")
        return path

    def run(self, name, image, build, improvements, keys, missing=()):
        output = self.work / f"{name}.txt"
        if output.exists():
            output.unlink()

        command = [str(self.dump), "--exe", str(image), "--build", str(build)]
        for what in missing:
            command += ["--failsafe-missing", what]
        command += [str(output), str(self.ini(f"{name}.ini", keys))]

        environment = dict(os.environ)
        environment["OPCEP_IMPROVEMENTS"] = improvements
        completed = subprocess.run(command, env=environment, capture_output=True, text=True)
        return completed.returncode, output


def status_is_access_violation(code):
    return (code & 0xFFFFFFFF) == 0xC0000005


def contiguous_runs(rvas):
    runs = []
    for rva in sorted(rvas):
        if runs and rva == runs[-1][-1] + 1:
            runs[-1].append(rva)
        else:
            runs.append([rva])
    return runs


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--dump", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--work", default=None)
    args = parser.parse_args()

    dump = Path(args.dump).resolve()
    game = Path(args.game).resolve()
    work = Path(args.work) if args.work else Path.cwd() / "failsafe_parity"
    cell = Cell(dump, game, work)

    image_1080 = game / "GTAIV.exe"
    image_1000 = game / "GTAIV_1000.exe"
    if not image_1080.exists() or not image_1000.exists():
        print("skip: the two images are absent")
        return 0

    # 1. A healthy image: the group changes no byte of the write set.
    healthy = {"SuperLODFix": 1, "GarageModelFix": 1, "PlayerAsPedComponentsFix": 1}
    off_code, off_path = cell.run("healthy.off", image_1080, 9, "except:failsafe", healthy)
    on_code, on_path = cell.run("healthy.on", image_1080, 9, "all", healthy)
    check(off_code == 0 and on_code == 0, "healthy: both runs complete")
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    check(off == on, "healthy: the failsafe group changes no write")

    # 2. A forced-missing preamble pointer removes exactly the guarded 5-byte
    #    jump, and nothing else.
    for key, image, build, missing, expected_bytes in (
        ("superlod", image_1080, 9, "SuperLODFix lookup", 5),
        ("playerasped", image_1080, 9, "PlayerAsPedComponentsFix model lookup", 5),
        ("garage", image_1080, 9, "GarageModelFix model lookup", 10),
    ):
        keys = {key_name: 1 for key_name in healthy}
        off_code, off_path = cell.run(f"{key}.off", image, build, "except:failsafe", keys, (missing,))
        on_code, on_path = cell.run(f"{key}.on", image, build, "all", keys, (missing,))
        off = parse_dump(off_path) if off_path.exists() else {}
        on = parse_dump(on_path) if on_path.exists() else {}
        removed = set(off) - set(on)
        added = set(on) - set(off)
        check(off_code == 0 and on_code == 0, f"{key}: both runs complete")
        check(not added, f"{key}: the guard adds no write", sorted(added))
        check(len(removed) == expected_bytes,
            f"{key}: the guard removes exactly {expected_bytes} byte(s)", sorted(removed))
        runs = contiguous_runs(removed)
        check(all(len(run) == 5 for run in runs) and len(runs) * 5 == expected_bytes,
            f"{key}: the removed bytes are 5-byte jump(s)", runs)
        check(all(off[run[0]] == 0xE9 for run in runs),
            f"{key}: each removed write is an E9", [(run, hex(off[run[0]])) for run in runs])
        check(all(off[rva] == on.get(rva, off[rva]) for rva in set(off) & set(on)),
            f"{key}: every other write is unchanged")

    # 3. The 1.0.0.0 fault: the original writes hits[1] through an uninitialised
    #    entry; with the group off that is the fault, with it on the P13 count
    #    check skips the partial install and the cell completes.
    no_impostor_on = {"NoImpostorHeightCutoff": 1}
    off_code, off_path = cell.run("impostor.off", image_1000, 1, "except:failsafe", no_impostor_on)
    on_code, on_path = cell.run("impostor.on", image_1000, 1, "all", no_impostor_on)
    check(status_is_access_violation(off_code),
        "impostor 1.0.0.0: except:failsafe reproduces the original 0xC0000005", hex(off_code & 0xFFFFFFFF))
    check(on_code == 0, "impostor 1.0.0.0: the failsafe group completes", hex(on_code & 0xFFFFFFFF))

    # On 1.0.8.0 the pattern has both hits, so the guard is inert there.
    off_code, off_path = cell.run("impostor1080.off", image_1080, 9, "except:failsafe", no_impostor_on)
    on_code, on_path = cell.run("impostor1080.on", image_1080, 9, "all", no_impostor_on)
    check(off_code == 0 and on_code == 0, "impostor 1.0.8.0: both runs complete")
    check(parse_dump(off_path) == parse_dump(on_path),
        "impostor 1.0.8.0: the count check is inert with both hits")

    # 4. RemoveGFWLUpdatePopup on a pre-patch-6 build: no xlive.dll exists here,
    #    so the captured base is zero and the guard refuses the jump. The 1:1 run
    #    still installs it (the original writes through the null base when the
    #    game runs the body).
    gfwl = {"NoImpostorHeightCutoff": 0, "RemoveGFWLUpdatePopup": 1}
    off_code, off_path = cell.run("gfwl.off", image_1000, 1, "except:failsafe", gfwl)
    on_code, on_path = cell.run("gfwl.on", image_1000, 1, "all", gfwl)
    off = parse_dump(off_path) if off_path.exists() else {}
    on = parse_dump(on_path) if on_path.exists() else {}
    removed = set(off) - set(on)
    check(off_code == 0 and on_code == 0, "gfwl: both runs complete")
    check(len(removed) == 5 and all(len(run) == 5 for run in contiguous_runs(removed)),
        "gfwl: the guard removes exactly the 5-byte stub jump", sorted(removed))
    check(all(off[run[0]] == 0xE9 for run in contiguous_runs(removed)),
        "gfwl: the removed write starts with an E9")

    print(f"{checks} check(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
