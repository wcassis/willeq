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
#   lane 5  orangepizero 172.16.200.207 armv7l Allwinner H2+ Mali-400 lima     484 MB, SD, trixie
#
# Lane 5 (B1060, #380) is lane 1's sibling: the same armhf ci-arm/ publish, the same
# preset and ceilings, but Armbian trixie (Debian 13, glibc 2.41, system Mesa 25.0.7)
# and NO display output wired in its device tree (the Zero has no HDMI; display-engine
# and tv-encoder are `disabled`), so card0 is Lima, render-only. A DRM run needs a KMS
# device: pi_hdmi_force_on loads the stock vkms module there (pi_vkms_on below; Mesa's kmsro
# vkms_dri.so pairs its Virtual-1 connector with Lima); PI_DRM_CARD names that card
# (docs/reference/orangepizero.md).
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
    5)    PI_LANE_HOST=172.16.200.207; PI_LANE_NAME=orangepizero; PI_LANE_ARCH=armhf; PI_LANE_GPU=lima ;;
    *)    echo "scripts/pi/env.sh: unknown PI_LANE '${PI_LANE}' (1-5)" >&2
          return 2 2>/dev/null || exit 2 ;;
esac
: "${PI_HOST:=${PI_LANE_HOST}}"
case "${PI_HOST}" in
    10.0.30.192)    _pi_host_lane=1 ;;
    172.16.200.204) _pi_host_lane=2 ;;
    172.16.200.202) _pi_host_lane=3 ;;
    172.16.200.206) _pi_host_lane=4 ;;
    172.16.200.207) _pi_host_lane=5 ;;
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
    5) PI_LANE=5; PI_LANE_NAME=orangepizero; PI_LANE_ARCH=armhf; PI_LANE_GPU=lima ;;
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
       # WILLEQ_GPU_CENSUS_NO_FDINFO: reading a DRM fd's fdinfo panics this kernel (pvr_show_fdinfo);
       # the device-level fact rides in the lane env, not only in the in-process sysfs lookup.
       : "${PI_RUN_ENV:=LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri WILLEQ_GPU_CENSUS_NO_FDINFO=1}"
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
# B1034 (#357, follow-up from the review of !143): ARM_BUILD_BIN_DIR is
# EXPORTED, so a shell that sourced this file for one lane (or ran one rig
# for it) and then switched lanes kept the FIRST lane's bin dir for the
# second -- the same stale-export class as the PI_HOST refusal that landed
# in 72e8463d. It is therefore ALWAYS derived from the resolved lane (not
# caller-settable), like PI_USER and PI_LANE_PUBLISH.
if [ "${PI_LANE}" = 4 ]; then
    # Lane 4 has no cross build (the noble binary needs GLIBC_2.38, the board has
    # 2.31): its binaries come from the on-device build's publish (B1056).
    ARM_BUILD_BIN_DIR="${PI_LANE_PUBLISH}/by-branch/fable/bin"
elif [ "${PI_LANE_ARCH}" = aarch64 ]; then
    ARM_BUILD_BIN_DIR=/mnt/projects/fable/main/build-arm64-noble/bin
