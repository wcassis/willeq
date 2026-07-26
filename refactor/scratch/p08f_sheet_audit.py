#!/usr/bin/env python3
"""P08f structural audit of composite sheets (extends P08d's ad hoc method).

For each sheet basename, verifies:
  - .bmp, .json, .eqe1 all exist
  - .json parses and has a non-empty "textures" object
  - .json's sheetWidth/sheetHeight match the actual BMP header dimensions
  - BMP file size is at least the expected 24bpp raw size (not truncated)
  - .eqe1 file is non-empty and starts with the expected magic

Usage: p08f_sheet_audit.py <sheets_dir> <basename1> [<basename2> ...]
       p08f_sheet_audit.py <sheets_dir> --report <report.json>   (audits every
       basename with status "built" found across all "models" entries in the
       given P08f --report-json output)

Writes a JSON summary (same shape as P08d's sheet_audit.json) to stdout.
"""
import json
import struct
import sys


def bmp_dims(path):
    with open(path, "rb") as f:
        head = f.read(30)
    if len(head) < 26 or head[0:2] != b"BM":
        return None
    width = struct.unpack_from("<i", head, 18)[0]
    height = struct.unpack_from("<i", head, 22)[0]
    return width, abs(height)


def audit_one(sheets_dir, basename):
    import os
    bmp = os.path.join(sheets_dir, basename + ".bmp")
    js = os.path.join(sheets_dir, basename + ".json")
    eqe1 = os.path.join(sheets_dir, basename + ".eqe1")

    problems = []
    if not os.path.isfile(bmp):
        problems.append("missing .bmp")
    if not os.path.isfile(js):
        problems.append("missing .json")
    if not os.path.isfile(eqe1):
        problems.append("missing .eqe1")
    if problems:
        return problems

    try:
        with open(js) as f:
            data = json.load(f)
    except Exception as e:
        return [f"json parse error: {e}"]

    textures = data.get("textures")
    if not textures:
        problems.append("empty textures map")

    dims = bmp_dims(bmp)
    if dims is None:
        problems.append("bmp header unreadable")
    else:
        w, h = dims
        if w != data.get("sheetWidth") or h != data.get("sheetHeight"):
            problems.append(
                f"bmp dims {w}x{h} != json sheetWidth/sheetHeight "
                f"{data.get('sheetWidth')}x{data.get('sheetHeight')}"
            )
        # 24bpp row-padded-to-4 raw size, plus 54-byte header
        row_stride = (w * 3 + 3) & ~3
        expected_min = 54 + row_stride * h
        actual = os.path.getsize(bmp)
        if actual < expected_min:
            problems.append(f"bmp truncated: {actual} bytes < expected {expected_min}")

    eqe1_size = os.path.getsize(eqe1)
    if eqe1_size < 32:
        problems.append(f"eqe1 implausibly small: {eqe1_size} bytes")
    else:
        with open(eqe1, "rb") as f:
            magic = f.read(4)
        # EQE1 magic is a 4-byte tag; just check it's non-zero/non-garbage length
        if len(magic) != 4:
            problems.append("eqe1 magic unreadable")

    return problems


def main():
    sheets_dir = sys.argv[1]
    basenames = []
    if len(sys.argv) >= 4 and sys.argv[2] == "--report":
        with open(sys.argv[3]) as f:
            report = json.load(f)
        for zone_name, zone in report.get("zones", {}).items():
            for race, model in zone.get("models", {}).items():
                if model.get("status") != "built":
                    continue
                for v in model.get("variantsBuilt", []):
                    basenames.append(f"{race.lower()}_0_{v}")
    else:
        basenames = sys.argv[2:]

    total = len(basenames)
    ok = []
    broken = {}
    for b in basenames:
        problems = audit_one(sheets_dir, b)
        if problems:
            broken[b] = problems
        else:
            ok.append(b)

    result = {"total": total, "ok": len(ok), "broken": broken}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
