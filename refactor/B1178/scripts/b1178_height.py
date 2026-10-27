#!/usr/bin/env python3
"""B1178 ask1: unclipped target-height measurement for a b1118 lane-5 class leg.
Difference method (B1164): target silhouette = |with_target - without_target| over a
column band, threshold sweep 40/30/25/20/15/10, minpx 3, feet = last row above the
selection ring. Reports the target (TrollF) height in host px + the row extent + the
column centroid. The caster height is measured from the without-target still's central
figure silhouette (threshold 40 on the max-channel drop from the row median).
usage: b1178_height.py <with_target.png> <without_target.png> <label>
"""
import sys, numpy as np, cv2
wp, wo, label = sys.argv[1], sys.argv[2], sys.argv[3]
a = cv2.imread(wp).astype(np.int16)
b = cv2.imread(wo).astype(np.int16)
H, W, _ = a.shape
d = np.abs(a - b).max(axis=2)
print(f"# {label}: with={wp} without={wo} frame {W}x{H}")
# --- target = difference, over the full width (the target is right-of-center) ---
def sweep(mask, thr, minpx=3):
    cnt = (mask > thr).sum(axis=1)
    rows = [y for y in range(H) if cnt[y] >= minpx]
    if not rows:
        return None
    return rows[0], rows[-1], (rows[-1]-rows[0]+1)
for thr in (40,30,25,20,15,10):
    r = sweep(d, thr)
    if r:
        top, bottom, h = r
        # column centroid at the adopted threshold rows
        m = (d > thr)
        cols = np.nonzero(m.any(axis=0))[0]
        cc = int(np.round((cols.min()+cols.max())/2)) if len(cols) else -1
        print(f"# target thr{thr}: rows {top}..{bottom} h{h} col~{cc}")
    else:
        print(f"# target thr{thr}: no rows")
# adopt thr20 (B1166/B1164 method)
r = sweep(d, 20)
if r:
    top, bottom, h = r
    m = d > 20
    ys, xs = np.nonzero(m)
    cc = int(np.round(xs.mean())) if len(xs) else -1
    print(f"TARGET {label}: h={h} px rows {top}..{bottom} col_centroid={cc} (thr20, unclipped)")
# --- caster = central figure silhouette in the without-target still ---
# the caster is the large central figure; isolate dark pixels vs the row median
gray = cv2.cvtColor(b.astype(np.uint8), cv2.COLOR_BGR2GRAY).astype(np.int16)
# central band (caster is roughly center)
cx0, cx1 = W//2-120, W//2+120
band = gray[:, cx0:cx1]
rowmed = np.median(band, axis=1)
dark = (rowmed[:,None] - band) > 40   # pixels darker than the row median
cnt = dark.sum(axis=1)
rows = [y for y in range(H) if cnt[y] >= 3]
if rows:
    top, bottom = rows[0], rows[-1]
    print(f"CASTER {label}: h={bottom-top+1} px rows {top}..{bottom} (central-band dark, unclipped)")
else:
    print(f"CASTER {label}: no rows")
