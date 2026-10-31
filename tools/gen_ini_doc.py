#!/usr/bin/env python3
"""Generate docs/ini.md and docs/features.md from the shipped INI and the
option row tables that were later inlined into dllmain.cpp. This script
needs those tables restored from the evidence archive; the committed docs are
the current source. Run from the repository root:

    python tools/gen_ini_doc.py

The generated files are committed so a reader does not need Python.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INI = ROOT / "ZolikaPatch.ini"
TABLES = ROOT / "src" / "patch_data.cpp"
DOCS = ROOT / "docs"

BUILD_NAMES = {
    0: "1.0.0.0",
    1: "1.0.0.0",
    5: "1.0.4.0",
    6: "1.0.0.4",
    7: "1.0.6.0",
    8: "1.0.7.0",
    9: "1.0.8.0",
    10: "1.1.0.0",
    11: "1.1.1.0",
    12: "1.1.2.0",
    13: "1.1.3.0",
    15: "1.0.6.1",
    18: "1.2.0.43",
}

STRING_KEYS = {"OverrideGTADatPathTo"}
MANUAL_BUILDS = {
    "LaunchWithPlayGTAIV": "IV 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0 (not EFLC)"
}
EXTRA = {
    "Patch7RemoveModChecks": (
        "Undocumented alias. Setting it to 1 also enables the "
        "Patch6and7RemoveModChecks install set; the loader adds the alias to "
        "the config object before the option passes run.",
        "0",
    ),
}

FAULTS = {
    "SkipIntro": "The original compresses every post-menu load screen when the "
                 "site runs again, not only the first intro. Preserved.",
    "SkipMenu": "The first zero menu id becomes 6 unconditionally; with no "
                "savegame the original does not fall back to a new game. Preserved.",
    "RemoveBoundingBoxCulling": "The branch is inverted relative to the option "
                                "name; enabling it can make the world vanish / crash "
                                "LAN MP. The shipped INI keeps it at 0, the original's "
                                "own default. The faulting code is present.",
    "DrivebyFOVFix": "The resolver result and the cached entry are dereferenced "
                     "without a null check. An exception in the resolver yields 0 and "
                     "the following [eax+0Eh] read faults, exactly like the original.",
    "NewSavesCompatibility": "fread results are ignored, a size below 9 underflows "
                             "the allocation count, and the allocation is not checked. "
                             "Preserved.",
    "ForceCarHeadlightShadows": "The shared model lookup and the object are "
                                "dereferenced unconditionally. A missing preamble "
                                "pointer or a null object faults. Preserved.",
    "RemoveGFWLUpdatePopup": "With xlive.dll absent the base is 0 and the four "
                             "stores hit the low 0x66F770 page (access violation). "
                             "Preserved.",
    "GarageModelFix": "The lookup helper and the table-entry chain are "
                      "dereferenced unconditionally. Preserved.",
    "AddOptionsInMenu": "The focus-loss callback writes its saved site "
                        "unconditionally; a failed prime scan leaves it null. "
                        "Preserved.",
    "OutOfCommissionFix": "The decoder runs on every call (its guard has no "
                          "writer), neither allocation is checked, and the archive is "
                          "leaked each time. Preserved.",
    "CutsceneFixes": "The resolver result and the scene object are dereferenced "
                     "without checks, and the body self-modifies the game's camera "
                     "function. Preserved.",
    "NewMemorySystem": "The game's own allocator init never runs; allocations are "
                       "filled with 0xCD. Preserved.",
    "NoImpostorHeightCutoff": "On 1.0.0.0 the first block matches once, so the "
                              "original writes through an uninitialised vector entry. "
                              "The row set is reproduced.",
    "DualVehicleHeadlights": "The old/new destination is selected by a build "
                             "range check; both install rows carry their own masks.",
    "CarDynamicShadowFix": "With ForceCarHeadlightShadows=1 the EA00 write "
                           "destroys the E870 pattern first, so the original's P2 "
                           "scan fails; the arm order is preserved.",
    "FixPCIDKickbug": "The HKCU GFWL PCID value is deleted on every launch when "
                      "the key is on (the arm's absent-key default is 1).",
}


def parse_ini():
    sections = {}
    comments = {}
    section = ""
    pending = []
    for raw in INI.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line.strip("[]")
            sections.setdefault(section, {})
            pending = []
            continue
        if line.startswith(";"):
            pending.append(line.lstrip("; ").strip())
            continue
        if not line:
            pending = []
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            key = key.strip()
            sections.setdefault(section, {})[key] = value.strip()
            if pending:
                comments[key] = " ".join(pending)
            pending = []
    return sections, comments


def parse_rows(name: str):
    text = TABLES.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"const \w+ {name}\[\] = \{{(.*?)\n\}};", text, re.S)
    if not m:
        return []
    rows = []
    for line in m.group(1).splitlines():
        s = line.strip()
        if not s.startswith("{"):
            continue
        rows.append(s[s.index("{") + 1:s.rindex("}")])
    return rows


def parse_catalog():
    out = {}
    for row in parse_rows("kFeatures"):
        parts = [p.strip() for p in re.split(r',(?=(?:[^"]*"[^"]*")*[^"]*$)', row)]
        if len(parts) < 10:
            continue
        key = parts[0].strip('"')
        out[key] = {
            "observed": parts[1].strip('"'),
            "handler": parts[2],
            "build_gate": parts[4] == "true",
            "mask": int(parts[5].replace("u", ""), 0),
            "class": parts[9].strip('"'),
        }
    return out


def parse_arm_defaults():
    out = {}
    for row in parse_rows("kDefaultValues"):
        m = re.match(r'\s*"([^"]*)"\s*,\s*"([^"]*)"', row)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def parse_build_gates():
    out = {}
    for row in parse_rows("kBuildGates"):
        m = re.match(r'\s*"([^"]*)"\s*,\s*(0x[0-9A-Fa-f]+)', row)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def mask_text(mask: int) -> str:
    if mask == 0xFFFFFFFF:
        return "all builds"
    names = []
    for i in range(1, 32):
        if mask & (1 << i) and i in BUILD_NAMES and BUILD_NAMES[i] not in names:
            names.append(BUILD_NAMES[i])
    return ", ".join(names) if names else "none"


def esc(text: str) -> str:
    return text.replace("|", "\\|")


def main():
    sections, comments = parse_ini()
    options = sections.get("Options", {})
    catalog = parse_catalog()
    arms = parse_arm_defaults()
    gates = parse_build_gates()
    keys = list(options)
    for key in catalog:
        if key not in keys:
            keys.append(key)
    for key in EXTRA:
        if key not in keys:
            keys.append(key)
    keys.sort(key=str.lower)

    DOCS.mkdir(exist_ok=True)
    ini = [
        "# ZolikaPatch.ini reference",
        "",
        "Every key lives in `[Options]`. A key that is absent is **not** off: the",
        "loader substitutes the original arm's default (the *absent-key* column).",
        "A value must parse as base 10 and equal exactly `1` to enable the option.",
        "The *shipped* column is the value in the bundled `ZolikaPatch.ini`.",
        "",
        "The `[PopupLightAngles]` and `[HeadlightConeOffsets]` sections hold 512",
        "vehicle slots each (`Car1Name`/`Car1Angle`, `Car1Name`/`Car1Offset`, ...).",
        "The name is hashed with the game's model hash; the angle is in degrees",
        "and is converted to radians at load. An absent or empty name leaves a",
        "hole; a name without its value makes the whole load fail.",
        "",
        "| key | absent-key | shipped | builds | type | effect |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for key in keys:
        info = catalog.get(key, {})
        absent = arms.get(key, EXTRA.get(key, ("", ""))[1] if key in EXTRA else "-")
        shipped = options.get(key, "-")
        mask = gates.get(key, info.get("mask", 0xFFFFFFFF))
        builds = MANUAL_BUILDS.get(
            key, mask_text(mask) if key in gates or info.get("build_gate") else "all builds")
        desc = comments.get(key) or EXTRA.get(key, ("", ""))[0] or info.get("class", "")
        kind = "string" if key in STRING_KEYS else "bool"
        if key in STRING_KEYS:
            absent = "-"
        ini.append(
            f"| `{key}` | {esc(absent)} | {esc(shipped)} | {esc(builds)} | {kind} | {esc(desc)} |"
        )
    (DOCS / "ini.md").write_text("\n".join(ini), encoding="utf-8")

    feat = [
        "# Features",
        "",
        "One section per `[Options]` key. The behaviour described is the",
        "original's, including the preserved faults called out at the end of a",
        "section. Builds listed as *all* use pattern-gated installs: a build",
        "where the signature is absent simply skips the feature.",
        "",
    ]
    for key in keys:
        info = catalog.get(key, {})
        absent = arms.get(key, EXTRA.get(key, ("", ""))[1] if key in EXTRA else "-")
        shipped = options.get(key, "-")
        mask = gates.get(key, info.get("mask", 0xFFFFFFFF))
        builds = MANUAL_BUILDS.get(
            key, mask_text(mask) if key in gates or info.get("build_gate") else "all builds")
        desc = comments.get(key) or EXTRA.get(key, ("", ""))[0] or "See the option table."
        feat += [f"## {key}", "", desc, "",
                 f"- **Default:** shipped `{shipped}`; absent-key `{absent}`.",
                 f"- **Builds:** {builds}.",
                 f"- **Install class:** {info.get('class', 'n/a')}."]
        if key in FAULTS:
            feat += ["", f"**Preserved behaviour.** {FAULTS[key]}"]
        feat += [""]
    (DOCS / "features.md").write_text("\n".join(feat), encoding="utf-8")
    print(f"wrote {len(keys)} keys")


if __name__ == "__main__":
    main()
