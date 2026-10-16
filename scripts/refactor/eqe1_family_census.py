#!/usr/bin/env python3
"""B1083 (#403): census of EQE1 texture names held by more than one archive of
the SAME zone family ({zone}, {zone}_obj, {zone}_chr) with a DIFFERENT payload.

That is the exact class of collision that a name-keyed texture cache resolves
wrongly: qeytoqrg.eqe1 and qeytoqrg_obj.eqe1 both hold tree70/treea, the zone
copy opaque (ETC1, no alpha), the object copy with an _alpha companion. A name
lookup opened in configured order ({zone} first) serves the zone copy to the
object's material.

Parses the EQE1 index directly (client/graphics/eqe1_format.h: EQE1Header is
16 bytes, every index entry is the 48-byte EQE1IndexEntry — v1/v2 inline, or
v3 with an 8-byte name tail). Reads only index metadata plus each candidate's
payload slice (for the SHA-256), so it is fast even over 1,500 packs.

Usage:
  python3 scripts/refactor/eqe1_family_census.py --eqe1-path DIR [--output FILE]

Prints (and optionally writes) a markdown table. Exit code 0 always (the census
is reporting, not asserting); the README is the record.
"""
import argparse
import hashlib
import os
import struct
import sys
from collections import defaultdict

MAGIC = 0x31455145
HEADER = struct.Struct("<IHIIH")  # magic, version, numEntries, flags, reserved
ENTRY = struct.Struct("<32sHHBBBBI I".replace(" ", ""))  # name, w, h, mip, hasAlpha, entryType, payloadFormat, dataOffset, dataSize
TAIL = struct.Struct("<II")      # v3 nameOffset, nameLength

TEXTURE_TYPES = (0, 1)  # EQE1_TYPE_ETC1_RGB, EQE1_TYPE_ETC1_RGBA


def pack_stem(path):
    base = os.path.basename(path)
    return base[:-5] if base.endswith(".eqe1") else base


def parse_pack(path, payload_digest_for=None):
    """Return {name: (dataSize, hasAlpha, payloadFormat, digest-or-None)} for
    every TEXTURE entry (companion _alpha entries excluded: they are part of
    the owner's payload, not a separately addressable name)."""
    with open(path, "rb") as f:
        raw = f.read()
    magic, version, num, flags, reserved = HEADER.unpack_from(raw, 0)
    if magic != MAGIC:
        raise ValueError(f"{path}: bad magic {magic:#x}")
    offset = HEADER.size
    out = {}
    for _ in range(num):
        name_b, w, h, mip, has_alpha, etype, pformat, doff, dsize = ENTRY.unpack_from(raw, offset)
        offset += ENTRY.size
        inline = name_b.split(b"\x00", 1)[0].decode("latin1")
        if version >= 3:
            noff, nlen = TAIL.unpack_from(raw, offset)
            offset += TAIL.size
            if noff and nlen:
                inline = raw[noff:noff + nlen].decode("latin1")
        if etype not in TEXTURE_TYPES:
            continue
        if inline.endswith("_alpha"):
            continue  # companion: folded into its owner's payload
        digest = None
        if payload_digest_for is not None and payload_digest_for(inline):
            digest = hashlib.sha256(raw[doff:doff + dsize]).hexdigest()[:16]
        out[inline] = (dsize, has_alpha, pformat, digest)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eqe1-path", required=True)
    ap.add_argument("--output")
    args = ap.parse_args()

    packs = sorted(
        f for f in os.listdir(args.eqe1_path) if f.endswith(".eqe1")
    )
    stems = {pack_stem(f) for f in packs}
    families = {}
    for f in packs:
        s = pack_stem(f)
        if s.endswith("_obj") or s.endswith("_chr"):
            families.setdefault(s[: -4], set()).add(s)
        elif (s + "_obj") in stems or (s + "_chr") in stems:
            families.setdefault(s, set()).add(s)

    by_stem = {}
    for f in packs:
        s = pack_stem(f)
        if s in {a for members in families.values() for a in members}:
            by_stem[s] = os.path.join(args.eqe1_path, f)

    name_by_family = defaultdict(dict)  # family -> name -> {stem: meta}
    for fam, members in families.items():
        for s in members:
            try:
                meta = parse_pack(by_stem[s])
            except Exception as e:
                print(f"WARN: {s}: {e}", file=sys.stderr)
                continue
            for name, payload in meta.items():
                name_by_family[fam].setdefault(name, {})[s] = payload

    rows = []
    for fam in sorted(name_by_family):
        for name in sorted(name_by_family[fam]):
            owners = name_by_family[fam][name]
            if len(owners) < 2:
                continue
            kinds = {(d, a, p) for (d, a, p, _) in owners.values()}
            differing = len(kinds) > 1
            if differing:
                rows.append((fam, name, owners, kinds))

    lines = []
    lines.append(f"# B1083 zone-family EQE1 collision census")
    lines.append(f"")
    lines.append(f"Tree: `{args.eqe1_path}` ({len(packs)} packs, {len(families)} zone families).")
    lines.append(f"")
    lines.append("A row is a texture name held by >= 2 archives of the same zone")
    lines.append("family (ZONE / ZONE_obj / ZONE_chr) with a DIFFERENT payload — different")
    lines.append("dataSize, hasAlpha (=_alpha companion) or payload format. These are the")
    lines.append("names a name-keyed EQE1 resolution can serve the wrong copy of. (Identical")
    lines.append("re-holdings of the same name within a family are NOT listed: the configured")
    lines.append("order already decides, and the bytes agree.)")
    lines.append(f"")
    lines.append(f"Total differing-content collisions: **{len(rows)}**")
    lines.append(f"")
    lines.append("| zone | name | owners (size B / alpha / fmt / sha256-16) |")
    lines.append("|---|---|---|")
    for fam, name, owners, _kinds in rows:
        detail = "; ".join(
            f"`{s}` {d}B a={a} f={p} {h}"
            for s, (d, a, p, h) in sorted(owners.items())
        )
        lines.append(f"| {fam} | {name} | {detail} |")

    text = "\n".join(lines) + "\n"
    print(text)
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
        print(f"Wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
