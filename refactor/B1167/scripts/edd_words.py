#!/usr/bin/env python3
"""B1167: print every drawing word of the given spellsnew.edd entries (0-based file index,
the eff slot's em number), one row per word, one column per entry. Offsets and types per
docs/spellsnew_format.md (ints: flags/counts/shape/colours; floats: the rest).

usage: edd_words.py <em> [<em> ...] [--edd /mnt/projects/EverQuestP1999/spellsnew.edd]
"""
import struct, sys

WORDS = [
    (0x60, 'i', 'orient to track'), (0x64, 'i', 'no depth write'), (0x68, 'i', 'additive'),
    (0x6C, 'i', 'size/vel x attach scale'), (0x70, 'i', 'sprite mode'), (0x74, 'i', 'follow emitter'),
    (0x78, 'f', 'run time T'), (0x7C, 'f', 'lifetime L'), (0x80, 'i', 'burst'), (0x84, 'i', 'per tick'),
    (0x88, 'f', 'ticks/s'), (0x8C, 'f', 'delay'), (0x90, 'f', 'alpha fade-in'), (0x94, 'f', 'alpha fade-out'),
    (0x98, 'f', 'size grow-in'), (0x9C, 'f', 'size shrink-out'), (0xA0, 'f', 'density dist'),
    (0xA4, 'f', 'peak opacity'), (0xA8, 'i', 'shape'), (0xAC, 'f', 'radius X'), (0xB0, 'f', 'radius Y'),
    (0xB4, 'f', 'height/Z'), (0xB8, 'f', 'offset ax1'), (0xBC, 'f', 'offset ax2'), (0xC0, 'f', 'offset ax3'),
    (0xC4, 'f', 'tilt A'), (0xC8, 'f', 'tilt B'), (0xCC, 'f', 'width min'), (0x184, 'f', 'width max'),
    (0x17C, 'f', 'height min'), (0x180, 'f', 'height max'), (0x18C, 'i', 'one random'),
    (0xD0, 'f', 'depth bias'), (0xD4, 'i', 'R0'), (0xD8, 'i', 'G0'), (0xDC, 'i', 'B0'),
    (0xE0, 'i', 'R1'), (0xE4, 'i', 'G1'), (0xE8, 'i', 'B1'),
    (0xEC, 'f', 'v1 min'), (0xF0, 'f', 'v1 max'), (0xF4, 'f', 'v1 acc'),
    (0xF8, 'f', 'v2 min'), (0xFC, 'f', 'v2 max'), (0x100, 'f', 'v2 acc'),
    (0x104, 'f', 'v3 min'), (0x108, 'f', 'v3 max'), (0x10C, 'f', 'v3 acc'),
    (0x110, 'f', 'radial min'), (0x114, 'f', 'radial max'), (0x118, 'f', 'radial acc'),
    (0x11C, 'f', 'swirl min'), (0x120, 'f', 'swirl max'), (0x124, 'f', 'swirl acc'),
    (0x128, 'f', 'gravity'), (0x12C, 'f', '+X drift'), (0x130, 'i', 'frames'), (0x134, 'f', 'anim fps'),
    (0x138, 'f', 'spin min'), (0x188, 'f', 'spin max'), (0x174, 'i', 'random start angle'),
    (0x16C, 'f', 'size multiplier'), (0x178, 'i', 'spin aligned'), (0x190, 'f', 'h shrink-out'),
    (0x194, 'f', 'w shrink-out'), (0x198, 'i', 'draw r<0'), (0x19C, 'i', 'shape x attach scale'),
]


def main(argv):
    path = '/mnt/projects/EverQuestP1999/spellsnew.edd'
    if '--edd' in argv:
        i = argv.index('--edd'); path = argv[i + 1]; del argv[i:i + 2]
    ems = [int(a) for a in argv]
    buf = open(path, 'rb').read()
    ents = [buf[8 + 416 * k: 8 + 416 * (k + 1)] for k in ems]
    print('%-6s %-24s' % ('off', 'word') + ''.join('%12s' % ('em%d' % k) for k in ems))
    print('%-6s %-24s' % ('', 'name') + ''.join('%12s' % e[:64].split(b'\0')[0].decode('latin1')[-11:] for e in ents))
    print('%-6s %-24s' % ('', 'texture') + ''.join('%12s' % e[0x40:0x60].split(b'\0')[0].decode('latin1') for e in ents))
    for off, t, name in WORDS:
        vals = [struct.unpack_from('<' + t, e, off)[0] for e in ents]
        cells = ''.join(('%12d' % v) if t == 'i' else ('%12.4g' % v) for v in vals)
        print('+0x%-4X %-24s%s' % (off, name, cells))


if __name__ == '__main__':
    main(sys.argv[1:])
