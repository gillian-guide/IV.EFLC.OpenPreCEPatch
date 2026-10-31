#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Automated INI write-set matrix for OpenPreCEPatch.

For every cell (shipped defaults, all options off/on, each option alone, each
section alone and the legacy ZolikaPatch.ini import) this tool generates a
temporary INI, runs ``opcep_parity_dump`` against a game executable and records
the resulting write set, its hash and the arm that produced each byte.

The committed result lives in ``tests/parity/matrix.json``; ``--check`` compares
the current tree against it. A pure refactor must keep it byte-identical, and an
improvement must change only the cells its new key owns.

Examples:
    python tools/opcep_matrix.py --check
    python tools/opcep_matrix.py --update
    python tools/opcep_matrix.py --cells 'option:Move|option:New' --report out.json
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini"
PARITY_DIR = ROOT / "tests" / "parity"
GOLDEN = PARITY_DIR / "matrix.json"
DEFAULT_EXE = ROOT / "build" / "x64" / "Release" / "opcep_parity_dump.exe"
DEFAULT_GAME = ROOT.parent / "game_refs" / "GTAIV.exe"
DEFAULT_LEGACY = PARITY_DIR / "fixtures" / "ZolikaPatch.legacy.ini"

TABLE_SECTIONS = {"PopupLightAngles", "HeadlightConeOffsets"}

LINE_RE = re.compile(
    r"^(?P<indent>\s*)(?P<key>[A-Za-z0-9_]+)"
    r"(?P<eq>\s*=\s*)(?P<value>[^/]*?)(?P<comment>\s*//.*)?$"
)


@dataclass
class Option:
    key: str
    section: str
    value: str
    is_bool: bool


@dataclass
class Template:
    lines: list[str]
    options: dict[str, Option] = field(default_factory=dict)
    sections: list[str] = field(default_factory=list)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_template(path: Path) -> Template:
    lines = path.read_text(encoding="utf-8").splitlines()
    template = Template(lines=lines)
    section = None
    table = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and "]" in stripped:
            section = stripped[1 : stripped.index("]")]
            table = section in TABLE_SECTIONS
            if not table and section not in template.sections:
                template.sections.append(section)
            continue
        if table or section is None or stripped.startswith(";") or "=" not in line:
            continue
        match = LINE_RE.match(line)
        if not match:
            continue
        key = match.group("key")
        value = match.group("value").strip()
        template.options[key] = Option(
            key=key,
            section=section,
            value=value,
            is_bool=value in {"0", "1"},
        )
    return template


def render(template: Template, overrides: dict[str, str]) -> str:
    """Rebuild the INI with the same comments/spacing, replacing values."""
    out: list[str] = []
    section = None
    table = False
    for line in template.lines:
        stripped = line.strip()
        if stripped.startswith("[") and "]" in stripped:
            section = stripped[1 : stripped.index("]")]
            table = section in TABLE_SECTIONS
            out.append(line)
            continue
        if table or section is None or stripped.startswith(";") or "=" not in line:
            out.append(line)
            continue
        match = LINE_RE.match(line)
        if not match:
            out.append(line)
            continue
        key = match.group("key")
        value = overrides.get(key, match.group("value").strip())
        comment = match.group("comment") or ""
        out.append(f"{match.group('indent')}{key}{match.group('eq')}{value}{comment}")
    return "\n".join(out) + "\n"


def cell_cells(template: Template) -> list[tuple[str, dict[str, str], str]]:
    bool_keys = [key for key, option in template.options.items() if option.is_bool]
    all_off = {key: "0" for key in bool_keys}
    cells: list[tuple[str, dict[str, str], str]] = [
        ("defaults", {}, "all-off"),
        ("all-off", dict(all_off), "all-off"),
        ("all-on", {key: "1" for key in bool_keys}, "all-off"),
    ]
    for key in bool_keys:
        overrides = dict(all_off)
        overrides[key] = "1"
        cells.append((f"option:{key}", overrides, "all-off"))
    for section in template.sections:
        keys = [
            key
            for key, option in template.options.items()
            if option.is_bool and option.section == section
        ]
        if not keys:
            continue
        overrides = dict(all_off)
        for key in keys:
            overrides[key] = "1"
        cells.append((f"section:{section}", overrides, "all-off"))
    # Dependent keys: measure against the option they modify, not all-off.
    if "SkipIntro" in all_off and "SkipIntroShowScreens" in all_off:
        combo = dict(all_off)
        combo["SkipIntro"] = "1"
        combo["SkipIntroShowScreens"] = "1"
        cells.append(("combo:SkipIntroShowScreens", combo, "option:SkipIntro"))
    cells.append(("legacy", {}, "all-off"))
    return cells


