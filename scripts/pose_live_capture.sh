#!/usr/bin/env bash
# B874 / GitLab #218 -- the FIRST pose log from a ZONED-IN client, as a
# repeatable live test.
#
# `--pose-log` writes one 14-column TSV row per RENDERED frame
# (include/client/graphics/pose_log_format.h), and since MR !38 it is refused
# in any run without a renderer -- which rules out `--scenario` (it forces the
# headless Automated lane). The supported way to DRIVE a RENDERED client is
# `--script` (H03/T29: "wait N" + slash-command lines through the same unified
# dispatch as stdin), on the default surfaceless-EGL llvmpipe device -- the
# route scripts/camera_match_sweep.sh already takes. No new client mode.
#
# What it does, in order (every step has a deadline; none waits >120 s
# without progress):
#   1. lock (atomic, owner `pose-live-capture` or --owner; held lock = exit 64)
#   2. up.sh, then snapshot the character rows (restore_lib.sh)
#   3. WORLD DOWN, seed the commanded pose into character_data, read it back,
#      world up again, boot the pose's zone
#   4. run the rendered client: --script (camera chase, hold still, /camera
#      get, /turn, /move, /q) + --pose-log --pose-every 1 --pose-max-frames N,
#      under a dead-client / stall watchdog
#   5. WORLD DOWN (the zone writes the character back on shutdown), read the
#      server's final view of the character
#   6. scripts/pose_live_check.py: the log against the seed, the server's
#      final row and kChaseRig -- the verdict
#   7. restore the snapshot with the world down, down.sh, release the lock
#      (trap: runs on every exit path)
#
# Exit codes: 0 all checks passed; 1 a check failed; 3 the client never wrote
# the pose log (never zoned in / died / stalled); 64 lock held or bad option;
# 65 the server would not come up / the pose would not seed / lane mismatch;
# 66 the snapshot did not restore.
#
# Usage: scripts/pose_live_capture.sh [--out DIR] [--max-rows N] [--owner NAME]
#   The lane is EQEMU_HOST, exactly as every other scripts/eqemu/ tool reads
#   it (B911/B912 generalised this; it used to force `local`): unset means
#   env.sh's default, the eqemu VM; `EQEMU_HOST=local` selects a stack on this
#   machine. The client config is DERIVED from the selected lane and its host
#   is checked against that lane's address before anything is launched -- a
#   past run pointed a client at the wrong lane by accident
#   (feedback-lane-safe-defaults): local = one of this machine's own
#   addresses, VM = EQEMU_HOST itself and NOT an address of this machine.
#   --owner names the eqemu lock holder (default pose-live-capture).

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
export EQEMU_HOST

OWNER="pose-live-capture"
CHAR="${EQ_TEST_CHARACTER}"          # Testchar (willeq_test, GM status)
# The commanded pose, SERVER frame (character_data units: x, y, z, heading in
# EQ 0-512 units -- zone/client_packet.cpp stores EQ12toFloat(ppu->heading) =
# raw/4, and client.cpp Save copies m_Position.w into m_pp.heading).
# qeynos2's own safe point (zone.safe_x/y/z) with a non-symmetric heading, so
# an X/Y swap or a heading flip shows as a number, not a coincidence.
ZONE_ID=2
ZONE_SHORT="qeynos2"
SEED_X="-74"; SEED_Y="428"; SEED_Z="3"; SEED_H="100"
# The scripted walk, CLIENT frame (/move's documented frame): 15 units along
# client +X from the seed (client x = server y).
MOVE_TO="443 -74 3"
MAX_ROWS=600
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --max-rows) MAX_ROWS="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B874-live/run-${STAMP}"
mkdir -p "$OUT"
LOG="$OUT/client.log"
POSE="$OUT/pose.tsv"
SAY_LOG="$OUT/rig.log"
say() { printf '[pose_live_capture %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$SAY_LOG" >&2; }

EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
SRC_BIN="${WILLEQ_BIN:-$REPO/build/bin/willeq}"
[ -x "$SRC_BIN" ] || { say "no willeq binary at $SRC_BIN -- build first"; exit 64; }
[ -d "$EQ_PATH" ] || { say "no EQ client files at $EQ_PATH"; exit 64; }

# CAP_NET_ADMIN lets the client force a 1 MB UDP receive buffer; without it
# a zone-in burst can drop packets (CLAUDE.md "Linux Capabilities"). setcap
# needs a LOCAL-disk copy (it fails on the NFS review tree); if sudo is not
# available the run goes ahead without, and says so.
BIN_DIR="$(mktemp -d /tmp/pose_live_capture.XXXXXX)"
BIN="$BIN_DIR/willeq"
trap 'rm -rf "$BIN_DIR"' EXIT   # replaced by cleanup() once the lock is ours
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
      "eq_client_path": "${EQ_PATH}"
    }
  ]
}
EOF
cfg_host="$(sed -n 's/.*"host"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CFG" | head -1)"
lane_ok=0
if eqemu_is_local; then
    hostname -I | tr ' ' '\n' | grep -qx "$cfg_host" && lane_ok=1
    [ "$cfg_host" = 127.0.0.1 ] && lane_ok=1
