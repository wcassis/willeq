#!/usr/bin/env python3
# B1151: regenerate every numeric census quoted in TRACE.md / edd_rows_traced.tsv
# from the raw spellsnew.edd (oracle client copy on the ThinkPad).
#
#   scp thinkpad:/mnt/projects/EverQuestP1999/spellsnew.edd /tmp/spellsnew.edd
#   python3 refactor/B1151/edd_census.py /tmp/spellsnew.edd
#
# Layout: 8-byte header + 2118 x 416-byte CEmitterDef records.
#   +0x00 name (64 B)  +0x40 texture (64 B)
#   +0x6C D6C int  +0x70 D70 int  +0xA8 D_A8 int
#   +0x130 D130 int (frame count)  +0x134 D134 float (fps)  +0x178 D178 int
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
for off in (0x6C, 0x70, 0xA8, 0x130, 0x134, 0x178):
    c = collections.Counter(flt(i, off) if off == 0x134 else word(i, off) for i in range(n))
    items = sorted(c.items())
    tot = sum(c.values())
    render = " ".join(f"{k:g}:{v}" if off == 0x134 else f"{k}:{v}" for k, v in items)
    print(f"+{off:03x}  {render}   sum={tot}")

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