def safe_name(cell: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", cell)


def parse_writes(path: Path) -> list[tuple[int, int]]:
    writes: list[tuple[int, int]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        rva, value = line.split()
        writes.append((int(rva, 16), int(value, 16)))
    return writes


def parse_arms(path: Path) -> dict[int, tuple[int, str]]:
    arms: dict[int, tuple[int, str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split(maxsplit=3)
        if len(parts) != 4:
            continue
        rva, _old, new, arm = parts
        arms[int(rva, 16)] = (int(new, 16), arm)
    return arms


def canonical_writes(writes: list[tuple[int, int]]) -> bytes:
    return "".join(f"{rva:08X} {value:02X}\n" for rva, value in writes).encode()


def canonical_delta(rows: list[list]) -> bytes:
    return "".join(f"{rva:08X} {value:02X} {arm}\n" for rva, value, arm in rows).encode()


def arm_counts(rows: list[list]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for _rva, _value, arm in rows:
        counts[arm] = counts.get(arm, 0) + 1
    return dict(sorted(counts.items()))


def run_dump(
    exe: Path, game: Path, ini: Path, out: Path, attr: Path | None, cwd: Path,
    env_overrides: dict[str, str] | None = None,
) -> None:
    command = [str(exe), str(game), str(ini), str(out)]
    if attr is not None:
        command += ["--attribution", str(attr)]
    env = None
    if env_overrides:
        env = os.environ.copy()
        env.update(env_overrides)
    result = subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{' '.join(command)} failed ({result.returncode}): "
            f"{result.stderr.decode(errors='replace').strip()}"
        )


def build_cell(
    name: str,
    overrides: dict[str, str],
    template: Template,
    exe: Path,
    game: Path,
    work: Path,
    legacy_import: Path,
    env_overrides: dict[str, str],
) -> dict:
    ini = work / f"{safe_name(name)}.ini"
    out = work / f"{safe_name(name)}.writes.txt"
    attr = work / f"{safe_name(name)}.arms.txt"

    if name == "legacy":
        ini = legacy_import
    else:
        ini.write_text(render(template, overrides), encoding="utf-8")

    run_dump(exe, game, ini, out, attr, ROOT, env_overrides)
    writes = parse_writes(out)
    arms = parse_arms(attr)
    attributed = [
        [rva, value, arms.get(rva, (value, "unattributed"))[1]] for rva, value in writes
    ]
    return {
        "ini": ini.name,
        "writes": len(writes),
        "sha256": hashlib.sha256(canonical_writes(writes)).hexdigest(),
        "arms": attributed,
        "_write_rows": writes,
    }


def delta_against(base: list[list], cell: list[list]) -> list[list]:
    base_map = {rva: value for rva, value, _arm in base}
    return [row for row in cell if base_map.get(row[0]) != row[1]]


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def compare(expected: dict, actual: dict) -> list[str]:
    problems: list[str] = []
    if expected.get("game", {}).get("sha256") != actual["game"]["sha256"]:
        problems.append(
            "game image differs: golden {} vs current {}".format(
                expected.get("game", {}).get("sha256", "?")[:12],
                actual["game"]["sha256"][:12],
            )
        )
    if expected.get("template_sha256") != actual["template_sha256"]:
        problems.append(
            "INI template differs: golden {} vs current {}".format(
                str(expected.get("template_sha256"))[:12],
                actual["template_sha256"][:12],
            )
        )

    old_cells = expected.get("cells", {})
    new_cells = actual["cells"]
    for name in sorted(set(old_cells) | set(new_cells)):
        if name not in new_cells:
            problems.append(f"{name}: cell missing from current run")
            continue
        if name not in old_cells:
            problems.append(f"{name}: new cell ({new_cells[name]['writes']} writes)")
            continue
        old = old_cells[name]
        new = new_cells[name]
        if old["sha256"] != new["sha256"] or old["writes"] != new["writes"]:
            problems.append(
                f"{name}: write set changed ({old['writes']} -> {new['writes']} writes, "
                f"{old['sha256'][:12]} -> {new['sha256'][:12]})"
            )
            continue
        if old.get("arms") != new.get("arms"):
            problems.append(f"{name}: arm attribution changed {old.get('arms')} -> {new.get('arms')}")
            continue
        if old.get("delta_base") != new.get("delta_base"):
            problems.append(
                f"{name}: delta base changed {old.get('delta_base')} -> "
                f"{new.get('delta_base')}"
            )
            continue
        if old.get("delta_sha256") != new.get("delta_sha256") or old.get(
            "delta_writes"
        ) != new.get("delta_writes"):
            problems.append(
                f"{name}: delta changed ({old.get('delta_writes')} -> "
                f"{new.get('delta_writes')} bytes)"
            )
            continue
        if old.get("delta_arms") != new.get("delta_arms"):
            problems.append(
                f"{name}: delta attribution changed {old.get('delta_arms')} -> "
                f"{new.get('delta_arms')}"
            )
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare with the golden matrix (default)")
    parser.add_argument("--update", action="store_true", help="rewrite the golden matrix")
    parser.add_argument("--report", type=Path, help="write the computed matrix here")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--legacy", type=Path, default=DEFAULT_LEGACY)
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument("--cells", help="only run cells whose name matches this regex")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                        help="set an environment variable for every dump run "
                             "(e.g. --env OPCEP_IMPROVEMENTS=none)")
    parser.add_argument("--keep", action="store_true", help="keep temporary INIs under .scratch")
    args = parser.parse_args(argv)

    env_overrides: dict[str, str] = {}
    for item in args.env:
        if "=" not in item:
            parser.error(f"--env expects NAME=VALUE, got {item!r}")
        name, value = item.split("=", 1)
        env_overrides[name] = value

    if not args.exe.exists():
        parser.error(f"dump executable not found: {args.exe}")
    if not args.game.exists():
        parser.error(f"game executable not found: {args.game}")
    if not args.legacy.exists():
        parser.error(f"legacy INI fixture not found: {args.legacy}")

    template = parse_template(TEMPLATE)
    cells = cell_cells(template)
    if args.cells:
        pattern = re.compile(args.cells)
        cells = [cell for cell in cells if pattern.search(cell[0])]
    cell_bases = {name: base for name, _overrides, base in cells}

    work = ROOT / ".scratch" / "matrix"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    legacy_import = work / "legacy.imported.ini"
    result = subprocess.run(
        [str(args.exe), "--import-legacy", str(args.legacy), str(legacy_import)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        return 1

    computed: dict[str, dict] = {}
    print(
        f"opcep_matrix: {len(cells)} cells, {args.jobs} jobs, "
        f"game={args.game.name}",
        flush=True,
    )
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(
                build_cell,
                name,
                overrides,
                template,
                args.exe,
                args.game,
                work,
                legacy_import,
                env_overrides,
            ): name
            for name, overrides, _base in cells
        }
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            name = futures[future]
            computed[name] = future.result()
            if done % 10 == 0 or done == len(cells):
                print(f"  {done}/{len(cells)} cells", flush=True)

    # The per-option/section entries record only their delta against all-off;
    # every cell keeps its full write set in tests/parity/writesets and every
    # delta its per-byte attribution in tests/parity/deltas.
    all_off_rows = computed.get("all-off", {}).get("arms", [])
    raw_rows = {name: cell.pop("arms") for name, cell in computed.items()}
    for name, cell in computed.items():
        rows = raw_rows[name]
        cell.pop("ini", None)
        cell["arms"] = arm_counts(rows)
        cell["file"] = f"writesets/{safe_name(name)}.txt"
        cell["_rows"] = rows
        if name.startswith(("option:", "section:", "combo:")):
            base = cell_bases.get(name, "all-off")
            base_rows = raw_rows.get(base, all_off_rows)
            delta = delta_against(base_rows, rows)
            cell["delta_base"] = base
            cell["delta_writes"] = len(delta)
            cell["delta_sha256"] = hashlib.sha256(canonical_delta(delta)).hexdigest()
            cell["delta_arms"] = arm_counts(delta)
            cell["delta_file"] = f"deltas/{safe_name(name)}.txt"
            cell["_delta_rows"] = delta

    matrix = {
        "schema": 1,
        "generated_by": "tools/opcep_matrix.py",
        "game": {
            "file": args.game.name,
            "size": args.game.stat().st_size,
            "sha256": sha256_file(args.game),
        },
        "template_sha256": sha256_file(TEMPLATE),
        "cells": computed,
    }

    def write_baselines(destination: Path) -> None:
        writesets = destination / "writesets"
        deltas = destination / "deltas"
        writesets.mkdir(parents=True, exist_ok=True)
        deltas.mkdir(parents=True, exist_ok=True)
        for name, cell in computed.items():
            writesets.joinpath(f"{safe_name(name)}.txt").write_text(
                canonical_writes(cell["_write_rows"]).decode(), encoding="utf-8"
            )
            if "_delta_rows" in cell:
                deltas.joinpath(f"{safe_name(name)}.txt").write_text(
                    canonical_delta(cell["_delta_rows"]).decode(), encoding="utf-8"
                )
        for stale in writesets.glob("*.txt"):
            if stale.stem not in {safe_name(name) for name in computed}:
                stale.unlink()
        for stale in deltas.glob("*.txt"):
            if stale.stem not in {safe_name(name) for name in computed}:
                stale.unlink()

    public_matrix = {
        key: (
            {
                cell_name: {k: v for k, v in cell.items() if not k.startswith("_")}
                for cell_name, cell in value.items()
            }
            if key == "cells"
            else value
        )
        for key, value in matrix.items()
    }

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(public_matrix, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"opcep_matrix: wrote {args.report}", flush=True)

    status = 0
    if args.update:
        PARITY_DIR.mkdir(parents=True, exist_ok=True)
        write_baselines(PARITY_DIR)
        GOLDEN.write_text(
            json.dumps(public_matrix, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"opcep_matrix: updated {GOLDEN}", flush=True)
    else:
        if not GOLDEN.exists():
            print(f"opcep_matrix: no golden at {GOLDEN}; run --update", file=sys.stderr)
            status = 1
        else:
            problems = compare(load_json(GOLDEN), public_matrix)
            if problems:
                print("opcep_matrix: FAIL", file=sys.stderr)
                for problem in problems:
                    print(f"  {problem}", file=sys.stderr)
                status = 1
            else:
                print(
                    f"opcep_matrix: PASS ({len(computed)} cells, "
                    f"{sum(cell['writes'] for cell in computed.values())} writes)",
                    flush=True,
                )

    if not args.keep:
        shutil.rmtree(work)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
