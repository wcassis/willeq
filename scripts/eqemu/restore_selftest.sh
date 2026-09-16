#!/usr/bin/env bash
# T493 (board B784): the regression net for the eqemu snapshot restore.
#
# The defect it pins is not "the restore SQL is wrong" -- it is "the restore
# ran while the world was still up, so the world wrote the rows back
# underneath it". Two MEASURED facts make that invisible to every other check
# in the tree:
#
#   * the pre-T493 `pkill` chain killed its OWN shell -- the container-side
#     `bash -c "pkill -f /tmp/spire-launcher; pkill -f 'bin/world'; ..."`
#     matches its own command line on the FIRST pattern, so the exec died with
#     143 and world, the zone fleet and the launcher all stayed up. "Restoring
#     with the world DOWN" never took the world down at all.
#   * the zone writes its in-memory copy of a character back when it SHUTS
#     DOWN, not when the client disconnects (measured: a row perturbed while
#     Testbuddy was in world survived the client's death by 60 s untouched,
#     and was overwritten the instant the zone got SIGTERM). So a restore can
#     verify byte-identical and then be undone by the `down.sh` that follows
#     it -- which is why the after-snapshot diff alone never caught this.
#
# The net therefore makes the divergence deliberate and deterministic instead
# of waiting for a save to land:
#
#   1. take the lock (unless --no-lock), bring the stack up if it is down
#   2. snapshot PRISTINE -- what the database must be left at, whatever happens
#   3. run a headless client (--automated) and wait for "Zone connection
#      complete!"; the zone now holds its own copy of that character
#   4. move the character's row in the DATABASE to a sentinel position. The
#      zone still holds the old one, which is exactly the state a live run
#      leaves: snapshot and world disagree.
#   5. snapshot BEFORE (with the sentinel) and kill the client -- scenario.sh's
#      own order
#   6. run the restore UNDER TEST, wall-clock timed
#   7. run down.sh's process-kill step, i.e. the teardown that follows every
#      restore, and give the zone its shutdown save
#   8. snapshot AFTER and diff: a restore that left the world up loses the
#      sentinel here
#   9. put PRISTINE back with the world down, and verify that too
#
# PASS = byte-identical AND no world/zone process was alive when the restore
# finished AND the restore took no longer than --max-seconds (default 40).
# Reached by a user as `scripts/scenario.sh --selftest`.
#
# Usage:
#   scripts/eqemu/restore_selftest.sh [--unit T###] [--config FILE]
#                                     [--max-seconds N] [--login-timeout N]
#                                     [--keep-server] [--no-lock] [--legacy]
#
# --legacy runs the PRE-T493 restore (pkill in the container + one UPDATE per
# row) instead of the shared one, which is how this net is shown to FAIL on
# the defect it pins rather than merely passing on the fix.
#
# Exit: 0 pass | 64 lock held | 65 server would not come up | 66 restore
#       mismatch | 67 the client never reached the world | 68 restore too slow
#       | 69 the world was still up when the restore finished
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"

UNIT="T493"
CONFIG=""
MAX_SECONDS=40
LOGIN_TIMEOUT=180
SAVE_SETTLE=15
KEEP_SERVER=0
TAKE_LOCK=1
LEGACY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --unit) UNIT="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --max-seconds) MAX_SECONDS="$2"; shift 2 ;;
        --login-timeout) LOGIN_TIMEOUT="$2"; shift 2 ;;
        --settle) SAVE_SETTLE="$2"; shift 2 ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --no-lock) TAKE_LOCK=0; shift ;;
        --legacy) LEGACY=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

# T489: not Testchar. peer.json is Testbuddy, the character B784 was measured
# on. The default follows the LANE: EQEMU_HOST=local -> peer.local.json (this
# host), otherwise peer.json (the VM). The orchestrator ran this in local mode
# without --config and pointed a client at the VM for 180 s -- a lane leak.
if [ -z "$CONFIG" ]; then
    if [ "${EQEMU_HOST:-}" = "local" ]; then
        CONFIG="$REPO/scripts/scenario_configs/peer.local.json"
    else
        CONFIG="$REPO/scripts/scenario_configs/peer.json"
    fi
fi

# build/ is not populated in a task worktree; the shared build is in main.
BIN="${WILLEQ_BIN:-}"
if [ -z "$BIN" ]; then
    for c in "$REPO/build/bin/willeq" /mnt/projects/fable/main/build/bin/willeq; do
        [ -x "$c" ] && { BIN="$c"; break; }
    done
fi

OUT="/mnt/projects/fable/review/${UNIT}/restore_selftest"
mkdir -p "$OUT"
SUFFIX="fixed"; [ "$LEGACY" -eq 1 ] && SUFFIX="legacy"
PRISTINE="$OUT/pristine_${SUFFIX}.tsv"
BEFORE="$OUT/before_${SUFFIX}.tsv"
AFTER="$OUT/after_${SUFFIX}.tsv"
FINAL="$OUT/final_${SUFFIX}.tsv"
CLIENTLOG="$OUT/client_${SUFFIX}.log"
SERVERLOG="$OUT/server_${SUFFIX}.log"

