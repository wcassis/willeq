#!/usr/bin/env bash
# T410 (board B138): run one live scenario end to end and exit with its code.
#
# This is the piece every live unit on the board re-implements by hand
# (review/T377|T386|T391|T401/scripts): take the eqemu lock, bring the server
# up, snapshot the character rows, run a client, restore the snapshot with the
# world DOWN, bring the server down, release the lock. The difference is that
# the RESULT is this script's exit status, not a transcript a human reads.
#
# Usage:
#   scripts/scenario.sh <scenario> [--timeout N] [--hold N] [--spell ID]
#                                  [--config FILE] [--peer-config FILE]
#                                  [--unit T###] [--keep-server]
#   scripts/scenario.sh --selftest [--unit T###] [--config FILE] [...]
#
# --selftest runs scripts/eqemu/restore_selftest.sh, the regression net for the
# snapshot restore this script performs (T493, board B784): it logs a character
# in, makes the snapshot and the world disagree, restores, tears the server
# down and checks that the snapshot survived. Everything after --selftest is
# passed to it. Run it after ANY change to the restore path.
#
# Exit codes are the client's, passed through unchanged:
#   0 pass   1 fail   2 timeout   3 could-not-start
# plus this script's own operator errors:
#   64 lock held by another session   65 server would not come up
#   66 snapshot/restore mismatch
#
# It NEVER waits for the lock and never overwrites one: a held lock is exit
# 64 immediately (T224 clobbered another session's lock 90 s after seeing it
# absent -- the take below is `set -o noclobber`, atomic).

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# T493: `--selftest` is how a user reaches the restore's regression net. It
# takes the lock itself, so hand it the whole command line and get out of the
# way.
if [ "${1:-}" = "--selftest" ]; then
    shift
    exec bash "$REPO/scripts/eqemu/restore_selftest.sh" "$@"
fi

SCENARIO="${1:?usage: scenario.sh <scenario> [options] | scenario.sh --selftest [options]}"
shift

# eqemu runs on its own VM; env.sh supplies eq_docker/mysql + the lock on that host.
source "$REPO/scripts/eqemu/env.sh"
# T493 (board B784): ONE snapshot/restore implementation, shared with
# scripts/llmbench/{record,teardown}.sh. Do not inline another copy here.
source "$REPO/scripts/eqemu/restore_lib.sh"
UNIT="T410"
TIMEOUT=300
HOLD=300
SPELL=46
CONFIG=""
PEER_CONFIG=""
KEEP_SERVER=0

while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --hold) HOLD="$2"; shift 2 ;;
        --spell) SPELL="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --peer-config) PEER_CONFIG="$2"; shift 2 ;;
        --unit) UNIT="$2"; shift 2 ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

OUT="/mnt/projects/fable/review/${UNIT}/scenario"
mkdir -p "$OUT"
LOG="$OUT/${SCENARIO}_client.log"
PEERLOG="$OUT/${SCENARIO}_peer.log"
BEFORE="$OUT/${SCENARIO}_snapshot_before.tsv"
AFTER="$OUT/${SCENARIO}_snapshot_after.tsv"
# A task worktree has no build/ of its own; the shared build lives in main.
BIN="${WILLEQ_BIN:-}"
if [ -z "$BIN" ]; then
    for c in "$REPO/build/bin/willeq" /mnt/projects/fable/main/build/bin/willeq; do
        [ -x "$c" ] && { BIN="$c"; break; }
    done
fi

[ -n "$CONFIG" ] || CONFIG="$REPO/scripts/scenario_configs/${SCENARIO}.json"
[ -n "$PEER_CONFIG" ] || PEER_CONFIG="$REPO/scripts/scenario_configs/peer.json"

say() { printf '[scenario.sh] %s\n' "$*" >&2; }

# ---- 1. the lock, taken atomically -----------------------------------------
if ! eqemu_lock_take "${UNIT} scenario.sh ${SCENARIO}"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lock taken"