else
    # The VM lane: the config must name EQEMU_HOST itself, and EQEMU_HOST must
    # not be one of THIS machine's addresses (that would be the local lane
    # under another name, and a local stack may belong to another session).
    [ "$cfg_host" = "$EQEMU_HOST" ] && lane_ok=1
    if hostname -I | tr ' ' '\n' | grep -qx "$cfg_host" || [ "$cfg_host" = 127.0.0.1 ]; then
        lane_ok=0
    fi
fi
if [ "$lane_ok" -ne 1 ]; then
    say "LANE MISMATCH: config host $cfg_host is not this lane's server (EQEMU_HOST=$EQEMU_HOST) -- refusing"
    exit 65
fi
say "lane EQEMU_HOST=$EQEMU_HOST, client config host $cfg_host (verified)"

# ---- 1. lock -------------------------------------------------------------------
if ! eqemu_lock_take "$OWNER"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lock taken: $(eqemu_lock_show)"

BEFORE="$OUT/snapshot_before.tsv"
AFTER="$OUT/snapshot_after.tsv"
CLIENT_PID=""
CLEANED=0
RESTORE_BAD=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ -n "$CLIENT_PID" ]; then
        kill "$CLIENT_PID" >/dev/null 2>&1
        sleep 2
        kill -9 "$CLIENT_PID" >/dev/null 2>&1
    fi
    eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
    bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/server.log" 2>&1
    eqemu_lock_release
    rm -rf "$BIN_DIR"
    say "stack down, lock released; lock now: '$(eqemu_lock_show)'"
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

char_row() {
    eqemu_sql "SELECT zone_id,x,y,z,heading FROM character_data WHERE name='${CHAR}';" | head -1
}

# ---- 2. server up + snapshot ------------------------------------------------
say "up.sh"
if ! timeout 200 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/server.log" 2>&1; then
    say "up.sh failed -- see $OUT/server.log"
    exit 65
fi
eqemu_snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"

# ---- 3. seed the commanded pose with the WORLD DOWN ---------------------------
# A zone that holds the character in memory (linkdead) writes it back when it
# shuts down, over any row written while it runs (T493/B784), and resumes
# that cached entity on the next login (camera_match_sweep.sh's second trap).
eqemu_world_stop
eqemu_sql "UPDATE character_data SET zone_id=${ZONE_ID}, x=${SEED_X}, y=${SEED_Y}, z=${SEED_Z}, heading=${SEED_H} WHERE name='${CHAR}';" >/dev/null
SEED_ROW="$(char_row)"
WANT_ROW="$(printf '%s\t%s\t%s\t%s\t%s' "$ZONE_ID" "$SEED_X" "$SEED_Y" "$SEED_Z" "$SEED_H")"
if [ "$SEED_ROW" != "$WANT_ROW" ]; then
    say "seed did not read back: db='$SEED_ROW' want='$WANT_ROW'"
    exit 65
fi
printf 'zone_id\tx\ty\tz\theading\n%s\n' "$SEED_ROW" > "$OUT/server_seed.tsv"
say "seed read back from character_data: $SEED_ROW"
eqemu_world_start
if ! timeout 200 bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/server.log" 2>&1; then
    say "up.sh (after seed) failed -- see $OUT/server.log"
    exit 65
fi
if [ "$(server_process_count "zone ${ZONE_SHORT}")" = "0" ]; then
    server_exec_bg "./bin/zone ${ZONE_SHORT} > logs/zone_${ZONE_SHORT}_posecap.log 2>&1"
    if ! eqemu_zone_booted "$ZONE_SHORT" "logs/zone_${ZONE_SHORT}_posecap.log" 90; then
        say "zone $ZONE_SHORT did not boot"
        exit 65
    fi
fi
say "zone $ZONE_SHORT up"

# ---- 4. the rendered, scripted client ------------------------------------------
TOUR="$OUT/tour.txt"
# Timed to fit the row cap: the client renders ~50 fps here, so 600 rows at
# --pose-every 1 cover ~12 s from the first gameplay frame. `#loc` is the
# SERVER's live view of the character (zone/gm_commands/loc.cpp prints
# GetPosition(): x, y, z and heading in 0-512 units); Testchar is GM status.
# /camera chase is sent TWICE: the first, held until zone-in, is applied and
# then overridden to First Person at zone-in (run-20260929T021344Z's log:
# "Camera mode: Chase Camera" at 02:14:54.545, `/camera get` 5 s later reads
# "First Person Camera", and all 600 rows say First Person).
# `#save` (self-targeted; zone/gm_commands/save.cpp -> Client::Save(2)) is
# there because NOTHING ELSE wrote the moved pose to character_data in
# run-20260929T021741Z: not the /q disconnect, not the container stop.
cat > "$TOUR" <<EOF
# held until fully zoned in (Application::updateScript), then:
/camera chase
wait 1.5
/camera chase
wait 0.5
/say #loc
wait 2.5
/camera get
wait 0.3
/turn 90
wait 1
/say #loc
wait 0.5
/move ${MOVE_TO}
wait 3
/say #loc
wait 0.5
/camera get
wait 0.3
/target ${CHAR}
wait 0.3
/say #save
wait 1
/q
EOF
ARGS=( -c "$CFG" --renderer gles2 -r 800 600 --no-audio
       --eqe1-path "$EQE1_PATH" --log-level=INFO
       --script "$TOUR"
       --pose-log "$POSE" --pose-every 1 --pose-max-frames "$MAX_ROWS" )
