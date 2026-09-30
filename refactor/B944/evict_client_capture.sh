#!/usr/bin/env bash
# B944 / GitLab #262 -- the RENDERED client zoned in with the texture cache's
# eviction ceiling forced low, so ConstrainedTextureCache evicts during a real
# zone load and during play. Answers "can the CLIENT crash, FATAL or draw
# wrong when the cache evicts under pressure?" with a run, not an argument.
#
# The ceiling is forced through the client's EXISTING memory-spec form of
# --constrained (`TOTALxTEXTURExFRAMEBUFFER` in MiB,
# ConstrainedRendererConfig::parseMemorySpec) -- no new client knob, and the
# shipped preset is untouched. NOTE the spec form is not the orangepi preset:
# it sets totalMemoryBudgetBytes/maxTextureDimension/clip distances from the
# total, and config/constrained_presets.json has no entry for a spec name, so
# the JSON overrides are not applied. That is fine for an eviction test (the
# question is what eviction does), and every run records its command line.
#
# Tour (Application --script, held until fully zoned in): turn through the
# four headings twice, with pauses, so different regions -- and so different
# textures -- are drawn after the zone load already evicted; then /q.
# Frames are dumped (--dump-frames-dir) so a placeholder checkerboard or a
# garbled texture is on file for Will to look at.
#
# Structure (lock, ONE snapshot, restore with the world DOWN, down.sh,
# release, lock state printed LAST) is scripts/client_ledger_capture.sh's.
#
# Usage: refactor/B944/evict_client_capture.sh --out DIR --bin PATH
#            [--zones z1,z2] [--spec 256x4x16] [--s3d] [--owner b944]
#            [--res W H] [--env K=V ...] [--no-setcap]
#   --s3d   run WITHOUT --eqe1-path (the S3D/BMP decode path)
#   --env   extra environment for the client (repeatable), e.g.
#           ASAN_OPTIONS=... for an ASan build
#   --no-setcap  do not give the copy CAP_NET_ADMIN. Measured: without it the
#           zone-in packet burst can overflow the default UDP buffer and the
#           client stalls at "Finalizing connection" (gfaydark, 2026-09-30),
#           so an ASan run keeps the capability and the rig accepts the
#           LeakSanitizer exit 1 that follows a clean shutdown instead
# Exit: 0 every run exited 0; 1 a run did not; 3 bad usage; 64 lock held;
# 65 the stack would not come up / lane mismatch; 66 the restore failed.

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/eqemu/zone_place_lib.sh"
export EQEMU_HOST

ZONES="qeynos2"
SPEC="256x4x16"
OWNER="b944"
OUT=""
SRC_BIN=""
S3D=0
RES_W=800; RES_H=600
EXTRA_ENV=()
SETCAP=1
ZONE_BOOT_S="${ZONE_BOOT_S:-150}"
LATE_SAVE_S=10
while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --spec) SPEC="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --bin) SRC_BIN="$2"; shift 2 ;;
        --s3d) S3D=1; shift ;;
        --res) RES_W="$2"; RES_H="$3"; shift 3 ;;
        --env) EXTRA_ENV+=("$2"); shift 2 ;;
        --no-setcap) SETCAP=0; shift ;;
        -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done
