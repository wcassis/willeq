#!/usr/bin/env bash
# B924 / GitLab #243 -- the 5-minute idle hold rig for the llvmpipe
# post-ready gpu_driver growth.
#
# What it does (one run per invocation, one zone):
#   * takes the eqemu lane lock, brings the stack up, takes the ONE snapshot
#     (the lane's own scripts: env.sh, restore_lib.sh, zone_place_lib.sh)
#   * runs the RENDERED client (surfaceless EGL on llvmpipe, the Pi recipe
#     --renderer gles2 --constrained orangepi -r 1280 720, WILLEQ_BASELINE=1)
#     zoned into $ZONE and HOLDS it standing still for HOLD_S seconds
#   * the client's own ledger does the sampling: WILLEQ_LEDGER_IDLE_EVERY +
#     WILLEQ_LEDGER_IDLE_COUNT put [memledger] points every 30 s past
#     steady_60s (idle_90 ... idle_330), each carrying frames= and
#     gpu_driver=/owned=/rss_anon=; WILLEQ_IDLE_MEMMAP=1 prints the full
#     [memmap] smaps census beside steady_60s and every idle point, so the
#     growing mapping is named by the client itself
#   * RssAnon is ALSO sampled every 2 s from /proc/<pid>/status
#     (samples.tsv), so the peak is not only the 30 s points'
#   * WILLEQ_FPS_CAP (optional) paces the render loop for the frames-vs-time
#     A/B hold (attribution step)
#   * restores the snapshot with the world DOWN, brings the stack down,
#     releases the lock, and prints the lock state LAST (the lane discipline
#     from scripts/client_ledger_capture.sh)
#
# Exit codes (the lane's): 0 ok; 64 lock held; 65 stack up / lane mismatch;
# 66 restore failed; 68 bad inputs.

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/eqemu/zone_place_lib.sh"
export EQEMU_HOST

ZONE="gfaydark"
OUT=""
SRC_BIN="${WILLEQ_BIN:-$REPO/build/bin/willeq}"
HOLD_S=420                 # total: ready (~30-60 s) + 5 min standing still
FPS_CAP="${WILLEQ_FPS_CAP:-}"
IDLE_EVERY="${WILLEQ_LEDGER_IDLE_EVERY:-30}"
IDLE_COUNT="${WILLEQ_LEDGER_IDLE_COUNT:-10}"
OWNER="b924-llvmpipe-growth"
RES_W=1280; RES_H=720
ZONE_BOOT_S="${ZONE_BOOT_S:-150}"
EXTRA_ENV=()   # B924: --env K=V ... (e.g. WILLEQ_LEDGER_TRACE=gpu_driver)

while [ $# -gt 0 ]; do
    case "$1" in
        --zone) ZONE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --bin) SRC_BIN="$2"; shift 2 ;;
        --hold) HOLD_S="$2"; shift 2 ;;
        --fps-cap) FPS_CAP="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --env) EXTRA_ENV+=( "$2" ); shift 2 ;;
        -h|--help) sed -n 2,33p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done

