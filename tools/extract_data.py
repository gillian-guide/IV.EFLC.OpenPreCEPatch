#!/usr/bin/env python3
"""Regenerate the two data files that are too large to edit by hand.

The committed copies are complete, so building does not run this script.
Use it if you want to rebuild them from files you own.

  python tools/extract_data.py --check
  python tools/extract_data.py --models models.txt
  python tools/extract_data.py --ooc /path/to/ZolikaPatch_original.asi

Model list format: one model name per line, in the game's model-table order.
The list is the same 38,455-entry table the mod uses for diagnostic messages.

OOC payload: the original mod stores a compressed `finale2.sco` resource at
image offset 0x100B6798 (file offset 0xB5B98). --ooc verifies the compressed
SHA-256, decodes it with the same opcode stream the loader uses, verifies the
decoded table hash and writes data/finale2.sco.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODELS_CPP = ROOT / "src" / "catalogue_data.cpp"
OOC_SCO = ROOT / "data" / "finale2.sco"
OOC_H = ROOT / "src" / "ooc_payload.h"

MODEL_COUNT = 38455
MODEL_SHA256 = "797ed84dd621336a8d60e252d101efe320919b3321e0abf94ca61de39bbe1c66"
OOC_SCO_COUNT = 179536
OOC_SCO_SHA256 = "86d2bb26100061f6af79ed9dc359a8e82c47a0a37f06d6a1b02c81ec6d0bcce0"
OOC_PACKED_COUNT = 163796
OOC_FILE_OFFSET = 0xB5B98
OOC_PACKED_SHA256 = "6dc0e5fe174de83d5dce941556bbdb537f24867ae4ba33ddbba3396c2e08b042"


def read_committed_models() -> list[str]:
    text = MODELS_CPP.read_text(encoding="utf-8")
    return re.findall(r'^\s*"([^"]*)",?\s*$', text, re.M)


def write_models(names: list[str]) -> None:
    out = [
        "// The multiplayer model-name catalogue, in the original ASI's table order.",
        f"// Extracted from the game's model table; {MODEL_COUNT:,} entries. The index is the",
        "// model id and the name is what the MP spawn probes print. Regenerate with",
        "// tools/extract_data.py --models.",
        "",
        "#include <cstddef>",
        "",
        "extern const char* const kModelNames[];",
        "const char* const kModelNames[] = {",
    ]
    for name in names:
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'  "{escaped}",')
    out += [
        "};",
        "",
        "extern const std::size_t kModelNameCount = sizeof(kModelNames) / sizeof(kModelNames[0]);",
        f'static_assert(kModelNameCount == 0x9637, "model catalogue must keep all {MODEL_COUNT:,} entries");',
        "",
        "",
    ]
    MODELS_CPP.write_text("\n".join(out), encoding="utf-8")


def check() -> int:
    names = read_committed_models()
    digest = hashlib.sha256(("\n".join(names) + "\n").encode()).hexdigest()
    ok = len(names) == MODEL_COUNT and digest == MODEL_SHA256
    print(f"models: {len(names)} entries, sha256={digest} {'OK' if ok else 'MISMATCH'}")
    if not OOC_SCO.exists():
        print("finale2.sco: missing")
        return 1
    data = OOC_SCO.read_bytes()
    odigest = hashlib.sha256(data).hexdigest()
    ook = len(data) == OOC_SCO_COUNT and odigest == OOC_SCO_SHA256
    print(f"finale2.sco: {len(data)} bytes, sha256={odigest} {'OK' if ook else 'MISMATCH'}")
    if ook:
        import compress_ooc
        packed = compress_ooc.compress(data)
        round_trip = compress_ooc.decode(packed, OOC_SCO_COUNT) == data
        print(f"compressed:  {len(packed)} bytes, round-trip {'OK' if round_trip else 'FAILED'}")
        ook = round_trip
    if not OOC_H.exists():
        print("ooc_payload.h: missing")
        return 1
    static = re.findall(r"0x([0-9a-fA-F]{2})", OOC_H.read_text(encoding="utf-8"))
    static_blob = bytes(int(v, 16) for v in static)
    shash = hashlib.sha256(static_blob).hexdigest()
    sok = len(static_blob) == OOC_PACKED_COUNT and shash == OOC_PACKED_SHA256
    print(f"ooc_payload.h: {len(static_blob)} bytes, sha256={shash} "
          f"{'OK' if sok else 'MISMATCH'}")
    return 0 if ok and ook and sok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="verify the committed files")
    ap.add_argument("--models", metavar="TXT", help="one model name per line")
    ap.add_argument("--ooc", metavar="ASI", help="original ZolikaPatch ASI")
    args = ap.parse_args()
    if args.check:
        return check()
    if args.models:
        names = [line.strip() for line in Path(args.models).read_text(
            encoding="utf-8", errors="replace").splitlines() if line.strip()]
        digest = hashlib.sha256(("\n".join(names) + "\n").encode()).hexdigest()
        if len(names) != MODEL_COUNT:
            print(f"error: expected {MODEL_COUNT} names, got {len(names)}", file=sys.stderr)
            return 2
        if digest != MODEL_SHA256:
            print(f"error: model list hash {digest} does not match {MODEL_SHA256}",
                  file=sys.stderr)
            return 2
        write_models(names)
        print(f"wrote {MODELS_CPP} ({len(names)} names)")
        return 0
    if args.ooc:
        raw = Path(args.ooc).read_bytes()
        blob = raw[OOC_FILE_OFFSET:OOC_FILE_OFFSET + OOC_PACKED_COUNT]
        digest = hashlib.sha256(blob).hexdigest()
        if len(blob) != OOC_PACKED_COUNT or digest != OOC_PACKED_SHA256:
            print(f"error: payload hash {digest} does not match {OOC_PACKED_SHA256}",
                  file=sys.stderr)
            return 2
        import compress_ooc
        sco = compress_ooc.decode(blob, OOC_SCO_COUNT)
        if hashlib.sha256(sco).hexdigest() != OOC_SCO_SHA256:
            print("error: decoded table hash does not match", file=sys.stderr)
            return 2
        OOC_SCO.parent.mkdir(parents=True, exist_ok=True)
        OOC_SCO.write_bytes(sco)
        print(f"wrote {OOC_SCO} ({len(sco)} bytes)")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
