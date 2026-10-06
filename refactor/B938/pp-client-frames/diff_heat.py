#!/usr/bin/env python3
# Visualize where two frames differ: a heatmap (white = different) plus a
# count of differing pixels in the 3D world region vs the fixed UI regions.
# UI regions are the static screen rects (from the probe frame's layout):
#   status top-left, combat log bottom-left, hotbar bottom-center, gems bottom-right.
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int16)
b = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.int16)
d = np.abs(a - b).max(axis=2)  # max abs channel diff per pixel
H, W = d.shape
mask = (d > 8).astype(np.uint8)  # differing-pixel map (threshold 8 above FP16 noise)

# Fixed UI rects (x0,y0,x1,y1) at 1280x720, from the rendered layout:
ui = {
    "status_topleft": (0, 0, 300, 120),
    "combatlog_botleft": (0, 500, 520, 720),
    "hotbar_botcenter": (360, 660, 920, 720),
    "gems_botright": (1180, 470, 1280, 720),
    "target_topright": (980, 0, 1280, 150),
}
def in_rect(x, y, r): return r[0] <= x < r[2] and r[1] <= y < r[3]
ui_mask = np.zeros((H, W), bool)
for name, r in ui.items():
    ui_mask[r[1]:r[3], r[0]:r[2]] = True
world_mask = ~ui_mask

diff_total = int(mask.sum())
diff_ui = int((mask & ui_mask).sum())
diff_world = int((mask & world_mask).sum())
world_px = int(world_mask.sum())
print(f"frame size {W}x{H}")
print(f"differing px (thr>8): {diff_total}")
print(f"  in fixed-UI regions : {diff_ui}")
print(f"  in 3D world region  : {diff_world}  ({100*diff_world/world_px:.2f}% of world's {world_px} px)")

# Write the heatmap (differing pixels white on dark) so it can be inspected.
heat = np.zeros((H, W, 3), np.uint8)
heat[..., 0] = (mask * 255)
Image.fromarray(heat).save(sys.argv[3] if len(sys.argv) > 3 else "diff_heat.png")
print(f"wrote heatmap: {sys.argv[3] if len(sys.argv) > 3 else 'diff_heat.png'}")