else
    ARM_BUILD_BIN_DIR=/mnt/projects/fable/main/build-arm-noble/bin
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
    # The lane's run environment (PI_RUN_ENV, lane 4: the IMG loader paths + the
    # fdinfo kill switch) is applied HERE, once, for every rig and hand run; the
    # rigs never prefix it themselves. PI_RUN_ENV / PI_KMSCUBE_ARGS are exported
    # too, for the sudo boundary inside kmscube_floor.sh. "$*" is what ssh would
    # have joined anyway.
    local pre="export PI_RUN_ENV='${PI_RUN_ENV:-}' PI_KMSCUBE_ARGS='${PI_KMSCUBE_ARGS:-}'"
    local kv
    for kv in ${PI_RUN_ENV:-}; do pre="$pre $kv"; done
    # shellcheck disable=SC2086
    ssh ${PI_SSH_OPTS} "${PI_USER}@${PI_HOST}" "$pre; $*"
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
# B1034 (#357): the review of !143 found two holes in that --
#   1. the `sudo -n tee` result was UNCHECKED: if the write failed (no
#      passwordless sudo on a new image, the connector gone), PI_HDMI_FORCED
#      was still set and the rig ran against a connector that was still
#      "disconnected";
#   2. after a CRASHED run (kill -9 of the rig, the ssh link dying mid-run, a
#      reboot) the connector stayed forced "on", which reads "connected":
#      every later pi_hdmi_force_on no-oped ("already connected") and nothing
#      ever wrote the pre-force status back, so the lane's hotplug behaviour
#      was gone until a human found the sysfs file.
# Both are fixed the way the pi_lock is: the state lives ON THE DEVICE.
# pi_hdmi_force_on, before forcing, atomically reads the current status and
# writes it to $PI_HDMI_RECORD ("<owner> <status>", root-owned, so the
# record survives the crash by construction), and checks the write: a
# failed write fails the run (the rig exits 66) WITHOUT setting the flag.
# At the START of every run pi_hdmi_force_on checks the record: a stale one
# means a crashed run left the connector forced, and the recorded status is
# restored BEFORE this run proceeds (this is what "the lane_smoke/sweep start
# restores from that record" means -- they begin with pi_hdmi_force_on).
# pi_hdmi_restore restores from the record when one exists even though THIS
# shell never forced (a fresh shell's cleanup is the crash-recovery path);
# a record owned by a DIFFERENT live rig is refused, like a foreign lock.
PI_HDMI_CONN="${PI_HDMI_CONN:-/sys/class/drm/card0-HDMI-A-1}"
PI_HDMI_RECORD="${PI_HDMI_RECORD:-/var/tmp/pi_hdmi_record}"
# The identity a record carries. The hostname is stable across re-runs from
# the same host (crash recovery works from any new shell here) and distinct
# per host (a foreign host's record is refused, like a foreign pi_lock).
: "${PI_HDMI_OWNER:=$(hostname)}"
PI_HDMI_FORCED=0
pi_hdmi_write_status() {
    # $1 = the value to write. Echoes the connector's status AFTER the write
    # and RETURNS 0 when the kernel accepted the store, non-zero when it
    # rejected the value or the write could not run (no passwordless sudo).
    # The caller decides took-or-not from the RETURN CODE, never the echo: the
    # kernel NORMALIZES an accepted value and REJECTS a read-only one --
    # B1034, measured on lane 2's RK3328 (2026-10-18): `on` is accepted and
    # reads back "connected" (NOT "on"); `detect` is accepted and reads back
    # "connected"/"disconnected" by monitor presence; `connected` and
    # `disconnected` are REJECTED stores (the write returns an error, rc 1);
    # `off` is accepted. So the pre-B1034 check `!= on` / `!= detect` on the
    # read-back fails on a real connector even when the force/restore
    # succeeded, and a bare after-read cannot tell a rejected or no-op store
    # from a normalized one -- only the write's exit code can.
    # (No set -e up the call chain: the caller checks the return code.) The
    # value travels as `printf %s 'VALUE' | sudo -n tee <file>`: a
    # machine-recognizable command shape (the selftest's fake device matches
    # it) and no shell-metacharacter class the values can carry.
    pi_remote "rc=0; printf %s '$1' | sudo -n tee $PI_HDMI_CONN/status >/dev/null 2>/dev/null || rc=\$?; sleep 1; echo \$(cat $PI_HDMI_CONN/status 2>/dev/null); exit \$rc" </dev/null
}
pi_hdmi_record_get() {
    # "<owner> <status>" or empty when there is no record.
    pi_remote "cat $PI_HDMI_RECORD 2>/dev/null" </dev/null
}
pi_hdmi_record_write() {
    # $1 = owner, $2 = the PRE-FORCE status; non-zero when the write failed.
    pi_remote "printf %s '$1 $2' | sudo -n tee $PI_HDMI_RECORD >/dev/null 2>/dev/null" </dev/null
}
pi_hdmi_record_clear() {
    # root-owned (pi_hdmi_record_write stores it through sudo tee): a plain
    # rm cannot remove it and /var/tmp's sticky bit makes that an
    # "Operation not permitted" that was silently ignored -- the record then
    # survived every restore, re-triggering the crash-recovery path on the
    # next run. Sudo the removal, and CHECK it: a record that cannot be
    # cleared is one that will be misread later.
    pi_remote "sudo -n rm -f $PI_HDMI_RECORD" </dev/null
}
pi_hdmi_restore() {
    [ "${PI_DRM_VIRTUAL:-0}" = 1 ] && { pi_vkms_off; return; }
    local rec st
    rec="$(pi_hdmi_record_get)"
    if [ -n "$rec" ]; then
        # The record is authoritative across shells: restore from it even
        # when this shell never forced (the crash-recovery path).
        local owner="${rec% *}"
        if [ "$owner" != "${PI_HDMI_OWNER:-}" ]; then
            echo "lane: restore: record '$rec' is not this shell's (owner $owner); refusing" >&2
            return 1
        fi
        st="${rec#* }"
        if [ "$st" = on ]; then
            # It was "on" before the crashed run forced it: the kernel
            # connector was already hot (a monitor is actually attached), so
            # there is nothing to restore -- clear the bookkeeping and leave
            # the connector as it is.
            if ! pi_hdmi_record_clear; then
                echo "lane: restore: could not clear $PI_HDMI_RECORD (no passwordless sudo?)" >&2
                return 1
            fi
        else
            # It was disconnected (or absent): the only valid write back is
            # "detect" -- hotplug behaviour. (sysfs will not take the word
            # "disconnected"; that is a read-only state the kernel rejects.)
            # Check the write's EXIT CODE, not the read-back: "detect" is
            # accepted and reads back "connected"/"disconnected" by monitor
            # presence, never the literal "detect" (B1034, lane 2).
            if ! pi_hdmi_write_status detect >/dev/null; then
                echo "lane: restore: the 'detect' write was rejected or could not run (record kept in $PI_HDMI_RECORD)" >&2
                return 1
            fi
            if ! pi_hdmi_record_clear; then
                echo "lane: restore: the 'detect' write took but $PI_HDMI_RECORD could not be cleared (no passwordless sudo?)" >&2
                return 1
            fi
            echo "connector restored to 'detect' (record said '$st') from $PI_HDMI_RECORD (a crashed run left it forced)"
        fi
        PI_HDMI_FORCED=0
        return 0
    fi
    [ "${PI_HDMI_FORCED:-0}" -eq 1 ] || return 0
    if ! pi_hdmi_write_status detect >/dev/null; then
        echo "lane: restore: the 'detect' write was rejected or could not run" >&2
        return 1
    fi
    PI_HDMI_FORCED=0
}
pi_hdmi_force_on() {
    [ "${PI_DRM_VIRTUAL:-0}" = 1 ] && { pi_vkms_on; return; }   # B1060: lane 5 has no connector to force
    local st rec
    # Crash recovery FIRST: a stale record means a previous run died with
    # the connector still forced -- restore its pre-force status before
    # deciding anything about THIS run.
    rec="$(pi_hdmi_record_get)"
    if [ -n "$rec" ]; then
        if [ "${rec% *}" = "${PI_HDMI_OWNER:-}" ]; then
            echo "lane: force: stale record '$rec' from this owner's own previous run; restoring before proceeding"
        else
            echo "lane: force: record '$rec' belongs to another owner" >&2
            return 1
        fi
        if ! pi_hdmi_restore; then
            echo "lane: force: the recorded-status restore failed (see $PI_HDMI_RECORD on the device)" >&2
            return 1
        fi
    fi
    st="$(pi_remote cat "$PI_HDMI_CONN/status" </dev/null 2>/dev/null)"
    [ "$st" = connected ] && return 0
    # Record the PRE-FORCE status on the device (the crash-surviving half),
    # then force -- and CHECK the write (the unchecked half, B1034).
    pi_hdmi_record_write "${PI_HDMI_OWNER:-unknown}" "$st" || {
        echo "lane: force: could not write $PI_HDMI_RECORD (no passwordless sudo for ${PI_USER}?)" >&2
        return 1
    }
    local after
    if ! after="$(pi_hdmi_write_status on)"; then
        echo "lane: force: the sysfs write was rejected or the store could not run (no passwordless sudo?); recorded '$st' in $PI_HDMI_RECORD, restore it" >&2
        return 1
    fi
    pi_remote "cat $PI_HDMI_CONN/modes | head -3 | tr '\n' ' '; echo" </dev/null | sed 's/^/connector forced: /'
    PI_HDMI_FORCED=1
}

