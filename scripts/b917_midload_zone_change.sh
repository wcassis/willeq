#!/usr/bin/env bash
# B917 (issue #236) / B918 (issue #237): a server-directed zone change that
# lands WHILE the client's first zone load is still running, on a RENDERED
# x86 client (surfaceless EGL, --script), as a repeatable live run.
#
# The Orange Pi sweep hit it by accident (a death or a kick before `ready`,
# refactor/B854/RENDERED_SWEEP.md finding 1). This rig makes it deterministic:
# the character is seeded into a zone whose load is long, and the --script's
# FIRST command -- dispatched the moment the client is "fully connected",
# which is BEFORE the zone's graphics load starts -- is a GM `#zone <dest>`.
# The server moves the character while the loading thread is still building
# the first zone: exactly a GM summon during a load.
#
# Steps (every wait has a deadline; nothing waits >180 s without progress):
#   1. lock (atomic, --owner, default b917-midload; held = exit 64)
#   2. up.sh, snapshot the character rows (restore_lib.sh)
#   3. WORLD DOWN, seed the character at the start zone's safe point, read it
#      back, world up, boot the start zone
#   4. the rendered client with --script (#zone, wait, /loc, /q), under a
#      dead-client / stall watchdog; frames dumped once the game renders
#   5. restore the snapshot with the world down, down.sh, release the lock
#      (trap: every exit path), print the lock state last
#
# Exit: the CLIENT's exit code (0 clean /q, 4 = B918's kClientFatalExit), or
# 64 lock held / bad option, 65 server/seed/lane failure, 66 restore mismatch,
# 124 watchdog kill.
#
# Usage: EQEMU_HOST=local scripts/b917_midload_zone_change.sh --out DIR \
#          [--bin PATH] [--start charasis] [--dest qeynos] [--owner NAME]
#          [--cpus LIST]
#
# --cpus pins the client to those CPUs (taskset). On this 40-core x86 host
# charasis loads in ~1.2 s, faster than the ~1.6 s the server takes to hand
# the character to the destination zone, so the move lands AFTER `ready` and
# the race never opens (first attempt, 2026-09-29). One CPU slows the
# CPU-bound load towards the Pi's (charasis 12.5 s there) while the
# network-bound handshake is unchanged -- the Pi's ordering, not a new one.

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/eqemu/zone_place_lib.sh"
export EQEMU_HOST

OWNER="b917-midload"
START_ZONE="charasis"
DEST_ZONE="qeynos"
OUT=""
SRC_BIN="$REPO/build/bin/willeq"
CPUS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --bin) SRC_BIN="$2"; shift 2 ;;
        --start) START_ZONE="$2"; shift 2 ;;
        --dest) DEST_ZONE="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --cpus) CPUS="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
[ -n "$OUT" ] || { echo "--out is required" >&2; exit 64; }
mkdir -p "$OUT"
LOG="$OUT/client.log"
SAY_LOG="$OUT/rig.log"
LOGS="$OUT"            # zone_place_lib.sh: per-zone boot logs
ZONE_BOOT_S=150        # zone_place_lib.sh: boot-marker deadline
CHAR="${EQ_TEST_CHARACTER}"
say() { printf '[b917 %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$SAY_LOG" >&2; }

EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
[ -x "$SRC_BIN" ] || { say "no willeq binary at $SRC_BIN"; exit 64; }

BIN_DIR="$(mktemp -d /tmp/b917_midload.XXXXXX)"
BIN="$BIN_DIR/willeq"
trap 'rm -rf "$BIN_DIR"' EXIT
cp "$SRC_BIN" "$BIN"
if sudo -n setcap cap_net_raw,cap_net_admin+ep "$BIN" 2>/dev/null; then
    say "capabilities set on $BIN"
else
    say "WARNING: could not setcap $BIN -- running without CAP_NET_ADMIN"
fi
sha256sum "$SRC_BIN" > "$OUT/binary_sha256.txt"

# ---- the client config, derived from the lane, and checked ------------------
if eqemu_is_local; then
    CLIENT_HOST="$(hostname -I | awk '{print $1}')"
else
    CLIENT_HOST="$(eqemu_port_host)"
fi
CFG="$OUT/client.json"
cat > "$CFG" <<EOF
{
  "clients": [
    {
      "host": "${CLIENT_HOST}",
      "port": ${LOGIN_PORT},
      "user": "${EQ_TEST_ACCOUNT}",
      "pass": "${EQ_TEST_PASSWORD}",
      "server": "${EQ_TEST_SERVER_LONGNAME}",
      "character": "${CHAR}",
      "eq_client_path": "${EQ_PATH}"
    }
  ]
}
EOF
lane_ok=0
if eqemu_is_local; then
    hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" && lane_ok=1
else
    [ "$CLIENT_HOST" = "$EQEMU_HOST" ] && lane_ok=1
    hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" && lane_ok=0
fi
[ "$lane_ok" -eq 1 ] || { say "LANE MISMATCH: $CLIENT_HOST vs EQEMU_HOST=$EQEMU_HOST"; exit 65; }
say "lane EQEMU_HOST=$EQEMU_HOST, client host $CLIENT_HOST (verified)"

# ---- 1. lock ------------------------------------------------------------------
if ! eqemu_lock_take "$OWNER"; then
    say "eqemu lease is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lease taken: $(eqemu_lock_show)"
eqemu_lease_start_renewer

BEFORE="$OUT/snapshot_before.tsv"
AFTER="$OUT/snapshot_after.tsv"
CLIENT_PID=""
CLEANED=0
RESTORE_BAD=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ -n "$CLIENT_PID" ]; then
        kill "$CLIENT_PID" >/dev/null 2>&1; sleep 2; kill -9 "$CLIENT_PID" >/dev/null 2>&1
    fi
    eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
    timeout 170 bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/server.log" 2>&1
    eqemu_lease_stop_renewer
    eqemu_lock_release
    rm -rf "$BIN_DIR"
    say "stack down, lease released; lease now: '$(eqemu_lock_show)'"
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

