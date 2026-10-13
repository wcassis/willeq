#!/usr/bin/env bash
# B909 / GitLab #229 -- the CLIENT binary's own memory ledger, measured live.
#
# Every memory number the gate produces comes from gles2_zone_benchmark, not
# from willeq. This rig runs the RENDERED client (surfaceless EGL on
# llvmpipe, Will's Pi recipe `--renderer gles2 --constrained orangepi -r 1280
# 720`, WILLEQ_BASELINE=1 per the 2026-09-21 feature-parity ruling) zoned into
# each requested zone, lets it sit past `steady_60s`, quits it, and keeps the
# whole client log. The lifecycle points ([memledger] zone_load_complete,
# ready, steady_60s ...) are the CLIENT's own output; the table is made from
# them by scripts/client_ledger_table.py and never by hand.
#
# Driving a rendered client: `--script` (H03/T29), exactly as
# scripts/pose_live_capture.sh does -- `--scenario` forces the headless
# Automated lane, which never builds the render pipeline and so never runs
# the zone load being measured. The script's lines are held until the client
# is fully zoned in.
#
# One run per (zone, mode). A mode picks the ONE category whose owner rows the
# client prints ([memledger-startup] lines, WILLEQ_LEDGER_OWNERS=<category>):
#   spatial, zone_mesh  -- the B909 labels (sp.*, zm.*) name the owners
#   objects             -- the B936 labels (obj.meshes, obj.instances, obj.anim)
#   game_state          -- the B941 labels (gs.navmesh, gs.nav_query,
#                          gs.collision_map, gs.zone_lines, gs.zone_sources;
#                          B951 adds gs.collision_map.{verts,tris,refs,
#                          nodes,leaves,flags}, the raycast mesh arrays).
#                          B945 also sets
#                          WILLEQ_LEDGER_TRACE=game_state:login_world and the
#                          [memledger-trace] rows split the misc row by call
#                          site (the B903 method; resolve with --bin).
#   audio               -- the B977 labels (au.soundfont_gm, au.soundfont_custom,
#                          au.sound_buffers, au.music, au.mixer_sfx,
#                          au.backend, au.pfs_index, au.zone_emitters;
#                          B989 removed au.sfx_cache: the sfx cache stopped
#                          copying the decoded PCM (B1015 then deleted the
#                          sfx LRU entirely -- the one cache is the
#                          AudioManager's SoundBufferCache); run it
#                          WITHOUT --no-audio, or the row is empty
#   zone_parse          -- the shared S3D/WLD parser carries no labels, so this
#                          mode ALSO sets WILLEQ_LEDGER_TRACE=zone_parse:startup
#                          and the [memledger-trace] rows split it by call site
#                          (the B903 method); resolve them with --bin.
# The category totals are on every run's [memledger] lines, so each zone gets
# one independent measurement per mode (run-to-run agreement is checked by the
# table script).
#
# Placement is scripts/eqemu/zone_place_lib.sh (boot_zone/seed_char/kill_zone,
# the zone-in sweep's own helpers); the lock, the ONE snapshot and the ONE
# restore-with-the-world-down are the lane's (restore_lib.sh). Every exit path
# restores, brings the stack down, releases the lock and prints the lock state
# LAST.
#
# Watchdog (no step waits >180 s without progress): the client is killed if it
# dies, if it has not reached `ready` 180 s after launch, if its log does not
# grow for 120 s, or 330 s overall. RssAnon is also sampled from
# /proc/<pid>/status every 2 s (rss_samples.tsv), so the peak is not only
# the lifecycle points'.
#
# Exit: 0 every run reached steady_60s; 1 a run did not; 3 bad usage;
# 64 lock held; 65 the stack would not come up / lane mismatch; 66 the
# snapshot did not restore; 68 bad inputs (no binary, no EQ files).
#
# Usage: scripts/client_ledger_capture.sh [--zones z1,z2] [--modes m1,m2]
#            [--out DIR] [--owner NAME] [--bin PATH] [--hold S] [--no-audio]
#            [--res W H]   (default 1280 720, the Pi recipe)
#            [--assert-budgets]  also exit 1 if any budgeted category of any
#                                run is over its orangepi budget at ready or
#                                steady_60s (the `ClientLedgerLive` ctest)
#            [--tour FILE]  the client --script (default: wait HOLD, /q)
#            [--client-arg ARG]...  extra client arguments (B941 used
#                                --log-module=PATHFIND:DEBUG)
#            [--char-level N --char-exp E]  the eligibility leg of
#                                scripts/pi_rendered_zone_sweep.sh: set the
#                                character's level/exp after the snapshot, so a
#                                min-level zone (growthplane, min 46) does not
#                                kick it; the snapshot restore puts both back
#                                (B909b)
#   Lane: EQEMU_HOST as every scripts/eqemu tool reads it (default the VM).
#   MAPS_PATH / NAVMESH_PATH: the collision map and navmesh roots (default
#   /mnt/projects/maps/base and /mnt/projects/maps/nav, the Pi recipe's; set
#   to "" to run without them, as every run before B941 did).

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/eqemu/zone_place_lib.sh"
export EQEMU_HOST

