#!/bin/bash
# P33c: launch willeq with cwd = the session-local wind-OFF config scratch
# dir, so config/display_settings.json (animatedTrees:false) resolves
# instead of the committed config/. All other args are absolute paths.
set -euo pipefail
cd /mnt/projects/fable/wt/slot1/data/captures/runs/windoff
stdbuf -oL -eL timeout 120 /mnt/projects/fable/wt/slot1/build-gles2/bin/willeq \
  -c /mnt/projects/fable/wt/slot1/data/captures/p33c_willeq.json \
  --renderer gles2 --no-audio \
  --script /mnt/projects/fable/wt/slot1/data/captures/p33c_qeynos2_wind_off.txt \
  --dump-frames-dir /mnt/projects/fable/videos/p33_tree_wind/frames_wind_off --dump-every 1
