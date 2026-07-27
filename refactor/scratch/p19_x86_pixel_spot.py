#!/usr/bin/env python3
"""P19 x86 pixel spot check: radius vs full-PP still pixel-identical at 2-3
poses under the CURRENT fixture (regression check on the Q03 losslessness
result), both WITHOUT and WITH the full final stack (merge+composite+occluder
256) to confirm the stack doesn't change the losslessness property."""
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
BIN = PROJECT_DIR / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = "/mnt/projects/EverQuestP1999"
EQE1_PATH = "/mnt/projects/EverQuest_ETC1"
OUT_DIR = PROJECT_DIR / "build-gles2" / "ci_artifacts" / "p19_pixel"
OUT_DIR.mkdir(parents=True, exist_ok=True)

PROD_ARGS = ["--player-light-level", "9"]  # current fixture (Q03 amendment, GLS level 9)
FULL_STACK = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
              "--eqe1-path", EQE1_PATH, "--occluder-budget", "256"]

POSES = [
    ("befallen", "bf472", "-308.0,-235.2,5.0,41.1,-5.0"),
    ("befallen", "bf987", "-388.9,-298.8,5.0,61.5,-5.0"),
    ("qeynos2", "qz_f0", "0,0,5.0,0,-5.0"),  # placeholder overwritten below from manifest
]


def load_golden_poses():
    import json
    manifest = json.load(open(PROJECT_DIR / "tests/goldens/manifest.json"))
    out = []
    for z in manifest["zones"]:
        zone = z["zone"]
        args = z["extraArgs"]
        i = 0
        idx = 0
        while i < len(args):
            assert args[i] == "--camera-pose"
            out.append((zone, f"{zone}_f{idx}", args[i + 1]))
            i += 2
            idx += 1
    return out


def run(zone, pose, tag, mode, stack):
    prefix = OUT_DIR / f"{zone}_{tag}_{mode}{'_stack' if stack else ''}"
    cmd = [str(BIN), "--zone", zone, "--eq-path", EQ_PATH,
           "--frames", "1", "--camera-pose", pose,
           "--dump-png-prefix", str(prefix), "--dump-frames", "0"]
    cmd += PROD_ARGS
    if stack:
        cmd += FULL_STACK
    if mode == "fullpp":
        pass  # no --adaptive-lighting => full per-pixel everywhere
    elif mode == "radius":
        cmd += ["--adaptive-lighting", "--pp-budget-mode", "radius"]
    else:
        raise ValueError(mode)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"FAIL run: {' '.join(cmd)}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
        sys.exit(1)
    png_path = Path(f"{prefix}_f0.png")
    return png_path


def compare(a, b):
    from PIL import Image, ImageChops
    ia = Image.open(a).convert("RGB")
    ib = Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return False, -1, 255, ia.size[0] * ia.size[1]
    diff = ImageChops.difference(ia, ib)
    bbox = diff.getbbox()
    total_px_early = ia.size[0] * ia.size[1]
    if bbox is None:
        return True, 0, 0, total_px_early
    diff_count = 0
    max_delta = 0
    for px in diff.getdata():
        m = max(px)
        if m:
            diff_count += 1
            if m > max_delta:
                max_delta = m
    total_px = ia.size[0] * ia.size[1]
    return (diff_count == 0 and max_delta == 0), diff_count, max_delta, total_px


def main():
    golden = load_golden_poses()
    qz_golden = [p for p in golden if p[0] == "qeynos2"][:1]
    targets = [p for p in POSES if p[0] == "befallen"] + qz_golden
    print(f"[poses] using: {targets}")

    for stack in (False, True):
        print(f"\n=== stack={'FULL' if stack else 'none (base fixture)'} ===")
        for zone, tag, pose in targets:
            fullpp_png = run(zone, pose, tag, "fullpp", stack)
            radius_png = run(zone, pose, tag, "radius", stack)
            identical, diff_count, max_delta, total_px = compare(fullpp_png, radius_png)
            pct = 100.0 * diff_count / total_px if total_px > 0 else -1
            print(f"  {zone}/{tag} (stack={stack}): identical={identical} "
                  f"diff_px={diff_count}/{total_px} ({pct:.4f}%) max_delta={max_delta}")


if __name__ == "__main__":
    main()
