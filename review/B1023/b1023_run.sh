#!/usr/bin/env bash
# B1023 lane-1 (Orange Pi) 15-frame visual-gate run: the gate's OWN
# --platform pi benchmark invocation for each of the five manifest gate
# zones, out of the published armhf build, on the Orange Pi under the B1023
# lock. Frames + CSV + memory-json land in OUT (on the device /tmp), then
# get pulled back to the evidence dir. Same shape as B1037's b1037_run.sh,
# retargeted at lane 1's ci-arm publish + nollvm binary.
#
# Poses are read from the manifest's OWN platforms.pi section (copied to the
# device as $OUT/manifest.json) -- never transcribed by hand, so the run is
# byte-identical to what scripts/ci/visual_gate.py run_benchmark() would run.
#
# usage: b1023_run.sh <out-dir-on-device>   (manifest copied to $OUT first)
set -uo pipefail
OUT="$1"
D=/mnt/projects/fable/ci-arm/e58a42e2735e4c048e2f5d3e67aa393d646de40a
EQ=/mnt/projects/EverQuestP1999
BENCH="$D/bin/gles2_zone_benchmark-nollvm"   # B1024: the Pi set runs the
mkdir -p "$OUT"                              # shipped nollvm binary (docs/ci.md)
M="$OUT/manifest.json"

{
  date -u -Iseconds
  uname -a
  cat /proc/device-tree/model
  for c in /sys/class/drm/card*; do echo "card: $(basename "$c"): $(basename "$(readlink "$c/device/driver" 2>/dev/null)" 2>/dev/null)"; done
  cat "$D/BUILD_INFO" 2>/dev/null
} > "$OUT/device.txt" 2>&1
{ date -u -Iseconds; echo "thermal before"; } > "$OUT/thermal_before.txt"

# Zones, in the manifest's own order (source of truth).
ZONES=$(python3 - "$M" <<'PY'
import json,sys
m=json.load(open(sys.argv[1]))
print(' '.join(z['zone'] for z in m['platforms']['pi']['zones']))
PY
)
echo "zones: $ZONES" >> "$OUT/runner.log"
FAIL=0
for Z in $ZONES; do
  ZD="$OUT/$Z"; mkdir -p "$ZD"
  echo "=== $Z ===" >> "$OUT/runner.log"
  # The zone's extraArgs verbatim from the manifest (all --camera-pose pairs).
  POSES=$(python3 - "$M" "$Z" <<'PY'
import json,sys
m=json.load(open(sys.argv[1]))
for z in m['platforms']['pi']['zones']:
    if z['zone']==sys.argv[2]:
        print(' '.join(z['extraArgs']))
        break
PY
)
  # run_benchmark() with cfg.platform == "pi": --bench, --zone, --eq-path,
  # --frames 60, --warmup 5, --res 800 600, --dump-frames 0,1,2,
  # --dump-png-prefix <out>/<zone>, --csv, --memory-json, --occluder-budget
  # 16, productionConfig.args, extraArgs, then --drm.
  set -- "$BENCH" --zone "$Z" --eq-path "$EQ" \
    --frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2 \
    --dump-png-prefix "$OUT/$Z" --csv "$OUT/$Z.csv" --memory-json "$OUT/${Z}_mem.json" \
    --occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 \
    --pp-budget-mode count --player-light-level 9
  set -- "$@" $POSES --drm
  echo "$ $*" >> "$OUT/runner.log"
  # 400 s bound per zone: keeps the run bounded so it never holds the lane
  # open-ended; iceclad/timorous at 800x600 are the slowest.
  WILLEQ_DATA_ROOT="$D" WILLEQ_CACHE_DIR=/tmp/b1023g/cache timeout 400 "$@" > "$OUT/${Z}.log" 2>&1 &
  BPID=$!
  BENCH_PID=""
  for _ in $(seq 1 20); do
    BENCH_PID=$(awk '{print $1}' "/proc/$BPID/task/$BPID/children" 2>/dev/null | head -1)
    [ -n "$BENCH_PID" ] && break
    kill -0 "$BPID" 2>/dev/null || break
    sleep 0.2
  done
  # poll_maps: snapshot /proc/<pid>/maps the moment libgallium is mapped.
  for _ in $(seq 1 240); do
    if grep -q libgallium "/proc/$BENCH_PID/maps" 2>/dev/null; then
      cp "/proc/$BENCH_PID/maps" "$OUT/${Z}_maps.txt" 2>/dev/null
      break
    fi
    kill -0 "$BENCH_PID" 2>/dev/null || break
    sleep 0.5
  done
  wait "$BPID"; RC=$?
  echo "${Z}_exit=$RC" > "$OUT/${Z}_exit"
  echo "$Z exit=$RC" >> "$OUT/runner.log"
  for F in 0 1 2; do
    [ -f "$OUT/${Z}_f${F}.png" ] && mv "$OUT/${Z}_f${F}.png" "$ZD/f${F}.png"
  done
  grep -m1 '^\[gldriver\]' "$OUT/${Z}.log" >> "$OUT/mesa.txt" 2>/dev/null
  grep -m1 'EGL: Config depth' "$OUT/${Z}.log" >> "$OUT/mesa.txt" 2>/dev/null
  [ "$RC" = 0 ] || FAIL=1
  sleep 3
done
{ date -u -Iseconds; echo "thermal after"; } > "$OUT/thermal_after.txt" 2>&1
echo "GATE_RUN_DONE FAIL=$FAIL" >> "$OUT/runner.log"
exit $FAIL
