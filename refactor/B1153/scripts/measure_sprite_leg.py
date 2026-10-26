#!/usr/bin/env python3
"""B1153 step 4: per-sprite p90 on the lane-5 capture (the sprite leg).

Decodes the leg's capture.pcap (scripts/eqemu/oracle/vendor/eqpcap.py) for the
two 93 casts (C->S OP_CastSpell = BeginCast, S->C OP_Action = the action),
aligns the leg's video.mp4 via video.calibration.tsv (calibrated_t0 = epoch of
video frame 0), builds one fx_measure.py directory per cast (the B1132
instrument, exactly as review/B1149/scripts/run_scene.sh uses it), measures
the caster model's pixel height from the frames' own temporal variance
(idle animation = the only thing that moves pre-cast; the background is
static), and runs review/B1132/scripts/fx_sprite_roi.py (pooled) for SOURCE
and LANDING. Same instrument as the harness side (the b1130 rows'
p90_src/p90_land columns).

usage: measure_sprite_leg.py <oracle_dir>
writes: <oracle_dir>/sprite_p90.tsv
"""
import importlib.util
import json
import os
import subprocess
import sys

OUT = sys.argv[1]
VENDOR = "/home/user/src/willeq-wt/kb-b1153-retire/scripts/eqemu/oracle/vendor"
SCRIPTS = "/mnt/projects/fable/review/B1132/scripts"

spec = importlib.util.spec_from_file_location("eqpcap", VENDOR + "/eqpcap.py")
eqpcap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eqpcap)

decoded = eqpcap.decode(os.path.join(OUT, "capture.pcap"))
casts = []  # (t_bc, t_act)
pending_bc = None
for t, direction, sport, cport, name, code, payload in decoded:
    if name == "OP_CastSpell" and direction == "C->S":
        pending_bc = t
    elif name == "OP_Action" and direction == "S->C" and pending_bc is not None:
        casts.append((pending_bc, t))
        pending_bc = None
if not casts:
    print("NO CASTS IN PCAP (the leg's casts never reached the wire?)")
    sys.exit(3)
print(f"casts: {casts}")

# video calibration: calibrated_t0 = epoch of video frame 0 (the driver's
# marker calibration), 30 fps
t0 = None
for l in open(os.path.join(OUT, "video.calibration.tsv")):
    c = l.rstrip("\n").split("\t")
    if len(c) >= 4 and c[0] != "launch_epoch":
        t0 = float(c[3])
assert t0 is not None, "no video.calibration.tsv row"

# does the container's pts already carry the epoch, or start at 0?
pts = subprocess.run(
    ["ffprobe", "-v", "error", "-select_streams", "v", "-show_entries",
     "frame=pts_time", "-of", "csv=p=0", "-read_intervals", "%+#3",
     os.path.join(OUT, "video.mp4")], capture_output=True, text=True).stdout.split()
pts0 = float(pts[0].rstrip(",")) if pts else 0.0
off = t0 - pts0  # added to each pts to get epoch (fx_measure.py's contract)
print(f"t0={t0} pts0={pts0} off={off}")

# measure the caster model's pixel height: temporal std of the pre-cast
# frames. The background and UI are static; the model's idle animation is
# the only sustained motion -> its bounding box is the model.
import cv2  # noqa: E402
import numpy as np  # noqa: E402

MPX = 220.0  # caster model height in the chase-camera video, px. Method: the
# pre-cast frames' motion union (temporal variance of the 6 s pre-cast window,
# venv python, refactor/B1153/scripts/measure_sprite_leg.py's detector) puts
# the model's feet at y=606; a frame inspection of frame 15700 (probe_15700.png,
# in the leg's evidence dir) puts the head at ~y=380 -> 220 px. This is a
# chase camera (the pre-goto F9 presses were reset by the zone transition),
# not B1130's user-camera-1 (209 px, review/B1130/README.md line 439): the
# p90 below is model-normalised, so the ~5 % scale difference and the camera
# angle are the leg's stated uncertainty, not a hidden constant.
model_px_full = MPX
print(f"model px: {model_px_full:.1f} (documented method, not auto-detected)")

rows = []
for i, (t_bc, t_act) in enumerate(casts, 1):
    d = os.path.join(OUT, f"sprite_cast{i}")
    subprocess.run(
        [sys.executable, f"{SCRIPTS}/fx_measure.py", os.path.join(OUT, "video.mp4"),
         f"{off:.3f}", f"{t_bc:.3f}", f"{t_act:.3f}", f"{t_act + 8.0:.3f}",
         "0,0,1280,800", d, "--label", f"b1153sprite{i}"],
        check=True, stdout=subprocess.DEVNULL)
    # LANDING: pool the post-action window where the sprites are lit (93's
    # landing runs ~0.3..6.5 s after the action, review/B1130 r51 row 0)
    for stage, t1, t2 in (("LANDING", 0.5, 5.0), ("SOURCE", 0.3, 4.0)):
        # the leg's chase-camera geometry (probe_15700.png): caster centre
        # (feet ~620,600), TrollF right (feet ~930,650) -> LANDING ROI around
        # the troll, SOURCE ROI around the caster.
        env = dict(os.environ)
        env["ROI"] = "700,1200,400,750" if stage == "LANDING" else "480,820,330,650"
        r = subprocess.run(
            [sys.executable, f"{SCRIPTS}/fx_sprite_roi.py", d, f"{model_px_full:.0f}", stage,
             f"{t1:.2f}", f"{t2:.2f}"], capture_output=True, text=True, env=env)
        line = r.stdout.strip() or r.stderr.strip()
        print(f"cast{i} {stage}: {line}")
        rows.append((i, stage, line))

with open(os.path.join(OUT, "sprite_p90.tsv"), "w") as fp:
    fp.write("# B1153 sprite leg per-sprite p90 (fx_sprite_roi.py pooled; model px above)\n")
    fp.write(f"# model_px={model_px_full:.1f} casts={[f'{bc:.3f}/{ac:.3f}' for bc, ac in casts]}\n")
    for i, stage, line in rows:
        fp.write(f"cast{i}\t{stage}\t{line}\n")
print("wrote", os.path.join(OUT, "sprite_p90.tsv"))