: "${OUT:?--out DIR is required}"
mkdir -p "$OUT"
RIG_LOG="$OUT/rig.log"
LOGS="$OUT/server"
mkdir -p "$LOGS"
say() { printf '[b924_hold %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$RIG_LOG" >&2; }

EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
MAPS_PATH="${MAPS_PATH-/mnt/projects/maps/base}"
NAVMESH_PATH="${NAVMESH_PATH-/mnt/projects/maps/nav}"
[ -x "$SRC_BIN" ] || { say "no willeq binary at $SRC_BIN -- build first"; exit 68; }
[ -d "$EQ_PATH" ] || { say "no EQ client files at $EQ_PATH"; exit 68; }
[ -d "$EQE1_PATH" ] || { say "no EQE1 tree at $EQE1_PATH"; exit 68; }
CHAR="${EQ_TEST_CHARACTER}"

# Local-disk copy for setcap (CAP_NET_ADMIN; fails on NFS). The sha is
# recorded so the evidence names the exact bytes that ran.
BIN_DIR="$(mktemp -d /tmp/b924_hold.XXXXXX)"
BIN="$BIN_DIR/willeq"
trap 'rm -rf "$BIN_DIR"' EXIT
cp "$SRC_BIN" "$BIN"
if sudo -n setcap cap_net_raw,cap_net_admin+ep "$BIN" 2>/dev/null; then
    say "capabilities set on $BIN"
else
    say "WARNING: could not setcap $BIN -- running without CAP_NET_ADMIN"
fi

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
      "eq_client_path": "${EQ_PATH}",
      "maps_path": "${MAPS_PATH}",
      "navmesh_path": "${NAVMESH_PATH}"
    }
  ]
}
EOF
if eqemu_is_local; then
    hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" || { say "LANE MISMATCH (local)"; exit 65; }
else
    [ "$CLIENT_HOST" = "$EQEMU_HOST" ] || hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" || {
        say "LANE MISMATCH: config host $CLIENT_HOST is not this lane's server (EQEMU_HOST=$EQEMU_HOST) -- refusing"
        exit 65
    }
fi
say "lane EQEMU_HOST=$EQEMU_HOST, client config host $CLIENT_HOST (verified)"

{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) dirty=$(git -C "$REPO" status --porcelain -- src include | wc -l)"
    echo "binary=$SRC_BIN sha256=$(sha256sum "$SRC_BIN" | cut -d' ' -f1)"
    echo "zone=$ZONE hold_s=$HOLD_S fps_cap=${FPS_CAP:-none} idle_every=$IDLE_EVERY idle_count=$IDLE_COUNT"
    echo "start_utc=$(date -u +%FT%TZ)"
} > "$OUT/run_meta.txt"

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
CURRENT_ZONE=""
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ -n "$CLIENT_PID" ]; then
        kill "$CLIENT_PID" >/dev/null 2>&1
        sleep 2
        kill -9 "$CLIENT_PID" >/dev/null 2>&1
    fi
    [ -n "$CURRENT_ZONE" ] && kill_zone "$CURRENT_ZONE"
    if [ -s "$BEFORE" ]; then
        eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
    else
        say "no snapshot was taken -- nothing to restore"
    fi
    timeout 170 bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/stack_down.log" 2>&1
    eqemu_lease_stop_renewer
    eqemu_lock_release
    rm -rf "$BIN_DIR"
    echo "end_utc=$(date -u +%FT%TZ) restore_bad=$RESTORE_BAD" >> "$OUT/run_meta.txt"
    say "stack down, lease released"
    say "lease-show (FREE = free): '$(eqemu_lock_show)'"
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

# ---- 2. server up + the ONE snapshot -------------------------------------------
say "up.sh"
if ! timeout 175 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/stack_up.log" 2>&1; then
    say "up.sh failed -- see $OUT/stack_up.log"
    exit 65
fi
eqemu_stack_ready || { say "eqemu_stack_ready is false after up.sh"; exit 65; }
eqemu_snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"

# ---- 3. the zone: bounce it and seed the character at its safe point --------
CURRENT_ZONE="$ZONE"
if ! boot_zone "$ZONE"; then
    say "[$ZONE] zone did not boot"
    exit 65
fi
if ! seed_char "$ZONE"; then
    say "[$ZONE] seed failed"
    exit 65
fi

# ---- 4. the hold ----------------------------------------------------------------
DIR="$OUT/$ZONE"
mkdir -p "$DIR"
LOG="$DIR/client.log"
SAMPLES="$DIR/samples.tsv"
TOUR="$DIR/tour.txt"
cat > "$TOUR" <<EOF
# B924: stand still; the ledger samples on its own (idle points every ${IDLE_EVERY} s)
wait ${HOLD_S}
/q
EOF
ENVV=( "WILLEQ_BASELINE=1" "TZ=UTC"
       "WILLEQ_LEDGER_IDLE_EVERY=$IDLE_EVERY" "WILLEQ_LEDGER_IDLE_COUNT=$IDLE_COUNT"
       "WILLEQ_IDLE_MEMMAP=1" )
