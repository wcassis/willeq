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

SCENARIO="${1:?usage: scenario.sh <scenario> [options]}"
shift

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# eqemu runs on its own VM; env.sh supplies eq_docker/mysql + the lock on that host.
source "$REPO/scripts/eqemu/env.sh"
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
BIN="$REPO/build/bin/willeq"

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
    if [ "$KEEP_SERVER" -eq 0 ]; then
        restore_snapshot
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/${SCENARIO}_server.log" 2>&1
    fi
    eqemu_lock_release
    say "lock released"
}
trap cleanup EXIT INT TERM

sql() {
    # One-shot SQL against the akk-stack peq db (T401's sql.sh, inlined so the
    # lane has no dependency outside the repo).
    eq_docker exec "${MARIADB_CONTAINER}" mysql -uroot -p"$(mariadb_root_password)" peq -B -N -e "$1"
}

snapshot() {
    # The rows a scenario can plausibly move. Ordered, so a diff is meaningful.
    sql "SELECT id,name,zone_id,x,y,z,heading,level,exp FROM character_data ORDER BY id;" > "$1"
    sql "SELECT id,platinum,gold,silver,copper FROM character_currency ORDER BY id;" >> "$1"
}

restore_snapshot() {
    [ -s "$BEFORE" ] || return 0
    # T377's lesson, repeated in T401: the WORLD writes character rows back on
    # shutdown, so a restore applied while it is up is silently undone. Stop
    # world/zone/loginserver (and the spire launcher that respawns them),
    # leaving only mariadb, then write.
    say "restoring server snapshot with the world DOWN"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "pkill -f /tmp/spire-launcher; pkill -f 'bin/world'; pkill -f 'bin/zone'; pkill -f 'bin/loginserver'" \
        >/dev/null 2>&1
    sleep 6
    while IFS=$'\t' read -r id name zone x y z h lvl exp; do
        [ -n "${id:-}" ] || continue
        [ -n "${name:-}" ] || continue
        sql "UPDATE character_data SET zone_id=${zone},x=${x},y=${y},z=${z},heading=${h},level=${lvl},exp=${exp} WHERE id=${id};" >/dev/null
    done < <(grep -v '^$' "$BEFORE" | awk -F'\t' 'NF==9')
    while IFS=$'\t' read -r id p g s c; do
        [ -n "${id:-}" ] || continue
        sql "UPDATE character_currency SET platinum=${p},gold=${g},silver=${s},copper=${c} WHERE id=${id};" >/dev/null
    done < <(grep -v '^$' "$BEFORE" | awk -F'\t' 'NF==5')
    snapshot "$AFTER"
    if diff -q "$BEFORE" "$AFTER" >/dev/null 2>&1; then
        say "snapshot restored byte-identically"
    else
        say "SNAPSHOT MISMATCH -- see $BEFORE vs $AFTER"
        RESTORE_BAD=1
    fi
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
