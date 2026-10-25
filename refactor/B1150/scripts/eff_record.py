#!/usr/bin/env python3
# B1150 (#470): print one spellsnew.eff record's slot words (the same layout
# the C++ reader decodes, include/client/spell/spells_new_eff_reader.h:
# record N at N*268, name[64], three 68-byte stages = int32 sound +
# 4 x {int32 emitter, int32 minLevel, int32 anchor, int32 attachPoint}).
# Usage: eff_record.py <record> [--client DIR]
#   record  spellsnew.eff record index (1-based; spells_us.txt field 145)
#   --client DIR  (default /mnt/projects/EverQuestP1999)
import argparse
import struct
import sys

ANCHOR = {0: "world", 1: "caster", 2: "missile", 3: "target"}
ATTACH = {0: "none", 1: "default", 2: "chest", 3: "head", 4: "lhand",
          5: "rhand", 6: "lfoot", 7: "rfoot", 8: "origin", 9: "leye",
          10: "reye", 11: "mouth", 12: "floorunderchest"}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("record", type=int)
    p.add_argument("--client", default="/mnt/projects/EverQuestP1999")
    a = p.parse_args()
    data = open(f"{a.client}/spellsnew.eff", "rb").read()
    n = len(data) // 268
    if not 1 <= a.record < n:
        print(f"record {a.record} out of range (file has {n - 1} records)", file=sys.stderr)
        return 1
    off = a.record * 268
    name = data[off:off + 64].rstrip(b"\x00").split(b"\x00")[0].decode(errors="replace")
    print(f"record {a.record}: {name!r}  (file {a.client}/spellsnew.eff, {len(data)} bytes)")
    for si, sname in enumerate(("Source", "Middle", "Target")):
        soff = off + 64 + si * 68
        sound, = struct.unpack_from("<i", data, soff)
        print(f"  {sname:6} sound={sound}")
        for s in range(4):
            em, lvl, anch, att = struct.unpack_from("<4i", data, soff + 4 + s * 16)
            tag = "empty" if em < 1 else f"em{em}"
            print(f"    slot{s} {tag:7} minLevel={lvl:<4} "
                  f"anchor={ANCHOR.get(anch, anch):7} attach={ATTACH.get(att, att)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
