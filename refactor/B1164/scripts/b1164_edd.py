#!/usr/bin/env python3
"""B1164: print the spellsnew.edd words of record 80's four landing emitters (spell 93).

The file is an 8-byte header followed by 416-byte entries; entry k (counted from 0) is at
8 + 416*k. Field types follow docs/spellsnew_format.md: +0x6C and +0x19C are INT flags
(read as floats they print 1.4e-45 = 0.0), the size words are floats.

usage: b1164_edd.py pop|size [spellsnew.edd]
  pop  : burst (+0x80), per-tick count (+0x84), ticks/s (+0x88), lifetime (+0x7C),
         run time (+0x78), delay (+0x8C), and the sustained rate perTick*tps
  size : width (+0xCC..+0x184), height (+0x17C..+0x180), multiplier (+0x16C),
         +0x19C (int), depth bias (+0xD0), +0x6C (int), peak opacity (+0xA4)
"""
import struct, sys

EMITTERS = (280, 282, 9, 281)


def entry(buf, k):
    e = buf[8 + 416 * k: 8 + 416 * (k + 1)]
    I = lambda o: struct.unpack_from('<i', e, o)[0]
    F = lambda o: struct.unpack_from('<f', e, o)[0]
    name = e[:64].split(b'\0')[0].decode('latin1')
    tex = e[0x40:0x60].split(b'\0')[0].decode('latin1')
    return name, tex, I, F


def main():
    mode = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) > 2 else '/mnt/projects/EverQuestP1999/spellsnew.edd'
    buf = open(path, 'rb').read()
    if mode == 'pop':
        print('emitter\tname\ttex\tburst(D80)\tperTick(D84)\ttps(D88)\trate=perTick*tps\tlife(D7C)\trun(D78)\tdelay(D8C)')
        for k in EMITTERS:
            n, t, I, F = entry(buf, k)
            print(f'{k}\t{n}\t{t}\t{I(0x80)}\t{I(0x84)}\t{F(0x88):g}\t{I(0x84) * F(0x88):g}/s\t{F(0x7C):g}\t{F(0x78):g}\t{F(0x8C):g}')
    elif mode == 'size':
        print('emitter\tname\ttex\tw(DCC..D184)\th(D17C..D180)\tmult(D16C)\tD19C(int)\tD19C(as float)\tdepthBias(D0D0)\tD6C(int)\tpeakOpac(D0A4)')
        for k in EMITTERS:
            n, t, I, F = entry(buf, k)
            print(f'{k}\t{n}\t{t}\t{F(0xCC):g}..{F(0x184):g}\t{F(0x17C):g}..{F(0x180):g}\t{F(0x16C):g}\t{I(0x19C)}\t{F(0x19C):.3g}\t{F(0xD0):g}\t{I(0x6C)}\t{F(0xA4):g}')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