[ -n "$OUT" ] && [ -n "$SRC_BIN" ] || { echo "--out and --bin are required" >&2; exit 3; }
mkdir -p "$OUT"
LOGS="$OUT/server"
mkdir -p "$LOGS"
RIG_LOG="$OUT/rig.log"
say() { printf '[b944 %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$RIG_LOG" >&2; }

EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
[ -x "$SRC_BIN" ] || { say "no willeq binary at $SRC_BIN"; exit 3; }
CHAR="${EQ_TEST_CHARACTER}"

BIN_DIR="$(mktemp -d /tmp/b944_client.XXXXXX)"
BIN="$BIN_DIR/willeq"
cp "$SRC_BIN" "$BIN"
if [ "$SETCAP" -eq 0 ]; then
    say "--no-setcap: running without CAP_NET_ADMIN (LeakSanitizer cannot run in a capability binary)"
elif sudo -n setcap cap_net_raw,cap_net_admin+ep "$BIN" 2>/dev/null; then
    say "capabilities set on $BIN"
else
    say "WARNING: could not setcap $BIN -- running without CAP_NET_ADMIN"
fi

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
if [ "$lane_ok" -ne 1 ]; then
    say "LANE MISMATCH: config host $CLIENT_HOST is not this lane's server (EQEMU_HOST=$EQEMU_HOST) -- refusing"
    rm -rf "$BIN_DIR"
    exit 65
fi
say "lane EQEMU_HOST=$EQEMU_HOST, client config host $CLIENT_HOST (verified)"
{
    echo "host=$(hostname) commit=$(git -C "$REPO" rev-parse HEAD) dirty=$(git -C "$REPO" status --porcelain -- src include | wc -l)"
    echo "binary=$SRC_BIN sha256=$(sha256sum "$SRC_BIN" | cut -d' ' -f1)"
    echo "lane=$EQEMU_HOST zones=$ZONES spec=$SPEC s3d=$S3D res=${RES_W}x${RES_H} env=${EXTRA_ENV[*]:-}"
    echo "start_utc=$(date -u +%FT%TZ)"
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

say "up.sh"
if ! timeout 175 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/stack_up.log" 2>&1; then
    say "up.sh failed -- see $OUT/stack_up.log"
    exit 65
fi
eqemu_stack_ready || { say "eqemu_stack_ready is false after up.sh"; exit 65; }
eqemu_snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"

run_one() {
    local zone="$1" dir="$OUT/$1"
    mkdir -p "$dir/frames"
    local log="$dir/client.log" tour="$dir/tour.txt"
    cat > "$tour" <<EOF
# held until fully zoned in, then turn through the four headings twice
wait 8
/turn 90
wait 6
/turn 180
wait 6
/turn 270
wait 6
/turn 0
wait 6
/turn 90
wait 6
/turn 180
wait 6
/turn 270
wait 6
/turn 0
wait 6
/q
EOF
    local -a envs=( "WILLEQ_BASELINE=1" "TZ=UTC" "${EXTRA_ENV[@]}" )
    local -a args=( -c "$CFG" --renderer gles2 --constrained "$SPEC" -r "$RES_W" "$RES_H"
                    --log-level=INFO --no-audio --script "$tour"
                    --dump-frames-dir "$dir/frames" --dump-every 30 --dump-max-frames 60 )
    [ "$S3D" -eq 1 ] || args+=( --eqe1-path "$EQE1_PATH" )
    printf '%s\n' "${envs[@]}" > "$dir/env.txt"
    printf '%q ' "$BIN" "${args[@]}" > "$dir/command_line.txt"; echo >> "$dir/command_line.txt"
    say "[$zone] launching"
    local start; start=$(date +%s)
    ( cd "$REPO" && exec env "${envs[@]}" "$BIN" "${args[@]}" ) > "$log" 2>&1 &
    CLIENT_PID=$!
    local last_size=0 last_growth=$start now size killed=""
    while kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 2
        now=$(date +%s)
        size=$(stat -c %s "$log" 2>/dev/null || echo 0)
        if [ "$size" -ne "$last_size" ]; then last_size=$size; last_growth=$now; fi
        if [ $((now - last_growth)) -ge 150 ]; then killed="log silent for 150 s"; fi
        if [ $((now - start)) -ge 480 ]; then killed="480 s overall deadline"; fi
        if [ -n "$killed" ]; then
            say "[$zone] WATCHDOG: $killed -- killing the client"
            kill "$CLIENT_PID" 2>/dev/null; sleep 3; kill -9 "$CLIENT_PID" 2>/dev/null
            break
        fi
    done
    wait "$CLIENT_PID" 2>/dev/null
    local rc=$?
    CLIENT_PID=""
    local landed=n
    grep -q "LoadZoneSources: Loaded zone '${zone}'" "$log" && landed=y
    local asan=n lsan=n clean=n
    grep -q "ERROR: AddressSanitizer" "$log" && asan=y
    # An ASan build's LeakSanitizer cannot run in a capability binary and
    # exits 1 AFTER a clean shutdown -- that is not a client failure.
    grep -q "LeakSanitizer has encountered a fatal error" "$log" && lsan=y
    grep -q "Application shutdown complete" "$log" && clean=y
    printf 'zone=%s rc=%s wall_s=%s landed=%s watchdog=%s asan=%s lsan_exit=%s clean_shutdown=%s evict_warn=%s placeholder_warn=%s fatal=%s frames=%s\n' \
        "$zone" "$rc" "$(( $(date +%s) - start ))" "$landed" "${killed:-none}" "$asan" "$lsan" "$clean" \
        "$(grep -c 'Evicting texture\|evicted texture' "$log")" \
        "$(grep -c 'placeholder checkerboard' "$log")" \
        "$(grep -c '\[FATAL\]' "$log")" \
        "$(ls "$dir/frames" 2>/dev/null | wc -l)" > "$dir/result.txt"
    say "[$zone] $(cat "$dir/result.txt")"
    [ "$landed" = y ] && [ "$asan" = n ] && [ "$clean" = y ] && { [ "$rc" -eq 0 ] || [ "$lsan" = y ]; }
}

FAILS=0
for zone in ${ZONES//,/ }; do
    CURRENT_ZONE="$zone"
    if ! boot_zone "$zone"; then
        say "[$zone] zone did not boot"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue
    fi
    if ! seed_char "$zone"; then
        say "[$zone] seed failed"; FAILS=$((FAILS + 1)); kill_zone "$zone"; continue
    fi
    run_one "$zone" || FAILS=$((FAILS + 1))
    sleep "$LATE_SAVE_S"
    kill_zone "$zone"
    CURRENT_ZONE=""
done

say "runs failed: $FAILS (evidence in $OUT)"
cleanup
[ "$RESTORE_BAD" -eq 1 ] && exit 66
[ "$FAILS" -eq 0 ] || exit 1
exit 0
