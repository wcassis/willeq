#!/usr/bin/env python3
# B1195: print the lit-pixel series around the source onset for both lanes (93 rep 1),
# to separate a dim-ramp (lit low, rising slowly) from a late-spawn (lit zero until late).
import sys, os
def series(root, cast):
    p = os.path.join(root, cast, 'lit80.tsv')
    rows = [l.rstrip('\n').split('\t') for l in open(p)][1:]
    out = []
    for r in rows:
        tb, ta, lit = float(r[1]), float(r[2]), float(r[3])
        if -0.5 <= tb <= 2.0:
            out.append((tb, ta, lit))
    return out
s5 = series(sys.argv[1], 'lane5e_93_1')
s2 = series(sys.argv[2], 'lane2c_93_1')
print('t_bc  lane5_lit   lane2_lit')
i2 = 0
for tb, ta, l5 in s5:
    while i2 < len(s2) and s2[i2][0] < tb - 0.16:
        i2 += 1
    l2 = s2[i2][2] if i2 < len(s2) and s2[i2][0] <= tb + 0.16 else -1
    if i2 < len(s2) and s2[i2][0] <= tb + 0.16:
        print(f'{tb:+.2f}  {l5:10.0f}  {l2:10.0f}')
