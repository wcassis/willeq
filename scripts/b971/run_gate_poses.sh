#!/usr/bin/env bash
# B971: the visual gate's golden poses on the Pi (run ON the Pi, under the Pi lock).
# Copied from /mnt/projects/fable/review/B967/pi/run_gate_poses.sh (output paths only);
# B971 runs it with bin/gles2_zone_benchmark-nollvm (the new publish from
# scripts/pi/publish_arm_build.sh) to compare the D16/no-stencil surface against
# the B967 goldens for z-fighting.
# usage: run_gate_poses.sh <benchmark-binary (Mesa-prefix rpath copy)> <zones,comma> <tag> ["extra args"]
#   tag   = before | after (output dir suffix)
#   extra = appended to the bench -- (last wins), e.g. "--player-light-level 0"
# Output goes to Pi-local /tmp while a run is live, then to NFS review/B971/pi/gate/<zone>_<tag>/.
set -u
BENCH="$1"; ZONES="$2"; TAG="$3"; EXTRA="${4:-}"
POSES=/mnt/projects/fable/review/B960/pi/gate_poses.tsv
for zone in ${ZONES//,/ }; do
  args="$(awk -F'\t' -v z="$zone" '$1==z {print $2}' "$POSES")"
  [ -n "$args" ] || { echo "no poses for $zone"; continue; }
  OUT=/tmp/b971_gate_${zone}_$TAG
  rm -rf "$OUT"; mkdir -p "$OUT/cache"
  sync; echo 3 | sudo -n tee /proc/sys/vm/drop_caches >/dev/null
  t0=$(date +%s)
  # shellcheck disable=SC2086
  env WILLEQ_CACHE_DIR="$OUT/cache" \
    timeout 900 "$BENCH" --drm $args $EXTRA --dump-png-prefix "$OUT/$zone" --csv "$OUT/$zone.csv" \
    > "$OUT/bench.log" 2>&1 &
  pid=$!
  # A gate zone runs ~15-17 s on the Pi: snapshot the maps early (the first
  # B965 'before' pass slept 15 s and missed three of five processes).
  sleep 6
  cpid="$(pgrep -P "$pid" | head -1)"
  grep -o '/[^ ]*libgallium[^ ]*\|/[^ ]*libLLVM[^ ]*' "/proc/${cpid:-$pid}/maps" 2>/dev/null | sort -u > "$OUT/mesa_maps.log"
  wait "$pid"
  rc=$?
  echo "rc=$rc wall_s=$(( $(date +%s) - t0 ))" >> "$OUT/bench.log"
  DEST="/mnt/projects/fable/review/B971/pi/gate/${zone}_$TAG"
  rm -rf "$DEST"; mkdir -p "$DEST"
  cp "$OUT"/*.png "$OUT"/*.log "$OUT"/*.csv "$DEST/" 2>/dev/null
  rm -rf "$OUT"
  echo "gate $zone $TAG rc=$rc maps=$(tr '\n' ' ' < "$DEST/mesa_maps.log")"
done
