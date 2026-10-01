#!/usr/bin/env bash
# B964 (GitLab #282): x86 rendered-client runs for the impostor port --
# the client on the orangepi preset (Will's Pi recipe, surfaceless llvmpipe),
# zoned into each zone, with a GM-spawned crowd of player-race NPCs
# (scripts/b964/make_tour.py), idle then moving, then /q. The client's own
# log carries everything the tables are made from: [impostor] (tier counts,
# pages, creation/refresh counts, unit times), [impostor-frames] (frame-time
# p50/p95/max per 5 s window), [memledger] / [memgpu] / [memledger-owner]
# owner=tex.impostor at every lifecycle point. scripts/b964/crowd_table.py
# folds them.
#
# A variant is <mode><N><layout>:
#   mode   imp  = impostors on (the preset's 20 MiB budget)
#          off  = WILLEQ_IMPOSTORS=0 (the feature alone off -- the A/B)
#          base = WILLEQ_BASELINE=1 (the original-client feature set)
#   N      crowd size (0 = the zone's own population only)
#   layout near (10-60 units) | far (100-160 units); absent when N is 0
# e.g. imp0, imp50far, imp200far, imp200near, off200far, base0.
#
# Every (zone, variant) boots a FRESH zone process (the spawned NPCs live in
# the zone process) and re-seeds the character at the safe point. One lock,
# one snapshot, one restore with the world DOWN (restore_lib.sh); every exit
# path restores, brings the stack down and releases the lock, lock state last.
#
# Usage: scripts/b964/crowd_run.sh [--zones z1,z2] [--variants v1,v2]
#          [--out DIR] [--owner NAME] [--bin PATH] [--res W H]
#          [--char-level N --char-exp E]   (growthplane: min level 46)
# Lane: EQEMU_HOST (scripts/eqemu/env.sh), default local for this rig.
# Exit: 0 every run reached steady_60s; 1 a run did not; 3 usage; 64 lock
#       held; 65 stack would not come up; 66 snapshot did not restore.

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export EQEMU_HOST="${EQEMU_HOST:-local}"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/eqemu/zone_place_lib.sh"

ZONES="qeynos2,gfaydark,growthplane"
VARIANTS="imp0,imp50far,imp200far,imp200near,off200far,base0"
OWNER="B964"
OUT=""
SRC_BIN="$REPO/build/bin/willeq"
RES_W=1280; RES_H=720
CHAR_LEVEL=""; CHAR_EXP=""
ZONE_BOOT_S="${ZONE_BOOT_S:-150}"
LATE_SAVE_S=10
while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --variants) VARIANTS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --bin) SRC_BIN="$2"; shift 2 ;;
        --res) RES_W="$2"; RES_H="$3"; shift 3 ;;
        --char-level) CHAR_LEVEL="$2"; shift 2 ;;
        --char-exp) CHAR_EXP="$2"; shift 2 ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B964/x86/crowd-${STAMP}"
LOGS="$OUT/server"
mkdir -p "$LOGS"
RIG_LOG="$OUT/rig.log"
say() { printf '[b964_crowd %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$RIG_LOG" >&2; }
EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
CHAR="${EQ_TEST_CHARACTER}"
[ -x "$SRC_BIN" ] || { say "no willeq at $SRC_BIN"; exit 3; }

BIN_DIR="$(mktemp -d /tmp/b964_crowd.XXXXXX)"
BIN="$BIN_DIR/willeq"
cp "$SRC_BIN" "$BIN"
sudo -n setcap cap_net_raw,cap_net_admin+ep "$BIN" 2>/dev/null || say "WARNING: no setcap"

if eqemu_is_local; then CLIENT_HOST="$(hostname -I | awk '{print $1}')"; else CLIENT_HOST="$(eqemu_port_host)"; fi
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
      "maps_path": "/mnt/projects/maps/base",
      "navmesh_path": "/mnt/projects/maps/nav"
    }
  ]
}
EOF
{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) dirty=$(git -C "$REPO" status --porcelain -- src include | wc -l)"
    echo "binary=$SRC_BIN sha256=$(sha256sum "$SRC_BIN" | cut -d' ' -f1)"
    echo "lane=$EQEMU_HOST client_host=$CLIENT_HOST zones=$ZONES variants=$VARIANTS res=${RES_W}x${RES_H}"
    echo "start_utc=$(date -u +%FT%TZ) loadavg=$(cut -d' ' -f1-3 /proc/loadavg) nproc=$(nproc)"
} > "$OUT/run_meta.txt"

if ! eqemu_lock_take "$OWNER"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    rm -rf "$BIN_DIR"
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
    if [ -n "$CLIENT_PID" ]; then kill "$CLIENT_PID" >/dev/null 2>&1; sleep 2; kill -9 "$CLIENT_PID" >/dev/null 2>&1; fi
    [ -n "$CURRENT_ZONE" ] && kill_zone "$CURRENT_ZONE"
    if [ -s "$BEFORE" ]; then eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1; fi
    timeout 170 bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/stack_down.log" 2>&1
    eqemu_lock_release
    rm -rf "$BIN_DIR"
    echo "end_utc=$(date -u +%FT%TZ) restore_bad=$RESTORE_BAD" >> "$OUT/run_meta.txt"
    say "stack down, lock released; lock-show (empty = free): '$(eqemu_lock_show)'"
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