# ---- 2. up + snapshot ---------------------------------------------------------
say "up.sh"
if ! timeout 175 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/server.log" 2>&1; then
    say "up.sh failed -- see $OUT/server.log"; exit 65
fi
eqemu_snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"

# ---- 3. seed with the WORLD DOWN, then boot the start zone -----------------------
ZID="$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='${START_ZONE}';" | head -1 | tr -d '[:space:]')"
SX="$(eqemu_sql "SELECT ROUND(safe_x,3) FROM zone WHERE short_name='${START_ZONE}';" | head -1 | tr -d '[:space:]')"
SY="$(eqemu_sql "SELECT ROUND(safe_y,3) FROM zone WHERE short_name='${START_ZONE}';" | head -1 | tr -d '[:space:]')"
SZ="$(eqemu_sql "SELECT ROUND(safe_z,3) FROM zone WHERE short_name='${START_ZONE}';" | head -1 | tr -d '[:space:]')"
[ -n "$ZID" ] || { say "no zone row for $START_ZONE"; exit 65; }
eqemu_world_stop
eqemu_sql "UPDATE character_data SET zone_id=${ZID}, x=${SX}, y=${SY}, z=${SZ}, heading=0 WHERE name='${CHAR}';" >/dev/null
GOT="$(eqemu_sql "SELECT CONCAT_WS(',',zone_id,ROUND(x,3),ROUND(y,3),ROUND(z,3)) FROM character_data WHERE name='${CHAR}';" | tail -1 | tr -d '\r[:space:]')"
WANT="${ZID},$(printf '%.3f' "$SX"),$(printf '%.3f' "$SY"),$(printf '%.3f' "$SZ")"
[ "$GOT" = "$WANT" ] || { say "seed did not read back: db='$GOT' want='$WANT'"; exit 65; }
say "seed read back: ${CHAR} -> ${START_ZONE} (${WANT})"
eqemu_world_start
if ! timeout 175 bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/server.log" 2>&1; then
    say "up.sh (after seed) failed"; exit 65
fi
boot_zone "$START_ZONE" || { say "zone $START_ZONE did not boot"; exit 65; }
say "zone $START_ZONE booted"

# ---- 4. the rendered, scripted client ---------------------------------------------
TOUR="$OUT/tour.txt"
cat > "$TOUR" <<EOF
# Held until fully connected (Application::updateScript) -- which is BEFORE
# the zone graphics load starts -- so the #zone lands mid-load.
/say #zone ${DEST_ZONE}
wait 40
/loc
wait 1
/q
EOF
ARGS=( -c "$CFG" --renderer gles2 -r 800 600 --no-audio
       --eqe1-path "$EQE1_PATH" --log-level=INFO
       --script "$TOUR"
       --dump-frames-dir "$OUT/frames" --dump-every 60 --dump-max-frames 4 )
mkdir -p "$OUT/frames"
printf '%q ' ${CPUS:+taskset -c "$CPUS"} "$SRC_BIN" "${ARGS[@]}" > "$OUT/command_line.txt"; echo >> "$OUT/command_line.txt"
say "launching: $(cat "$OUT/command_line.txt")"
PIN=()
[ -n "$CPUS" ] && PIN=( taskset -c "$CPUS" )
( cd "$REPO" && TZ=UTC exec "${PIN[@]}" "$BIN" "${ARGS[@]}" ) > "$LOG" 2>&1 &
CLIENT_PID=$!
t0=$(date +%s); last_size=0; last_growth=$t0; KILLED=""
while kill -0 "$CLIENT_PID" 2>/dev/null; do
    sleep 2
    now=$(date +%s)
    size=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
    if [ "$size" -ne "$last_size" ]; then last_size=$size; last_growth=$now; fi
    [ $((now - last_growth)) -ge 60 ] && KILLED="no log growth for 60 s"
    [ $((now - t0)) -ge 170 ] && KILLED="170 s overall deadline"
    if [ -n "$KILLED" ]; then
        say "WATCHDOG: $KILLED -- killing the client"
        kill "$CLIENT_PID" 2>/dev/null; sleep 3; kill -9 "$CLIENT_PID" 2>/dev/null
        break
    fi
done
wait "$CLIENT_PID" 2>/dev/null
CLIENT_RC=$?
CLIENT_PID=""
[ -n "$KILLED" ] && CLIENT_RC=124
say "client exit $CLIENT_RC after $(( $(date +%s) - t0 )) s${KILLED:+ (watchdog: $KILLED)}"
echo "$CLIENT_RC" > "$OUT/client_exit_code.txt"
{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) lane=$EQEMU_HOST"
    echo "start=$START_ZONE dest=$DEST_ZONE cpus=${CPUS:-all} client_rc=$CLIENT_RC"
} > "$OUT/run_meta.txt"

# ---- 5. teardown (also the trap) ---------------------------------------------------
cleanup
[ "$RESTORE_BAD" -eq 1 ] && { say "restore mismatch"; exit 66; }
exit "$CLIENT_RC"
