#!/usr/bin/env python3
"""B1157: render the hand-anchor strip from emitter_origin.tsv (spell 93,
b1118 rig). The three Source-stage emitters (fxid 1 = the spell's Source
sprite, 49153 = LHAND, 32769 = RHAND, B1144's per-slot Source) are dumped by
the harness every rendered frame while the cast is open; the strip shows
their world origin (wx wy wz, model units) and screen position (sx sy px)
per frame -- the glow riding the raised hand through t06.

usage: hand_strip.py <emitter_origin.tsv> <out.tsv>
"""
import sys

src, out = sys.argv[1], sys.argv[2]
rows = [l.rstrip('\n').split('\t') for l in open(src)][1:]
# row: frame t fxid kind wx wy wz sx sy (9 fields). The harness writes all
# open emitters per frame; keep one row per (frame, kind) and emit frames in
# time order, emitters ordered LHAND, RHAND, sprite.
order = {'LHAND': 0, 'RHAND': 1}
byframe = {}
for r in rows:
    byframe.setdefault((int(r[0]), r[3]), r)
with open(out, 'w') as f:
    f.write('frame\tt\tfxid\tkind\twx\twy\twz\tsx\tsy\n')
    for (frame, kind), r in sorted(byframe.items(), key=lambda kv: (kv[0][0], order.get(kv[0][1], 2))):
        f.write('\t'.join(r) + '\n')
print(f'{len(byframe)} rows -> {out}')