say "up.sh"
if ! timeout 175 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/stack_up.log" 2>&1; then say "up.sh failed"; exit 65; fi
eqemu_stack_ready || { say "eqemu_stack_ready is false after up.sh"; exit 65; }
eqemu_snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"
if [ -n "$CHAR_LEVEL" ]; then
    eqemu_sql "UPDATE character_data SET level=${CHAR_LEVEL}${CHAR_EXP:+, exp=${CHAR_EXP}} WHERE name='${CHAR}';" >/dev/null
    say "eligibility leg: level $(eqemu_sql "SELECT level FROM character_data WHERE name='${CHAR}';" | tail -1) (restored by the snapshot)"
fi

run_one() {  # run_one <zone> <variant>
    local zone="$1" variant="$2" dir="$OUT/$1/$2"
    mkdir -p "$dir"
    local mode n layout
    mode="$(sed -E 's/^([a-z]+)[0-9].*$/\1/' <<< "$variant")"
    n="$(sed -E 's/^[a-z]+([0-9]+).*$/\1/' <<< "$variant")"
    layout="$(sed -E 's/^[a-z]+[0-9]+//' <<< "$variant")"; [ -n "$layout" ] || layout=none
    local sx sy sz
    sx="$(eqemu_sql "SELECT ROUND(safe_x,3) FROM zone WHERE short_name='${zone}';" | head -1 | tr -d '[:space:]')"
    sy="$(eqemu_sql "SELECT ROUND(safe_y,3) FROM zone WHERE short_name='${zone}';" | head -1 | tr -d '[:space:]')"
    sz="$(eqemu_sql "SELECT ROUND(safe_z,3) FROM zone WHERE short_name='${zone}';" | head -1 | tr -d '[:space:]')"
    python3 "$REPO/scripts/b964/make_tour.py" --safe "$sx" "$sy" "$sz" --count "$n" --layout "$layout" --out "$dir/tour.txt"
    local -a envs=( "TZ=UTC" )
    case "$mode" in
        imp) ;;
        off) envs+=( "WILLEQ_IMPOSTORS=0" ) ;;
        base) envs+=( "WILLEQ_BASELINE=1" ) ;;
        *) say "bad variant $variant"; return 1 ;;
    esac
    local -a args=( -c "$CFG" --renderer gles2 --constrained orangepi -r "$RES_W" "$RES_H"
                    --eqe1-path "$EQE1_PATH" --log-level=INFO --script "$dir/tour.txt" --no-audio )
    printf '%s\n' "${envs[@]}" > "$dir/env.txt"
    printf '%q ' "$BIN" "${args[@]}" > "$dir/command_line.txt"; echo >> "$dir/command_line.txt"
    local log="$dir/client.log" start; start=$(date +%s)
    say "[$zone/$variant] launching (mode=$mode n=$n layout=$layout)"
    ( cd "$REPO" && exec env "${envs[@]}" "$BIN" "${args[@]}" ) > "$log" 2>&1 &
    CLIENT_PID=$!
    printf 't_s\trss_anon_kb\n' > "$dir/rss_samples.tsv"
    local last_size=0 last_growth=$start now size killed="" ra
    while kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 2
        now=$(date +%s)
        ra="$(awk '/^RssAnon:/{print $2}' "/proc/$CLIENT_PID/status" 2>/dev/null)"
        [ -n "$ra" ] && printf '%s\t%s\n' "$((now - start))" "$ra" >> "$dir/rss_samples.tsv"
        size=$(stat -c %s "$log" 2>/dev/null || echo 0)
        if [ "$size" -ne "$last_size" ]; then last_size=$size; last_growth=$now; fi
        [ $((now - last_growth)) -ge 120 ] && killed="log silent for 120 s"
        [ $((now - start)) -ge 420 ] && killed="420 s overall deadline"
        if [ -n "$killed" ]; then
            say "[$zone/$variant] WATCHDOG: $killed"
            kill "$CLIENT_PID" 2>/dev/null; sleep 3; kill -9 "$CLIENT_PID" 2>/dev/null
            break
        fi
    done
    wait "$CLIENT_PID" 2>/dev/null
    local rc=$?
    CLIENT_PID=""
    local steady=0 landed=n
    grep -q '\[memledger\] point=steady_60s ' "$log" && steady=1
    grep -q "LoadZoneSources: Loaded zone '${zone}'" "$log" && landed=y
    printf 'zone=%s variant=%s rc=%s wall_s=%s steady_60s=%s landed=%s watchdog=%s\n' \
        "$zone" "$variant" "$rc" "$(( $(date +%s) - start ))" "$steady" "$landed" "${killed:-none}" > "$dir/result.txt"
    say "[$zone/$variant] $(cat "$dir/result.txt")"
    [ "$steady" -eq 1 ] && [ "$landed" = y ]
}

FAILS=0
for zone in ${ZONES//,/ }; do
    for variant in ${VARIANTS//,/ }; do
        CURRENT_ZONE="$zone"
        if ! boot_zone "$zone"; then say "[$zone/$variant] zone did not boot"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue; fi
        if ! seed_char "$zone"; then say "[$zone/$variant] seed failed"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue; fi
        run_one "$zone" "$variant" || FAILS=$((FAILS + 1))
        sleep "$LATE_SAVE_S"
        kill_zone "$zone"
        CURRENT_ZONE=""
    done
done
say "runs failed: $FAILS (evidence in $OUT)"
cleanup
[ "$RESTORE_BAD" -eq 1 ] && exit 66
[ "$FAILS" -eq 0 ] || exit 1
exit 0
