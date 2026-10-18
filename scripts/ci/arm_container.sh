#!/usr/bin/env bash
# B1093 (#413): the ARM cross-build container dies with the job that started it.
#
# scripts/build-arm-noble.sh and scripts/build-arm64-noble.sh used to run the compile as a
# foreground `docker run --rm`. When a gitlab-runner job is cancelled or times out, the
# runner kills the job shell and with it the docker CLI client -- but dockerd keeps the
# container running until its own command ends, i.e. until the whole cross-build is
# done (up to the 2 h cold time). Nothing named the container, so nothing could find it:
# on 2026-10-18 the ThinkPad carried FOUR such orphans at -j8/-j12 (load average 46)
# beside the jobs it was supposed to be running.
#
# Three layers, each measured against a failure the previous one did not cover:
#   1. `arm_container_guard <prefix>` fills ARM_CONTAINER_ARGS (a unique --name, the label
#      below, --init so the container's PID 1 forwards signals) and installs a trap on
#      EXIT/TERM/INT/HUP that `docker kill`s that name; `arm_container_run <args...>` runs
#      `docker run --rm "${ARM_CONTAINER_ARGS[@]}" <args...>` IN THE BACKGROUND and waits.
#      bash defers a trap while a FOREGROUND child runs (measured: SIGTERM to the shell
#      left a foreground container alive); a signal interrupts `wait`, the trap fires, and
#      `wait` returns the signal status to the caller. Covers Ctrl-C and a SIGTERM.
#   2. A detached WATCHDOG (setsid, stdio on /dev/null, so it holds none of the runner's
#      pipes) polls the script's PARENT and the script itself every few seconds and
#      `docker kill`s the container when either is gone. Measured 2026-10-18 on
#      gitlab-runner-1 (job 8777): a runner CANCEL killed only the job shell -- the build
#      script was reparented to PID 1 with no signal at all, its trap never ran, the
#      container compiled on for 7 minutes until killed by hand, and the job sat in
#      "canceling" the whole time because the orphan still held the runner's pipe. The
#      parent dying is the one signal that path leaves behind. A hand run's parent is
#      the interactive shell, so a closed terminal kills the build too, which is right.
#   3. `arm_container.sh --sweep` kills every container on this host that carries the
#      label, printing what it killed. The CI ARM jobs run it first: the runners are
#      concurrent=1, so a labelled container alive at job start can only be an orphan
#      (a SIGKILL of the script skips every trap; layer 2 needs a few seconds).
#
# The label, not the name, is the contract: a hand run and a CI run share it, and the
# sweep is deliberately NOT in the build scripts (a dev box may legitimately run two).
ARM_CONTAINER_LABEL="willeq.ci.arm=1"
ARM_CONTAINER_WATCH_INTERVAL="${ARM_CONTAINER_WATCH_INTERVAL:-3}"

arm_container_guard() {
    local prefix="$1" parent=$PPID self=$$
    ARM_CONTAINER_NAME="${prefix}-$$-$(date +%s)"
    ARM_CONTAINER_ARGS=(
        --name "$ARM_CONTAINER_NAME"
        --label "$ARM_CONTAINER_LABEL"
        --init
    )
    # shellcheck disable=SC2064  # expand the name now; the trap must not depend on a later state
    trap "docker kill '$ARM_CONTAINER_NAME' >/dev/null 2>&1 || true" EXIT TERM INT HUP
    # Layer 2: the watchdog outlives a killed job shell; it exits on its own once the
    # script is gone (one last docker kill, a no-op after a clean finish).
    setsid bash -c '
        name=$1; parent=$2; self=$3; every=$4
        while kill -0 "$parent" 2>/dev/null && kill -0 "$self" 2>/dev/null; do sleep "$every"; done
        docker kill "$name" >/dev/null 2>&1 || true
    ' arm_container_watchdog "$ARM_CONTAINER_NAME" "$parent" "$self" "$ARM_CONTAINER_WATCH_INTERVAL" \
        </dev/null >/dev/null 2>&1 &
    disown $! 2>/dev/null || true
    echo "container:     $ARM_CONTAINER_NAME (label $ARM_CONTAINER_LABEL; killed with this shell or its parent $parent)"
}

# arm_container_run <docker run args after --rm>  -> the container's exit status
arm_container_run() {
    local pid rc=0
    docker run --rm "${ARM_CONTAINER_ARGS[@]}" "$@" &
    pid=$!
    wait "$pid" || rc=$?
    return "$rc"
}

arm_container_sweep() {
    local ids
    ids="$(docker ps -q --filter "label=$ARM_CONTAINER_LABEL")"
    if [ -z "$ids" ]; then
        echo "arm_container sweep on $(hostname): no orphaned ARM build container"
        return 0
    fi
    echo "arm_container sweep on $(hostname): killing orphaned ARM build container(s):"
    docker ps --filter "label=$ARM_CONTAINER_LABEL" --format '  {{.ID}} {{.Names}} {{.Image}} up {{.RunningFor}}'
    # shellcheck disable=SC2086  # one id per word is the point
    docker kill $ids >/dev/null
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    case "${1:-}" in
        --sweep) arm_container_sweep ;;
        *) echo "usage: $0 --sweep   (or source it and call arm_container_guard <prefix> + arm_container_run <args>)" >&2; exit 2 ;;
    esac
fi
