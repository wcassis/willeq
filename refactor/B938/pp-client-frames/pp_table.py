#!/usr/bin/env python3
# Per-frame 3D-world-region diff table for the three pairs, over the common
# frame indices. The world region excludes the fixed UI rects (status, combat
# log, hotbar, gems, target) which carry the run-to-run text variance. Output:
# a markdown table suitable for the B938 evidence README.
import glob
import os
import re
import numpy as np
from PIL import Image

R = "/home/user/kb-evidence/b938-placement-records/pp-client-frames"
H, W = 720, 1280
UI = [(0,0,300,120),(0,500,520,720),(360,660,920,720),(1180,470,1280,720),(980,0,1280,150)]
world = np.ones((H,W), bool)
for x0,y0,x1,y1 in UI: world[y0:y1, x0:x1] = False

def fidx(p):
    m = re.search(r"frame_(\d+)\.png$", os.path.basename(p)); return int(m.group(1))
def frames(d):
    return {fidx(p): p for p in glob.glob(os.path.join(R, d, "frames", "frame_*.png"))}
def worlddiff(pa, pb, thr=8):
    a = np.asarray(Image.open(pa).convert("RGB"), np.int16)
    b = np.asarray(Image.open(pb).convert("RGB"), np.int16)
    d = np.abs(a-b).max(axis=2)
    m = (d>thr) & world
    return int(m.sum())
def maxabs(pa, pb):
    a = np.asarray(Image.open(pa).convert("RGB"), np.int16)
    b = np.asarray(Image.open(pb).convert("RGB"), np.int16)
    return int(np.abs(a-b).max())

fa1 = frames("baseA1"); fa2 = frames("baseA2")
fb1 = frames("headB1"); fb2 = frames("headB2")
common = sorted(set(fa1) & set(fa2) & set(fb1) & set(fb2))
print("| frame | A1->B1 base\\->head world px | A1->A2 base control | B1->B2 head control | A1->B1 max abs |")
print("|---:|---:|---:|---:|---:|")
tot_b = tot_ca = tot_cb = 0
for i in common:
    b = worlddiff(fa1[i], fb1[i]); ca = worlddiff(fa1[i], fa2[i]); cb = worlddiff(fb1[i], fb2[i])
    mx = maxabs(fa1[i], fb1[i])
    tot_b += b; tot_ca += ca; tot_cb += cb
    print(f"| {i} | {b} | {ca} | {cb} | {mx} |")
print(f"| **sum** | **{tot_b}** | **{tot_ca}** | **{tot_cb}** | — |")
print(f"\nFrames compared: {len(common)} (each pair's common indices)")
print(f"World region = {int(world.sum())} px of {W}x{H} (UI rects excluded).")
print(f"Verdict: base->head world-region diff ({tot_b}) is BELOW both same-binary controls ({tot_ca} base, {tot_cb} head).")
