#!/usr/bin/env python3
"""B1188 finding 6: matched-t box comparison. Same box [60:360,770:1030]
(B1191's scene-matched pillar box), same frame index, same tail baseline
(40 frames), two frame dirs: the B1191 fresh tip render (pre-B1188 tree,
pack 1cdd31e7) and this fix head's re-render (1:1 atlas, pack 785690e6).
Diff-based per-channel means over lit80 / px>150 in the box, exactly
b1191_scan_box.py's math, at a named frame instead of each side's own peak."""
import os
import sys
import numpy as np, cv2

d1, d2, globpat, box, tail, frames = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), [int(x) for x in sys.argv[6].split(',')]
r0, r1, c0, c1 = [int(x) for x in box.split(':')]

def load(p):
    return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)


def scan(d, tag):
    fl = sorted((os.path.join(d, f) for f in os.listdir(d) if f.startswith('frame_')), key=lambda p: int(os.path.basename(p)[6:11]))
    imgs = [load(p) for p in fl]
    base = np.median(np.stack([im for im in imgs[-tail:]]), axis=0)
    out = [f'{tag} (base mean B={base[r0:r1, c0:c1].mean(axis=(0,1))[2]:.1f}):']
    for i in frames:
        dimg = np.clip(imgs[i] - base, 0, 255)[r0:r1, c0:c1]
        mx = dimg.max(axis=2)
        for nm, thr in (('lit80', 80), ('px150', 150)):
            m = mx > thr
            if m.sum() > 50:
                mm = dimg[m].mean(axis=0)
                out.append(f'  f{i} (t={i/30:.2f}s) {nm}: n={int(m.sum())} R={mm[0]:.1f} G={mm[1]:.1f} B={mm[2]:.1f} G/R={mm[1]/max(mm[0],1):.3f}')
    print('\n'.join(out))

scan(d1, 'TIP  (pre-B1188, 1cdd31e7)')
scan(d2, 'FIXHEAD (B1188 1:1, 785690e6)')
