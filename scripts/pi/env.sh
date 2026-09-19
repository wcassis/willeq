#!/usr/bin/env bash
# H04 — shared environment for Orange Pi tier-2 timing scripts.
# Source this from bench_pi.sh / compare_pi.sh (or their Python engines).
# Every value can be overridden by exporting it before sourcing.

: "${PI_HOST:=10.0.30.192}"
: "${PI_USER:=user}"
: "${PI_SSH_OPTS:=-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new}"

# ARM cross-build lands in the MAIN worktree, not this task worktree --
# scripts/build-arm-noble.sh always builds there. Override if needed.
: "${ARM_BUILD_BIN_DIR:=/mnt/projects/fable/main/build-arm-noble/bin}"

# Same NFS mount visible on both the VM and the Pi.
: "${PI_EQ_PATH:=/mnt/projects/EverQuestP1999}"
: "${PI_EQE1_PATH:=/mnt/projects/EverQuest_ETC1}"

# Pi tmpfs -- I/O rule: ALL timed-run output goes here, never to NFS/SD
# while frames are being timed. 242MB total capacity, watch the ceiling.
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

# T503 -- the Pi is single-tenant (483 MB, one DRM master): every run takes
# this lock first, from ANY host (dev box, hermes-ext workers). Same contract
# as scripts/eqemu/env.sh's eqemu lock: atomic (noclobber) ON the Pi itself.
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

export PI_HOST PI_USER PI_SSH_OPTS ARM_BUILD_BIN_DIR PI_EQ_PATH PI_EQE1_PATH \
       PI_REMOTE_TMP PI_REMOTE_BIN PI_LOCK PROJECT_DIR MANIFEST_PATH ARTIFACTS_DIR \
       BASELINE_DIR H04_TOTAL_FRAMES H04_COOLDOWN_SEC H04_WARMUP_REPS
