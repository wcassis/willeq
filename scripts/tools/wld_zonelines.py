#!/usr/bin/env python3
"""B1068: census of the zone-line regions a zone's own WLD carries.

The official client zones when the player enters a BSP region flagged by a
fragment 0x29 whose region-type string starts DRNTP / WTNTP / LANTP
(docs/reference/wld_fragments.md, 0x29 Region Flag). The server only holds the
zone-in destinations (zone_points, matched by number). This scanner reads the
S3D (PFS) archive and the WLD inside it directly -- no client build needed --
and prints every zone-line 0x29 entry with its fragment NAME and its BODY
string (the two can differ: the client must decode the one that carries the
DRNTP form).

Usage (always run with -I; the inputs are data, never imported):
    python3 -I scripts/tools/wld_zonelines.py <zone.s3d>...        # per-entry listing
    python3 -I scripts/tools/wld_zonelines.py --table <eq_dir> <zone_lines_dir>
        # one row per data/zone_lines/<zone>.json: BSP zone-line entries and
        # regions vs curated JSON entries (with and without bounds)
"""
import json
import os
import struct
import sys
import zlib

_KEY = (0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A)
_NAMES_CRC = 0x61580AC9
_ZONELINE_PREFIXES = ("DRNTP", "WTNTP", "LANTP")


def _decode(b):
    return bytes(c ^ _KEY[i % 8] for i, c in enumerate(b))


def pfs_files(path):
    """Return {name: bytes} for every file in a PFS (.s3d) archive."""
    with open(path, "rb") as f:
        d = f.read()
    diroff = struct.unpack_from("<I", d, 0)[0]
    n = struct.unpack_from("<I", d, diroff)[0]
    ents = [struct.unpack_from("<III", d, diroff + 4 + 12 * i) for i in range(n)]

    def inflate(off, size):
        out = bytearray()
        p = off
        while len(out) < size:
            cl, _ul = struct.unpack_from("<II", d, p)
            p += 8
            out += zlib.decompress(d[p:p + cl])
            p += cl
        return bytes(out)

    names = []
    blobs = []
    for crc, off, size in sorted(ents, key=lambda e: e[1]):
        blob = inflate(off, size)
        if crc == _NAMES_CRC:
            cnt = struct.unpack_from("<I", blob, 0)[0]
            p = 4
            for _ in range(cnt):
                ln = struct.unpack_from("<I", blob, p)[0]
                p += 4
                names.append(blob[p:p + ln].rstrip(b"\0").decode(errors="replace"))
                p += ln
        else:
            blobs.append(blob)
    return dict(zip((n.lower() for n in names), blobs))


def wld_region_flags(w):
    """Yield (name, body_string, region_count) for every 0x29 fragment."""
    _magic, _ver, nfrag = struct.unpack_from("<III", w, 0)
    hsz = struct.unpack_from("<I", w, 20)[0]
    strings = _decode(w[28:28 + hsz])
    p = 28 + hsz
    for _ in range(nfrag):
        sz, fid = struct.unpack_from("<II", w, p)
        body = w[p + 8:p + 8 + sz]
        p += 8 + sz
        if fid != 0x29:
            continue
        nameref, _flags, rc = struct.unpack_from("<iII", body, 0)
        name = ""
        if nameref < 0:
            end = strings.find(b"\0", -nameref)
            name = strings[-nameref:end].decode(errors="replace")
        q = 12 + 4 * rc
        text = ""
        if q + 4 <= len(body):
            ssz = struct.unpack_from("<I", body, q)[0]
            text = _decode(body[q + 4:q + 4 + ssz]).rstrip(b"\0").decode(errors="replace")
        yield name, text, rc


def is_zoneline(name, text):
    return any(s.upper().startswith(_ZONELINE_PREFIXES) for s in (name, text))


def zone_wld(path):
    z = os.path.basename(path)[:-4].lower()
    return z, pfs_files(path).get(z + ".wld")


def list_zone(path):
    z, wld = zone_wld(path)
    if wld is None:
        print(f"{z}: no {z}.wld in {path}")
        return
    regs = list(wld_region_flags(wld))
    zl = [r for r in regs if is_zoneline(r[0], r[1])]
    print(f"{z}: 0x29 fragments={len(regs)} zoneline={len(zl)} "
          f"regions={sum(r[2] for r in zl)}")
    for name, text, rc in zl:
        print(f"   regions={rc:3d} name={name} body={text}")


def table(eq_dir, json_dir):
    rows = []
    for fn in sorted(os.listdir(json_dir)):
        if not fn.endswith(".json"):
            continue
        z = fn[:-5]
        with open(os.path.join(json_dir, fn)) as f:
            zl = json.load(f).get("zone_lines", [])
        jtot = len(zl)
        jb = sum(1 for e in zl if "bounds" in e)
        s3d = os.path.join(eq_dir, z + ".s3d")
        if os.path.exists(s3d):
            _z, wld = zone_wld(s3d)
            regs = list(wld_region_flags(wld)) if wld else []
            zls = [r for r in regs if is_zoneline(r[0], r[1])]
            bsp_e, bsp_r = str(len(zls)), str(sum(r[2] for r in zls))
        else:
            bsp_e = bsp_r = "no-s3d"
        rows.append((z, bsp_e, bsp_r, jtot, jb))
    print("zone\tbsp_entries\tbsp_regions\tjson_entries\tjson_with_bounds")
    for r in rows:
        print("\t".join(str(x) for x in r))
    have = [r for r in rows if r[1] != "no-s3d"]
    print(f"# zones={len(rows)} with_s3d={len(have)} "
          f"bsp_zonelines>0={sum(1 for r in have if int(r[1]) > 0)} "
          f"bsp_entries={sum(int(r[1]) for r in have)} "
          f"bsp_regions={sum(int(r[2]) for r in have)} "
          f"json_entries={sum(r[3] for r in rows)} "
          f"json_with_bounds={sum(r[4] for r in rows)} "
          f"json_bounds_but_no_bsp={sum(1 for r in have if r[4] > 0 and int(r[1]) == 0)} "
          f"bsp_but_no_json_bounds={sum(1 for r in have if int(r[1]) > 0 and r[4] == 0)}")


def main(argv):
    if len(argv) == 4 and argv[1] == "--table":
        table(argv[2], argv[3])
        return 0
    if len(argv) < 2 or argv[1].startswith("-"):
        print(__doc__)
        return 2
    for p in argv[1:]:
        list_zone(p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
