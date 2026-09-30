#!/usr/bin/env bash
# B932: deterministic frames for the stock-vs-LLVM-less Mesa picture check.
# Runs ON THE PI (take pi_lock_take first).
#
# The live client cannot give a same-picture comparison between two runs:
# the server's game clock moves the sky/sun between legs (measured: iceclad
# night in one leg, day in the next). So the frames are taken with the
# gate's own offline renderer, gles2_zone_benchmark, at the manifest's
# static --camera-pose list (tests/goldens/manifest.json, the visual gate's
# poses and productionConfig), --drm on Lima, with the EQE1 pack as the
# client ships. Same benchmark binary on both sides; the no-LLVM copy only
# carries DT_RPATH=<prefix>/lib (scripts/pi/mesa_nollvm_select.sh).
# /proc/<pid>/maps of each run is saved (maps_<zone>.txt) as the proof of
# which Mesa rendered it.
#
# usage: mesa_frame_ab.sh <benchmark-binary> <out-dir> [zones...]
set -u
BIN="$1"; OUT="$2"; shift 2
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MANIFEST="$REPO/tests/goldens/manifest.json"
EQ="${PI_EQ_PATH:-/mnt/projects/EverQuestP1999}"
EQE1="${PI_EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
ZONES="${*:-qeynos2 gfaydark iceclad timorous}"
mkdir -p "$OUT"
for z in $ZONES; do
    args="$(python3 -c "
import json,sys
m=json.load(open('$MANIFEST'))
z=[x for x in m['zones'] if x['zone']=='$z'][0]
print(' '.join(m['productionConfig']['args'] + z['extraArgs']))")"
    # shellcheck disable=SC2086
    "$BIN" --zone "$z" --eq-path "$EQ" --eqe1-path "$EQE1" --drm --res 800 600 \
        --frames 3 --warmup 5 $args --dump-frames 0,1,2 --dump-png-prefix "$OUT/$z" \
        --csv "$OUT/$z.csv" > "$OUT/$z.log" 2>&1 &
    pid=$!
    # the maps proof: taken once the GL stack is up (the first pose is drawn
    # after load; poll for the gallium mapping rather than guess a delay)
    for _ in $(seq 1 120); do
        grep -q libgallium "/proc/$pid/maps" 2>/dev/null && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    grep -E '\.so' "/proc/$pid/maps" 2>/dev/null | awk '{print $6}' | sort -u > "$OUT/maps_$z.txt"
    wait "$pid"
    echo "$z exit $? pngs: $(ls "$OUT/$z"_f*.png 2>/dev/null | wc -l) gallium: $(grep -c libgallium "$OUT/maps_$z.txt") llvm: $(grep -c libLLVM "$OUT/maps_$z.txt")"
done
