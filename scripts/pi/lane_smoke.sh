#!/usr/bin/env bash
# B1030 (#350) -- the first-light smoke for an aarch64 SBC lane, run from dev-u24.
#   PI_LANE=2 scripts/pi/lane_smoke.sh <sha> [out-dir]
# Takes the lane's lock, then ON THE DEVICE (everything it needs is on the NFS share):
#   1. the GL floor: scripts/pi/kmscube_floor.sh with the system kmscube (3 runs) --
#      RssAnon/RssFile of a trivial GL app under this lane's Mesa, the number every
#      willeq figure on the lane is read against (CLAUDE.md "GL floor MEASURED");
#   2. the offline zone benchmark from ci-arm64/<sha>/bin on the DRM output
#      (--drm, qeynos2, 300 frames, CSV) -- the production S3D/WLD + GLES2 path with
#      no server, so a crash here is the port, not the lane.
# Evidence lands in <out-dir> (default /mnt/projects/fable/review/B1030/lane<N>/):
# floor/run*.json + floor.txt, bench.csv, bench.log, device.txt. Exit 0 = both ran.
# This is a SMOKE: it proves the binary starts and renders; it blesses nothing.
set -uo pipefail
SHA="${1:?usage: PI_LANE=N lane_smoke.sh <sha> [out-dir]}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/env.sh"
OUT="${2:-/mnt/projects/fable/review/B1030/lane${PI_LANE}}"
D="/mnt/projects/fable/ci-arm64/$SHA"
OWNER="B1030-smoke-$$"
ZONE="${SMOKE_ZONE:-qeynos2}"; FRAMES="${SMOKE_FRAMES:-300}"
[ "$PI_LANE_ARCH" = aarch64 ] || { echo "lane $PI_LANE ($PI_LANE_NAME) is $PI_LANE_ARCH; this smoke is for the aarch64 lanes"; exit 2; }
mkdir -p "$OUT/floor"
pi_lock_take "$OWNER" || { echo "lane $PI_LANE lock held: $(pi_lock_show)"; exit 64; }
trap 'pi_lock_release "$OWNER"' EXIT
pi_remote "test -x $D/bin/willeq && test -x $D/bin/gles2_zone_benchmark" || { echo "no $D/bin on lane $PI_LANE"; exit 65; }

pi_remote "set -o pipefail; { uname -a; cat /proc/device-tree/model; echo; free -m | head -2; \
  for c in /sys/class/drm/card*; do echo \"\$(basename \$c): \$(basename \$(readlink \$c/device/driver 2>/dev/null) 2>/dev/null)\"; done; \
  cat /sys/class/drm/card*-HDMI-A-1/status; dpkg-query -W libgl1-mesa-dri; cat $D/BUILD_INFO; }" > "$OUT/device.txt" 2>&1
echo "--- lane $PI_LANE ($PI_LANE_NAME): GL floor ---"
pi_remote "bash $HERE/kmscube_floor.sh /usr/bin/kmscube $OUT/floor 3 12" 2>&1 | tee "$OUT/floor.txt"
echo "--- lane $PI_LANE ($PI_LANE_NAME): offline zone benchmark ($ZONE, $FRAMES frames, --drm) ---"
pi_remote "mkdir -p /tmp/b1030 && cd $D && WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1030/cache \
  timeout 600 $D/bin/gles2_zone_benchmark --zone $ZONE --eq-path $PI_EQ_PATH --eqe1-path $PI_EQE1_PATH \
  --drm --frames $FRAMES --csv /tmp/b1030/bench.csv > /tmp/b1030/bench.log 2>&1; echo BENCH_EXIT=\$? >> /tmp/b1030/bench.log; \
  cp /tmp/b1030/bench.log $OUT/bench.log; cp /tmp/b1030/bench.csv $OUT/bench.csv 2>/dev/null; true"
tail -n 12 "$OUT/bench.log"
grep -q "BENCH_EXIT=0" "$OUT/bench.log" || { echo "lane $PI_LANE: benchmark did not exit 0 (see $OUT/bench.log)"; exit 1; }
echo "lane $PI_LANE ($PI_LANE_NAME): smoke PASS -- evidence in $OUT"