say() { printf '[restore_selftest] %s %s\n' "$(date '+%H:%M:%S')" "$*" >&2; }

[ -n "$BIN" ] && [ -x "$BIN" ] || { say "no willeq binary (set WILLEQ_BIN)"; exit 65; }
[ -f "$CONFIG" ] || { say "no client config at $CONFIG"; exit 65; }
# Lane guard: the config's host must be the lane's server, whichever way the
# mismatch runs (a VM config on the local lane, or a local config on the VM).
CFG_HOST="$(sed -n 's/.*"host"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CONFIG" | head -1)"
LANE_HOST="$(eqemu_port_host)"
case "$CFG_HOST" in
    "$LANE_HOST"|127.0.0.1|localhost) ;;
    *) if ! { [ "$LANE_HOST" = 127.0.0.1 ] && hostname -I | tr ' ' '\n' | grep -qx "$CFG_HOST"; }; then
           say "LANE MISMATCH: config host ${CFG_HOST} is not this lane's server (${LANE_HOST}, EQEMU_HOST=${EQEMU_HOST:-unset}) -- refusing"
           exit 65
       fi ;;
esac
CHAR="$(sed -n 's/.*"character"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CONFIG" | head -1)"
[ -n "$CHAR" ] || { say "no \"character\" in $CONFIG"; exit 65; }

if [ "$TAKE_LOCK" -eq 1 ]; then
    if ! eqemu_lock_take "${UNIT} restore_selftest"; then
        say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
        exit 64
    fi
    say "lock taken"
fi

CLIENT_PID=""
CLEANED=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    kill ${CLIENT_PID:-} >/dev/null 2>&1
    sleep 2
    kill -9 ${CLIENT_PID:-} >/dev/null 2>&1
    # Whatever went wrong above, the database goes back to PRISTINE, and it is
    # done with the world down so the repair cannot be undone in turn.
    if [ -s "$PRISTINE" ]; then
        eqemu_world_stop
        eqemu_sql "$(eqemu_restore_sql_from "$PRISTINE")" >/dev/null
        eqemu_snapshot "$FINAL"
        if diff -q "$PRISTINE" "$FINAL" >/dev/null 2>&1; then
            say "database left at PRISTINE (verified)"
        else
            say "WARNING: could not put the database back to PRISTINE -- $PRISTINE vs $FINAL"
            diff "$PRISTINE" "$FINAL" | head -20 >&2
        fi
    fi
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" >>"$SERVERLOG" 2>&1
        say "server down"
    else
        eqemu_world_start
        say "eqemu-server container restarted (--keep-server)"
    fi
    [ "$TAKE_LOCK" -eq 1 ] && { eqemu_lock_release; say "lock released"; }
}
trap cleanup EXIT INT TERM

# ---- the pre-T493 restore, kept ONLY so this net can be shown to fail ------
legacy_restore_snapshot() {
    say "LEGACY restore: pkill inside the container, one UPDATE per row"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "pkill -f /tmp/spire-launcher; pkill -f 'bin/world'; pkill -f 'bin/zone'; pkill -f 'bin/loginserver'" \
        >/dev/null 2>&1
    sleep 6
    while IFS=$'\t' read -r id name zone x y z h lvl exp; do
        [ -n "${id:-}" ] || continue
        [ -n "${name:-}" ] || continue
        eqemu_sql "UPDATE character_data SET zone_id=${zone},x=${x},y=${y},z=${z},heading=${h},level=${lvl},exp=${exp} WHERE id=${id};" >/dev/null
    done < <(grep -v '^$' "$BEFORE" | awk -F'\t' 'NF==9')
    while IFS=$'\t' read -r id p g s c; do
        [ -n "${id:-}" ] || continue
        eqemu_sql "UPDATE character_currency SET platinum=${p},gold=${g},silver=${s},copper=${c} WHERE id=${id};" >/dev/null
    done < <(grep -v '^$' "$BEFORE" | awk -F'\t' 'NF==5')
    eqemu_snapshot "$AFTER"
    diff -q "$BEFORE" "$AFTER" >/dev/null 2>&1 \
        && say "snapshot restored BYTE-IDENTICALLY (as the restore itself sees it)" \
        || say "SNAPSHOT MISMATCH at the restore's own diff"
}

world_procs() {
    # How many world/zone processes are alive inside the container. A stopped
    # container answers 0 (the exec fails and prints nothing).
    local n
    n="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c \
            "ps -eo args | grep -cE '[b]in/(world|zone)'" 2>/dev/null | tr -d '[:space:]')"
    [ -n "$n" ] || n=0
    echo "$n"
}

# ---- 1. server up ---------------------------------------------------------
# Unconditionally, because up.sh adopts a running fleet (T457, board B483) and
# because a port probe cannot tell this lane apart from a dead one: with the
# stack running under docker on THIS host, 5998 and 9000 are published ports,
# so docker-proxy completes the connect() even when nothing inside the
# container is listening (MEASURED T493 with world/zone/loginserver all at
# zero). The process counts are the only honest check.
say "bringing eqemu up (up.sh adopts anything already running)"
bash "$REPO/scripts/eqemu/up.sh" > "$SERVERLOG" 2>&1 || { say "up.sh failed -- see $SERVERLOG"; exit 65; }
if ! server_process_running world || ! server_process_running loginserver; then
    say "up.sh returned 0 but world/loginserver are not running -- see $SERVERLOG"
    exit 65