ZONES="gfaydark,dreadlands,iceclad,timorous"
MODES="spatial,zone_mesh,zone_parse"
OWNER="b909-client-ledger"
OUT=""
SRC_BIN="${WILLEQ_BIN:-$REPO/build/bin/willeq}"
HOLD_S=75          # seconds after zone-in before /q: ready arms steady_60s
AUDIO=1
ASSERT=0   # --assert-budgets: the live ctest mode (client_ledger_table.py check)
RES_W=1280; RES_H=720   # Will's Pi recipe
CHAR_LEVEL=""; CHAR_EXP=""   # B909b: the eligibility leg (see --char-level)
TOUR_FILE=""; EXTRA_ARGS=()  # B941: --tour / --client-arg (a live pathing check)
ZONE_BOOT_S="${ZONE_BOOT_S:-150}"
LATE_SAVE_S=10
while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --modes) MODES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --bin) SRC_BIN="$2"; shift 2 ;;
        --hold) HOLD_S="$2"; shift 2 ;;
        --no-audio) AUDIO=0; shift ;;
        --assert-budgets) ASSERT=1; shift ;;
        --res) RES_W="$2"; RES_H="$3"; shift 3 ;;
        --char-level) CHAR_LEVEL="$2"; shift 2 ;;
        --char-exp) CHAR_EXP="$2"; shift 2 ;;
        --tour) TOUR_FILE="$2"; shift 2 ;;
        --client-arg) EXTRA_ARGS+=( "$2" ); shift 2 ;;
        -h|--help) sed -n '2,56p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done
for m in ${MODES//,/ }; do
    case "$m" in spatial|zone_mesh|zone_parse|objects|game_state|audio) ;; *) echo "unknown mode: $m" >&2; exit 3 ;; esac
done

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B909/run-${STAMP}"
mkdir -p "$OUT"
LOGS="$OUT/server"
mkdir -p "$LOGS"
RIG_LOG="$OUT/rig.log"
say() { printf '[client_ledger %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$RIG_LOG" >&2; }

EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
# B941: the collision map and the navmesh, exactly as the Pi recipe
# (scripts/pi_rendered_zone_sweep.sh) loads them. Before B941 this rig's config
# named neither, so every x86 client ledger ran WITHOUT them and its
# `game_state` row was ~0.3 MB where the Pi's was up to 54 MB.
MAPS_PATH="${MAPS_PATH-/mnt/projects/maps/base}"
NAVMESH_PATH="${NAVMESH_PATH-/mnt/projects/maps/nav}"
[ -x "$SRC_BIN" ] || { say "no willeq binary at $SRC_BIN -- build first"; exit 68; }
[ -d "$EQ_PATH" ] || { say "no EQ client files at $EQ_PATH"; exit 68; }
[ -d "$EQE1_PATH" ] || { say "no EQE1 tree at $EQE1_PATH"; exit 68; }
CHAR="${EQ_TEST_CHARACTER}"

# Local-disk copy for setcap (CAP_NET_ADMIN: the 1 MB UDP receive buffer; it
# fails on NFS). The sha of the copy is recorded, so the evidence names the
# exact bytes that ran.
BIN_DIR="$(mktemp -d /tmp/client_ledger.XXXXXX)"
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
lane_ok=0
if eqemu_is_local; then
    hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" && lane_ok=1
else
    [ "$CLIENT_HOST" = "$EQEMU_HOST" ] && lane_ok=1
    hostname -I | tr ' ' '\n' | grep -qx "$CLIENT_HOST" && lane_ok=0
fi
if [ "$lane_ok" -ne 1 ]; then
    say "LANE MISMATCH: config host $CLIENT_HOST is not this lane's server (EQEMU_HOST=$EQEMU_HOST) -- refusing"
    exit 65
fi
say "lane EQEMU_HOST=$EQEMU_HOST, client config host $CLIENT_HOST (verified)"

{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) dirty=$(git -C "$REPO" status --porcelain -- src include | wc -l)"
    echo "binary=$SRC_BIN sha256=$(sha256sum "$SRC_BIN" | cut -d' ' -f1)"
    echo "lane=$EQEMU_HOST zones=$ZONES modes=$MODES hold_s=$HOLD_S audio=$AUDIO res=${RES_W}x${RES_H} maps=${MAPS_PATH} navmesh=${NAVMESH_PATH}"
    echo "start_utc=$(date -u +%FT%TZ)"
} > "$OUT/run_meta.txt"

