#!/usr/bin/env bash
# T497 (board B797): the regression net for "is the eqemu stack READY?".
#
# The defect it pins is not "the probe is flaky" -- it is that the probe tests
# the wrong thing entirely. 5998/9000/7000 are PUBLISHED container ports
# (/mnt/projects/akk-stack/docker-compose.yml:35-46), so `docker-proxy` binds
# them on the HOST for as long as the CONTAINER runs and completes any
# connect() whether or not a server process exists inside it. Every readiness
# gate in the tree was that connect(): `up.sh` printed `world port 9000: ready
# (0s)` against a fleet at zero processes, and `record.sh` / `f9_cycle.sh`
# skipped `up.sh` altogether on the same evidence (T493 lost a 3.5-minute run
# to it).
#
# So this net makes the difference observable rather than waiting for a run to
# fail downstream:
#
#   1. take the lock (unless --no-lock) and bring the stack up
#   2. assert the readiness definition under test is TRUE
#   3. kill the fleet AND the container's entrypoint supervisor with
#      eq_container_pkill (board B798 -- a hand-written container pkill kills
#      its own wrapper), and poll until world+loginserver+zone are all 0
#   4. assert the container is STILL Up and both host ports STILL answer --
#      the B797 premise itself, re-measured every run
#   5. VERDICT: the readiness definition under test must now be FALSE
#   6. bring the stack back up and assert ready again
#
# --legacy evaluates the PRE-T497 definition (the host-side TCP probe on 5998
# and 9000, i.e. what `eqemu_port_open` was) and is expected to FAIL with 70;
# that is how this net is known to test something. The default evaluates
# `eqemu_stack_ready` and passes.
#
# Usage:
#   scripts/eqemu/ready_selftest.sh [--unit T###] [--legacy] [--no-lock]
#                                   [--keep-server] [--kill-deadline N]
#
# Exit: 0 pass | 64 lock held | 65 setup failure (up.sh, or the supervisor won
#       the kill race) | 70 VERDICT: the readiness definition said READY with
#       the fleet at zero processes | 71 the B797 premise did not reproduce
#       (container down, or a host port stopped answering)
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"

UNIT="T497"
LEGACY=0
TAKE_LOCK=1
KEEP_SERVER=0
KILL_DEADLINE=60
while [ $# -gt 0 ]; do
    case "$1" in
        --unit) UNIT="$2"; shift 2 ;;
        --legacy) LEGACY=1; shift ;;
        --no-lock) TAKE_LOCK=0; shift ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --kill-deadline) KILL_DEADLINE="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

SUFFIX="fixed"; [ "$LEGACY" -eq 1 ] && SUFFIX="legacy"
OUT="/mnt/projects/fable/review/${UNIT}/ready_selftest"
mkdir -p "$OUT"
SERVERLOG="$OUT/server_${SUFFIX}.log"
PSLOG="$OUT/ps_${SUFFIX}.txt"
RESULT="$OUT/result_${SUFFIX}.txt"
: > "$SERVERLOG"; : > "$PSLOG"

say() { printf '[ready_selftest] %s %s\n' "$(date '+%H:%M:%S')" "$*" >&2; }

ready_under_test() {
    if [ "$LEGACY" -eq 1 ]; then
        # The pre-T497 definition, verbatim: a host-side TCP connect on both
        # ports. `eqemu_host_port_reachable` IS the old `eqemu_port_open`,
        # body unchanged -- only the name says what it proves.
        eqemu_host_port_reachable "${LOGIN_PORT}" \
            && eqemu_host_port_reachable "${WORLD_PORT}"
    else
        eqemu_stack_ready
    fi
}

census() {
    # census <label> -- append the container's process table and the counts
    # this test reasons about to the evidence file.
    {
        echo "---- $1 ($(date -Iseconds)) ----"
        echo "loginserver=$(server_process_count loginserver) world=$(server_process_count world) zone=$(server_process_count zone)"
        eq_docker exec "${EQEMU_CONTAINER}" bash -c 'ps -eo pid,ppid,args' 2>&1 | head -60
    } >> "$PSLOG" 2>&1
}

if [ "$TAKE_LOCK" -eq 1 ]; then
    if ! eqemu_lock_take "${UNIT} ready_selftest"; then
        say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
        exit 64
    fi
    say "lock taken"
fi

CLEANED=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" >>"$SERVERLOG" 2>&1
        say "server down"
    fi
    [ "$TAKE_LOCK" -eq 1 ] && { eqemu_lock_release; say "lock released"; }
}
trap cleanup EXIT INT TERM

