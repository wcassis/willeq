#!/usr/bin/env python3
"""B1162: print the official client's per-class skill table for some skills.

usage: client_skill_table.py <eqgame.exe> <skill> [<skill> ...]

The skill object constructor FUN_00448d94 (review/B1141 eqgame.exe.c 65366-65385)
copies, for each class c (0-based) and skill s < 0x4b, from the .data table at
0x698c30 + s*0x118 + c*28000 bytes: puVar1[5] (the level-70 cap) into the
object's cap[class] (offset 0x260 + class*4, read by FUN_0044a335) and the
first level whose entry is non-zero (scan of the 70 dwords that start 0x100
bytes before puVar1) into offset 0xc + class*4 (FUN_0044a2e4). So the table is
[class][skill][level 1..70] of u32 caps, row start 0x698b30. FUN_0041b0de
(the skill-usable check) returns the racial value only when cap[class] == 0.
Prints one row per class 1..16: class, first level, cap at 50/60/70.
"""
import struct, sys
exe = open(sys.argv[1], 'rb').read()
DATA_VA, DATA_OFF = 0x692000, 0x292000   # .data VMA / file offset (objdump -h)
ROW0 = 0x698c30 - 0x100
NAMES = 'WAR CLR PAL RNG SHD DRU MNK BRD ROG SHM NEC WIZ MAG ENC BST BER'.split()
print('skill\tclass\tname\tfirst_level\tcap50\tcap60\tcap70')
for s in map(int, sys.argv[2:]):
    for c in range(1, 17):
        va = ROW0 + ((c - 1) * 7000 + s * 70) * 4
        off = va - DATA_VA + DATA_OFF
        row = struct.unpack('<70I', exe[off:off + 280])
        first = next((i + 1 for i, v in enumerate(row) if v), 0)
        print(f'{s}\t{c}\t{NAMES[c-1]}\t{first}\t{row[49]}\t{row[59]}\t{row[69]}')