# ---- 1. lock ------------------------------------------------------------------
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
    eqemu_lock_release
    rm -rf "$BIN_DIR"
    echo "end_utc=$(date -u +%FT%TZ) restore_bad=$RESTORE_BAD" >> "$OUT/run_meta.txt"
    say "stack down, lock released"
    say "lock-show (empty = free): '$(eqemu_lock_show)'"
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
if [ -n "$CHAR_LEVEL" ]; then
    # Same statement as scripts/pi_rendered_zone_sweep.sh's eligibility leg;
    # level and exp are snapshot columns, so the restore puts them back.
    eqemu_sql "UPDATE character_data SET level=${CHAR_LEVEL}${CHAR_EXP:+, exp=${CHAR_EXP}} WHERE name='${CHAR}';" >/dev/null
    got_level="$(eqemu_sql "SELECT level FROM character_data WHERE name='${CHAR}';" | tail -1 | tr -d '[:space:]')"
    say "ELIGIBILITY LEG: ${CHAR} level set to ${got_level} (asked ${CHAR_LEVEL}/${CHAR_EXP:-exp unchanged}); the snapshot restore puts it back"
    echo "eligibility_leg_char_level=${got_level} exp=${CHAR_EXP:-unchanged}" >> "$OUT/run_meta.txt"
    [ "$got_level" = "$CHAR_LEVEL" ] || { say "level did not read back"; exit 65; }
fi

