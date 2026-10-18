#!/usr/bin/env python3
"""B1051-VERIFY per-NPC comparison -- official (oracle) vs XU4 vs x86, same server
+ same pose (Testchar, qeynos2, seeded x/y/z/heading, first-person heading fixed
by the DB row so all three clients look the same way).

For each spot, three stills (official / xu4 / x86) of the SAME world view. A
central band is cropped (the region the eye is looking at -- the NPCs in front
of the player) and, per client, the B1051 texture-diversity metric is computed
(coarse-unique, gold, red, top-5 share) exactly as
refactor/B1051/b1051_per_npc_metrics.py does. A cross-client colour-histogram
distance (official vs xu4, official vs x86) is the "does this client render the
same world as the oracle" number; the official-vs-x86 distance is the reference
band (x86 llvmpipe is a known-good renderer of the SAME fix), so a willeq client
whose distance to official sits in that band is PASSING, and one whose central
band is flat (coarse-unique near the pre-fix ~42, or distance far above
official-vs-x86) is a FAIL.

Usage:
  b1051_verify_compare.py <spot> <official.png> <xu4.png> <x86.png>
Prints a table row. Exit 0 (the numbers are the result; the verdict is in the
numbers, judged by the caller/VERIFY.md).

CROP: central band x 0.25w..0.75w, y 0.25h..0.70h of the 1280x720 frame -- the
frontal region where the in-front-of-player NPCs sit in first person. (The
b1051_per_npc_metrics.py crop is a narrower torso band for a CENTERED subject;
here the subject is "the world in front", so the band is wider.)
"""
import sys

try:
    from PIL import Image
    import numpy as np
except Exception as e:  # noqa: BLE001
    print("NOPIL", e)
    sys.exit(0)

CROP = (0.25, 0.25, 0.75, 0.70)


def crop_arr(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    box = (int(w * CROP[0]), int(h * CROP[1]), int(w * CROP[2]), int(h * CROP[3]))
    return np.asarray(im.crop(box), dtype=np.int32)


def metric(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    gold = float(np.mean((r > 150) & (g > 120) & (b < 120)))
    red = float(np.mean((r > 150) & (g < 110) & (b < 110)))
    q = (a // 16)
    uniq = len(set(map(tuple, q.reshape(-1, 3).tolist())))
    codes = q[..., 0] * 256 * 256 + q[..., 1] * 256 + q[..., 2]
    vals, counts = np.unique(codes, return_counts=True)
    order = np.argsort(-counts)
    top5 = float(counts[order[:5]].sum() / codes.size)
    return gold, red, uniq, top5


def hist(a, bins=32):
    # Joint R,G histogram over 32x32 bins (8-level cells), built manually to
    # avoid numpy histogram2d's sample-shape quirks.
    r = np.clip(a[..., 0] // 8, 0, 31).ravel()
    g = np.clip(a[..., 1] // 8, 0, 31).ravel()
    idx = r * bins + g
    h = np.bincount(idx, minlength=bins * bins).astype(np.float64).reshape(bins, bins)
    if h.sum() > 0:
        h /= h.sum()
    return h


def hdist(a, b, bins=32):
    # Total-variation distance between the two joint R,G histograms, 0..1
    # (0 = identical colour distribution, 1 = disjoint). The official-vs-x86
    # value is the reference band; xu4 within that band (or lower) is a PASS.
    pa, pb = hist(a, bins), hist(b, bins)
    return float(0.5 * np.abs(pa - pb).sum())


def main():
    spot, off, xu4, x86 = sys.argv[1:5]
    A, B, C = crop_arr(off), crop_arr(xu4), crop_arr(x86)
    mo, mb, mc = metric(A), metric(B), metric(C)
    d_ox = hdist(A, B)
    d_ox8 = hdist(A, C)
    print(f"spot={spot}")
    for name, m, f in (("official", mo, off), ("xu4", mb, xu4), ("x86", mc, x86)):
        print(f"  {name:9s} gold={m[0]:.4f} red={m[1]:.4f} coarse_unique={m[2]:4d} "
              f"top5_share={m[3]:.3f}  [{f}]")
    print(f"  dist(official,xu4)={d_ox:.4f}  dist(official,x86)={d_ox8:.4f}  "
          f"(x86 = reference band; xu4 within ~1.5x of x86-dist => PASS)")


if __name__ == "__main__":
    main()
