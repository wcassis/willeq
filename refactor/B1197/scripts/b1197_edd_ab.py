#!/usr/bin/env python3
"""B1197: patch one or more words of one spellsnew.edd record and verify.

Usage: b1197_edd_ab.py <src.edd> <record> <out.edd> <off> <float> [...]
  record   1-based EDD index (the client reader's convention:
           entryBytes(i) = magic(8) + i*416, i >= 1 --
           src/client/spell/spells_new_edd_reader.cpp:135-137)
  off      word offset within the 416-byte entry (e.g. 0x7C = life L word)
  float    new value (written as little-endian IEEE-754 float)

Prints the record name/texture (proof the right record is addressed), the
before/after value of every patched word, and the first file byte that
differs from the source. Run on hermes-ext against the NFS P1999 file; the
patched EDD is then copied to the ThinkPad and rendered by
refactor/B1197/scripts/b1197_ab_render.sh.
"""
import struct
import sys

src, rec, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
patches = []
i = 4
while i < len(sys.argv):
    patches.append((int(sys.argv[i], 0), float(sys.argv[i + 1])))
    i += 2
assert patches, 'no patches given'

MAGIC, ENTRY = 8, 416
data = bytearray(open(src, 'rb').read())
assert data[:4] == b'EDD\x00', 'not an EDD file'
assert len(data) == MAGIC + ((len(data) - MAGIC) // ENTRY) * ENTRY, 'size not a multiple'
off = MAGIC + rec * ENTRY
assert off + ENTRY <= len(data), f'record {rec} out of range'
name = bytes(data[off:off + 32]).split(b'\x00')[0].decode(errors='replace')
tex = bytes(data[off + 0x40:off + 0x70]).split(b'\x00')[0].decode(errors='replace')
print(f'record {rec}: name={name!r} tex={tex!r} entry at byte {off}')

for woff, val in patches:
    bo = off + woff
    old = struct.unpack_from('<f', data, bo)[0]
    struct.pack_into('<f', data, bo, val)
    new = struct.unpack_from('<f', data, bo)[0]
    print(f'  +{woff:#04x}: {old!r} -> {new!r} (file byte {bo}..{bo + 3})')

orig = open(src, 'rb').read()
first = next((k for k in range(len(data)) if data[k] != orig[k]), None)
open(out, 'wb').write(bytes(data))
print(f'first differing file byte: {first}')
print(f'wrote {out} ({len(data)} bytes, source {len(orig)})')
