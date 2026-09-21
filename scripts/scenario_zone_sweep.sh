#!/usr/bin/env bash
# B853 (issue #99): the LIVE zone-in sweep over Kunark and Velious zones.
#
# B852 proved the 118 era zones load clean OFFLINE (one
# gles2_zone_benchmark process per zone, `refactor/B852/results_118.tsv`).
# This sweep proves a LOGGED-IN client reaches IsFullyZonedIn() in them with
# the server's own spawns, doors and objects -- the part the offline
# benchmark never sees.
#
# What one sweep does (the card's lane discipline, all in ONE lock hold):
#   1. take the eqemu lock (kb-b853-live-zonein-sweep), bring the stack up
#      ONCE (up.sh), snapshot the character rows ONCE (restore_lib.sh);
#   2. for each zone in the list, HEAVIEST FIRST (s3d_bytes, B852's
#      ordering, from scripts/ci/zone_content_sweep_zones.tsv):
#        a. bounce the zone process to a fresh `zone <short>` (the zone
#           holds the LINKDEAD client cache -- the second trap
#           scripts/camera_match_sweep.sh documents -- so a live zone never
#           re-reads the seeded pose) and wait for its boot marker
#           (eqemu_zone_booted);
#        b. seed the test character at the zone's safe point -- the zone
#           table's own safe_x/safe_y/safe_z, exactly as eqemu's own
#           safe-return write stores them (world/worlddb.cpp: `SET zone_id,
#           x, y, z, heading` from entry.safe_*). Never invented
#           coordinates. The write-readback-retry loop (also from
#           camera_match_sweep.sh) survives the late linkdead save;
#        c. run `willeq --scenario zone_in` headless (surfaceless EGL on
#           llvmpipe -- the client's default graphics path, no display)
#           under a per-zone timeout AND a log-silence watchdog;
#        d. record exit code (0/1/2/3 taxonomy), wall time, FATAL/ERROR
#           count, first FATAL line, placeholder-model count,
#           late-open EQE1 count and append ONE TSV row.
#           A failing zone NEVER stops the sweep.
#   3. restore the ONE snapshot with the world DOWN (eqemu_restore_snapshot),
#      bring the stack DOWN, release the lock -- including on failure (trap).
#
# Default zone set: the Kunark + Velious rows B852 swept clean
# (refactor/B852/results_118.tsv, clean==1). A B852 red row is skipped by
# construction (soldunga: #100's EQE1 31-char-name FATAL; hole: #101's
# memory ceiling) -- its live zone-in would hit the same known defect.
#
# Resume: the TSV is APPENDED, so a partial run leaves one row per zone it
# reached; re-invoking with the remaining zones (same --out-dir) continues
# the file. The verdict counts the whole file.
#
# --selftest: the card's two-zone net (stonebrunt Kunark + iceclad Velious),
# written to /home/user/kb-evidence/b853-live-zonein-sweep/selftest.
#
# Exit codes: 0 every row in the TSV clean, 1 at least one non-clean row,
# 64 lock held, 65 the stack would not come up, 66 snapshot mismatch,
# 68 bad inputs (no binary / no zone-table row for a zone), 3 bad usage.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$REPO/scripts/eqemu/env.sh"
# T493: the ONE snapshot/restore implementation.
source "$REPO/scripts/eqemu/restore_lib.sh"

EVIDENCE="/home/user/kb-evidence/b853-live-zonein-sweep"
OUT_DIR="$EVIDENCE/full"
# B852's zone census: the ordering source (s3d_bytes) for --zones input.
B852_LIST="$REPO/scripts/ci/zone_content_sweep_zones.tsv"
# B852's committed results: the default (skip-loud) zone set.
B852_RESULTS="$REPO/refactor/B852/results_118.tsv"
TIMELIST=""
ZONES=""
BIN="${WILLEQ_BIN:-$REPO/build/bin/willeq}"
CHAR="${SWEEP_CHAR:-$EQ_TEST_CHARACTER}"
TIMEOUT_S="${SWEEP_TIMEOUT_S:-300}"
WATCHDOG_S="${SWEEP_WATCHDOG_S:-150}"
ZONE_BOOT_S="${SWEEP_ZONE_BOOT_S:-180}"
LATE_SAVE_S="${SWEEP_LATE_SAVE_S:-10}"
SELFTEST=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--zones z1,z2,...] [--list FILE] [--out-dir DIR]
                        [--bin PATH] [--char NAME] [--timeout N]
                        [--watchdog N] [--selftest]

