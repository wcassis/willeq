#!/usr/bin/env python3
"""B1170 (#489): write a 0x4033 (OP_SomeItemPacketMaybe) payload, 0x88 bytes.

SYNTHETIC: the lane-5 server never sends a type-9 (spell) object (README (C)),
so the harness-vs-model pin runs on a payload laid out at the offsets the
client's received branch copies (README (A)), with the fields named below.

usage: python3 -I make_payload.py <out.bin> --spell <id> [--word 1.5] [--damped 1]
           [--type 9] [--heading 0] [--aim 0] [--source 1] [--target 2] [--model IT10]
"""
import argparse
import struct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--spell', type=int, required=True)
    ap.add_argument('--word', type=float, default=1.5)
    ap.add_argument('--damped', type=int, default=1)
    ap.add_argument('--type', type=int, default=9)
    ap.add_argument('--heading', type=float, default=0.0)
    ap.add_argument('--aim', type=float, default=0.0)
    ap.add_argument('--source', type=int, default=1)
    ap.add_argument('--target', type=int, default=2)
    ap.add_argument('--model', default='')
    a = ap.parse_args()
    b = bytearray(0x88)
    struct.pack_into('<f', b, 0x28, a.word)        # thrust word
    struct.pack_into('<f', b, 0x2c, a.heading)
    struct.pack_into('<f', b, 0x3c, a.aim)
    struct.pack_into('<I', b, 0x4c, a.source)
    struct.pack_into('<I', b, 0x50, a.target)
    struct.pack_into('<I', b, 0x5c, a.spell)
    b[0x62] = a.damped
    b[0x63] = a.type
    m = a.model.encode('latin-1')[:0x88 - 0x65 - 1]
    b[0x65:0x65 + len(m)] = m
    open(a.out, 'wb').write(bytes(b))
    print('%s: %d bytes, spell %d type %d word %g +0x62 %d' % (a.out, len(b), a.spell, a.type, a.word, a.damped))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