# B1060 (#380): a lane with NO display output (lane 5, the Orange Pi Zero: no HDMI,
# display-engine/tv-encoder `disabled` in its device tree, card0 = Lima, render-only)
# gets a virtual KMS device for the run: the stock vkms module (Virtual-1 connector,
# 1024x768 preferred, 800x600 and 1280x720 offered), whose scanout Mesa's kmsro
# vkms_dri.so pairs with the Lima render node -- measured: kmscube renders on
# `Mali400` at 60 fps through it (review/B1060/drm/). Nothing is persisted: vkms is
# loaded only when it is absent, unloaded only by the rig that loaded it, and its
# card node (root-only 0600 when it appears: no udev seat tag) is opened to the
# `video` group the client runs in. The client's own card scan (B1054,
# drm_card_select.h) skips the connector-less Lima card0 and takes the vkms card.
# kmscube does not (libdrm cannot enumerate vkms's faux bus), so the floor
# pins it through PI_KMSCUBE_ARGS.
case "${PI_LANE}" in
    5) : "${PI_DRM_VIRTUAL:=1}"; : "${PI_DRM_CARD:=/dev/dri/card1}"
       : "${PI_KMSCUBE_ARGS:=-D ${PI_DRM_CARD}}" ;;
    *) : "${PI_DRM_VIRTUAL:=0}"; : "${PI_DRM_CARD:=}"; : "${PI_KMSCUBE_ARGS:=}" ;;
