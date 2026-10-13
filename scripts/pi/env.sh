#!/usr/bin/env bash
# H04 — shared environment for Orange Pi tier-2 timing scripts.
# Source this from bench_pi.sh / compare_pi.sh (or their Python engines).
# Every value can be overridden by exporting it before sourcing.

: "${PI_SSH_OPTS:=-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new}"

# B1030 (#350) -- the SBC LANES. Four single-tenant devices (lane 4 = B1056), each with its
# own lock ON the device (PI_LOCK below is a path on the remote host, so one
# lock per device falls out of the lane choice). `PI_LANE=N` picks the device,
# exactly like scripts/eqemu/env.sh's EQEMU_LANE: a card or a script names its
# LANE, never an address. A PI_HOST alone still works (the lane id then
# follows the host), so every pre-B1030 caller is unchanged; an unlisted
# PI_HOST keeps whatever lane the caller said (default 1). But an explicit
# PI_LANE together with a PI_HOST that belongs to a DIFFERENT lane is an ERROR
# (exit 2 from a script, return 2 in an interactive shell), never a silent
# override: PI_HOST is exported, so a shell
# that sourced this once for lane 1 and then ran `PI_LANE=3 bash <script>`
# used to get PI_LANE rewritten to 1 and reach the Orange Pi (B1030 review,
# scripts/pi/env_selftest.sh). Unset PI_HOST (or set it to the lane's host)
# to switch lanes in such a shell.
#
#   lane 1  orangepi  10.0.30.192     armv7l  Allwinner H3  Mali-400  lima      483 MB
#   lane 2  rock64    172.16.200.204  aarch64 RK3328        Mali-450  lima      4 GB, eMMC
#   lane 3  rock4d    172.16.200.202  aarch64 RK3576        Mali-G52  panfrost  12 GB, SD
#   lane 4  a7s       172.16.200.206  aarch64 Allwinner A733 PowerVR BXM-4-64 pvr (IMG DDK 24.2)  4 GB, eMMC
#
# Lane 4 (B1056, #376) is Debian 11 bullseye on a vendor 5.15 kernel, user `radxa`,
# and its GPU is Imagination's, not Mali: the IMG DDK blob (`/usr/local/lib/dri/pvr_dri.so`
# behind IMG's own Mesa-derived libEGL/libgbm in /usr/local/lib) does the GLES2 work, so
# nothing Lima- or Panfrost-specific transfers. Its glibc is 2.31, so the noble-sysroot
# ci-arm64/ publish does NOT run there (GLIBC_2.38): the lane's binary is built ON the
# device by scripts/sbc/a7s/build_native.sh and published under ci-arm64-bullseye/<sha>/
# in the same shape as ci-arm64/ (PI_LANE_PUBLISH below names the root per lane).
#
# Lane 1 keeps everything it had (the armhf CI publish under ci-arm/, the
# LLVM-less Mesa prefix, the `orangepi` constrained preset). Lanes 2-3 run the
# aarch64 build (scripts/build-arm64-noble.sh, published under ci-arm64/ by
# scripts/pi/publish_arm64_build.sh) with the system Mesa; neither has a
# constrained preset of its own yet -- that is a ruling request with
# measurements attached, not something this table may invent (CLAUDE.md,
# "Goldens ... are bless-only").
_pi_lane_asked="${PI_LANE:-}"
case "${PI_LANE:-}" in
    ""|1) PI_LANE_HOST=10.0.30.192;    PI_LANE_NAME=orangepi; PI_LANE_ARCH=armhf;   PI_LANE_GPU=lima ;;
    2)    PI_LANE_HOST=172.16.200.204; PI_LANE_NAME=rock64;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=lima ;;
    3)    PI_LANE_HOST=172.16.200.202; PI_LANE_NAME=rock4d;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=panfrost ;;
    4)    PI_LANE_HOST=172.16.200.206; PI_LANE_NAME=a7s;      PI_LANE_ARCH=aarch64; PI_LANE_GPU=pvr ;;
    *)    echo "scripts/pi/env.sh: unknown PI_LANE '${PI_LANE}' (1-4)" >&2
          return 2 2>/dev/null || exit 2 ;;
esac
: "${PI_HOST:=${PI_LANE_HOST}}"
case "${PI_HOST}" in
    10.0.30.192)    _pi_host_lane=1 ;;
    172.16.200.204) _pi_host_lane=2 ;;
    172.16.200.202) _pi_host_lane=3 ;;
    172.16.200.206) _pi_host_lane=4 ;;
    *)              _pi_host_lane= ;;   # an unlisted host keeps whatever the caller said
esac
if [ -n "${_pi_host_lane}" ] && [ -n "${_pi_lane_asked}" ] && [ "${_pi_lane_asked}" != "${_pi_host_lane}" ]; then
    echo "scripts/pi/env.sh: PI_LANE=${_pi_lane_asked} but PI_HOST=${PI_HOST} is lane ${_pi_host_lane}; refusing (unset PI_HOST to switch lanes)" >&2
    unset _pi_lane_asked _pi_host_lane
    # A SCRIPT that sources this exits here: most Pi rigs run without set -e, and
    # a `return 2` they ignore would carry on against the wrong device. Only an
    # interactive shell gets a return (so the operator's terminal survives).
    case $- in *i*) return 2 ;; esac
    exit 2