[ -n "$FPS_CAP" ] && ENVV+=( "WILLEQ_FPS_CAP=$FPS_CAP" )
ENVV+=( "${EXTRA_ENV[@]}" )
ARGS=( -c "$CFG" --renderer gles2 --constrained orangepi -r "$RES_W" "$RES_H"
       --eqe1-path "$EQE1_PATH" --log-level=INFO --script "$TOUR" )
printf '%s\n' "${ENVV[@]}" > "$DIR/env.txt"
printf '%q ' "$BIN" "${ARGS[@]}" > "$DIR/command_line.txt"; echo >> "$DIR/command_line.txt"

say "[$ZONE] launching (hold ${HOLD_S} s, fps_cap=${FPS_CAP:-none})"
START=$(date +%s)
( cd "$REPO" && exec env "${ENVV[@]}" "$BIN" "${ARGS[@]}" ) > "$LOG" 2>&1 &
CLIENT_PID=$!
printf 't_s\trss_anon_kb\tvm_rss_kb\n' > "$SAMPLES"
LAST_SIZE=0 LAST_GROWTH=$START READY=0 KILLED=""
while kill -0 "$CLIENT_PID" 2>/dev/null; do
    sleep 2
    NOW=$(date +%s)
    RA="$(awk '/^RssAnon:/{print $2}' "/proc/$CLIENT_PID/status" 2>/dev/null)"
    VR="$(awk '/^VmRSS:/{print $2}' "/proc/$CLIENT_PID/status" 2>/dev/null)"
    [ -n "$RA" ] && printf '%s\t%s\t%s\n' "$((NOW - START))" "$RA" "$VR" >> "$SAMPLES"
    SIZE=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
    if [ "$SIZE" -ne "$LAST_SIZE" ]; then LAST_SIZE=$SIZE; LAST_GROWTH=$NOW; fi
    if [ "$READY" -eq 0 ] && grep -q '\[memledger\] point=ready ' "$LOG" 2>/dev/null; then
        READY=1; say "[$ZONE] ready after $((NOW - START)) s"
    fi
    if [ "$READY" -eq 0 ] && [ $((NOW - START)) -ge 180 ]; then KILLED="no ready point in 180 s"; fi
    if [ $((NOW - LAST_GROWTH)) -ge 120 ]; then KILLED="log silent for 120 s"; fi
    if [ $((NOW - START)) -ge $((HOLD_S + 90)) ]; then KILLED="hold deadline"; fi
    if [ -n "$KILLED" ]; then
        say "[$ZONE] WATCHDOG: $KILLED -- killing the client"
        kill "$CLIENT_PID" >/dev/null 2>&1; sleep 3; kill -9 "$CLIENT_PID" >/dev/null 2>&1
        break
    fi
done
wait "$CLIENT_PID" 2>/dev/null
RC=$?
CLIENT_PID=""

{
    echo "client_rc=$RC (130 = killed by the hold deadline, expected)"
    grep -q '\[memledger\] point=ready ' "$LOG" && echo "ready_point=yes" || echo "ready_point=no"
    echo "idle_points=$(grep -c '\[memledger\] point=idle_' "$LOG")"
    echo "memmap_rows=$(grep -c '^\[memmap\]' "$LOG" || true)"
    echo "watchdog=${KILLED:-none}"
} > "$DIR/result.txt"
cat "$DIR/result.txt"
say "[$ZONE] done: $(grep -c '\[memledger\] point=' "$LOG") ledger points, $(grep -c '^\[memmap\]' "$LOG" || true) memmap rows"
exit 0
