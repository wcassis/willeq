#!/usr/bin/env python3
"""B1164: print one spellsnew.eff record's three stages (sound + four 16-byte slots:
edd index, min level, anchor, attach point; layout docs/spellsnew_format.md).
usage: b1164_eff.py <record> [spellsnew.eff]"""
import struct, sys
r = int(sys.argv[1]); path = sys.argv[2] if len(sys.argv) > 2 else '/mnt/projects/EverQuestP1999/spellsnew.eff'
b = open(path, 'rb').read(); e = b[268 * r:268 * (r + 1)]
print('record', r, e[:64].split(b'\0')[0].decode('latin1'))
for st in range(3):
    base = 0x40 + 0x44 * st
    print('stage', ('Source', 'Middle', 'Target')[st], 'sound', struct.unpack_from('<i', e, base)[0],
          [struct.unpack_from('<4i', e, base + 4 + 16 * k) for k in range(4)])
