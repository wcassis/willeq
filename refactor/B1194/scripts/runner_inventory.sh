#!/usr/bin/env bash
# B1194 (#513): the runner-VM inventory for the README table.
#
# Runs from a CI job (b1194-inv-*) on the host the job landed on (gitlab-runner-1/2
# or thinkpad) -- the only way to execute commands on the runner VMs from the
# board (they answer no inbound port from hermes-ext or the ThinkPad: TCP 22,
# 9252 and 2222 all time out, measured 2026-10-29, evidence in the README).
#
# Read-only, no sudo. Every fact is printed as `$ command` then the command's
# output, so the README can quote the exact line. The same run is teed to
# /mnt/projects/fable/review/B1194/<hostname>/inventory-<UTC>.txt (the NAS
# share, readable from hermes-ext) so the evidence outlives the job log.
set -uo pipefail

HOST=$(hostname)
OUT=/mnt/projects/fable/review/B1194/"$HOST"
mkdir -p "$OUT" 2>/dev/null || OUT="$(mktemp -d)"
TS=$(date -u +%Y%m%dT%H%M%SZ)

{
    echo "# B1194 inventory: $HOST at $TS (job user: $(id -un))"

    run() {
        echo
        echo "\$ $*"
        "$@" 2>&1
        echo "[exit $?]"
    }

    run hostname
    run uptime
    run nproc
    run "free -g"
    run "cat /proc/loadavg"
    run "grep -m1 'model name' /proc/cpuinfo"
    run "df -h / | tail -1"

    echo
    echo "## toolchain (vs the ThinkPad: gcc 13.3.0, no clang, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1)"
    for t in gcc g++ clang cmake ninja ccache gitlab-runner pkg-config python3; do
        if command -v "$t" >/dev/null 2>&1; then
            run "command -v $t"
            run "$t --version | head -1"
        else
            echo "\$ $t --version"; echo "[not installed]"; echo
        fi
    done

    echo
    echo "## GLES2/Irrlicht deps (CI's build-test configure needs these; list from B1008's provision_runner.sh + the x86 Dockerfile)"
    run "dpkg-query -W -f='\${Package} \${Version}\n' build-essential cmake ninja-build libssl-dev zlib1g-dev libfmt-dev libglm-dev libjsoncpp-dev libcereal-dev libboost-graph-dev librecast-dev libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libdrm-dev libx11-dev freerdp3-dev libwinpr3-dev libasound2-dev mesa-utils"
    for p in egl glesv2 gbm drm; do
        if pkg-config --exists "$p" 2>/dev/null; then
            echo "pkg-config $p: $(pkg-config --modversion "$p")"
        else
            echo "pkg-config $p: NOT FOUND"
        fi
    done
    echo "\$ EGL_PLATFORM=surfaceless eglinfo -B | grep -iE 'version|renderer' | head -3"
    EGL_PLATFORM=surfaceless eglinfo -B 2>/dev/null | grep -iE 'version|renderer' | head -3
    echo "[exit $?]"

    echo
    echo "## gitlab-runner (concurrency + tags + the CI_JOBS env)"
    run "gitlab-runner --version | head -1"
    if [ -r /etc/gitlab-runner/config.toml ]; then
        echo "\$ grep -E 'concurrent|tag_list|CI_JOBS|environment|executor' /etc/gitlab-runner/config.toml"
        grep -E 'concurrent|tag_list|CI_JOBS|environment|executor' /etc/gitlab-runner/config.toml
        echo "[exit $?]"
    else
        echo "/etc/gitlab-runner/config.toml: not readable by $(id -un)"
    fi
    echo "CI_JOBS=${CI_JOBS:-unset}"

    echo
    echo "## NFS mounts (the NAS shares; whether the hermes-ext worktree export reaches here)"
    run "mount | grep -i ' nfs' || echo 'no nfs mounts'"
    run "df -h /mnt/projects 2>/dev/null || echo '/mnt/projects not a mount'"
    run "ls /mnt/projects 2>/dev/null | head -10"
    # TCP probes: is hermes-ext's NFS port reachable from this host at all?
    for ip in 172.16.200.100 172.16.200.205; do
        if timeout 3 bash -c "exec 3<>/dev/tcp/$ip/2049" 2>/dev/null; then
            echo "tcp $ip:2049 (NFS): OPEN"
        else
            echo "tcp $ip:2049 (NFS): CLOSED/FILTERED"
        fi
    done

    echo
    echo "## ccache (CI keeps it outside the checkout: ~gitlab-runner/.cache/ccache, 20G per docs/ci.md)"
    if command -v ccache >/dev/null 2>&1; then
        run "ccache -s | head -8"
        run "du -sh \"\${CCACHE_DIR:-\$HOME/.cache/ccache}\""
    fi

    echo
    echo "## capabilities (can a CI job on this host set rbuild up?)"
    run "sudo -n true && echo 'sudo -n: OK' || echo 'sudo -n: NOT allowed'"
    run "ls -d /home/user/src/willeq-wt 2>/dev/null || echo '/home/user/src/willeq-wt: absent'"
    run "ls -d /home/user/willeq-builds 2>/dev/null || echo '/home/user/willeq-builds: absent'"
} | tee "$OUT/inventory-$TS.txt"