# ---- 3. one rendered run --------------------------------------------------------
# run_one <zone> <mode>: writes $OUT/<zone>/<mode>/{client.log,rss_samples.tsv,
# tour.txt,command_line.txt,env.txt,result.txt}. Returns 0 iff steady_60s was
# recorded for that zone.
run_one() {
    local zone="$1" mode="$2" dir="$OUT/$1/$2"
    mkdir -p "$dir"
    local log="$dir/client.log" samples="$dir/rss_samples.tsv" tour="$dir/tour.txt"
    if [ -n "$TOUR_FILE" ]; then
        cp "$TOUR_FILE" "$tour"
    else
        cat > "$tour" <<EOF
# held until fully zoned in (Application::updateScript), then idle past steady_60s
wait ${HOLD_S}
/q
EOF
    fi
    local -a envs=( "WILLEQ_BASELINE=1" "WILLEQ_LEDGER_OWNERS=${mode}" "TZ=UTC"
                    "WILLEQ_LEDGER_IDLE_EVERY=${WILLEQ_LEDGER_IDLE_EVERY:-10}" )
    [ "$mode" = zone_parse ] && envs+=( "WILLEQ_LEDGER_TRACE=zone_parse:startup" "WILLEQ_LEDGER_TRACE_DEPTH=8" )
    # B945: the game_state misc row is site-split the same way the issue's
    # trace recipe names it -- every allocation of the category from the
    # zone-in handshake (login_world) onward, backtraced.
    [ "$mode" = game_state ] && envs+=( "WILLEQ_LEDGER_TRACE=game_state:login_world" "WILLEQ_LEDGER_TRACE_DEPTH=8" )
    local -a args=( -c "$CFG" --renderer gles2 --constrained orangepi -r "$RES_W" "$RES_H"
                    --eqe1-path "$EQE1_PATH" --log-level=INFO --script "$tour" )
    [ "$AUDIO" -eq 1 ] || args+=( --no-audio )
    [ "${#EXTRA_ARGS[@]}" -gt 0 ] && args+=( "${EXTRA_ARGS[@]}" )
    printf '%s\n' "${envs[@]}" > "$dir/env.txt"
    printf '%q ' "$BIN" "${args[@]}" > "$dir/command_line.txt"; echo >> "$dir/command_line.txt"
    say "[$zone/$mode] launching"
    local start; start=$(date +%s)
    ( cd "$REPO" && exec env "${envs[@]}" "$BIN" "${args[@]}" ) > "$log" 2>&1 &
    CLIENT_PID=$!
    printf 't_s\trss_anon_kb\tvm_rss_kb\n' > "$samples"
    local last_size=0 last_growth=$start now size killed="" ready=0 ra vr
    while kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 2
        now=$(date +%s)
        ra="$(awk '/^RssAnon:/{print $2}' "/proc/$CLIENT_PID/status" 2>/dev/null)"
        vr="$(awk '/^VmRSS:/{print $2}' "/proc/$CLIENT_PID/status" 2>/dev/null)"
        [ -n "$ra" ] && printf '%s\t%s\t%s\n' "$((now - start))" "$ra" "$vr" >> "$samples"
        size=$(stat -c %s "$log" 2>/dev/null || echo 0)
        if [ "$size" -ne "$last_size" ]; then last_size=$size; last_growth=$now; fi
        if [ "$ready" -eq 0 ] && grep -q '\[memledger\] point=ready ' "$log" 2>/dev/null; then
            ready=1; say "[$zone/$mode] ready after $((now - start)) s"
        fi
        if [ "$ready" -eq 0 ] && [ $((now - start)) -ge 180 ]; then killed="no ready point in 180 s"; fi
        if [ $((now - last_growth)) -ge 120 ]; then killed="log silent for 120 s"; fi
        if [ $((now - start)) -ge 330 ]; then killed="330 s overall deadline"; fi
        if [ -n "$killed" ]; then
            say "[$zone/$mode] WATCHDOG: $killed -- killing the client"
            kill "$CLIENT_PID" 2>/dev/null; sleep 3; kill -9 "$CLIENT_PID" 2>/dev/null
            break
        fi
    done
    wait "$CLIENT_PID" 2>/dev/null
    local rc=$?
    CLIENT_PID=""
    local steady=0
    grep -q '\[memledger\] point=steady_60s ' "$log" && steady=1
    local landed=n
    grep -q "LoadZoneSources: Loaded zone '${zone}'" "$log" && landed=y
    # The zone link dropping mid-run (measured: the iceclad zone port refusing
    # ~50 s after zone-in, B909) leaves `/q` held "waiting for connection"
    # until the 330 s deadline. Recorded so a steady_60s taken while the
    # client was reconnecting is visible in the table, not silent.
    local lost
    lost="$(grep -m1 'Zone connection lost' "$log" | sed -E 's/^\[([^]]*)\].*/\1/' | tr ' ' 'T')"
    printf 'zone=%s mode=%s rc=%s wall_s=%s ready=%s steady_60s=%s landed=%s watchdog=%s zone_link_lost=%s start_utc=%s\n' \
        "$zone" "$mode" "$rc" "$(( $(date +%s) - start ))" "$ready" "$steady" "$landed" "${killed:-none}" "${lost:-no}" \
        "$(date -u -d @"$start" +%FT%TZ)" > "$dir/result.txt"
    say "[$zone/$mode] $(cat "$dir/result.txt")"
    [ "$steady" -eq 1 ] && [ "$landed" = y ]
}

FAILS=0
for zone in ${ZONES//,/ }; do
    for mode in ${MODES//,/ }; do
        CURRENT_ZONE="$zone"
        if ! boot_zone "$zone"; then
            say "[$zone/$mode] zone did not boot"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue
        fi
        if ! seed_char "$zone"; then
            say "[$zone/$mode] seed failed"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue
        fi
        run_one "$zone" "$mode" || FAILS=$((FAILS + 1))
        # The zone writes the linkdead character back; let it land before the
        # zone process is stopped (the sweep's late-save rule).
        sleep "$LATE_SAVE_S"
        kill_zone "$zone"
        CURRENT_ZONE=""
    done
done

say "runs failed: $FAILS (evidence in $OUT)"
CHECK_RC=0
if [ "$ASSERT" -eq 1 ]; then
    python3 "$REPO/scripts/client_ledger_table.py" check --run "$OUT" 2>&1 | tee -a "$RIG_LOG"
    CHECK_RC=${PIPESTATUS[0]}
fi
cleanup   # prints the lock state as its last line
[ "$RESTORE_BAD" -eq 1 ] && exit 66
[ "$FAILS" -eq 0 ] || exit 1
exit "$CHECK_RC"