# ---- 1. the stack up ------------------------------------------------------
say "bringing eqemu up (up.sh adopts anything already running)"
bash "$REPO/scripts/eqemu/up.sh" >>"$SERVERLOG" 2>&1 \
    || { say "up.sh failed -- see $SERVERLOG"; exit 65; }
census "after up.sh"

# ---- 2. it says ready -----------------------------------------------------
if ! ready_under_test; then
    say "SETUP FAIL: the ${SUFFIX} readiness check is FALSE right after up.sh"
    exit 65
fi
say "${SUFFIX} readiness check is TRUE after up.sh (as it must be)"

# ---- 3. kill the fleet AND the supervisor ---------------------------------
# The supervisor first, or it respawns what we kill. Both patterns are shapes
# eq_container_pkill's guard accepts; a hand-written '/tmp/spire-launcher'
# would match the wrapper and kill the exec instead (board B798).
say "killing the container's supervisor and the fleet"
eq_container_pkill TERM '[s]pire-launcher' || say "supervisor pkill returned $?"
eq_container_pkill TERM '[e]qemu-server:launcher' || say "launcher pkill returned $?"
eq_container_pkill TERM '/bin/(loginserver|world|zone)( |$)' || say "fleet pkill returned $?"
sleep 3
eq_container_pkill KILL '[s]pire-launcher' || true
eq_container_pkill KILL '/bin/(loginserver|world|zone)( |$)' || true

waited=0
until [ "$(server_process_count world)" = "0" ] \
   && [ "$(server_process_count zone)" = "0" ] \
   && [ "$(server_process_count loginserver)" = "0" ]; do
    if [ "$waited" -ge "$KILL_DEADLINE" ]; then
        say "SETUP FAIL: the fleet is still up after ${KILL_DEADLINE}s (the supervisor won the race) -- see $PSLOG"
        census "kill deadline"
        exit 65
    fi
    eq_container_pkill KILL '/bin/(loginserver|world|zone)( |$)' || true
    sleep 2
    waited=$((waited + 2))
done
say "fleet at zero processes after ${waited}s"
census "fleet at zero"

# ---- 4. the B797 premise, re-measured -------------------------------------
compose ps > "$OUT/compose_ps_${SUFFIX}.txt" 2>&1
if ! grep -q "${EQEMU_CONTAINER}" "$OUT/compose_ps_${SUFFIX}.txt"; then
    say "PREMISE FAIL: ${EQEMU_CONTAINER} is not in \`compose ps\` any more -- the kill took the container down"
    exit 71
fi
HOSTPORTS="open"
eqemu_host_port_reachable "${LOGIN_PORT}" || HOSTPORTS="login-closed"
eqemu_host_port_reachable "${WORLD_PORT}" || HOSTPORTS="${HOSTPORTS},world-closed"
say "container still Up; host ports ${LOGIN_PORT}/${WORLD_PORT}: ${HOSTPORTS}"
if [ "$HOSTPORTS" != "open" ]; then
    say "PREMISE FAIL: a host port stopped answering with the container up -- B797 did not reproduce"
    exit 71
fi

# ---- 5. the verdict -------------------------------------------------------
RC=0
if ready_under_test; then
    say "VERDICT FAIL: the ${SUFFIX} readiness check says READY with world=0 loginserver=0 zone=0"
    RC=70
else
    say "VERDICT PASS: the ${SUFFIX} readiness check is FALSE with the fleet at zero"
fi

{
    echo "unit=${UNIT} definition=${SUFFIX} rc=${RC}"
    echo "host_ports_with_fleet_at_zero=${HOSTPORTS} kill_seconds=${waited}"
    echo "eqemu_host=${EQEMU_HOST} container=${EQEMU_CONTAINER}"
    date -Iseconds
} > "$RESULT"

[ "$RC" -ne 0 ] && exit "$RC"

# ---- 6. and it comes back -------------------------------------------------
say "bringing the stack back up"
bash "$REPO/scripts/eqemu/up.sh" >>"$SERVERLOG" 2>&1 \
    || { say "up.sh failed on the second bring-up -- see $SERVERLOG"; exit 65; }
census "after second up.sh"
if ! ready_under_test; then
    say "SETUP FAIL: not ready after the second up.sh -- see $SERVERLOG"
    exit 65
fi
say "SELFTEST PASS: ready after up.sh, NOT ready with the fleet killed, ready again"
exit 0