CLEANED=0
RESTORE_BAD=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    kill ${PEER_PID:-} ${CLIENT_PID:-} >/dev/null 2>&1
    sleep 2
    kill -9 ${PEER_PID:-} ${CLIENT_PID:-} >/dev/null 2>&1
    # The restore runs on EVERY path now, --keep-server included: it is what
    # leaves the database as the run found it, and leaving a moved character
    # behind for the next unit is the whole point of B784. It stops the
    # eqemu-server container itself, so --keep-server starts it again.
    restore_snapshot
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/${SCENARIO}_server.log" 2>&1
    else
        eqemu_world_start
        say "eqemu-server container restarted (--keep-server)"
    fi
    eqemu_lock_release
    say "lock released"
}
trap cleanup EXIT INT TERM

# The snapshot and the restore are restore_lib.sh's, not this script's.
snapshot() { eqemu_snapshot "$1"; }

restore_snapshot() {
    # T493 (board B784). What this used to do, and why none of it survives:
    # it `pkill`ed world/zone/loginserver INSIDE the eqemu-server container and
    # then issued one UPDATE per row. Both halves were broken. The pkill chain
    # matched its OWN command line on the first pattern (`/tmp/spire-launcher`)
    # and killed the exec's shell before the other three ran -- MEASURED: rc
    # 143, and 27 world/zone processes still alive afterwards -- and the
    # per-row loop cost 22 s locally and 2 min over ssh. So the restore ran
    # against a live world, reported itself byte-identical, and was then undone
    # by the zone's shutdown save when down.sh followed it: MEASURED, Testbuddy
    # back at 0/0 over the snapshot's 1111/2222.
    #
    # eqemu_restore_snapshot stops the CONTAINER (mariadb is a separate one and
    # stays up) and sends the whole restore as one statement batch.
    # scripts/eqemu/restore_selftest.sh pins it; `scenario.sh --selftest` runs
    # that.
    eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
}

# ---- 2. server up ----------------------------------------------------------
say "bringing eqemu up"
if ! bash "$REPO/scripts/eqemu/up.sh" > "$OUT/${SCENARIO}_server.log" 2>&1; then
    say "up.sh failed -- see $OUT/${SCENARIO}_server.log"
    exit 65
fi

# ---- 3. snapshot -----------------------------------------------------------
snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"

# ---- 4. the peer, for two_client_survive ------------------------------------
# The partner is a PEER PROCESS running zone_in on the second account -- the
# client never forks a second client (it would need another character's whole
# login config, which lives here). T391 staggered the partner by ~55 s so it
# arrives as a fresh spawn packet rather than inside the bulk zone-in list;
# the observer's peer window (180 s from ITS zone-in) covers that.
PEER_PID=""
if [ "$SCENARIO" = "two_client_survive" ]; then
    if [ ! -f "$PEER_CONFIG" ]; then
        say "two_client_survive needs --peer-config <file> (a second account)"
        exit 64
    fi
    (
        sleep 55
        "$BIN" -c "$PEER_CONFIG" --scenario zone_in --timeout "$TIMEOUT" \
            --log-level=INFO > "$PEERLOG" 2>&1
    ) &
    PEER_PID=$!
    say "peer scheduled (zone_in, +55s, pid $PEER_PID)"
fi

# ---- 5. the run ------------------------------------------------------------
say "running --scenario ${SCENARIO} --timeout ${TIMEOUT}"
"$BIN" -c "$CONFIG" --scenario "$SCENARIO" --timeout "$TIMEOUT" \
    --scenario-hold "$HOLD" --scenario-spell "$SPELL" \
    --log-level=INFO > "$LOG" 2>&1 &
CLIENT_PID=$!
wait "$CLIENT_PID"
CODE=$?
CLIENT_PID=""
say "client exit ${CODE}"
grep -E '\[SCENARIO\]|^SCENARIO ' "$LOG" | tail -20 >&2

# ---- 6. teardown (trap) ----------------------------------------------------
cleanup
if [ "$RESTORE_BAD" -eq 1 ]; then
    say "restore mismatch overrides the scenario result"
    exit 66
fi
exit "$CODE"
