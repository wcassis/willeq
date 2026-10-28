#!/usr/bin/env python3
"""B1178 ask1 (v2): unclipped target-height via connected components.
target = largest 8-connected component of |with - without| > thr, after a small
closing. Reports its height (row extent), column centroid, area, and the threshold
sweep. Robust to the scattered environmental noise that defeats a naive row count.
usage: b1178_height2.py <with_target.png> <without_target.png> <label>
"""
import sys, numpy as np, cv2
wp, wo, label = sys.argv[1], sys.argv[2], sys.argv[3]
a = cv2.imread(wp).astype(np.int16)
b = cv2.imread(wo).astype(np.int16)
H, W, _ = a.shape
d = np.abs(a - b).max(axis=2)
print(f"# {label}: frame {W}x{H}, diff mean {d.mean():.2f} p95 {np.percentile(d,95):.0f}")
kernel = np.ones((3,3), np.uint8)
for thr in (40,30,25,20,15,10):
    m = (d > thr).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, kernel)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 4)
    if n < 2:
        print(f"# target thr{thr}: no component"); continue
    # largest component (skip bg=0)
    areas = stats[1:, cv2.CC_STAT_AREA]
    i = 1 + int(np.argmax(areas))
    x,y,w,h,area = stats[i]
    cx = cent[i][0]
    print(f"# target thr{thr}: LARGEST h={h} rows {y}..{y+h-1} col~{int(cx)} area={area}")
# adopted = thr20 largest component
m = (d > 20).astype(np.uint8); m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, kernel)
n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 4)
if n >= 2:
    areas = stats[1:, cv2.CC_STAT_AREA]; i = 1 + int(np.argmax(areas))
    x,y,w,h,area = stats[i]; cx = int(cent[i][0])
    print(f"TARGET {label}: h={h} px rows {y}..{y+h-1} col_centroid={cx} area={area} (thr20 largest component, unclipped)")
else:
    print(f"TARGET {label}: no component")
# caster = largest dark component in the central band of the without-target still
gray = cv2.cvtColor(b.astype(np.uint8), cv2.COLOR_BGR2GRAY).astype(np.int16)
cx0, cx1 = W//2-140, W//2+140
band = gray[:, cx0:cx1]
rowmed = np.median(band, axis=1)
dark = ((rowmed[:,None] - band) > 40).astype(np.uint8)
dark = cv2.morphologyEx(dark, cv2.MORPH_CLOSE, kernel)
n, lab, stats, cent = cv2.connectedComponentsWithStats(dark, 4)
if n >= 2:
    areas = stats[1:, cv2.CC_STAT_AREA]; i = 1 + int(np.argmax(areas))
    x,y,w,h,area = stats[i]
    print(f"CASTER {label}: h={h} px rows {y}..{y+h-1} col_centroid={int(cent[i][0])+cx0} area={area} (central-band dark, unclipped)")
else:
    print(f"CASTER {label}: no component")
