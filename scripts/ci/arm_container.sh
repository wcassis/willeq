#!/usr/bin/env bash
# B1093 (#413): the ARM cross-build container dies with the shell that started it.
#
# scripts/build-arm-noble.sh and scripts/build-arm64-noble.sh used to run the compile as a
# foreground `docker run --rm`. When a gitlab-runner job is cancelled or times out, the
# runner kills the job shell and with it the docker CLI client -- but dockerd keeps the
# container running until its own command ends, i.e. until the whole cross-build is
# done (up to the 2 h cold time). Nothing named the container, so nothing could find it:
# on 2026-10-18 the ThinkPad carried FOUR such orphans at -j8/-j12 (load average 46)
# beside the jobs it was supposed to be running.
#
# Two halves:
#   sourced: `arm_container_guard <prefix>` fills ARM_CONTAINER_ARGS (a unique --name, the
#       label below, --init so the container's PID 1 forwards signals) and installs a trap
#       on EXIT/TERM/INT/HUP that `docker kill`s that name; `arm_container_run <args...>`
#       then runs `docker run --rm "${ARM_CONTAINER_ARGS[@]}" <args...>` IN THE BACKGROUND
#       and waits for it. The background + wait is the load-bearing part: bash defers a
#       trap while a FOREGROUND child runs (measured 2026-10-18: SIGTERM to the shell left
#       the foreground container alive), but a signal interrupts `wait`, the trap fires,
#       the container dies, and `wait` returns the signal status to the caller.
#   run as `arm_container.sh --sweep`: kills every container on this host that carries
#       the label, printing what it killed. The CI ARM jobs run it first: the runners are
#       concurrent=1, so a labelled container alive at job start can only be an orphan of
#       a job the runner already gave up on (a SIGKILL skips every trap).
#
# The label, not the name, is the contract: a hand run and a CI run share it, and the
# sweep is deliberately NOT in the build scripts (a dev box may legitimately run two).
ARM_CONTAINER_LABEL="willeq.ci.arm=1"

arm_container_guard() {
    local prefix="$1"
    ARM_CONTAINER_NAME="${prefix}-$$-$(date +%s)"
    ARM_CONTAINER_ARGS=(
        --name "$ARM_CONTAINER_NAME"
        --label "$ARM_CONTAINER_LABEL"
        --init
    )
    # shellcheck disable=SC2064  # expand the name now; the trap must not depend on a later state
    trap "docker kill '$ARM_CONTAINER_NAME' >/dev/null 2>&1 || true" EXIT TERM INT HUP
    echo "container:     $ARM_CONTAINER_NAME (label $ARM_CONTAINER_LABEL; killed with this shell)"
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
