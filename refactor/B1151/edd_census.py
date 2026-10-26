#!/usr/bin/env python3
# B1151: regenerate every numeric census quoted in refactor/B1151/README.md
# from the raw spellsnew.edd (oracle client copy on the ThinkPad).
#
#   scp thinkpad:/mnt/projects/EverQuestP1999/spellsnew.edd /tmp/spellsnew.edd
#   python3 refactor/B1151/edd_census.py /tmp/spellsnew.edd
#
# Layout: 8-byte header + 2118 x 416-byte CEmitterDef records.
#   +0x00 name (64 B)  +0x40 texture (64 B)
# Motion/spin/flag words (review/B1141/edd_fields_from_code.tsv rows):
#   int:   +0x60 orient  +0x64 nodepth  +0x70 mode  +0x74 follow
#          +0x174 randomStartSpin  +0x178 spinAlign  +0x198 drawNegRadius
#   float: +0xEC..+0x12C (per-axis/radial/swirl/gravity/drift words),
#          +0x138 spinMin  +0x188 spinMax
#   frame: +0x130 N  +0x134 fps
# Every distribution prints sum=2118 (the record count).
import collections
import struct
import sys

path = sys.argv[1]
data = open(path, "rb").read()
HDR, REC = 8, 416
n = (len(data) - HDR) // REC
assert len(data) == HDR + n * REC, (len(data), n)


def word(i, off):
    return struct.unpack_from("<I", data, HDR + i * REC + off)[0]


def flt(i, off):
    return struct.unpack_from("<f", data, HDR + i * REC + off)[0]


def name(i):
    return data[HDR + i * REC: HDR + i * REC + 64].split(b"\0")[0].decode("ascii", "replace")


def tex(i):
    return data[HDR + i * REC + 64: HDR + i * REC + 128].split(b"\0")[0].decode("ascii", "replace")


print(f"size={len(data)}  records={n}  header={data[:8].hex(' ')}")

INT_WORDS = {
    0x60: "orient", 0x64: "noDepthWrite", 0x6C: "scaleSizes", 0x70: "spriteMode",
    0x74: "follow", 0xA8: "shape", 0x130: "frameCount", 0x174: "randomStartSpin",
    0x178: "spinAlign", 0x198: "drawNegativeRadius",
}
FLOAT_WORDS = {
    0xEC: "upMin", 0xF0: "upMax", 0xF4: "upAccel",
    0xF8: "axis2Min", 0xFC: "axis2Max", 0x100: "axis2Accel",
    0x104: "axis3Min", 0x108: "axis3Max", 0x10C: "axis3Accel",
    0x110: "radialMin", 0x114: "radialMax", 0x118: "radialAccel",
    0x11C: "swirlMin", 0x120: "swirlMax", 0x124: "swirlAccel",
    0x128: "gravity", 0x12C: "driftX",
    0x134: "frameRate", 0x138: "spinMin", 0x188: "spinMax",
}

print("-- int words --")
for off in sorted(INT_WORDS):
    c = collections.Counter(word(i, off) for i in range(n))
    render = " ".join(f"{k}:{v}" for k, v in sorted(c.items()))
    print(f"+{off:03x} {INT_WORDS[off]:<20} {render}   sum={sum(c.values())}")

print("-- float words (exact-value distributions; nonzero count) --")
for off in sorted(FLOAT_WORDS):
    c = collections.Counter(flt(i, off) for i in range(n))
    nz = sum(v for k, v in c.items() if k != 0.0)
    items = sorted(c.items())
    render = " ".join(f"{k:g}:{v}" for k, v in items)
    print(f"+{off:03x} {FLOAT_WORDS[off]:<14} nonzero={nz:<4} {render}   sum={sum(c.values())}")

print("-- records 2114..2117 (0-based) --")
for i in (2114, 2115, 2116, 2117):
    print(f"e{i}  {name(i)}  {tex(i)}  d178={word(i, 0x178)}  d70={word(i, 0x70)}")

print("-- all records with D178 != 0 --")
for i in range(n):
    if word(i, 0x178):
        print(f"e{i}  {name(i)}  {tex(i)}  d178={word(i, 0x178)}")

print("-- all records with D70 == 2 --")
for i in range(n):
    if word(i, 0x70) == 2:
        print(f"e{i}  {name(i)}  {tex(i)}")

print("-- records with mode 1/2 AND any nonzero motion word --")
for i in range(n):
    if word(i, 0x70) in (1, 2) and any(flt(i, off) != 0.0 for off in (
            0xEC, 0xF0, 0xF4, 0xF8, 0xFC, 0x100, 0x104, 0x108, 0x10C,
            0x110, 0x114, 0x118, 0x11C, 0x120, 0x124, 0x128, 0x12C)):
        print(f"e{i}  {name(i)}  {tex(i)}  d70={word(i, 0x70)}")