esac
PI_VKMS_LOADED=0
pi_vkms_on() {
    # Loads vkms if absent (records that in PI_VKMS_LOADED), checks the vkms card is
    # PI_DRM_CARD with a connected Virtual-1, opens it to `video`. Non-zero = no KMS.
    local out rc
    out="$(pi_remote "set -e; if lsmod | grep -q '^vkms '; then echo preloaded; else sudo -n modprobe vkms; echo loaded; fi; \
        sleep 1; c=\$(basename ${PI_DRM_CARD}); test \"\$(cat /sys/class/drm/\$c-Virtual-1/status)\" = connected; \
        sudo -n chgrp video ${PI_DRM_CARD}; sudo -n chmod 0660 ${PI_DRM_CARD}" </dev/null 2>&1)"; rc=$?
    echo "vkms: ${out//$'\n'/ } (${PI_DRM_CARD}, rc=$rc)"
    case "$out" in loaded*) PI_VKMS_LOADED=1 ;; esac
    return $rc
}
pi_vkms_off() {
    [ "${PI_VKMS_LOADED:-0}" -eq 1 ] || return 0
    pi_remote "sudo -n rmmod vkms" </dev/null && PI_VKMS_LOADED=0
}

export PI_LANE PI_LANE_NAME PI_LANE_ARCH PI_LANE_GPU PI_LANE_PUBLISH PI_RUN_ENV PI_DRM_VIRTUAL PI_DRM_CARD PI_KMSCUBE_ARGS PI_HOST PI_USER PI_SSH_OPTS ARM_BUILD_BIN_DIR PI_EQ_PATH PI_EQE1_PATH \
       PI_HDMI_CONN PI_HDMI_RECORD PI_HDMI_OWNER PI_REMOTE_TMP PI_REMOTE_BIN PI_LOCK PROJECT_DIR MANIFEST_PATH ARTIFACTS_DIR \
       BASELINE_DIR H04_TOTAL_FRAMES H04_COOLDOWN_SEC H04_WARMUP_REPS