Options:
  --zones z1,z2     sweep only these zones (comma-separated, short names);
                    heavy-first order comes from B852's census
  --list FILE       zone list TSV (B852's zone_content_sweep_zones.tsv format
                    zone/expansion/s3d_bytes/notes); Kunark/Velious rows are
                    swept heavy-first, other rows are ignored
  --out-dir DIR     TSV + per-zone logs (default: $OUT_DIR)
  --bin PATH        willeq binary (default: $REPO/build/bin/willeq)
  --char NAME       test character (default: ${CHAR})
  --timeout N       per-zone client wall-clock seconds (default $TIMEOUT_S)
  --watchdog N      client log-silence seconds before SIGKILL (default $WATCHDOG_S)
  --selftest        two-zone net (stonebrunt + iceclad) in $EVIDENCE/selftest
Exit: 0 all clean, 1 a non-clean row, 64 lock held, 65 stack up failed,
      66 snapshot mismatch, 68 bad inputs, 3 bad usage.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --list) TIMELIST="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --bin) BIN="$2"; shift 2 ;;
        --char) CHAR="$2"; shift 2 ;;
        --timeout) TIMEOUT_S="$2"; shift 2 ;;
        --watchdog) WATCHDOG_S="$2"; shift 2 ;;
        --selftest) SELFTEST=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 3 ;;
    esac
done

if [ "$SELFTEST" -eq 1 ]; then
    ZONES="stonebrunt,iceclad"
    OUT_DIR="$EVIDENCE/selftest"
fi
if [ -z "$ZONES" ] && [ -z "$TIMELIST" ]; then
    [ -f "$B852_RESULTS" ] || { echo "FATAL: no B852 results at $B852_RESULTS (run B852 first)" >&2; exit 68; }
    ZONES="$(awk -F'\t' 'NR>1 && ($2=="kunark" || $2=="velious") && $14=="1" {print $1}' "$B852_RESULTS" | tr '\n' ',' | sed 's/,$//')"
fi

TSV="$OUT_DIR/results.tsv"
LOGS="$OUT_DIR/logs"
TSV_HEADER='zone	expansion	s3d_bytes	status	exit_code	wall_s	fatal_count	error_count	error_map_count	first_fatal	zone_loaded_in	zone_name	spawn_count	door_count	placeholder_model_count	late_open_eqe1_count	clean'

say() { printf '[zone_sweep] %s\n' "$*" >&2; }

mkdir -p "$LOGS"
[ -x "$BIN" ] || { say "no willeq binary at $BIN (build: cmake --build build --target willeq)"; exit 68; }
[ -n "$ZONES" ] || [ -n "$TIMELIST" ] || { usage; exit 3; }

# ---- the zone set: "zone<TAB>expansion<TAB>s3d_bytes" rows -----------------
# --zones: expansion/bytes come from B852's census, empty when unknown.
# --list: the file's own Kunark/Velious rows (not_in_t490_census excluded,
#         the same convention B852's sweep used).
ZONE_ROWS=""
if [ -n "$ZONES" ]; then
    IFS=',' read -ra ZARR <<< "$ZONES"
    for z in "${ZARR[@]}"; do
        [ -n "$z" ] || continue
        row="$(awk -F'\t' -v z="$z" 'NR>1 && $1==z {print $2 "\t" $3}' "$B852_LIST" 2>/dev/null | head -1)"
        ZONE_ROWS="${ZONE_ROWS}${z}	${row}"$'\n'
    done
else
    [ -f "$TIMELIST" ] || { say "no --list file at $TIMELIST"; exit 3; }
    ZONE_ROWS="$(awk -F'\t' 'NR>1 && ($2=="kunark" || $2=="velious") && $4!="not_in_t490_census" {print $1 "\t" $2 "\t" $3}' "$TIMELIST")"$'\n'
