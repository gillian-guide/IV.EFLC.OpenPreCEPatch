#!/usr/bin/env python3
"""Emit the per-feature installers from the recovered patch tables.

One-off recovery step, kept for provenance: it reads the row tables recovered
from the shipped ASI (`patch_data.cpp`) and the global/body mappings
(`rebased.cpp`, `hooks.cpp`), then writes the installers a human would have
written - one function per INI option, each scanning its signature and calling
`injector::` directly.

    python tools/gen_installers.py

It rewrites `patches.cpp` in place and reduces `patch_data.cpp` to the
RemoveUselessChecks list. The tables it consumes come from the evidence archive;
they are not needed by the build afterwards.

Historical note: this generator once routed slot stores through a `Publish()`
trace wrapper. The checked-in `patches.cpp` is hand-maintained now and stores
the globals directly (as the original handler did); do not regenerate over it.
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCH_DATA = ROOT / "src" / "patch_data.cpp"
PATCHES = ROOT / "src" / "patches.cpp"
HOOKS = ROOT / "src" / "hooks.cpp"
REBASED = ROOT / "src" / "rebased.cpp"

BUILDS = ["v1000", "v1040", "v1004", "v1060", "v1070", "v1080", "v1061",
          "v1100", "v1110", "v1120", "v1130", "v12043"]
ORDINAL = {"v1000": 1, "v1040": 5, "v1004": 6, "v1060": 7, "v1070": 8, "v1080": 9,
           "v1100": 10, "v1110": 11, "v1120": 12, "v1130": 13, "v1061": 15, "v12043": 18}
BY_ORDINAL = {v: k for k, v in ORDINAL.items()}

# Bodies whose install is deliberately withheld: the original writes these jumps
# inside its own module, whose layout this one does not reproduce.
# Every destination the shipped mod jumps into has a body here, so nothing is
# withheld. (The set stays so a future row that must be dropped is one edit.)
WITHHELD = set()

MODULE_LOW, MODULE_HIGH = 0x10000000, 0x101AFFFF


# ---------------------------------------------------------------- parsing
def table_body(text, name):
    start = re.search(r"^\s*const\s+\w+\s+" + re.escape(name) + r"\[\]\s*=\s*\{", text, re.M)
    if not start:
        raise SystemExit(f"table not found: {name}")
    i, depth = start.end(), 1
    while depth and i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[start.end():i - 1]


def split_rows(body):
    out, depth, start, i = [], 0, None, 0
    while i < len(body):
        c = body[i]
        if c == '"':
            i += 1
            while i < len(body) and body[i] != '"':
                i += 2 if body[i] == "\\" else 1
        elif c == "{":
            if depth == 0:
                start = i
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                out.append(body[start + 1:i])
        i += 1
    return out


def split_fields(row):
    out, depth, cur, i = [], 0, [], 0
    while i < len(row):
        c = row[i]
        if c == '"':
            j = i + 1
            while j < len(row) and row[j] != '"':
                j += 2 if row[j] == "\\" else 1
            cur.append(row[i:j + 1])
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def value(text):
    text = text.strip()
    if text.startswith('"'):
        return text.strip('"')
    if text.startswith("{"):
        return [value(v) for v in split_fields(text.strip("{}"))]
    if text == "true":
        return True
    if text == "false":
        return False
    return int(re.sub(r"[uUlL]+$", "", text), 0)


def rows_of(text, name):
    return [[value(f) for f in split_fields(r)] for r in split_rows(table_body(text, name))]


def parse_mapping(text, var):
    out = {}
    for match in re.finditer(r'if \(' + var + r' == "([^"]+)"\)\s*\n?\s*return [^;]*?&?(\w+)\s*\)?\s*;', text):
        out[match.group(1)] = match.group(2)
    return out


def parse_body_decls(text, names):
    out = {}
    for name in names:
        match = re.search(
            r'((?:extern\s+"C"\s*)?(?:__declspec\(\w+\)\s*)?[A-Za-z_][^;{}\n]*?)'
            r'\b' + re.escape(name) + r'\s*(\([^;{]*\))\s*(?:noexcept\s*)?(?://[^\n]*)?\s*\{', text)
        if match:
            out[name] = (match.group(1).strip(), match.group(2).strip())
    return out


def parse_bodies(text):
    out = {}
    for match in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+)u?\s*,\s*[^}]*?&(\w+)\s*\)?\s*\}", text):
        out[int(match.group(1), 16)] = match.group(2)
    return out


# ---------------------------------------------------------------- helpers
def bits(names):
    mask = 0
    for name in names:
        mask |= 1 << ORDINAL[name]
    return mask


def builds_of(mask):
    return [b for b in BUILDS if mask & (1 << ORDINAL[b])]


def rva_allows(gates, handler, rva, ordinal):
    for g_handler, g_rva, kind, g_value in gates:
        if g_handler != handler or g_rva != rva:
            continue
        return [ordinal == g_value, ordinal >= g_value, ordinal > g_value,
                ordinal <= g_value, ordinal < g_value, True][kind]
    return False


ACCESSORS = {
    "g_dlc_register_rva": "DlcRegisterRva()",
    "g_dlc_apply_rva": "DlcApplyRva()",
    "g_startup_time": "StartupTimeGlobal()",
}


def global_lvalue(key, name, lookup):
    if name in ACCESSORS:
        return ACCESSORS[name]
    if name.startswith("zpatch_"):
        return name
    return f'*reinterpret_cast<std::uint32_t*>({lookup}("{key}"))'


def symbol_of(option):
    base, _, suffix = option.partition("@")
    name = "Install" + re.sub(r"[^0-9A-Za-z]", "", base)
    if suffix:
        clean = re.sub(r"[^0-9A-Za-z]", "", suffix)
        name += clean[:1].upper() + clean[1:]
    return name


class Emitter:
    def __init__(self):
        self.lines, self.indent = [], 0

    def raw(self, text=""):
        self.lines.append(("\t" * self.indent + text) if text else "")

    def open(self, text):
        self.raw(text)
        self.raw("{")
        self.indent += 1

    def close(self, text="}"):
        self.indent -= 1
        self.raw(text)

    def text(self):
        return "\n".join(self.lines)


def hex_text(text):
    return re.sub(r"\s+", " ", text).strip().upper()


def build_list(builds):
    return ", ".join(f"GameBuild::{b}" for b in builds)


def main():
    data = PATCH_DATA.read_text(encoding="utf-8", errors="replace")
    hooks = HOOKS.read_text(encoding="utf-8", errors="replace")
    rebased = REBASED.read_text(encoding="utf-8", errors="replace")
    old_patches = PATCHES.read_text(encoding="utf-8", errors="replace")

    bodies = parse_bodies(hooks)
    slot_globals = parse_mapping(rebased, "slot_name")
    symbol_globals = parse_mapping(rebased, "symbol")
    option_globals = parse_mapping(rebased, "option")

    t_patches = rows_of(data, "kPatches")
    t_rva = rows_of(data, "kRvaPatches")
    t_nop = rows_of(data, "kNopPatches")
    t_rva_gates = rows_of(data, "kRvaGates")
    t_build_gates = rows_of(data, "kBuildGates")
    t_defaults = rows_of(data, "kDefaultValues")
    t_rva_jumps = rows_of(data, "kRvaJumps")
    t_sig_jumps = rows_of(data, "kSignatureJumps")
    t_indirect = rows_of(data, "kIndirectWrites")
    t_match_jumps = rows_of(data, "kMatchJumps")
    t_match_literals = rows_of(data, "kMatchLiterals")
    t_literal_stores = rows_of(data, "kLiteralStores")
    t_features = rows_of(data, "kFeatures")
    t_anchors = rows_of(data, "kAnchors")
    t_suppressed = rows_of(data, "kSuppressedPatches")
    t_sig_patches = rows_of(data, "kSignaturePatches")
    t_sig_writes = rows_of(data, "kSignatureWrites")
    t_slots = rows_of(data, "kSlotPatches")

    defaults = {row[0]: row[1] for row in t_defaults}
    anchors = defaultdict(list)
    for option, signature in t_anchors:
        anchors[option].append(signature)
    build_gates = {row[0]: row[1] for row in t_build_gates}
    feature_masks = {row[0]: row[5] for row in t_features}


    def option_builds(option):
        mask = build_gates.get(option)
        if mask is None or mask == 0xFFFFFFFF:
            return set(BUILDS)
        return set(builds_of(mask))

    def feature_builds(option):
        mask = feature_masks.get(option)
        if mask is None or mask == 0xFFFFFFFF:
            return set(BUILDS)
        return set(builds_of(mask))

    work = defaultdict(list)

    def add(option, kind, *payload):
        work[option].append((kind,) + payload)

    for row in t_slots:
        option, signature, slot, kind, field, extra, site_field, index, build = row
        builds = {BY_ORDINAL[build]} if build else set(BUILDS)
        add(option, "slot", signature, slot, kind, field, extra, site_field, index,
            builds & option_builds(option))

    # A body jump goes in only when every body jump of that option resolves: the
    # original branches into a whole handler, not into half of one.
    unresolved = {row[0] for row in t_rva
                  if row[3] == 1 and (row[5] in WITHHELD or row[5] not in bodies)}
    for row in t_rva:
        option, site_rva, opcode, kind, build, operand, _reserved, slot = row
        builds = {BY_ORDINAL[build]} if build else set(BUILDS)
        if kind == 1 and option in unresolved:
            continue
        add(option, "rva", site_rva, opcode, kind, operand, slot, builds)

    groups = defaultdict(list)
    for row in t_sig_patches:
        option, signature, site_offset, opcode, dest_kind, dest, site_deref, dest_symbol, bts = row
        groups[(option, signature)].append(
            (site_offset, opcode, dest_kind, dest, site_deref, dest_symbol, bts))
    for (option, signature), members in groups.items():
        if any(m[2] == 0 and (m[3] in WITHHELD or m[3] not in bodies) for m in members):
            continue
        if any(m[2] == 4 and m[3] > 0xFF for m in members):
            continue
        add(option, "siginstall", signature, members, feature_builds(option))

    for option, signature, site_offset, bts, index, fallback in t_sig_writes:
        add(option, "sigwrite", signature, site_offset, bts, index, fallback, feature_builds(option))

    for option, module_rva, opcode, destination, handler in t_rva_jumps:
        if destination in WITHHELD or destination not in bodies:
            continue
        builds = {name for name in feature_builds(option)
                  if rva_allows(t_rva_gates, handler, module_rva, ORDINAL[name])}
        if builds:
            add(option, "rvajump", module_rva, opcode, destination, builds)

    for option, signature, offset, destination in t_sig_jumps:
        if destination in WITHHELD or destination not in bodies:
            continue
        add(option, "sigjump", signature, offset, destination, feature_builds(option))

    for option, signature, offset, width, literal in t_indirect:
        add(option, "indirect", signature, offset, width, literal, feature_builds(option))

    for option, signature, index, offset, destination, opcode in t_match_jumps:
        if destination in WITHHELD or destination not in bodies:
            continue
        add(option, "matchjump", signature, index, offset, destination, opcode, feature_builds(option))

    for option, signature, index, offset, width, literal in t_match_literals:
        add(option, "matchliteral", signature, index, offset, width, literal, feature_builds(option))

    for option, signature, offset, width, literal in t_literal_stores:
        add(option, "literalstore", signature, offset, width, literal, feature_builds(option))

    rva_sites = defaultdict(list)
    for row in t_rva:
        if row[3] not in (3, 5):
            rva_sites[row[1]].append(row[0])
    jump_sites = {(row[0], row[1], row[2]) for row in t_sig_jumps}
    jump_sites |= {(row[0], row[1], row[3]) for row in t_match_jumps}
    suppressed = {(row[0], row[1], row[2], hex_text(row[3]).replace(" ", "")) for row in t_suppressed}

    for row in t_patches:
        option, signature, offset, length, bts, module_rva, handler = row
        if length == 1 and hex_text(bts) in ("E9", "E8"):
            continue
        if (option, signature, offset) in jump_sites:
            continue
        if (handler, signature, offset, hex_text(bts).replace(" ", "")) in suppressed:
            continue
        if module_rva and option in rva_sites.get(module_rva, []):
            continue
        builds = option_builds(option)
        if module_rva:
            builds = {name for name in builds
                      if rva_allows(t_rva_gates, handler, module_rva, ORDINAL[name])}
        if builds:
            add(option, "literal", signature, offset, bts, module_rva, builds)

    # Two recovered rows can publish the same value into the same slot (one from
    # the slot table, one from the RVA-install table); a human would write it once.
    for option in list(work):
        seen = set()
        kept = []
        for kind, *payload in work[option]:
            key = None
            if kind == "slot" and payload[2] == 3:
                key = ("rva", payload[1], payload[4])
            elif kind == "rva" and payload[2] == 3:
                key = ("rva", payload[4], payload[3])
            if key is not None:
                if key in seen:
                    continue
                seen.add(key)
            kept.append((kind, *payload))
        work[option] = kept

    order = []
    for table in (t_patches, t_rva, t_slots, t_sig_patches, t_sig_writes, t_rva_jumps,
                  t_sig_jumps, t_indirect, t_match_jumps, t_match_literals, t_literal_stores):
        for row in table:
            if row[0] not in order:
                order.append(row[0])
    order = [o for o in order if work.get(o)]

    # ------------------------------------------------------------ preamble
    tail = old_patches[old_patches.index("PreambleReport FindGamePointers"):]
    constants = []
    for line in old_patches.splitlines():
        if line.startswith("constexpr ") and re.search(r"\bk[A-Za-z0-9_]+\b", line):
            name = re.search(r"\bk[A-Za-z0-9_]+\b", line).group(0)
            if name in tail and name not in [c.split("=")[0].split()[-1] for c in constants]:
                constants.append(line.rstrip())

    # ------------------------------------------------------------ emit
    out = Emitter()
    out.raw("// The installers, one per INI option, in the original loader's order.")
    out.raw("//")
    out.raw("// Each function is the arm the shipped mod runs for that option: gate on the")
    out.raw("// INI key and the build, scan the site, then call injector:: directly. Slots")
    out.raw("// are published before the site they describe is overwritten, because the")
    out.raw("// original reads a resume out of the site first. A missing signature is a clean")
    out.raw("// skip; a wrong write is not.")
    out.raw()
    out.raw('#include "pch.h"')
    out.raw('#include "patches.h"')
    out.raw()
    out.raw("#include <cstdio>")
    out.raw("#include <cstring>")
    out.raw("#include <initializer_list>")
    out.raw("#include <string_view>")
    out.raw("#include <vector>")
    out.raw()
    out.raw("#define WIN32_LEAN_AND_MEAN")
    out.raw("#include <windows.h>")
    out.raw()
    out.raw("#if defined(ZP_DUMP_TOOL)")
    out.raw('// The write-set dump is a 64-bit harness; the vendored injector is 32-bit.')
    out.raw('#include "../tests/dump_injector/injector/injector.hpp"')
    out.raw("#elif defined(ZP_TRACE_WRITES)")
    out.raw("// Debug ASI: the same stores, logged for a live parity check.")
    out.raw('#include "../tests/trace_injector/injector/injector.hpp"')
    out.raw("#else")
    out.raw('#include "injector/injector.hpp"')
    out.raw("#endif")
    out.raw()
    out.raw('#include "hooks.h"')
    out.raw('#include "patch_data.h"')
    out.raw('#include "rebased.h"')
    out.raw()
    out.raw("// The bodies live in hooks.cpp; an installer takes their address.")
    decls = parse_body_decls(hooks, sorted(set(bodies.values())))
    for name in sorted(set(bodies.values())):
        prefix, params = decls.get(name, ("void", "()"))
        prefix = " ".join(prefix.replace("__declspec(naked)", "").split())
        out.raw(f"{prefix} {name}{params};")
    out.open("namespace")
    out.raw("constexpr std::size_t kDwordLength = 4;")
    out.raw("constexpr std::uint32_t kFusionFixProbeRva = 0x20D7u + 4u;")
    out.raw()
    out.raw("bool bMenuIntegration = false;")
    out.raw()
    out.raw("bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds)")
    out.raw("{")
    out.raw("\tfor (const GameBuild candidate : builds)")
    out.raw("\t{")
    out.raw("\t\tif (candidate == build)")
    out.raw("\t\t\treturn true;")
    out.raw("\t}")
    out.raw("\treturn false;")
    out.raw("}")
    out.raw()
    out.raw("// A signature scan; a miss is the one failure an arm tolerates.")
    out.open("std::uint8_t* Scan(const GameImage& image, std::string_view signature)")
    out.raw("Match match;")
    out.raw("if (!FindPattern({image.base, image.size}, signature, match))")
    out.raw("\treturn nullptr;")
    out.raw("return const_cast<std::uint8_t*>(match.address);")
    out.close()
    out.raw()
    out.open("std::vector<std::uint8_t*> ScanAll(const GameImage& image, std::string_view signature)")
    out.raw("std::vector<const std::uint8_t*> found;")
    out.raw("FindAllPatterns({image.base, image.size}, signature, found);")
    out.raw()
    out.raw("std::vector<std::uint8_t*> matches;")
    out.raw("matches.reserve(found.size());")
    out.raw("for (const std::uint8_t* match : found)")
    out.raw("\tmatches.push_back(const_cast<std::uint8_t*>(match));")
    out.raw("return matches;")
    out.close()
    out.raw()
    out.raw("// A dword field inside a match: a call displacement, or a site pointer.")
    out.open("std::uint32_t Field(const std::uint8_t* match, std::ptrdiff_t offset)")
    out.raw("std::uint32_t value = 0;")
    out.raw("std::memcpy(&value, match + offset, sizeof(value));")
    out.raw("return value;")
    out.close()
    out.raw()
    out.raw("bool InImage(const GameImage& image, std::uintptr_t address)")
    out.raw("{")
    out.raw("\tconst auto base = reinterpret_cast<std::uintptr_t>(image.base);")
    out.raw("\treturn address >= base && address + kDwordLength <= base + image.size;")
    out.raw("}")
    out.raw()
    out.raw("// Where the branch at `siteOffset` goes: the original re-points these.")
    out.open("std::uintptr_t BranchTarget(const std::uint8_t* match, std::ptrdiff_t siteOffset)")
    out.raw("const auto displacement = static_cast<std::int32_t>(Field(match, siteOffset + 2));")
    out.raw("return reinterpret_cast<std::uintptr_t>(match) + siteOffset + 6 +")
    out.raw("       static_cast<std::uintptr_t>(displacement);")
    out.close()
    out.raw()
    out.open("std::uint8_t Nibble(char c)")
    out.raw("if (c >= '0' && c <= '9')")
    out.raw("\treturn static_cast<std::uint8_t>(c - '0');")
    out.raw("if (c >= 'A' && c <= 'F')")
    out.raw("\treturn static_cast<std::uint8_t>(c - 'A' + 10);")
    out.raw("if (c >= 'a' && c <= 'f')")
    out.raw("\treturn static_cast<std::uint8_t>(c - 'a' + 10);")
    out.raw("return 0;")
    out.close()
    out.raw()
    out.raw("// Hex text to bytes, then a blind store: the original never validates a site.")
    out.open("void WriteHex(std::uint8_t* site, std::string_view hex)")
    out.raw("std::uint8_t bytes[64];")
    out.raw("std::size_t length = 0;")
    out.raw("std::size_t i = 0;")
    out.open("while (i + 1 < hex.size() && length < sizeof(bytes))")
    out.raw("if (hex[i] == ' ')")
    out.open("")
    out.raw("++i;")
    out.raw("continue;")
    out.close()
    out.raw("bytes[length++] = static_cast<std::uint8_t>((Nibble(hex[i]) << 4) | Nibble(hex[i + 1]));")
    out.raw("i += 2;")
    out.close()
    out.raw("injector::WriteMemoryRaw(site, bytes, length, true);")
    out.close()
    out.raw()
    out.raw("// Slots are the pointers the shims tail-jump to. Publishing goes through")
    out.raw("// one function so a debug build can log the value (ZP_TRACE_WRITES).")
    out.raw("void TraceSlot(const void* slot, std::uint32_t value) noexcept")
    out.raw("{")
    out.raw("#if defined(ZP_TRACE_WRITES)")
    out.raw("\tstatic std::FILE* log = nullptr;")
    out.raw("\tif (log == nullptr)")
    out.raw("\t\tlog = std::fopen(\"ZolikaPatch.writes.log\", \"a\");")
    out.raw("\tif (log == nullptr)")
    out.raw("\t\treturn;")
    out.raw("\tstd::fprintf(log, \"slot %08lX %08lX\\n\",")
    out.raw("\t             static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(slot)),")
    out.raw("\t             static_cast<unsigned long>(value));")
    out.raw("\tstd::fflush(log);")
    out.raw("#else")
    out.raw("\t(void)slot;")
    out.raw("\t(void)value;")
    out.raw("#endif")
    out.raw("}")
    out.raw()
    out.raw("void Publish(std::uint32_t& slot, std::uint32_t value) noexcept")
    out.raw("{")
    out.raw("\tTraceSlot(&slot, value);")
    out.raw("\tslot = value;")
    out.raw("}")
    out.raw()
    out.raw("// A width-wide literal, little-endian.")
    out.open("void WriteValue(std::uint8_t* site, std::size_t width, std::uint32_t value)")
    out.raw("std::uint8_t bytes[kDwordLength];")
    out.raw("for (std::size_t i = 0; i < width; ++i)")
    out.raw("\tbytes[i] = static_cast<std::uint8_t>(value >> (8 * i));")
    out.raw("injector::WriteMemoryRaw(site, bytes, width, true);")
    out.close()
    out.raw()
    out.open("void Fill(std::uint8_t* site, std::size_t length, std::uint8_t value)")
    out.raw("std::uint8_t chunk[16];")
    out.raw("std::memset(chunk, value, sizeof(chunk));")
    out.open("for (std::size_t done = 0; done < length;)")
    out.raw("const std::size_t size = length - done < sizeof(chunk) ? length - done : sizeof(chunk);")
    out.raw("injector::WriteMemoryRaw(site + done, chunk, size, true);")
    out.raw("done += size;")
    out.close()
    out.close()
    out.raw()
    out.raw("// The FusionFix compatibility probe: a build of the fix we cannot share a")
    out.raw("// module with keeps its own bytes at 0x20D3/0x20D7.")
    out.open("bool FusionFixGateClosed()")
    out.raw('const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");')
    out.raw("if (module == nullptr)")
    out.raw("\treturn false;")
    out.raw("const auto* image = reinterpret_cast<const std::uint8_t*>(module);")
    out.raw("return ModuleLacksSignature(image, kFusionFixProbeRva);")
    out.close()
    out.raw()
    out.open("bool NewFilesConflictModulesPresent()")
    out.raw('return GetModuleHandleA("GTAC.dll") != nullptr ||')
    out.raw('       GetModuleHandleA("JacksIVFixes.asi") != nullptr;')
    out.close()
    out.raw()
    out.raw("// RemoveUselessChecks: the per-build NOP list, written before any scan so the")
    out.raw("// later arms see the same bytes the original did.")
    out.open("void ApplyUselessChecks(const PatchContext& ctx)")
    out.raw(f'if (!ctx.Option("RemoveUselessChecks", "{defaults.get("RemoveUselessChecks", "1")}"))')
    out.raw("\treturn;")
    out.raw()
    out.open("for (std::size_t i = 0; i < kUselessCheckCount; ++i)")
    out.raw("const UselessCheck& check = kUselessChecks[i];")
    out.raw("if (check.build != static_cast<std::uint8_t>(ctx.build))")
    out.raw("\tcontinue;")
    out.raw("if (static_cast<std::size_t>(check.rva) + check.length > ctx.image.size)")
    out.raw("\tcontinue;")
    out.raw("injector::WriteMemoryRaw(ctx.image.base + check.rva,")
    out.raw("\t                        const_cast<std::uint8_t*>(check.bytes), check.length, true);")
    out.close()
    out.close()

    def emit_arm(option, rows, name):
        by_mask = defaultdict(list)
        for kind, *payload in rows:
            by_mask[bits(payload[-1])].append((kind, payload))

        out.raw()
        out.raw(f"// {option}: {describe(rows)}")
        out.open(f"void {name}(const PatchContext& ctx)")
        for line in gate_lines(option, defaults):
            out.raw(line)
        uses_image = any(kind != "rva" or payload[2] in (0, 1, 2) for kind, *payload in rows)
        if uses_image:
            out.raw("const GameImage& image = ctx.image;")

        for signature in anchors.get(option, []):
            out.raw()
            out.raw("// The handler itself must be present before its sites are touched.")
            out.raw(f'if (Scan(image, "{signature}") == nullptr)')
            out.raw("\treturn;")
        for mask, members in by_mask.items():
            builds = builds_of(mask)
            if len(builds) != len(BUILDS):
                out.raw()
                out.raw(f"if (BuildIs(ctx.build, {{{build_list(builds)}}}))")
                out.raw("{")
                out.indent += 1
            emit_members(out, option, members, slot_globals, symbol_globals,
                         option_globals, bodies)
            if len(builds) != len(BUILDS):
                out.indent -= 1
                out.raw("}")
        out.close()

    for option in order:
        rows = work[option]
        publishes = [row for row in rows if is_publish(*row)]
        installs = [row for row in rows if not is_publish(*row)]
        bare = symbol_of(option)[len("Install"):]
        emit_arm(option, publishes, "Publish" + bare)
        emit_arm(option, installs, "Install" + bare)
    out.raw()
    out.raw("} // namespace")
    out.raw()
    out.raw("// One pass over every arm, in the original loader's order.")
    out.raw("#if defined(ZP_TRACE_WRITES)")
    out.raw("namespace injector")
    out.raw("{")
    out.raw('const char* g_traceArm = "?";')
    out.raw("}")
    out.raw("#endif")
    out.raw()
    out.raw("#if defined(ZP_TRACE_INSTALLS)")
    out.raw("// Debug builds only: name the arm that is about to run, so a boot fault can")
    out.raw("// be narrowed down from the log.")
    out.raw("void Trace(const char* name)")
    out.raw("{")
    out.raw("\tstatic std::FILE* log = std::fopen(\"ZolikaPatch.trace.log\", \"a\");")
    out.raw("\tif (log == nullptr)")
    out.raw("\t\treturn;")
    out.raw("\tstd::fprintf(log, \"arm %s\\n\", name);")
    out.raw("\tstd::fprintf(log, \"heap %s arm:%s\\n\",")
    out.raw("\t             HeapValidate(GetProcessHeap(), 0, nullptr) ? \"ok\" : \"BAD\", name);")
    out.raw("#if defined(ZP_TRACE_WRITES)")
    out.raw("\tinjector::g_traceArm = name;")
    out.raw("#endif")
    out.raw("\tstd::fflush(log);")
    out.raw("}")
    out.raw("#else")
    out.raw("void Trace(const char*) {}")
    out.raw("#endif")
    out.raw()
    out.raw("void PatchGame(GameBuild build, GameImage image, const ConfigDocument& config)")
    out.raw("{")
    out.raw("\tPatchContext ctx{image, reinterpret_cast<std::uintptr_t>(image.base), build,")
    out.raw("\t                  config};")
    out.raw()
    out.raw("\tTrace(\"RemoveUselessChecks\");")
    out.raw("\tApplyUselessChecks(ctx);")
    out.raw()
    out.raw("\t// Publish every slot before any arm writes a site: a site write can")
    out.raw("\t// overwrite the signature a later arm's slot publish scans for.")
    for option in order:
        bare = symbol_of(option)[len("Install"):]
        out.raw(f"\tTrace(\"publish {bare}\");")
        out.raw(f"\tPublish{bare}(ctx);")
    out.raw()
    for option in order:
        bare = symbol_of(option)[len("Install"):]
        out.raw(f"\tTrace(\"{bare}\");")
        out.raw(f"\tInstall{bare}(ctx);")
    out.raw("}")
    out.raw()
    out.raw("void SetMenuIntegration(bool available) noexcept")
    out.raw("{")
    out.raw("\tbMenuIntegration = available;")
    out.raw("}")
    out.raw()
    out.raw("constexpr std::uint32_t kCallOperandOffset = 6u;")
    out.raw("constexpr std::uint32_t kResolverSignatureLength = 10u;")
    out.raw("constexpr std::uint32_t kPreFn59C74Offset = 42u;")
    out.raw("constexpr std::uint32_t kPreFn5D63COffset = 48u;")
    out.raw("constexpr std::uint32_t kPreFn59CDCOffset = 48u;")
    for line in constants:
        out.raw(line)
    out.raw()
    out.lines.extend(tail.rstrip().splitlines())
    PATCHES.write_text(out.text() + "\n", encoding="utf-8")
    print(f"patches.cpp: {len(out.lines)} lines, {len(order)} installers")

    # ------------------------------------------------------------ NOP data
    nop = Emitter()
    nop.raw("// RemoveUselessChecks: the per-build NOP list, one row per site.")
    nop.raw("//")
    nop.raw("// A row is the original handler's own write: `length` NOP bytes at module")
    nop.raw("// base + rva, on build `build`. Kept as data because there are 7,489 rows.")
    nop.raw()
    nop.raw('#include "pch.h"')
    nop.raw('#include "patch_data.h"')
    nop.raw()
    nop.raw("const UselessCheck kUselessChecks[] = {")
    for build, rva, length, bts in t_nop:
        body = ", ".join(f"0x{b:02X}" for b in bts[:length])
        nop.raw(f"\t{{{build}u, 0x{rva:06X}u, {length}u, {{{body}}}}},")
    nop.raw("};")
    nop.raw()
    nop.raw("const std::size_t kUselessCheckCount =")
    nop.raw("\tsizeof(kUselessChecks) / sizeof(kUselessChecks[0]);")
    PATCH_DATA.write_text(nop.text() + "\n", encoding="utf-8")
    print(f"patch_data.cpp: {len(t_nop)} NOP rows")


def is_publish(kind, *payload):
    """A row that only records state, as opposed to writing a site."""
    if kind == "slot":
        return True
    if kind == "rva":
        return payload[2] in (3, 4)
    return False


def describe(rows):
    kinds = defaultdict(int)
    for kind, *_ in rows:
        kinds[kind] += 1
    return ", ".join(f"{count} {name}{'' if count == 1 else 's'}"
                     for name, count in sorted(kinds.items()))


def gate_lines(option, defaults):
    base = option.split("@")[0]
    default = defaults.get(base, "1")
    lines = []
    if option == "RestoreDeathMusic":
        lines.append("// The menu toggle drives this arm even when its own key is off.")
        lines.append('if (!bMenuIntegration && !ctx.Option("RestoreDeathMusic", "0"))')
        lines.append("\treturn;")
    elif option == "CarPartsShadowFix":
        lines.append("// One installer serves both keys.")
        lines.append('if (!ctx.Option("CarPartsShadowFix", "1") &&')
        lines.append('    !ctx.Option("CarDynamicShadowFix", "1"))')
        lines.append("\treturn;")
    elif option == "CarPartsShadowFix@standalone":
        lines.append('if (!ctx.Option("CarPartsShadowFix", "1"))')
        lines.append("\treturn;")
    elif option == "CarDynamicShadowFix@noE":
        lines.append('if (!ctx.Option("CarDynamicShadowFix", "1"))')
        lines.append("\treturn;")
    elif option in ("DualVehicleHeadlights@old", "DualVehicleHeadlights@new"):
        lines.append('if (!ctx.Option("DualVehicleHeadlights", "1"))')
        lines.append("\treturn;")
    elif option == "MiscFixes":
        lines.append("// XLivelessAddon already applies these, so the arm disables itself.")
        lines.append("if (zpatch_xliveless_present)")
        lines.append("\treturn;")
        lines.append('if (!ctx.Option("MiscFixes", "1"))')
        lines.append("\treturn;")
    elif option in ("SkipIntro", "SkipMenu", "BorderlessWindowed"):
        lines.append(f'if (!ctx.Option("{base}", "{default}"))')
        lines.append("\treturn;")
        lines.append("// FusionFix owns these when its own fix is on.")
        lines.append("if (FusionFixGateClosed())")
        lines.append("\treturn;")
    elif option == "NewFilesCompatibility":
        lines.append(f'if (!ctx.Option("{base}", "{default}"))')
        lines.append("\treturn;")
        lines.append("if (NewFilesConflictModulesPresent())")
        lines.append("\treturn;")
    else:
        lines.append(f'if (!ctx.Option("{base}", "{default}"))')
        lines.append("\treturn;")
    return lines


def signature_of(kind, payload):
    """The signature a row scans, or None for a row that needs no scan."""
    if kind in ("rva", "rvajump"):
        return None
    if kind == "slot":
        return None if payload[2] == 3 else payload[0]
    if kind == "literal":
        return None if payload[3] else payload[0]
    return payload[0]


def emit_members(out, option, members, slot_globals, symbol_globals, option_globals, bodies):
    """Emit one arm's rows, scanning each signature once.

    The original scans a site, then writes every row of that site. Scanning per
    row would break after the first write whenever the row patches bytes inside
    its own signature.
    """
    done = set()
    for index, (kind, payload) in enumerate(members):
        signature = signature_of(kind, payload)
        if signature is None:
            emit_statement(out, option, kind, payload, "<none>", slot_globals,
                           symbol_globals, option_globals, bodies)
            continue
        if signature in done:
            continue
        done.add(signature)
        group = [member for member in members[index:] if signature_of(*member) == signature]
        emit_scan_group(out, option, signature, group, slot_globals, symbol_globals,
                        option_globals, bodies)


def emit_scan_group(out, option, signature, group, slot_globals, symbol_globals,
                    option_globals, bodies):
    needs_many = any(kind in ("sigwrite", "matchjump", "matchliteral") for kind, _ in group)
    fallbacks = sorted({payload[4] for kind, payload in group
                        if kind == "sigwrite" and payload[4]})
    out.raw()
    if not needs_many:
        line = f'if (std::uint8_t* match = Scan(image, "{signature}"))'
        if len(line) <= 100:
            out.raw(line)
        else:
            out.raw("if (std::uint8_t* match = Scan(")
            out.raw(f'\timage, "{signature}"))')
        out.raw("{")
        out.indent += 1
        for kind, payload in group:
            emit_statement(out, option, kind, payload, "match", slot_globals,
                           symbol_globals, option_globals, bodies)
        out.indent -= 1
        out.raw("}")
        return

    out.raw("{")
    out.indent += 1
    line = f'std::vector<std::uint8_t*> matches = ScanAll(image, "{signature}");'
    if len(line) <= 100:
        out.raw(line)
    else:
        out.raw("std::vector<std::uint8_t*> matches =")
        out.raw(f'\tScanAll(image, "{signature}");')
    for fallback in fallbacks:
        out.raw(f'if (matches.size() <= 1u)')
        out.raw(f'\tmatches = ScanAll(image, "{fallback}");')
    out.open("if (!matches.empty())")
    out.raw("std::uint8_t* match = matches[0];")
    for kind, payload in group:
        emit_statement(out, option, kind, payload, "match", slot_globals,
                       symbol_globals, option_globals, bodies)
    out.indent -= 1
    out.raw("}")
    out.indent -= 1
    out.raw("}")


def emit_statement(out, option, kind, payload, match, slot_globals, symbol_globals,
                   option_globals, bodies):
    """One recovered row, as the statement a human would have written."""
    if kind == "rva":
        site_rva, opcode, rva_kind, operand, slot, _builds = payload
        op = "CALL" if opcode == 0xE8 else "JMP"
        if rva_kind == 0:
            out.raw(f"injector::Make{op}(image.base + 0x{site_rva:06X},")
            out.raw(f"\treinterpret_cast<void*>(ctx.gameBase + 0x{operand:06X}), true);")
        elif rva_kind == 1:
            out.raw(f"injector::Make{op}(image.base + 0x{site_rva:06X},")
            out.raw(f"\treinterpret_cast<void*>(&{bodies[operand]}), true);")
        elif rva_kind == 2:
            out.raw(f"Fill(image.base + 0x{site_rva:06X}, {operand}u, 0x{opcode:02X});")
        elif rva_kind == 3:
            target = global_lvalue(slot, slot_globals.get(slot, ""), "FindGlobalSlot")
            out.raw(f"Publish({target}, static_cast<std::uint32_t>(ctx.gameBase + 0x{operand:06X}));")
        elif rva_kind == 4:
            target = global_lvalue(slot, slot_globals.get(slot, ""), "FindGlobalSlot")
            out.raw(f"Publish({target}, 0x{operand:08X}u);")
        elif rva_kind == 5:
            target = symbol_globals.get(slot)
            out.raw(f"injector::WriteMemory<std::uint32_t>(image.base + 0x{site_rva:06X},")
            out.raw(f"\tstatic_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&{target})));")
        return

    if kind == "slot":
        _signature, slot, slot_kind, field, extra, site_field, _index, _builds = payload
        if slot not in slot_globals:
            return
        target = global_lvalue(slot, slot_globals[slot], "FindGlobalSlot")
        if slot_kind == 3:
            out.raw(f"Publish({target}, static_cast<std::uint32_t>("
                    f"reinterpret_cast<std::uintptr_t>(image.base) + 0x{extra:X}u));")
            return
        if slot_kind == 5:
            out.raw(f"Publish({target}, Field({match}, {field}));")
            return
        if slot_kind == 1:
            expr = f"reinterpret_cast<std::uintptr_t>({match}) + Field({match}, {field})"
            if extra:
                expr += f" + {extra}"
            out.raw(f"Publish({target}, static_cast<std::uint32_t>({expr}));")
            return
        if slot_kind == 2:
            out.raw("{")
            out.indent += 1
            out.raw(f"const auto address = reinterpret_cast<std::uintptr_t>({match}) +")
            out.raw(f"\tField({match}, {field}) + {extra};")
            out.raw("if (InImage(image, address))")
            out.raw(f"\tPublish({target}, *reinterpret_cast<const std::uint32_t*>(address));")
            out.indent -= 1
            out.raw("}")
            return
        if slot_kind == 4:
            out.raw("{")
            out.indent += 1
            out.raw(f"const auto site = static_cast<std::uintptr_t>(Field({match}, {site_field}));")
            out.raw("if (InImage(image, site))")
            out.raw("{")
            expr = f"site + Field(reinterpret_cast<const std::uint8_t*>(site), {field})"
            if extra:
                expr += f" + {extra}"
            out.raw(f"\tPublish({target}, static_cast<std::uint32_t>({expr}));")
            out.raw("}")
            out.indent -= 1
            out.raw("}")
            return
        expr = f"reinterpret_cast<std::uintptr_t>({match})"
        if extra:
            expr += f" + {extra}"
        out.raw(f"Publish({target}, static_cast<std::uint32_t>({expr}));")
        return

    if kind == "siginstall":
        _signature, members, _builds = payload
        for site_offset, opcode, dest_kind, dest, site_deref, dest_symbol, bts in members:
            site = match
            if site_offset > 0:
                site = f"{match} + {site_offset}"
            elif site_offset < 0:
                site = f"{match} - {-site_offset}"
            block = False
            if site_deref:
                out.raw("{")
                out.indent += 1
                out.raw("std::uint8_t* site = reinterpret_cast<std::uint8_t*>(")
                out.raw(f"\tstatic_cast<std::uintptr_t>(Field({site}, 0)));")
                site = "site"
                block = True
            op = "CALL" if opcode == 0xE8 else "JMP"
            if dest_kind == 0:
                out.raw(f"injector::Make{op}({site}, reinterpret_cast<void*>(&{bodies[dest]}), true);")
            elif dest_kind == 1:
                out.raw(f"injector::Make{op}({site}, reinterpret_cast<void*>({match} + {dest}), true);")
            elif dest_kind == 2:
                out.raw(f"const auto target = reinterpret_cast<std::uintptr_t>({match}) + {site_offset};")
                out.raw("if (InImage(image, target))")
                out.raw("{")
                out.raw(f"\tinjector::Make{op}({site},")
                out.raw(f"\t\treinterpret_cast<void*>(BranchTarget({match}, {site_offset})), true);")
                out.raw("}")
            elif dest_kind == 3:
                target = option_globals.get(option)
                out.raw(f"injector::WriteMemory<std::uint32_t>({site},")
                out.raw(f"\tstatic_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&{target})));")
            elif dest_kind == 4:
                out.raw(f"WriteValue({site}, 1, 0x{dest & 0xFF:02X}u);")
            elif dest_kind == 5:
                target = symbol_globals.get(dest_symbol)
                out.raw(f"injector::WriteMemory<std::uint32_t>({site},")
                out.raw(f"\tstatic_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&{target})));")
            elif dest_kind == 6:
                out.raw(f'WriteHex({site}, "{hex_text(bts)}");')
            if block:
                out.indent -= 1
                out.raw("}")
        return

    if kind == "rvajump":
        module_rva, opcode, destination, _builds = payload
        op = "CALL" if opcode == 0xE8 else "JMP"
        out.raw(f"injector::Make{op}(image.base + 0x{module_rva:06X},")
        out.raw(f"\treinterpret_cast<void*>(&{bodies[destination]}), true);")
        return

    if kind == "sigwrite":
        _signature, site_offset, bts, index, _fallback, _builds = payload
        target = "matches[0]" if index == 0 else f"matches[{index}]"
        if index:
            out.raw(f"if (matches.size() > {index}u)")
            out.raw(f"\tWriteHex({target} + {site_offset}, \"{hex_text(bts)}\");")
        else:
            out.raw(f'WriteHex({target} + {site_offset}, "{hex_text(bts)}");')
        return

    if kind == "sigjump":
        _signature, offset, destination, _builds = payload
        out.raw(f"injector::MakeJMP({match} + {offset}, "
                f"reinterpret_cast<void*>(&{bodies[destination]}), true);")
        return

    if kind == "indirect":
        _signature, offset, width, literal, _builds = payload
        out.raw("auto* target = reinterpret_cast<std::uint8_t*>(")
        out.raw(f"\tstatic_cast<std::uintptr_t>(Field({match}, {offset})));")
        out.raw("if (target != nullptr)")
        out.raw(f"\tWriteValue(target, {width}u, 0x{literal:X}u);")
        return

    if kind == "matchjump":
        _signature, index, offset, destination, opcode, _builds = payload
        op = "CALL" if opcode == 0xE8 else "JMP"
        out.raw(f"if (matches.size() > {index}u)")
        out.raw(f"\tinjector::Make{op}(matches[{index}] + {offset},")
        out.raw(f"\t\treinterpret_cast<void*>(&{bodies[destination]}), true);")
        return

    if kind == "matchliteral":
        _signature, index, offset, width, literal, _builds = payload
        value = literal_value(option, width, literal)
        if value is None:
            return
        if index < 0:
            out.raw("for (std::uint8_t* each : matches)")
            out.raw(f"\tWriteValue(each + {offset}, {width}u, {value});")
        else:
            out.raw(f"if (matches.size() > {index}u)")
            out.raw(f"\tWriteValue(matches[{index}] + {offset}, {width}u, {value});")
        return

    if kind == "literalstore":
        _signature, offset, width, literal, _builds = payload
        value = literal_value(option, width, literal)
        if value is None:
            return
        out.raw(f"WriteValue({match} + {offset}, {width}u, {value});")
        return

    if kind == "literal":
        _signature, offset, bts, module_rva, _builds = payload
        if module_rva:
            out.raw(f'WriteHex(image.base + 0x{module_rva:06X}, "{hex_text(bts)}");')
        else:
            out.raw(f'WriteHex({match} + {offset}, "{hex_text(bts)}");')
        return

    raise NotImplementedError(kind)


def literal_value(option, width, literal):
    """A literal store of a module address is rebased to our global."""
    if width == 4 and MODULE_LOW <= literal <= MODULE_HIGH:
        return f'static_cast<std::uint32_t>(FindGlobalValue("{option}"))'
    return f"0x{literal:X}u"


if __name__ == "__main__":
    sys.exit(main())
