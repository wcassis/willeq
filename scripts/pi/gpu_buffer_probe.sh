#!/usr/bin/env bash
# B955 (GitLab #273): run ONE command on the Pi the way the gpu_buffers row
# needs it measured, and keep every source beside it. Runs ON THE PI (take
# pi_lock_take first).
#
#   * the command runs in its OWN memory cgroup (systemd-run --user --scope,
#     same pid), so a willeq/benchmark [memgpu] line reads source=memcg_shmem;
#   * its stdout+stderr are prefixed with CLOCK_MONOTONIC seconds by a reader
#     OUTSIDE that cgroup (so the log never lands in the cgroup's shmem), the
#     clock the BO census shim stamps with;
#   * with B955_BO_CENSUS=<path to lima_bo_census.so> the command runs under
#     that LD_PRELOAD shim and every Lima GEM create/close lands in
#     <out>/bo.log (scripts/pi/lima_bo_census.py folds it). A setcap binary
#     ignores LD_PRELOAD: use an uncapped copy (the NFS one is uncapped);
#   * /proc/meminfo Shmem before and after, and the cgroup's memory.stat plus
#     scripts/pi/proc_mem_snapshot.py (root: the scanout framebuffers too)
#     every --every seconds while it runs.
#
# usage: gpu_buffer_probe.sh <out-dir> <every-s> -- <command...>
set -u
OUT="$1"; EVERY="$2"; shift 2
[ "${1:-}" = "--" ] && shift
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"
grep -E '^(Shmem|MemAvailable):' /proc/meminfo > "$OUT/meminfo_before.txt"
unit="b955-probe-$$-$(date +%s)"
pre=()
[ -n "${B955_BO_CENSUS:-}" ] && pre=(env B955_BOLOG="$OUT/bo.log" LD_PRELOAD="$B955_BO_CENSUS")
systemd-run --user --scope --quiet --collect --unit="$unit" "${pre[@]}" "$@" 2>&1 |
    python3 -u -c 'import sys,time
for l in sys.stdin: sys.stdout.write("%.6f %s" % (time.monotonic(), l))' > "$OUT/run.log" &
sleep 2
pid="$(systemctl --user show -p MainPID --value "$unit.scope" 2>/dev/null)"
[ -n "$pid" ] && [ "$pid" != 0 ] || pid="$(cat "/sys/fs/cgroup$(systemctl --user show -p ControlGroup --value "$unit.scope")/cgroup.procs" 2>/dev/null | head -1)"
echo "unit=$unit pid=$pid" > "$OUT/probe.txt"
i=0
while [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; do
    i=$((i + 1))
    sudo -n python3 "$HERE/proc_mem_snapshot.py" "$pid" --label "t$i" > "$OUT/snap_$(printf %03d $i).json" 2>/dev/null
    python3 -c 'import time; print("%.6f" % time.monotonic())' > "$OUT/snap_$(printf %03d $i).mono"
    sleep "$EVERY"
done
wait
grep -E '^(Shmem|MemAvailable):' /proc/meminfo > "$OUT/meminfo_after.txt"
echo "probe done: $(grep -c . "$OUT/run.log") log lines, $i snapshots"