fi
[ -n "$(printf '%s' "$ZONE_ROWS" | tr -d '[:space:]')" ] || { say "empty zone list"; exit 3; }

ZN="$(printf '%s' "$ZONE_ROWS" | grep -c .)"
say "sweeping ${ZN} zone(s), char=$CHAR, timeout=${TIMEOUT_S}s, watchdog=${WATCHDOG_S}s"

# ---- the lock, taken atomically --------------------------------------------
if ! eqemu_lock_take "kb-b853-live-zonein-sweep"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run (card rule: block the card, do not wait >10 min)"
    exit 64
fi
say "lock taken"

SNAPSHOT="$OUT_DIR/snapshot_before.tsv"
AFTER="$OUT_DIR/snapshot_after.tsv"
CLEANED=0
RESTORE_BAD=0
CLIENT_PID=""
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ -n "$CLIENT_PID" ]; then
        kill "$CLIENT_PID" >/dev/null 2>&1
        sleep 2
        kill -9 "$CLIENT_PID" >/dev/null 2>&1
    fi
    # The ONE snapshot, restored with the world DOWN -- the lane's discipline
    # (T493). Runs on EVERY exit path, --selftest included.
    eqemu_restore_snapshot "$SNAPSHOT" "$AFTER" say || RESTORE_BAD=1
    bash "$REPO/scripts/eqemu/down.sh" >"$OUT_DIR/stack_down.log" 2>&1
    say "stack down"
    eqemu_lock_release
    say "lock released"
}
trap cleanup EXIT INT TERM

# ---- 1. stack up, ONCE ------------------------------------------------------
say "bringing eqemu up"
if ! bash "$REPO/scripts/eqemu/up.sh" > "$OUT_DIR/stack_up.log" 2>&1; then
    say "up.sh failed -- see $OUT_DIR/stack_up.log"
    exit 65
fi
eqemu_stack_ready || { say "eqemu_stack_ready is false after up.sh"; exit 65; }

# ---- 2. snapshot, ONCE ------------------------------------------------------
eqemu_snapshot "$SNAPSHOT"
say "snapshot taken ($(wc -l < "$SNAPSHOT") rows)"

# ---- the per-zone helpers ---------------------------------------------------
# boot_zone <short> -- make sure a zone process for THIS zone is running and
# freshly booted. It holds the LINKDEAD client cache (camera_match_sweep.sh's
# second trap: a dead character's DB pose is never re-read by a live zone),
# so the run needs a process that started AFTER the seed. Bounce ONLY this
# zone's process (not the whole fleet -- the fleet's respawner would race the
# full pkill, and restarting 25 zones is both slow and wrong). The pkill goes
# through eq_container_pkill (board B798's guard); the pattern is safe because
# the literal "(" is not one of the alternatives.
boot_zone() {
    local short="$1" bootlog="$LOGS/zone_${short}_boot.log"
    # If a process for this zone is already running, bounce it; otherwise just
    # start it. Either way we end with a freshly-booted process for this zone.
    if server_process_running zone >/dev/null 2>&1; then
        eq_container_pkill TERM "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
        sleep 3
        eq_container_pkill KILL "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
    fi
    # Truncate the boot log: eqemu_zone_booted greps it for the boot marker,
    # and a marker left by a previous run would make the check pass instantly
    # against a process that never booted.
    server_exec "rm -f logs/zone_${short}_sweep.log" >/dev/null 2>&1
    server_exec_bg "./bin/zone ${short} > logs/zone_${short}_sweep.log 2>&1"
    if ! eqemu_zone_booted "$short" "logs/zone_${short}_sweep.log" "$ZONE_BOOT_S" >"$bootlog" 2>&1; then
        say "zone $short did not boot -- last 40 log lines:"
        server_exec "tail -40 logs/zone_${short}_sweep.log" 2>&1 || true
        return 1
    fi
    return 0
}

