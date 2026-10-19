#!/usr/bin/env python3
"""B1051 per-NPC still-to-still texture comparison.

For a set of stills taken at the SAME pose (same server, same /loc, same
heading) by different clients, this measures how close the willeq render of
the figure in view is to the OFFICIAL client's render of that same figure.

Method (mechanical, no vision):
- crop: a central torso band, x 0.30w..0.72w, y 0.12h..0.72h (the region the
  B1051 fix targets: the body's armour/texture, not the name tag or sky).
  This assumes the figure is roughly centred in the crop (same heading +
  same distance across the three clients, so the figure lands in roughly
  the same screen region).
- coarse_unique: distinct 16-level-per-channel colour cells (the QA metric).
  A multi-texture body = high count; the pre-fix ETC1 smear = one dominant
  mottle bucket (top5_share ~0.5+).
- gold / red fraction: the guard's signature colours (gold chest/tabard,
  red chevron). Present in the official + post-fix, absent in the smear.
- hist_dist: the per-NPC distance the card asks for -- a normalised
  earth-mover-free colour-histogram distance (mean abs diff of the 16x16x16
  quantised histogram, L1) between the willeq crop and the official crop.
  Small distance = willeq matches the oracle; a flat willeq body (low
  coarse_unique, one dominant bucket) yields a LARGE distance.

Usage: b1051_still_compare.py OFFICIAL still1 [still2 ...]
Prints a table: client -> crop box, coarse_unique, gold, red, top5,
hist_dist vs official. exit 0.
"""
import sys
try:
    from PIL import Image
    import numpy as np
except Exception as e:
    print("NOPIL", e); sys.exit(0)

def crop_metrics(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    box = (int(w*0.30), int(h*0.12), int(w*0.72), int(h*0.72))
    a = np.asarray(im.crop(box), dtype=np.int32)
    r, g, b = a[...,0], a[...,1], a[...,2]
    gold = float(np.mean((r>150)&(g>120)&(b<120)))
    red  = float(np.mean((r>150)&(g<110)&(b<110)))
    q = a//16
    codes = q[...,0]*256*256 + q[...,1]*256 + q[...,2]
    uniq = int(len(np.unique(codes)))
    vals, counts = np.unique(codes, return_counts=True)
    order = np.argsort(-counts)
    top5 = float(counts[order[:5]].sum()/codes.size)
    # 4-level-per-channel histogram for the distance (16^3 cells)
    q4 = a//40  # 64 levels total per channel is too big; 40 -> 6^3=216 cells
    hcodes = q4[...,0]*36 + q4[...,1]*6 + q4[...,2]
    hist = np.bincount(hcodes, minlength=216).astype(np.float64)
    hist /= hist.sum()
    return box, uniq, gold, red, top5, hist

def l1(h1, h2):
    return float(np.abs(h1-h2).sum()/2.0)

def main():
    official = sys.argv[1]
    others = sys.argv[2:]
    ob,ou,og,or_,ot5,oh = crop_metrics(official)
    print(f"{'client':28s} {'coarse':>7s} {'gold':>7s} {'red':>7s} {'top5':>7s} {'hist_dist':>10s}")
    print(f"{'OFFICIAL='+official.split('/')[-1]:28s} {ou:>10} {og:7.4f} {or_:7.4f} {ot5:7.3f} {'—':>10s}")
    for p in others:
        try:
            _,u,g,rd,t5,h = crop_metrics(p)
            d = l1(oh, h)
            print(f"{p.split('/')[-1]:28s} {u:>10} {g:7.4f} {rd:7.4f} {t5:7.3f} {d:10.4f}")
        except FileNotFoundError:
            print(f"{p:28s} MISSING")

if __name__ == "__main__":
    main()