fi

# ---- 2. pristine ----------------------------------------------------------
eqemu_snapshot "$PRISTINE"
say "pristine snapshot ($(wc -l < "$PRISTINE") rows) -> $PRISTINE"

# ---- 3. a character IN WORLD ----------------------------------------------
say "logging ${CHAR} in ($(basename "$CONFIG")) and leaving it in world"
"$BIN" -c "$CONFIG" --automated --log-level=INFO > "$CLIENTLOG" 2>&1 &
CLIENT_PID=$!
waited=0
until grep -q 'Zone connection complete' "$CLIENTLOG" 2>/dev/null; do
    if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
        say "the client exited before reaching the world -- see $CLIENTLOG"
        exit 67
    fi
    if [ "$waited" -ge "$LOGIN_TIMEOUT" ]; then
        say "the client never reached the world in ${LOGIN_TIMEOUT}s -- see $CLIENTLOG"
        exit 67
    fi
    sleep 1
    waited=$((waited + 1))
done
say "${CHAR} is in world after ${waited}s (pid ${CLIENT_PID})"

# ---- 4. make the snapshot and the world disagree --------------------------
# A live run does this by moving the character; doing it in SQL makes the
# divergence exact and removes the wait for a save to land.
SENTINEL_X=1111
SENTINEL_Y=2222
eqemu_sql "UPDATE character_data SET x=${SENTINEL_X},y=${SENTINEL_Y} WHERE name='${CHAR}';" >/dev/null
say "moved ${CHAR}'s row to the sentinel ${SENTINEL_X}/${SENTINEL_Y}; the zone still holds the old one"

# ---- 5. the snapshot the restore must reproduce ---------------------------
eqemu_snapshot "$BEFORE"
if diff -q "$PRISTINE" "$BEFORE" >/dev/null 2>&1; then
    say "BEFORE is identical to PRISTINE -- the perturbation did not take, the test would be vacuous"
    exit 66
fi
kill "$CLIENT_PID" >/dev/null 2>&1; sleep 2; kill -9 "$CLIENT_PID" >/dev/null 2>&1
CLIENT_PID=""
say "client killed (scenario.sh's order: clients die, then the restore runs)"

# ---- 6. the restore under test, timed -------------------------------------
T0=$(date +%s)
RC=0
if [ "$LEGACY" -eq 1 ]; then
    legacy_restore_snapshot
else
    eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RC=$?
fi
ELAPSED=$(( $(date +%s) - T0 ))
PROCS="$(world_procs)"
say "restore wall time: ${ELAPSED}s (budget ${MAX_SECONDS}s); world/zone processes still alive: ${PROCS}"

# ---- 7. the teardown that follows every restore ---------------------------
# down.sh's first step, verbatim. This is where a zone that is still up writes
# its in-memory copy of the character back over the restore.
say "sending down.sh's SIGTERM to loginserver/world/zone and settling ${SAVE_SETTLE}s"
eq_docker exec "${EQEMU_CONTAINER}" bash -c \
    "pkill -TERM -f '/bin/(loginserver|world|zone)( |\$)'" >/dev/null 2>&1
sleep "$SAVE_SETTLE"

# ---- 8. the verdict -------------------------------------------------------
eqemu_snapshot "$AFTER"
if ! diff -q "$BEFORE" "$AFTER" >/dev/null 2>&1; then
    say "SELFTEST FAIL: the snapshot did NOT survive the teardown -- $BEFORE vs $AFTER"
    diff "$BEFORE" "$AFTER" | head -20 >&2
    RC=66
fi

{
    echo "unit=${UNIT} impl=${SUFFIX} restore_seconds=${ELAPSED} budget=${MAX_SECONDS}"
    echo "world_procs_after_restore=${PROCS} login_seconds=${waited} settle=${SAVE_SETTLE}"
    echo "character=${CHAR} config=${CONFIG} bin=${BIN} eqemu_host=${EQEMU_HOST}"
    echo "rc=${RC}"
    date -Iseconds
} > "$OUT/result_${SUFFIX}.txt"

[ "$RC" -ne 0 ] && { say "SELFTEST FAIL (rc=${RC})"; exit "$RC"; }
if [ "$PROCS" -ne 0 ] 2>/dev/null; then
    say "SELFTEST FAIL: ${PROCS} world/zone process(es) were still alive when the restore finished"
    exit 69
fi
if [ "$ELAPSED" -gt "$MAX_SECONDS" ]; then
    say "SELFTEST FAIL: restore took ${ELAPSED}s, over the ${MAX_SECONDS}s budget"
    exit 68
fi
say "SELFTEST PASS: byte-identical through the teardown, in ${ELAPSED}s, world down"
exit 0