printf '%q ' "$BIN" "${ARGS[@]}" > "$OUT/command_line.txt"
echo >> "$OUT/command_line.txt"
say "launching: $(cat "$OUT/command_line.txt")"
START_MS="$(date +%s%3N)"
( cd "$REPO" && TZ=UTC exec "$BIN" "${ARGS[@]}" ) > "$LOG" 2>&1 &
CLIENT_PID=$!

# Watchdog: dead client, no pose-log open inside 120 s, no log/pose growth
# for 60 s, or 300 s overall -> kill.
t0=$(date +%s); last_size=0; last_growth=$t0; OPENED=0; KILLED=""
while kill -0 "$CLIENT_PID" 2>/dev/null; do
    sleep 2
    now=$(date +%s)
    size=$(( $(stat -c %s "$LOG" 2>/dev/null || echo 0) + $(stat -c %s "$POSE" 2>/dev/null || echo 0) ))
    if [ "$size" -ne "$last_size" ]; then last_size=$size; last_growth=$now; fi
    if [ "$OPENED" -eq 0 ] && grep -q 'pose-log: writing' "$LOG" 2>/dev/null; then
        OPENED=1; say "pose log opened after $((now - t0)) s"
    fi
    if [ "$OPENED" -eq 0 ] && [ $((now - t0)) -ge 120 ]; then KILLED="no pose-log open in 120 s"; fi
    if [ $((now - last_growth)) -ge 60 ]; then KILLED="no progress for 60 s"; fi
    if [ $((now - t0)) -ge 300 ]; then KILLED="300 s overall deadline"; fi
    if [ -n "$KILLED" ]; then
        say "WATCHDOG: $KILLED -- killing the client"
        kill "$CLIENT_PID" 2>/dev/null; sleep 3; kill -9 "$CLIENT_PID" 2>/dev/null
        break
    fi
done
wait "$CLIENT_PID" 2>/dev/null
CLIENT_RC=$?
CLIENT_PID=""
END_MS="$(date +%s%3N)"
say "client exit $CLIENT_RC after $(( (END_MS - START_MS) / 1000 )) s${KILLED:+ (watchdog: $KILLED)}"

# ---- 5. the server's final view -----------------------------------------------
# Read twice: straight after the client left (world still up), and after
# stopping the container, which SIGTERMs the zone -- the documented moment it
# saves the (linkdead) character's in-memory position (T493/B784).
QUIT_ROW="$(char_row)"
printf 'zone_id\tx\ty\tz\theading\n%s\n' "$QUIT_ROW" > "$OUT/server_after_quit.tsv"
say "server after quit (character_data, world still up): $QUIT_ROW"
eqemu_world_stop
FINAL_ROW="$(char_row)"
printf 'zone_id\tx\ty\tz\theading\n%s\n' "$FINAL_ROW" > "$OUT/server_final.tsv"
say "server final (character_data after zone shutdown save): $FINAL_ROW"

# ---- 6. the verdict -------------------------------------------------------------
VERDICT=0
if ! grep -q 'pose-log: writing' "$LOG" || [ ! -s "$POSE" ]; then
    say "the client never opened the pose log -- no evidence"
    VERDICT=3
else
    python3 "$REPO/scripts/pose_live_check.py" \
        --pose "$POSE" --client-log "$LOG" \
        --seed "$SEED_ROW" --final "$FINAL_ROW" --after-quit "$QUIT_ROW" \
        --run-start-ms "$START_MS" --run-end-ms "$END_MS" \
        --out "$OUT/comparison.tsv" 2>&1 | tee -a "$SAY_LOG"
    VERDICT=${PIPESTATUS[0]}
    [ "$VERDICT" -eq 0 ] || VERDICT=1
fi
{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) lane=$EQEMU_HOST"
    echo "run_start_utc=$(date -u -d @$((START_MS / 1000)) +%FT%TZ) run_end_utc=$(date -u -d @$((END_MS / 1000)) +%FT%TZ)"
    echo "client_rc=$CLIENT_RC verdict=$VERDICT"
    [ -s "$POSE" ] && echo "pose_rows=$(( $(wc -l < "$POSE") - 1 )) pose_sha256=$(sha256sum "$POSE" | cut -d' ' -f1)"
} > "$OUT/run_meta.txt"

# ---- 7. teardown (also the trap) ------------------------------------------------
cleanup
[ "$RESTORE_BAD" -eq 1 ] && { say "restore mismatch overrides the verdict"; exit 66; }
say "verdict $VERDICT (evidence in $OUT)"
exit "$VERDICT"
