#!/usr/bin/env bash
# B1030 (#350) -- the first-light smoke for an SBC lane, run from dev-u24 (aarch64 lanes;
# B1060 added the armhf lane 5, which runs the ci-arm/<sha>/ publish on a vkms card).
#   PI_LANE=2 scripts/pi/lane_smoke.sh <sha> [out-dir]
# Takes the lane's lock, then ON THE DEVICE (everything it needs is on the NFS share):
#   1. the GL floor: scripts/pi/kmscube_floor.sh with the system kmscube (3 runs) --
#      RssAnon/RssFile of a trivial GL app under this lane's Mesa, the number every
#      willeq figure on the lane is read against (CLAUDE.md "GL floor MEASURED");
#   2. the offline zone benchmark from <PI_LANE_PUBLISH>/<sha>/bin on the DRM output
#      (ci-arm64/ for lanes 2-3; lane 4 runs its own bullseye build, env.sh)
#      (--drm, qeynos2, 300 frames, CSV) -- the production S3D/WLD + GLES2 path with
#      no server, so a crash here is the port, not the lane.
# With no monitor on the lane's HDMI (the aarch64 boards share one KVM) the connector is
# forced "on" for the run via its sysfs status file and set back to "detect" after.
# Evidence lands in <out-dir> (default /mnt/projects/fable/review/B1030/lane<N>/):
# floor/run*.json + floor.txt, bench.csv, bench.log, device.txt. Exit 0 = both ran.
# This is a SMOKE: it proves the binary starts and renders; it blesses nothing.
# B1034 (#357): the floor script runs ON THE DEVICE, so its path must be one the
# device can read. The default is $HERE (this checkout's scripts/pi/), which is
# right when the runner and the lane share the filesystem (the ThinkPad, whose
# runner builds and runs this on the NAS worktree the Pi also mounts). When the
# script is driven from a host the lane cannot see (hermes-ext: the only host
# with a key to the lanes, no NAS mount), set FLOOR_PATH to the device-visible
# copy -- e.g. the NAS worktree /mnt/projects/fable/wt/<slug>/scripts/pi/kmscube_floor.sh.
set -uo pipefail
SHA="${1:?usage: PI_LANE=N lane_smoke.sh <sha> [out-dir]}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/env.sh"
FLOOR_PATH="${FLOOR_PATH:-$HERE/kmscube_floor.sh}"
OUT="${2:-/mnt/projects/fable/review/B1030/lane${PI_LANE}}"
D="${PI_LANE_PUBLISH:-/mnt/projects/fable/ci-arm64}/$SHA"   # B1056: per-lane publish root (lane 4 = ci-arm64-bullseye, lane 5 = ci-arm, B1060)
OWNER="B1030-smoke-$$"
ZONE="${SMOKE_ZONE:-qeynos2}"; FRAMES="${SMOKE_FRAMES:-300}"
case "$PI_LANE_ARCH" in aarch64|armhf) ;; *) echo "lane $PI_LANE ($PI_LANE_NAME) is $PI_LANE_ARCH; no publish for it"; exit 2 ;; esac
mkdir -p "$OUT/floor"
pi_lock_take "$OWNER" || { echo "lane $PI_LANE lock held: $(pi_lock_show)"; exit 64; }
trap 'pi_lock_release "$OWNER"' EXIT
pi_remote "test -x $D/bin/willeq && test -x $D/bin/gles2_zone_benchmark" || { echo "no $D/bin on lane $PI_LANE"; exit 65; }

# Headless run: no monitor on the lane's HDMI -> force the connector on for the run
# (pi_hdmi_force_on in env.sh; a no-op when it is connected), restore at exit.
trap 'pi_lock_release "$OWNER"; pi_hdmi_restore' EXIT
pi_hdmi_force_on || { echo "lane $PI_LANE: no KMS output for the run"; exit 66; }
# B1060: the thermal / CPU / GPU-clock state the run starts and ends in (the XU4's GPU sat at
# 24 MHz unnoticed, B1045) -- a `before` block here, an `after` block once the benchmark exits.
THERM='echo "--- $(date -u -Iseconds)"; for t in /sys/class/thermal/thermal_zone*; do echo "$(cat $t/type) $(cat $t/temp)"; done; \
  for c in /sys/devices/system/cpu/cpu0/cpufreq; do echo "cpu0 $(cat $c/scaling_governor) $(cat $c/scaling_cur_freq)"; done; \
  for d in /sys/class/devfreq/*; do echo "devfreq $(basename $d) $(cat $d/governor) cur=$(cat $d/cur_freq)"; cat $d/trans_stat; done 2>/dev/null'
pi_remote "$THERM" > "$OUT/thermal_before.txt" 2>&1

pi_remote "set -o pipefail; { uname -a; cat /proc/device-tree/model; echo; free -m | head -2; \
  for c in /sys/class/drm/card*; do echo \"\$(basename \$c): \$(basename \$(readlink \$c/device/driver 2>/dev/null) 2>/dev/null)\"; done; \
  for st in /sys/class/drm/card*-*/status; do echo \"\$st \$(cat \$st)\"; done; cat $PI_HDMI_CONN/status 2>/dev/null; dpkg-query -W libgl1-mesa-dri; ls /usr/local/lib/dri 2>/dev/null; cat $D/BUILD_INFO; }" > "$OUT/device.txt" 2>&1
echo "--- lane $PI_LANE ($PI_LANE_NAME): GL floor ---"
pi_remote "PI_KMSCUBE_ARGS='$PI_KMSCUBE_ARGS' bash $FLOOR_PATH /usr/bin/kmscube $OUT/floor 3 12" 2>&1 | tee "$OUT/floor.txt"
echo "--- lane $PI_LANE ($PI_LANE_NAME): offline zone benchmark ($ZONE, $FRAMES frames, --drm) ---"
pi_remote "mkdir -p /tmp/b1030 && cd $D && env WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1030/cache \
  timeout 600 $D/bin/gles2_zone_benchmark --zone $ZONE --eq-path $PI_EQ_PATH --eqe1-path $PI_EQE1_PATH \
  --drm --frames $FRAMES --csv /tmp/b1030/bench.csv > /tmp/b1030/bench.log 2>&1; echo BENCH_EXIT=\$? >> /tmp/b1030/bench.log; \
  cp /tmp/b1030/bench.log $OUT/bench.log; cp /tmp/b1030/bench.csv $OUT/bench.csv 2>/dev/null; true"
pi_remote "$THERM" > "$OUT/thermal_after.txt" 2>&1
tail -n 12 "$OUT/bench.log"
grep -q "BENCH_EXIT=0" "$OUT/bench.log" || { echo "lane $PI_LANE: benchmark did not exit 0 (see $OUT/bench.log)"; exit 1; }
echo "lane $PI_LANE ($PI_LANE_NAME): smoke PASS -- evidence in $OUT"