fi
case "${_pi_host_lane}" in
    1) PI_LANE=1; PI_LANE_NAME=orangepi; PI_LANE_ARCH=armhf;   PI_LANE_GPU=lima ;;
    2) PI_LANE=2; PI_LANE_NAME=rock64;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=lima ;;
    3) PI_LANE=3; PI_LANE_NAME=rock4d;   PI_LANE_ARCH=aarch64; PI_LANE_GPU=panfrost ;;
    4) PI_LANE=4; PI_LANE_NAME=a7s;      PI_LANE_ARCH=aarch64; PI_LANE_GPU=pvr ;;
    *) PI_LANE="${PI_LANE:-1}" ;;
esac
unset _pi_lane_asked _pi_host_lane

# Per-lane facts the rigs read instead of guessing (B1056): the ssh user (lane 4's
# image has no `user`), the publish root the lane's binary comes from, the DRM
# connector a headless run forces on, and the environment a run needs on the
# device (lane 4: the IMG libraries are in /usr/local/lib and the DRI driver in
# /usr/local/lib/dri; /etc/environment on that image uses `export`, which pam_env
# ignores, so an ssh session does not get them by itself).
case "${PI_LANE}" in
    4) : "${PI_USER:=radxa}"
       : "${PI_LANE_PUBLISH:=/mnt/projects/fable/ci-arm64-bullseye}"
       : "${PI_HDMI_CONN:=/sys/class/drm/card0-DP-1}"
       : "${PI_RUN_ENV:=LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri}"
       # kmscube's own device scan lands on card1 (pvrsrvkm, render-only: "no connected
       # connector!"); the display is sunxi-drm card0 and GBM there reaches the IMG driver.
       : "${PI_KMSCUBE_ARGS:=-D /dev/dri/card0}" ;;
    2|3) : "${PI_USER:=user}"
       : "${PI_LANE_PUBLISH:=/mnt/projects/fable/ci-arm64}"
       : "${PI_RUN_ENV:=}"; : "${PI_KMSCUBE_ARGS:=}" ;;
    *) : "${PI_USER:=user}"
       : "${PI_LANE_PUBLISH:=/mnt/projects/fable/ci-arm}"
       : "${PI_RUN_ENV:=}"; : "${PI_KMSCUBE_ARGS:=}" ;;
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
# lanes 2-3 have a 2 G / 5.9 G /tmp plus a /mnt/ram scratch tmpfs, lane 4 a 1 G /tmp
# plus a 1.5 G /mnt/ram. All are
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

# Headless DRM (B1030): the aarch64 boards share ONE KVM, so the lane's HDMI
# connector is usually "disconnected" and the DRM device (rightly) refuses to
# start. The connector's sysfs status file is writable by root: forcing it "on"
# makes the kernel offer its default modes (1024x768, 800x600, 640x480 ...) with
# no monitor; "detect" restores hotplug behaviour. pi_hdmi_force_on does it only
# when the connector is NOT connected and records that in PI_HDMI_FORCED, so a
# rig's cleanup calls pi_hdmi_restore unconditionally.
PI_HDMI_CONN="${PI_HDMI_CONN:-/sys/class/drm/card0-HDMI-A-1}"
PI_HDMI_FORCED=0
pi_hdmi_force_on() {
    local st
    st="$(pi_remote cat "$PI_HDMI_CONN/status" </dev/null 2>/dev/null)"
    [ "$st" = connected ] && return 0
    pi_remote "echo on | sudo -n tee $PI_HDMI_CONN/status >/dev/null; sleep 1; cat $PI_HDMI_CONN/status; head -3 $PI_HDMI_CONN/modes | tr '\n' ' '; echo" </dev/null \
        | sed 's/^/connector forced: /'
    PI_HDMI_FORCED=1
}
pi_hdmi_restore() {
    [ "${PI_HDMI_FORCED:-0}" -eq 1 ] || return 0
    pi_remote "echo detect | sudo -n tee $PI_HDMI_CONN/status >/dev/null" </dev/null
    PI_HDMI_FORCED=0
}

export PI_LANE PI_LANE_NAME PI_LANE_ARCH PI_LANE_GPU PI_LANE_PUBLISH PI_RUN_ENV PI_KMSCUBE_ARGS PI_HOST PI_USER PI_SSH_OPTS ARM_BUILD_BIN_DIR PI_EQ_PATH PI_EQE1_PATH \
       PI_HDMI_CONN PI_REMOTE_TMP PI_REMOTE_BIN PI_LOCK PROJECT_DIR MANIFEST_PATH ARTIFACTS_DIR \
       BASELINE_DIR H04_TOTAL_FRAMES H04_COOLDOWN_SEC H04_WARMUP_REPS
