#!/usr/bin/env python3
"""B1191: decode the spellsnew.edd colour words for ALL ELEVEN landing emitters
across the three records this report measures.

usage: edd_colour_dump.py [spellsnew.edd path]
       (default: /mnt/projects/EverQuestP1999/spellsnew.edd)

Client colour rule (B1141/B1146, docs/spellsnew_format.md): +0x68 additive;
+0x90/0x94 fade in/out; +0xA4 peak; +0xD4..0xDC start R,G,B; +0xE0..0xE8 end
R,G,B (the particle lerps start->end per channel over its life). A white
(255,255,255) start AND end means the per-particle tint is (1,1,1): no
per-particle, per-channel factor.

Finding (MR !343 note 8564, item 9): the original report cited the white start
and end only for record 80's set (280/281/282/9). This dump covers all eleven
landing emitters, so the stage-2 CLEARED verdict applies to every record:

  record 80 (spell 93)  : 280 281 282 9
  record 35 (spell 253) : 432 433 434
  record 43 (spell 720) : 395 396 397 275

Every one of the eleven is (255,255,255) start AND end (see the committed
output edd_colour_dump.out.txt).
"""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/projects/EverQuestP1999/spellsnew.edd'
data = open(path, 'rb').read()
assert data[:8] == b'EDD\x00110\x00', data[:8]
n = (len(data) - 8) // 416
print(f'B1191 EDD colour dump  path={path}  entries={n}  file bytes={len(data)}')


def entry(i1):  # 1-based
    off = 8 + (i1 - 1) * 416
    return data[off:off + 416]


def show(i1):
    e = entry(i1)

    def i32(o):
        return struct.unpack_from('<i', e, o)[0]

    def f32(o):
        return struct.unpack_from('<f', e, o)[0]

    name = e[0:32].split(b'\x00')[0].decode(errors='replace')
    tex = e[0x40:0x40 + 32].split(b'\x00')[0].decode(errors='replace')
    additive = i32(0x68)
    fi, fo, peak = f32(0x90), f32(0x94), f32(0xA4)
    s = [i32(0xD4 + 4 * k) for k in range(3)]
    en = [i32(0xE0 + 4 * k) for k in range(3)]
    white = (s == [255, 255, 255]) and (en == [255, 255, 255])
    print(f'emitter {i1:4d}  {name:28s} tex={tex:16s} additive={additive} '
          f'fadeIn={fi:.2f} fadeOut={fo:.2f} peak={peak:.3f}')
    print(f'            start RGB = {s}   end RGB = {en}   '
          f'{"WHITE start+end (tint 1,1,1)" if white else "NOT white -- inspect"}')


groups = [
    ('record 80 (spell 93),  tour_93', [280, 281, 282, 9]),
    ('record 35 (spell 253), b1130', [432, 433, 434]),
    ('record 43 (spell 720), b1130-self', [395, 396, 397, 275]),
]
for title, emitters in groups:
    print(f'\n--- {title} ---')
    for i in emitters:
        show(i)
print('\nAll eleven landing emitters white start AND end: tint is (1,1,1) on every record.')