# seed_char <short> -- place the character at the zone's safe point, with the
# server at rest, and verify the DB agrees. The safe point is the zone
# table's own row (safe_x/safe_y/safe_z), the same values eqemu's
# safe-return writes into character_data (world/worlddb.cpp). The
# write-readback-retry loop is camera_match_sweep.sh's: the late linkdead
# save can land after our UPDATE.
#
# THE COLUMN TRAP (selftest RED, 2026-09-21): character_data.zone_id stores
# the zone's STABLE zoneidnumber (eqemu's Zones:: enum space: qeynos2=2,
# relic=370, stonebrunt=100, dawnshroud=174 ...), NOT the zone table's
# auto-increment PK `id`. Seeding the PK sends the character into a DIFFERENT
# zone -- measured: seed zone_id=370 (stonebrunt's PK) and the client zoned
# into relic (370 is relic's zoneidnumber; relic.s3d is not a client asset,
# FATAL exit 2); seed zone_id=174 (iceclad's PK) and it zoned into
# dawnshroud (174 is dawnshroud's zoneidnumber; exit 0, wrong zone).
seed_char() {
    local short="$1" zone_id sx sy sz want i got
    zone_id="$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    if [ -z "$zone_id" ]; then
        say "no zone-table row for '${short}'"
        return 1
    fi
    sx="$(eqemu_sql "SELECT ROUND(safe_x,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    sy="$(eqemu_sql "SELECT ROUND(safe_y,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    sz="$(eqemu_sql "SELECT ROUND(safe_z,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    want="${zone_id},$(printf '%.3f' "$sx"),$(printf '%.3f' "$sy"),$(printf '%.3f' "$sz")"
    for i in 1 2 3 4 5 6; do
        # Do NOT touch last_login: the character-select row reads it
        # (world/worlddb.cpp: cse->LastLogin, and the GoHome rule does
        # now - last_login, so a 0/NULL row would look offline since the
        # epoch).
        eqemu_sql "UPDATE character_data SET zone_id=${zone_id}, x=${sx}, y=${sy}, z=${sz}, heading=0 WHERE name='${CHAR}';" >/dev/null
        sleep 5
        got="$(eqemu_sql "SELECT CONCAT_WS(',',zone_id,ROUND(x,3),ROUND(y,3),ROUND(z,3)) FROM character_data WHERE name='${CHAR}';" 2>/dev/null | tail -1 | tr -d '\r[:space:]')"
        if [ "$got" = "$want" ]; then
            say "seed confirmed: ${CHAR} -> ${short} (zoneidnumber ${zone_id}) safe point (${sx},${sy},${sz})"
            return 0
        fi
        say "seed not yet settled (db='${got}' want='${want}'), retrying"
    done
    say "could not pin ${CHAR}'s pose for ${short} (db='${got}' want='${want}')"
    return 1
}

# kill_zone <short> -- stop THIS zone's sweep process and free its port. The
# world assigns each zone process a port from a SMALL fixed range
# (measured: stonebrunt -> 7025; ~18 of my processes + 11 launcher-fleet
# processes exhausted it: "World did not have a port to assign from this
# server, the port range was not large enough" -- the zone boots, prints its
# marker, then shuts down, which the liveness check reports as
# zone_boot_failed). A dead zone's LINKDEAD client cache is gone with the
# process, so nothing is lost by stopping it: the NEXT run re-boots a fresh
# process after the next seed. eq_container_pkill (board B798's guard); the
# pattern is safe because the literal "(" is not one of the alternatives.
kill_zone() {
    local short="$1"
    eq_container_pkill TERM "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
    sleep 2
    eq_container_pkill KILL "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
}

# ---- 3. the client config for THIS lane ------------------------------------
CFG="$OUT_DIR/client.json"
cat > "$CFG" <<EOF
{
  "clients": [
    {
      "host": "$(eqemu_port_host)",
      "port": ${LOGIN_PORT},
      "user": "${EQ_TEST_ACCOUNT}",
      "pass": "${EQ_TEST_PASSWORD}",
      "server": "${EQ_TEST_SERVER_LONGNAME}",
      "character": "${CHAR}",
      "eq_client_path": "${REPO}/data/eq_client",
      "audio": {"enabled": false}
    }
  ]
}
EOF

TSV_STARTED=0
new_tsv() {
    printf '%s\n' "$TSV_HEADER" > "$TSV"
    TSV_STARTED=1
}
# Resume: a TSV from an earlier (possibly partial) run is CONTINUED, not
# truncated -- the verdict counts the whole file. A file without the header
# is not a resume target: replace it, so a stray file cannot poison the run.
if [ -f "$TSV" ] && head -1 "$TSV" | grep -qx "$TSV_HEADER"; then
    TSV_STARTED=1
    say "resuming: $(($(grep -c . "$TSV") - 1)) row(s) already in $TSV"
else
    new_tsv
fi

# run_zone <zone> <expansion> <s3d_bytes>
run_zone() {
    local short="$1" expansion="$2" s3d_bytes="$3"
    local log="$LOGS/${short}_client.log"
    local start end code status wall fatal_n error_n error_map first_fatal loaded zname spawns doors placeholder late clean age
    code=""
    [ "$TSV_STARTED" -eq 1 ] || new_tsv

    say "=== ${short} (${expansion:-?}, ${s3d_bytes:-?} s3d bytes) ==="
    if ! boot_zone "$short"; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$short" "$expansion" "${s3d_bytes:-}" "zone_boot_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 >> "$TSV"
        say "row: $short zone_boot_failed"
        return 1
    fi
    if ! seed_char "$short"; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$short" "$expansion" "${s3d_bytes:-}" "seed_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 >> "$TSV"
        say "row: $short seed_failed"
        return 1
    fi

    : > "$log"
    start=$(date +%s)
    "$BIN" -c "$CFG" --scenario zone_in --timeout "$TIMEOUT_S" \
        --log-level=INFO > "$log" 2>&1 &
    CLIENT_PID=$!
    # The dead-client watchdog: a child that stops writing to its log for
    # WATCHDOG_S seconds is presumed hung (a process-level timeout cannot
    # catch a loader hang that never logs). B852's sweep carries the same.
    while kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 5
        age=$(( $(date +%s) - $(stat -c %Y "$log") ))
        if [ "$age" -ge "$WATCHDOG_S" ]; then
            say "watchdog: $short silent for ${age}s -- SIGKILL"
            kill -9 "$CLIENT_PID" 2>/dev/null
            wait "$CLIENT_PID" 2>/dev/null
            code=137
            break
        fi
    done
    [ -n "$code" ] || { wait "$CLIENT_PID"; code=$?; }
    CLIENT_PID=""
    end=$(date +%s)
    wall=$((end - start))

    fatal_n="$(grep -c ' \[FATAL\] ' "$log" 2>/dev/null || true)"
    # [MAP] lines are the client's own region-map (pathfinding) lookup, which
    # is an optional asset (maps_path): a missing <zone>.map is logged ERROR
    # but the zone-in proceeds (measured: both selftest zones, 2 lines each,
    # exit 0). It is an environment-asset gap, not zone content, so it is
    # COUNTED AND RECORDED but not charged against clean.
    error_map="$(grep -c ' \[ERROR\] \[MAP\] ' "$log" 2>/dev/null || true)"
    error_n=$(( $(grep -c ' \[ERROR\] ' "$log" 2>/dev/null || true) - error_map ))
    first_fatal="$(grep -m1 ' \[FATAL\] ' "$log" 2>/dev/null | tr -d '\t' | head -c 240)"
    # THE zone-geometry marker for this lane. The client reports the
    # server's short_name (measured: "Loaded zone 'iceclad'", "Loaded zone
    # 'stonebrunt'"), so the name check below is the real "did we land in
    # the zone we seeded" assertion. B852's 'loadZoneSequential: complete'
    # line is a GPU-pipeline marker: --scenario runs the Automated lane,
    # which never initializes the render pipeline (application.cpp:
    # initGraphics gates to GraphicalInteractive), so that line can never
    # appear here and must not be the marker.
    if grep -q "LoadZoneSources: Loaded zone '${short}'" "$log" 2>/dev/null; then loaded="y"; else loaded="n"; fi
    zname="$(grep -m1 "scenario 'zone_in' starting: zoned in to" "$log" 2>/dev/null | sed -n "s/.*zoned in to '\([^']*\)'.*/\1/p")"
    # Server content actually received from the zone (the card's point: the
    # server's own spawns, doors and objects, which the offline benchmark
    # never sees). "Loaded N spawns in zone" (ZONE module) / "Received N
    # doors" (ENTITY module); a missing line means the zone sent none.
    spawns="$(grep -oE 'Loaded [0-9]+ spawns in zone' "$log" 2>/dev/null | grep -oE '[0-9]+' | head -1)"; spawns="${spawns:-0}"
    doors="$(grep -oE 'Received [0-9]+ doors' "$log" 2>/dev/null | grep -oE '[0-9]+' | head -1)"; doors="${doors:-0}"
    # Placeholder models and late-open EQE1 are GPU-pipeline signals
    # (RaceModelLoader / Eqe1ArchiveComponent log lines). They are structurally
    # 0 in the Automated lane -- the render pipeline is never initialized --
    # so they are RECORDED for parity with B852's TSV and as a canary (a
    # non-zero value means the lane started rendering, and the counts become
    # meaningful), but they cannot be the clean criterion here.
    placeholder="$( { grep -c 'has no model in any S3D archive' "$log" 2>/dev/null || true; grep -c 'rendered as placeholder cubes' "$log" 2>/dev/null || true; } | awk '{s+=$1} END {print s+0}' )"
    late="$(grep -c 'EQE1: late-open' "$log" 2>/dev/null || true)"

    if [ "$code" -ge 128 ]; then status="died_by_signal_$((code-128))"
    else status="ran"; fi

    clean=0
    if [ "$code" -eq 0 ] && [ -n "$zname" ] && [ "$zname" = "$short" ] \
       && [ "${fatal_n:-0}" -eq 0 ] && [ "${error_n:-0}" -eq 0 ] \
       && [ "$loaded" = "y" ] && [ "${placeholder:-0}" -eq 0 ]; then
        clean=1
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$short" "$expansion" "${s3d_bytes:-}" "$status" "$code" "$wall" \
        "${fatal_n:-0}" "${error_n:-0}" "${error_map:-0}" "${first_fatal:-}" "$loaded" "${zname:-}" \
        "$spawns" "$doors" "${placeholder:-0}" "${late:-0}" "$clean" >> "$TSV"
    say "row: $short exit=$code wall=${wall}s fatal=${fatal_n:-0} err=${error_n:-0} placeholders=${placeholder:-0} late_eqe1=${late:-0} clean=$clean"

    # The late-save trap, the other direction: the zone writes the character
    # back on linkdead (and again at its own shutdown). Let it land BEFORE we
    # stop the process -- killing the zone first could race the write, and
    # the next seed's readback-retry loop would then fight a stale row.
    sleep "$LATE_SAVE_S"
    # Free the zone's port NOW (see kill_zone: the world's port range is
    # small and every left-running process holds its port). Runs on every
    # exit path, including a dead client: a partially-booted zone process
    # still holds its port.
    kill_zone "$short"
    [ "$clean" -eq 1 ]
}

# ---- 4. the loop: heavy-first, a failure never stops the sweep -------------
# s3d_bytes descending; rows without a census entry (unknown bytes) sort
# after the known ones.
FAILURES=0
while IFS=$'\t' read -r z expansion s3d; do
    [ -n "$z" ] || continue
    if ! run_zone "$z" "${expansion:-}" "${s3d:-}"; then
        FAILURES=$((FAILURES + 1))
    fi
    say "progress: $(($(grep -c . "$TSV") - 1)) row(s) recorded, ${FAILURES} failure(s) in this run"
done <<< "$(sort -t"$(printf '\t')" -k3,3nr <<< "$ZONE_ROWS")"

# ---- 5. verdict -------------------------------------------------------------
ROWS=$(( $(grep -c . "$TSV") - 1 ))
CLEAN_ROWS="$(awk -F'\t' 'NR>1 && $17=="1"' "$TSV" | wc -l)"
say "sweep result: ${CLEAN_ROWS}/${ROWS} rows clean in ${TSV}"
awk -F'\t' 'NR>1 && $17!="1" {printf "  %s: %s (exit %s, fatal=%s err=%s) -- %s\n", $1, $4, $5, $7, $8, ($10!=""?$10:"(no FATAL; see log)")}' "$TSV" >&2

if [ "$RESTORE_BAD" -eq 1 ]; then
    say "restore mismatch overrides the sweep result"
    exit 66
fi
[ "$FAILURES" -eq 0 ] && exit 0
exit 1
