#!/usr/bin/env bash
# B952 (GitLab #270): one impostor measurement run of gles2_zone_benchmark.
#
# usage: impostor_run.sh <benchmark> <out-dir> <label> <zone> <N> <size> <unique|shared> <far|mixed> [extra benchmark args...]
#
# The scene: --entities N cycling the fourteen playable races (1..12, 128 Iksar,
# 130 Vah Shir) x both genders, animated, re-placed by --impostor-scene in front
# of the fixed pose in benchmarks/paths/<zone>_impostor_fixedpose_300f.path,
# drawn by --adaptive-lod with the distance tier at --lod-dist 80. The
# manifest's productionConfig flags (the orangepi preset mirror) are applied.
# Environment: B952_FRAMES (default 150), B952_WARMUP (default 10),
# B952_RES ("800 600"), B952_DRM=1 on the Pi, PI_EQ_PATH / PI_EQE1_PATH.
#
# Writes <out-dir>/<label>.log, .json (--memory-json), .csv, and .maps (the
# mapped GL libraries, taken while the run is up: the proof of which Mesa ran).
set -u
BIN="$1"; OUT="$2"; LABEL="$3"; ZONE="$4"; N="$5"; SIZE="$6"; MODE="$7"; SCENE="$8"; shift 8
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EQ="${PI_EQ_PATH:-/mnt/projects/EverQuestP1999}"
EQE1="${PI_EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
FRAMES="${B952_FRAMES:-150}"
WARMUP="${B952_WARMUP:-10}"
RES="${B952_RES:-800 600}"
DRM=()
[ "${B952_DRM:-0}" = 1 ] && DRM=(--drm)
mkdir -p "$OUT"
# shellcheck disable=SC2086
"$BIN" --zone "$ZONE" --eq-path "$EQ" --eqe1-path "$EQE1" "${DRM[@]}" --res $RES \
    --frames "$FRAMES" --warmup "$WARMUP" \
    --path-file "$REPO/benchmarks/paths/${ZONE}_impostor_fixedpose_300f.path" \
    --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9 \
    --entities "$N" --test-races 1,2,3,4,5,6,7,8,9,10,11,12,128,130 --entity-genders both \
    --animate --adaptive-lod --lod-dist 80 --adaptive-tier-dist 80 --impostor-scene "$SCENE" \
    --impostor-size "$SIZE" --impostor-mode "$MODE" \
    --memory-json "$OUT/$LABEL.json" --csv "$OUT/$LABEL.csv" "$@" > "$OUT/$LABEL.log" 2>&1 &
pid=$!
for _ in $(seq 1 600); do
    grep -q -E 'libgallium|swrast|llvmpipe' "/proc/$pid/maps" 2>/dev/null && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
grep -E '\.so' "/proc/$pid/maps" 2>/dev/null | awk '{print $6}' | sort -u > "$OUT/$LABEL.maps"
wait "$pid"
rc=$?
echo "$LABEL exit=$rc gallium=$(grep -c libgallium "$OUT/$LABEL.maps") llvm=$(grep -c libLLVM "$OUT/$LABEL.maps")"
exit $rc
