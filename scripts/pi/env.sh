#!/usr/bin/env bash
# H04 — shared environment for Orange Pi tier-2 timing scripts.
# Source this from bench_pi.sh / compare_pi.sh (or their Python engines).
# Every value can be overridden by exporting it before sourcing.

: "${PI_USER:=user}"
: "${PI_SSH_OPTS:=-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new}"

# B1030 (#350) -- the SBC LANES. Three single-tenant devices, each with its
# own lock ON the device (PI_LOCK below is a path on the remote host, so one
# lock per device falls out of the lane choice). `PI_LANE=N` picks the device,
# exactly like scripts/eqemu/env.sh's EQEMU_LANE: a card or a script names its
# LANE, never an address. An explicit PI_HOST still wins (and the lane id then
# follows the host that won), so every pre-B1030 caller is unchanged.
#
#   lane 1  orangepi  10.0.30.192     armv7l  Allwinner H3  Mali-400  lima      483 MB
#   lane 2  rock64    172.16.200.204  aarch64 RK3328        Mali-450  lima      4 GB, eMMC
#   lane 3  rock4d    172.16.200.202  aarch64 RK3576        Mali-G52  panfrost  12 GB, SD
#
# Lane 1 keeps everything it had (the armhf CI publish under ci-arm/, the
# LLVM-less Mesa prefix, the `orangepi` constrained preset). Lanes 2-3 run the
# aarch64 build (scripts/build-arm64-noble.sh, published under ci-arm64/ by
# scripts/pi/publish_arm64_build.sh) with the system Mesa; neither has a
# constrained preset of its own yet -- that is a ruling request with
# measurements attached, not something this table may invent (CLAUDE.md,
# "Goldens ... are bless-only").
case "${PI_LANE:-}" in
    ""|1) PI_LANE_HOST=10.0.30.192;    PI_LANE_NAME=orangepi; PI_LANE_ARCH=armhf;   PI_LANE_GPU=lima ;;
    2)    PI_LANE_HOST=172.16.200.204; PI_LANE_NAME=rock64;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=lima ;;
    3)    PI_LANE_HOST=172.16.200.202; PI_LANE_NAME=rock4d;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=panfrost ;;
    *)    echo "scripts/pi/env.sh: unknown PI_LANE '${PI_LANE}' (1-3)" >&2
          return 2 2>/dev/null || exit 2 ;;
esac
: "${PI_HOST:=${PI_LANE_HOST}}"
case "${PI_HOST}" in
    10.0.30.192)    PI_LANE=1; PI_LANE_NAME=orangepi; PI_LANE_ARCH=armhf;   PI_LANE_GPU=lima ;;
    172.16.200.204) PI_LANE=2; PI_LANE_NAME=rock64;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=lima ;;
    172.16.200.202) PI_LANE=3; PI_LANE_NAME=rock4d;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=panfrost ;;
    *) PI_LANE="${PI_LANE:-1}" ;;   # an unlisted host keeps whatever the caller said
esac

# ARM cross-build lands in the MAIN worktree, not this task worktree --
# scripts/build-arm-noble.sh / build-arm64-noble.sh always build there.
# Override if needed. The default follows the lane's architecture.
if [ "${PI_LANE_ARCH}" = aarch64 ]; then
    : "${ARM_BUILD_BIN_DIR:=/mnt/projects/fable/main/build-arm64-noble/bin}"
else
    : "${ARM_BUILD_BIN_DIR:=/mnt/projects/fable/main/build-arm-noble/bin}"
fi

# Same NFS mount visible on both the VM and the Pi.
: "${PI_EQ_PATH:=/mnt/projects/EverQuestP1999}"
: "${PI_EQE1_PATH:=/mnt/projects/EverQuest_ETC1}"

# Pi tmpfs -- I/O rule: ALL timed-run output goes here, never to NFS/SD/eMMC
# while frames are being timed. Lane 1 has 242 MB total, watch the ceiling;
# lanes 2-3 have a 2 G / 5.9 G /tmp plus a /mnt/ram scratch tmpfs. All are
# mounted nosuid: a binary that needs setcap goes in the ext4 home.
: "${PI_REMOTE_TMP:=/tmp}"
: "${PI_REMOTE_BIN:=/tmp/gles2_zone_benchmark_h04}"

# Project-relative paths (resolved by the caller, which cds nowhere --
# these are computed from this file's location).
PI_ENV_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$PI_ENV_SCRIPT_DIR/../.." && pwd)"

: "${MANIFEST_PATH:=$PROJECT_DIR/tests/goldens/manifest.json}"
: "${ARTIFACTS_DIR:=$PROJECT_DIR/benchmarks/pi_runs}"
: "${BASELINE_DIR:=$PROJECT_DIR/benchmarks/pi_baseline}"

# Total frames captured per zone per variant (repeats the manifest's static
# poses in order until this count is reached -- see pi_common.py).
: "${H04_TOTAL_FRAMES:=60}"

# Cooldown between benchmark invocations, in seconds (thermals).
: "${H04_COOLDOWN_SEC:=15}"

# Warmup repeats of the pose cycle discarded before computing p95/mean --
# the first repeat is consistently 2-3x slower than steady state (cold
# shader/texture cache, GPU DVFS ramp-up), confirmed across all zones in
# run1 (see H04 review). Raw CSVs still contain all H04_TOTAL_FRAMES frames.
: "${H04_WARMUP_REPS:=1}"

# T503 -- every SBC lane is single-tenant (one DRM master, and lane 1 has
# 483 MB): every run takes this lock first, from ANY host (dev box, hermes-ext
# workers). Same contract as scripts/eqemu/env.sh's eqemu lock: atomic
# (noclobber) ON the device itself -- so the path is per device by construction.
: "${PI_LOCK:=/home/${PI_USER}/.pi-lock}"

pi_remote() {
    # shellcheck disable=SC2086
    ssh ${PI_SSH_OPTS} "${PI_USER}@${PI_HOST}" "$@"
}
pi_lock_take() {
    # pi_lock_take <owner, e.g. T### or kb-slug> -- exit 0 = you own it;
    # non-zero = someone else does (or the Pi is unreachable): STOP.
    pi_remote bash -c "'set -o noclobber; echo \"\$1 \$(date -Iseconds) from \$2\" > ${PI_LOCK}'" _ "$1" "$(hostname)" 2>/dev/null
}
pi_lock_show() {
    # Prints the lock line, or nothing (exit 1) when the lock is free.
    pi_remote cat "${PI_LOCK}" 2>/dev/null
}
pi_lock_release() {
    # pi_lock_release <owner> -- refuses to drop a lock another owner holds.
    pi_remote bash -c "'[ ! -e ${PI_LOCK} ] || grep -q \"^\$1 \" ${PI_LOCK} && rm -f ${PI_LOCK}'" _ "$1"
}

export PI_LANE PI_LANE_NAME PI_LANE_ARCH PI_LANE_GPU PI_HOST PI_USER PI_SSH_OPTS ARM_BUILD_BIN_DIR PI_EQ_PATH PI_EQE1_PATH \
       PI_REMOTE_TMP PI_REMOTE_BIN PI_LOCK PROJECT_DIR MANIFEST_PATH ARTIFACTS_DIR \
       BASELINE_DIR H04_TOTAL_FRAMES H04_COOLDOWN_SEC H04_WARMUP_REPS
