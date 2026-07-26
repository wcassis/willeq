#!/usr/bin/env python3
"""P09c Phase C: three-way pixel comparison at the 9 golden views + 6 pinned
poses, production config: unmerged (production path, per-region world
matrix) vs baked-unmerged (same baked positions as merge, one draw per
region, identity transform) vs merged (coalesced baked draws).

Bars (per refactor/P09c_driver_fix_and_measure.md):
  - merged vs baked-unmerged: max_delta=0 at ALL 15 poses expected (isolates
    merge-only effects).
  - baked-unmerged vs unmerged: the pure FP transform-order delta (report,
    do not judge).
"""
import json
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
BIN = PROJECT_DIR / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = "/mnt/projects/EverQuestP1999"
OUT_DIR = PROJECT_DIR / "build-gles2" / "ci_artifacts" / "p09c_baked"
OUT_DIR.mkdir(parents=True, exist_ok=True)

PROD_ARGS = ["--adaptive-lighting", "--zone-pp-budget", "20",
             "--pp-budget-mode", "count", "--player-light-level", "9"]

manifest = json.load(open(PROJECT_DIR / "tests/goldens/manifest.json"))
golden_poses = []
for z in manifest["zones"]:
    zone = z["zone"]
    args = z["extraArgs"]
    i = 0
    while i < len(args):
        assert args[i] == "--camera-pose"
        golden_poses.append((zone, args[i+1]))
        i += 2

pinned_poses = [
    ("bf355", "-663.9,-297.1,5.0,118.2,-5.0"),
    ("bf472", "-308.0,-235.2,5.0,41.1,-5.0"),
    ("bf626", "-379.8,207.8,5.0,299.5,-5.0"),
    ("bf791", "-808.9,13.5,5.0,190.8,-5.0"),
    ("bf917", "-615.2,-318.2,5.0,107.7,-5.0"),
    ("bf987", "-388.9,-298.8,5.0,61.5,-5.0"),
]

def run(zone, pose, tag, mode):
    prefix = OUT_DIR / f"{zone}_{tag}_{mode}"
    cmd = [str(BIN), "--zone", zone, "--eq-path", EQ_PATH,
           "--frames", "1", "--camera-pose", pose,
           "--dump-png-prefix", str(prefix), "--dump-frames", "0"]
    cmd += PROD_ARGS
    if mode == "merged":
        cmd.append("--merge-zone-draws")
    elif mode == "baked":
        cmd.append("--baked-unmerged-zone-draws")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"FAIL run: {' '.join(cmd)}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
        sys.exit(1)
    return Path(f"{prefix}_f0.png")

def compare(a, b):
    from PIL import Image, ImageChops
    ia = Image.open(a).convert("RGB")
    ib = Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return False, -1, 255
    diff = ImageChops.difference(ia, ib)
    bbox = diff.getbbox()
    if bbox is None:
        return True, 0, 0
    diff_count = 0
    max_delta = 0
    for px in diff.getdata():
        m = max(px)
        if m:
            diff_count += 1
            if m > max_delta:
                max_delta = m
    return (diff_count == 0 and max_delta == 0), diff_count, max_delta

results = []
mvb_all_pass = True
for tag, (zone, pose) in [("golden%d" % i, gp) for i, gp in enumerate(golden_poses)] + \
                          [(t, (("befallen"), p)) for t, p in pinned_poses]:
    png_u = run(zone, pose, tag, "unmerged")
    png_b = run(zone, pose, tag, "baked")
    png_m = run(zone, pose, tag, "merged")
    mvb_id, mvb_diff, mvb_max = compare(png_m, png_b)
    bvu_id, bvu_diff, bvu_max = compare(png_b, png_u)
    mvb_all_pass &= mvb_id
    results.append((zone, tag, mvb_id, mvb_diff, mvb_max, bvu_id, bvu_diff, bvu_max))
    print(f"{zone:10s} {tag:10s} merged-vs-baked: {'PASS' if mvb_id else 'FAIL'} "
          f"diff={mvb_diff:6d} max_delta={mvb_max:3d}  |  "
          f"baked-vs-unmerged(FP): diff={bvu_diff:6d} max_delta={bvu_max:3d}")

print()
print("=== merged vs baked-unmerged (merge-only isolation) ===")
print(f"{'zone':10s} {'pose':10s} {'diff_px':>8s} {'max_delta':>10s} status")
for zone, tag, mvb_id, mvb_diff, mvb_max, *_ in results:
    print(f"{zone:10s} {tag:10s} {mvb_diff:8d} {mvb_max:10d} {'PASS' if mvb_id else 'FAIL'}")
print()
print("ALL PASS (merge mechanism is max_delta=0 vs baked, at every pose)" if mvb_all_pass
      else "SOME FAILED (merge-only bug remains, loop back to Phase B)")

print()
print("=== baked-unmerged vs production-path unmerged (pure FP transform-order delta) ===")
print(f"{'zone':10s} {'pose':10s} {'diff_px':>8s} {'max_delta':>10s}")
for zone, tag, *_ , bvu_id, bvu_diff, bvu_max in results:
    print(f"{zone:10s} {tag:10s} {bvu_diff:8d} {bvu_max:10d}")

sys.exit(0 if mvb_all_pass else 1)
