#!/usr/bin/env python3
"""P09 Phase B: pixel-identity check, merged vs unmerged, at the 9 golden
views + 6 pinned poses, under production config. Ad hoc scratch script
(not committed as a shared asset, per convention -- refactor/scratch/)."""
import json
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
BIN = PROJECT_DIR / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = "/mnt/projects/EverQuestP1999"
OUT_DIR = PROJECT_DIR / "build-gles2" / "ci_artifacts" / "p09_pixel"
OUT_DIR.mkdir(parents=True, exist_ok=True)

PROD_ARGS = ["--adaptive-lighting", "--zone-pp-budget", "20",
             "--pp-budget-mode", "count", "--player-light-level", "9"]

manifest = json.load(open(PROJECT_DIR / "tests/goldens/manifest.json"))
golden_poses = []  # (zone, pose_args)
for z in manifest["zones"]:
    zone = z["zone"]
    args = z["extraArgs"]
    # extraArgs is a flat list of repeated --camera-pose <spec> pairs
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

def run(zone, pose, tag, merged):
    prefix = OUT_DIR / f"{zone}_{tag}_{'merged' if merged else 'unmerged'}"
    cmd = [str(BIN), "--zone", zone, "--eq-path", EQ_PATH,
           "--frames", "1", "--camera-pose", pose,
           "--dump-png-prefix", str(prefix), "--dump-frames", "0"]
    cmd += PROD_ARGS
    if merged:
        cmd.append("--merge-zone-draws")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"FAIL run: {' '.join(cmd)}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
        sys.exit(1)
    draws = None
    for line in r.stdout.splitlines():
        if line.startswith("F0000"):
            draws = line
    png_path = Path(f"{prefix}_f0.png")
    return png_path, draws

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
all_pass = True
for tag, (zone, pose) in [("golden%d" % i, gp) for i, gp in enumerate(golden_poses)] + \
                          [(t, (("befallen"), p)) for t, p in pinned_poses]:
    png_u, line_u = run(zone, pose, tag, merged=False)
    png_m, line_m = run(zone, pose, tag, merged=True)
    identical, diff_count, max_delta = compare(png_u, png_m)
    all_pass &= identical
    results.append((zone, tag, pose, identical, diff_count, max_delta, line_u, line_m))
    status = "PASS" if identical else "FAIL"
    print(f"[{status}] {zone:10s} {tag:10s} diff_px={diff_count:6d} max_delta={max_delta:3d}  {line_u}")
    print(f"                                                                     {line_m}")

print()
print("zone       pose       diff_px  max_delta  status")
for zone, tag, pose, identical, diff_count, max_delta, line_u, line_m in results:
    print(f"{zone:10s} {tag:10s} {diff_count:7d}  {max_delta:9d}  {'PASS' if identical else 'FAIL'}")
print()
print("ALL PASS (max_delta=0 at all poses)" if all_pass else "SOME FAILED")
sys.exit(0 if all_pass else 1)
