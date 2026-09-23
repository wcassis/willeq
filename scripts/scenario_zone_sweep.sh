#!/usr/bin/env bash
# B853 (issue #99): the LIVE zone-in sweep over Kunark and Velious zones.
# B854 (issue #102): the same lane with a Pi client mode (--pi) -- a
# logged-in client on the Orange Pi (483 MB, the target platform).
#
# B852 proved the 118 era zones load clean OFFLINE (one
# gles2_zone_benchmark process per zone, `refactor/B852/results_118.tsv`).
# This sweep proves a LOGGED-IN client reaches IsFullyZonedIn() in them with
# the server's own spawns, doors and objects -- the part the offline
# benchmark never sees.
#
# Two lanes, one driver:
#   (VM lane, B853) the client runs ON THIS HOST, headless (surfaceless EGL
#     on llvmpipe -- the client's default graphics path, no display).
#   (Pi lane, B854 --pi) the client runs ON THE ORANGE PI under ssh
#     (scripts/pi/env.sh's pi_remote): CI-published ARM build of fable
#     (/mnt/projects/fable/ci-arm/by-branch/fable, BUILD_INFO names the sha),
#     Will's Pi recipe (--drm --renderer gles2 --constrained orangepi -r 1280
#     720), logs on the Pi's NFS review tree. ONE willeq on the Pi at a time
#     (483 MB, single-tenant DRM master), hence the pi lock.
#
# What one sweep does (the card's lane discipline, all in ONE lock hold):
#   1. take the eqemu lock (kb-b854-pi-zone-sweep), bring the stack up
#      ONCE (up.sh), snapshot the character rows ONCE (restore_lib.sh);
#      Pi lane: ALSO take the pi lock, after the eqemu lock, in that order,
#      released in reverse, on every exit path (trap).
#   2. for each zone in the list, HEAVIEST FIRST (s3d_bytes, B852's
#      ordering, from scripts/ci/zone_content_sweep_zones.tsv):
#        a. (B854, the LINKDEAD trap) seed the test character at the zone's
#           safe point FIRST, then bounce the zone process to a fresh
#           `zone <short>` and wait for its boot marker (eqemu_zone_booted).
#           The zone holds the LINKDEAD client cache -- the second trap
#           scripts/camera_match_sweep.sh documents -- so a live zone never
#           re-reads the seeded pose; the process must start AFTER the seed.
#        b. run `willeq --scenario zone_in` under a per-zone timeout AND a
#           log-silence watchdog -- this host (VM lane) or the Pi (Pi lane);
#           the Pi watchdog polls the remote log's line count over ssh.
#        c. record the exit code (0/1/2/3 taxonomy), wall time, FATAL/ERROR
#           count, first FATAL, the last [memledger] lifecycle point + peak
#           RssAnon + the three largest ledger categories at that point,
#           OOM evidence (dmesg diff, Pi lane) and append ONE TSV row.
#           A failing zone NEVER stops the sweep.
#   3. restore the ONE snapshot with the world DOWN (eqemu_restore_snapshot),
#      bring the stack DOWN, release the locks (Pi first, eqemu second) --
#      including on failure (trap).
#
# Lane note (both): --scenario forces Automated mode, and the client only
# initializes the GPU render pipeline in GraphicalInteractive
# (application.cpp: initGraphics gates to GraphicalInteractive), so neither
# lane renders -- the Pi lane runs the exact same Automated code path as
# B853's VM lane, on ARM hardware with 483 MB to survive in. The
# placeholder-model and late-open-EQE1 columns are structurally 0 here; they
# are recorded for parity with results_118.tsv only.
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
# --selftest: the two-zone net (stonebrunt Kunark + iceclad Velious),
# written to /home/user/kb-evidence/b853-live-zonein-sweep/selftest.
# With --pi: stonebrunt + sebilis into the B854 evidence dir.
#
# Exit codes: 0 every row in the TSV clean, 1 at least one non-clean row,
# 64 lock held (either lock), 65 the stack would not come up, 66 snapshot
# mismatch, 68 bad inputs (no binary / no zone-table row for a zone),
# 3 bad usage.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$REPO/scripts/eqemu/env.sh"
# T493: the ONE snapshot/restore implementation.
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/pi/env.sh"

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
# ---- B854 Pi lane -----------------------------------------------------------
PI=0
PI_DIR="${PI_SWEEP_DIR:-/mnt/projects/fable/ci-arm/by-branch/fable}"
PI_BIN="$PI_DIR/bin/willeq"
PI_CFG="${PI_SWEEP_CFG:-/mnt/projects/fable/review/pi/willeq_pi.json}"
PI_LOGS="${PI_SWEEP_LOGS:-/mnt/projects/fable/review/B854/pi/logs}"
PI_LOG_FLAGS="${PI_SWEEP_LOG_FLAGS:---log-level=INFO}"
# Pi watchdog default: the VM client logs per game tick (~60 Hz); the Pi
# client is slower, so give it 120 s of silence before presuming a hang.
PI_WATCHDOG_S="${PI_SWEEP_WATCHDOG_S:-120}"
# Optional runtime budget in minutes for the whole Pi sweep: when the
# elapsed time crosses 80 % of it, the loop STOPS CLEANLY at the current
# zone boundary and prints which zones remain (resume continues the TSV).
PI_RUNTIME_LIMIT_MIN="${PI_SWEEP_RUNTIME_LIMIT_MIN:-0}"
SELFTEST_ZONES=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [--zones z1,z2,...] [--list FILE] [--out-dir DIR]
                        [--bin PATH] [--char NAME] [--timeout N]
                        [--watchdog N] [--selftest]
                        [--pi] [--pi-dir DIR] [--pi-watchdog N]
                        [--pi-runtime-limit MIN]

Options:
  --zones z1,z2     sweep only these zones (comma-separated, short names);
                    heavy-first order comes from B852's census
  --list FILE       zone list TSV (B852's zone_content_sweep_zones.tsv format
                    zone/expansion/s3d_bytes/notes); Kunark/Velious rows are
                    swept heavy-first, other rows are ignored
  --out-dir DIR     TSV + per-zone logs (default: $OUT_DIR)
  --bin PATH        willeq binary (default: $REPO/build/bin/willeq)
  --char NAME       test character (default: ${CHAR}; the Pi lane uses the
                    character named in its own config $PI_CFG)
  --timeout N       per-zone client wall-clock seconds (default $TIMEOUT_S)
  --watchdog N      client log-silence seconds before SIGKILL (default $WATCHDOG_S)
  --selftest        two-zone net (stonebrunt + iceclad) in $EVIDENCE/selftest
  --pi              B854: run the client ON THE ORANGE PI (ssh, pi_remote):
                    ARM build at --pi-dir, Will's Pi recipe, per-zone logs on
                    the Pi's NFS review tree ($PI_LOGS). Takes BOTH locks:
                    eqemu (this VM) then pi; releases pi first.
  --pi-dir DIR      the PI-side ARM build dir (default: $PI_DIR)
  --pi-watchdog N   Pi client log-silence seconds (default $PI_WATCHDOG_S)
  --pi-runtime-limit MIN
                    whole-sweep budget in minutes; the loop stops cleanly at
                    the next zone boundary once 80 % has elapsed (default 0 =
                    no budget; resume picks up where it stopped)
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
        --pi) PI=1; shift ;;
        --pi-dir) PI_DIR="$2"; PI_BIN="$PI_DIR/bin/willeq"; shift 2 ;;
        --pi-watchdog) PI_WATCHDOG_S="$2"; shift 2 ;;
        --pi-runtime-limit) PI_RUNTIME_LIMIT_MIN="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 3 ;;
    esac
done

if [ "$SELFTEST" -eq 1 ]; then
    if [ "$PI" -eq 1 ]; then
        # stonebrunt: heaviest Kunark zone (B852 s3d_bytes 7.9 MB); sebilis:
        # mid-weight Kunark (1.7 MB) -- one heavy, one light, so the net
        # covers both ends of the Pi memory curve.
        SELFTEST_ZONES="stonebrunt,sebilis"
        ZONES="$SELFTEST_ZONES"
        OUT_DIR="/home/user/kb-evidence/b854-pi-zone-sweep/selftest"
    else
        ZONES="stonebrunt,iceclad"
        OUT_DIR="$EVIDENCE/selftest"
    fi
fi
if [ -z "$ZONES" ] && [ -z "$TIMELIST" ]; then
    [ -f "$B852_RESULTS" ] || { echo "FATAL: no B852 results at $B852_RESULTS (run B852 first)" >&2; exit 68; }
    ZONES="$(awk -F'\t' 'NR>1 && ($2=="kunark" || $2=="velious") && $14=="1" {print $1}' "$B852_RESULTS" | tr '\n' ',' | sed 's/,$//')"
fi

TSV="$OUT_DIR/results.tsv"
LOGS="$OUT_DIR/logs"
# B854: the Pi-lane TSV adds five memory/OOM columns to B853's 17. The lane
# is single-lane per file: a TSV written by the OTHER lane (17-col header)
# is NOT a resume target for this one -- mixing row widths in one file would
# make the whole file un-parseable.
TSV_HEADER='zone	expansion	s3d_bytes	status	exit_code	wall_s	fatal_count	error_count	error_map_count	first_fatal	zone_loaded_in	zone_name	spawn_count	door_count	placeholder_model_count	late_open_eqe1_count	clean'
if [ "$PI" -eq 1 ]; then
    TSV_HEADER="${TSV_HEADER}	last_memledger_point	peak_rss_anon_bytes	top3_memledger_categories_at_last_point	oom	oom_dmesg_line"
fi

say() { printf '[zone_sweep] %s\n' "$*" >&2; }

mkdir -p "$LOGS"
# The local binary is the VM lane's only input; the Pi lane runs the
# CI-published ARM build on the Pi (checked in the Pi block below), so the
# gate is lane-specific -- a Pi run does not need a local build at all.
[ "$PI" -eq 1 ] || { [ -x "$BIN" ] || { say "no willeq binary at $BIN (build: cmake --build build --target willeq)"; exit 68; }; }
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
        # The ${z}/${row} separator below is a REAL tab byte (0x09), exactly
        # like the committed B853 line: a double-quoted string does not expand
        # a literal backslash-t, and a mangled one would glue the zone name to
        # its expansion in every downstream read (B854 Pi selftest, 2026-09-21:
        # a boot log named "sebilis\tkunark", a zone boot attempted for that
        # literal name). The --list branch gets its tabs from awk itself.
        ZONE_ROWS="${ZONE_ROWS}${z}	${row}"$'\n'
    done
else
    [ -f "$TIMELIST" ] || { say "no --list file at $TIMELIST"; exit 3; }
    ZONE_ROWS="$(awk -F'\t' 'NR>1 && ($2=="kunark" || $2=="velious") && $4!="not_in_t490_census" {print $1 "\t" $2 "\t" $3}' "$TIMELIST")"$'\n'
fi
[ -n "$(printf '%s' "$ZONE_ROWS" | tr -d '[:space:]')" ] || { say "empty zone list"; exit 3; }

ZN="$(printf '%s' "$ZONE_ROWS" | grep -c .)"
LANE="vm"
[ "$PI" -eq 1 ] && LANE="pi"
say "sweeping ${ZN} zone(s) on the ${LANE} lane, char=$CHAR, timeout=${TIMEOUT_S}s, watchdog=${WATCHDOG_S}s"

# ---- the locks, taken in order, released in reverse ------------------------
if ! eqemu_lock_take "kb-b854-pi-zone-sweep"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run (card rule: block the card, do not wait >10 min)"
    exit 64
fi
say "eqemu lock taken"
PI_LOCK_TAKEN=0
if [ "$PI" -eq 1 ]; then
    # The Pi is single-tenant (T503): one DRM master, 483 MB, ONE willeq at a
    # time. This lane holds it for the whole sweep, like the eqemu lock.
    if ! pi_lock_take "kb-b854-pi-zone-sweep"; then
        say "pi lock is HELD by: $(pi_lock_show) -- refusing to run"
        eqemu_lock_release
        exit 64
    fi
    PI_LOCK_TAKEN=1
    say "pi lock taken ($(pi_lock_show))"
    # The client runs on the Pi: its binary, config and logs all live in NFS
    # paths that exist only ON THE PI -- never on this host.
    if ! pi_remote "test -x '$PI_BIN' && test -f '$PI_CFG' && mkdir -p '$PI_LOGS' /tmp/kbpi/cache"; then
        say "Pi prerequisites missing: ARM binary at $PI_BIN, config at $PI_CFG (check the ci-arm by-branch/fable tree and the pi config)"
        pi_lock_release "kb-b854-pi-zone-sweep"
        eqemu_lock_release
        exit 68
    fi
    PI_SHA="$(pi_remote "sed -n 's/^sha=//p' '$PI_DIR/BUILD_INFO' 2>/dev/null" | head -1)"
    say "pi client: $PI_BIN (BUILD_INFO sha=${PI_SHA:-unknown})"
    # The Pi lane runs its OWN config's character (the operator's Pi
    # credentials file names it; never printed). Resolve it before any
    # seeding so seed_char targets the right row.
    pi_char="$(pi_remote "grep -o '\"character\"[[:space:]]*:[[:space:]]*\"[^\"]*\"' '$PI_CFG' | head -1 | sed 's/.*\"\([^\"]*\)\"$/\1/'" 2>/dev/null | tr -d '\r[:space:]')"
    [ -n "$pi_char" ] && CHAR="$pi_char"
    [ -n "$CHAR" ] || { say "could not read the Pi config's character from $PI_CFG"; pi_lock_release "kb-b854-pi-zone-sweep"; eqemu_lock_release; exit 68; }
    say "pi character: ${CHAR}"
fi

SNAPSHOT="$OUT_DIR/snapshot_before.tsv"
AFTER="$OUT_DIR/snapshot_after.tsv"
CLEANED=0
RESTORE_BAD=0
CLIENT_PID=""
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    [ -n "$CLIENT_PID" ] && kill "$CLIENT_PID" >/dev/null 2>&1
    [ -n "$CLIENT_PID" ] && sleep 2
    [ -n "$CLIENT_PID" ] && kill -9 "$CLIENT_PID" >/dev/null 2>&1
    if [ "$PI" -eq 1 ]; then
        # A half-dead Pi client must not outlive the lock hold.
        pi_remote "pkill -9 -f 'willeq -c ${PI_CFG}' >/dev/null 2>&1; true" >/dev/null 2>&1 || true
    fi
    # The ONE snapshot, restored with the world DOWN -- the lane's discipline
    # (T493). Runs on EVERY exit path, --selftest included.
    eqemu_restore_snapshot "$SNAPSHOT" "$AFTER" say || RESTORE_BAD=1
    bash "$REPO/scripts/eqemu/down.sh" >"$OUT_DIR/stack_down.log" 2>&1
    say "stack down"
    # Locks released in REVERSE of acquisition: pi first, eqemu second.
    [ "$PI_LOCK_TAKEN" -eq 1 ] && pi_lock_release "kb-b854-pi-zone-sweep"
    eqemu_lock_release
    say "locks released"
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
# VM lane only: the Pi lane runs the client on the Pi with its own config
# ($PI_CFG, the operator's Pi credentials -- never printed here or in logs).
CFG="$OUT_DIR/client.json"
if [ "$PI" -ne 1 ]; then
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
fi

TSV_STARTED=0
new_tsv() {
    printf '%s\n' "$TSV_HEADER" > "$TSV"
    TSV_STARTED=1
}
# Resume: a TSV from an earlier (possibly partial) run is CONTINUED, not
# truncated -- the verdict counts the whole file. A file without the header
# is not a resume target: replace it, so a stray file cannot poison the run.
# A file whose header belongs to the OTHER lane (17 vs 22 columns) is a
# hard error: appending this lane's rows to it would make the file
# un-parseable, and silently replacing it would lose the other lane's
# measurements. Use a different --out-dir for that run.
if [ -f "$TSV" ]; then
    if head -1 "$TSV" | grep -qx "$TSV_HEADER"; then
        TSV_STARTED=1
        say "resuming: $(($(grep -c . "$TSV") - 1)) row(s) already in $TSV"
    elif head -1 "$TSV" | grep -q 'last_memledger_point'; then
        say "ERROR: $TSV is a Pi-lane TSV (22-column header) but this run is the ${LANE} lane -- use a different --out-dir"
        exit 3
    else
        say "ERROR: $TSV has an unknown header -- use a different --out-dir (refusing to overwrite it)"
        exit 3
    fi
else
    new_tsv
fi

# ---- the Pi-lane helpers (B854) ---------------------------------------------
# The client's log streams to /tmp/kbpi/.run_<zone>.log (tmpfs, GROWS LIVE --
# this is what the watchdog polls over ssh) and is copied to the Pi's NFS
# review tree ($PI_LOGS/<zone>.log) at the end of the run. The NFS copy is
# what a later re-run's resume reads; the /tmp copy is deleted per zone.
#
# pi_dmesg_diff <outfile> <lastmarkfile> -- OOM evidence: the dmesg lines
# added since the LAST zone boundary, filtered to OOM-killer lines. The mark
# file holds the dmesg line count at the previous boundary so a zone only
# sees its own kills (the kernel ring buffer persists across zones; after a
# Pi reboot the count resets and the diff is the whole buffer, which is
# correct: everything since boot is "new").
pi_dmesg_diff() {
    local out="$1" mark="$2" now before
    now="$(pi_remote 'sudo dmesg | wc -l' | tr -d '[:space:]')"
    before="$(cat "$mark" 2>/dev/null || echo 0)"
    : > "$out"
    if [ -n "$now" ] && [ "$now" -gt "$before" ]; then
        pi_remote "sudo dmesg | tail -n +$((before + 1))" 2>/dev/null |
            grep -iE "oom|killed process|out of memory" > "$out" || true
    fi
    echo "${now:-0}" > "$mark"
}

# pi_run_line_count <short> -- the line count of the LIVE /tmp run log (the
# watchdog's signal). Empty when the ssh call itself fails: the caller
# distinguishes "count unchanged" (silence) from "count unknown" (NFS/ssh
# hiccup -- do NOT start the silence clock on that).
pi_run_line_count() {
    pi_remote "wc -l < /tmp/kbpi/.run_${1}.log 2>/dev/null" 2>/dev/null | tr -d '[:space:]'
}

# pi_top3_categories <log> -- the three largest [memledger] CATEGORIES at the
# LAST lifecycle point of the log, "name(bytes);name(bytes);name(bytes)".
# The ledger line is "[memledger] point=X zone=Y seq=N enabled=1 rss_anon=...
# <kv fields> allocs=N <category>=live/peak ..." (toLogLine, memory_ledger.cpp):
# everything AFTER "allocs=" is a category's live/peak pair, everything before
# it is a scalar field that would otherwise match the same shape (seq,
# enabled, ledger_peak, malloc_inuse ...). unattributed is a bucket, not a
# category, and is excluded (the card asks for categories).
pi_top3_categories() {
    local log="$1"
    grep '\[memledger\] point=' "$log" 2>/dev/null | tail -1 |
        sed -E 's/^.* allocs=[0-9]+//' |
        grep -oE '[a-z_]+=[0-9]+/[0-9]+' |
        sed -E 's/^([a-z_]+)=([0-9]+)\/[0-9]+$/\2 \1/' |
        grep -vE '^[0-9]+ unattributed$' |
        sort -k1,1nr | head -3 | awk '{printf "%s(%d);", $2, $1}' | sed 's/;$//'
}

# run_zone_pi <zone> <expansion> <s3d_bytes> -- the Pi client run, B854's
# Will recipe, driven over ssh. ONE process on the Pi (its /tmp is a ~242 MB
# tmpfs). The watchdog polls the remote /tmp log's line count every 10 s:
# no new line for PI_WATCHDOG_S = dead, kill it THERE (a process-level
# timeout cannot catch a loader hang that never logs, and the OOM killer
# SIGKILLs without the client's catchable-shutdown path). An OOM kill shows
# up as exit 137 plus a dmesg line.
run_zone_pi() {
    local short="$1" expansion="$2" s3d_bytes="$3"
    local nlog="${PI_LOGS}/${short}.log"
    local hostlog="$LOGS/${short}_pi_client.log"
    local start end code status wall fatal_n error_n error_map first_fatal loaded zname spawns doors placeholder late clean
    local last_point peak_rss top3 oom oomline
    code=""
    [ "$TSV_STARTED" -eq 1 ] || new_tsv

    say "=== ${short} (${expansion:-?}, ${s3d_bytes:-?} s3d bytes) on the PI ==="
    pi_dmesg_diff "$LOGS/${short}_dmesg.diff" "$LOGS/.dmesg_mark"

    # Will's Pi recipe, verbatim from the card: the composite entities stay
    # OFF via the orangepi preset; the data root is the ARM build's own tree
    # (config/ + data/ ship there); the cache goes to the Pi's /tmp. The
    # inner scenario timeout is TIMEOUT_S - 60 so the outer `timeout` (the
    # wall-clock bound) always gets the last word. $PI_LOG_FLAGS is the
    # card's log-flag slot (default: --log-level=INFO, which is the level
    # that carries the [memledger] lines).
    local remote_cmd
    remote_cmd="mkdir -p /tmp/kbpi/cache '$PI_LOGS' && cd '$PI_DIR' && WILLEQ_DATA_ROOT='$PI_DIR' WILLEQ_CACHE_DIR=/tmp/kbpi/cache timeout -s TERM ${TIMEOUT_S} '$PI_BIN' -c '$PI_CFG' --drm --renderer gles2 --constrained orangepi -r 1280 720 --scenario zone_in --timeout $((TIMEOUT_S - 60)) $PI_LOG_FLAGS > /tmp/kbpi/.run_${short}.log 2>&1; echo \$? > /tmp/kbpi/.code_${short}; cp /tmp/kbpi/.run_${short}.log '$nlog'"

    start=$(date +%s)
    # </dev/null: a backgrounded child inherits this loop's here-string
    # stdin, and ssh (pi_remote) relays non-TTY stdin to the remote
    # shell -- the remote command is fully quoted, so ssh would swallow
    # the REMAINING loop lines (the next zones) as stdin. One iteration,
    # then read-EOF. (B854 selftest, 2026-09-21: sebilis never ran.)
    pi_remote "$remote_cmd" </dev/null >/dev/null 2>&1 &
    CLIENT_PID=$!
    local last_count="" silent_since=0
    while kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 10
        local now_count
        now_count="$(pi_run_line_count "$short")"
        if [ -n "$now_count" ] && [ "$now_count" != "$last_count" ]; then
            last_count="$now_count"
            silent_since=0
        elif [ -z "$now_count" ]; then
            : # ssh hiccup: keep the previous silence clock
        else
            [ "$silent_since" -eq 0 ] && silent_since=$(date +%s)
            if [ $(( $(date +%s) - silent_since )) -ge "$PI_WATCHDOG_S" ]; then
                say "watchdog: $short Pi client silent for ${PI_WATCHDOG_S}s (last ${last_count:-0} log lines) -- killing it on the Pi"
                pi_remote "pkill -9 -f 'willeq -c ${PI_CFG}' >/dev/null 2>&1; true" >/dev/null 2>&1
                break
            fi
        fi
    done
    wait "$CLIENT_PID" 2>/dev/null
    code="$(pi_remote "cat /tmp/kbpi/.code_${short} 2>/dev/null" | tr -d '[:space:]')"
    [ -n "$code" ] || code=137
    CLIENT_PID=""
    end=$(date +%s)
    wall=$((end - start))

    # Pull the client log to this host for the evidence dir (the Pi's /tmp
    # copy is deleted; the NFS copy stays for the operator).
    pi_remote "cat '$nlog' 2>/dev/null" > "$hostlog" 2>/dev/null || : > "$hostlog"
    pi_remote "rm -f /tmp/kbpi/.run_${short}.log /tmp/kbpi/.code_${short}" >/dev/null 2>&1 || true

    fatal_n="$(grep -c ' \[FATAL\] ' "$hostlog" 2>/dev/null || true)"
    error_map="$(grep -c ' \[ERROR\] \[MAP\] ' "$hostlog" 2>/dev/null || true)"
    error_n=$(( $(grep -c ' \[ERROR\] ' "$hostlog" 2>/dev/null || true) - error_map ))
    first_fatal="$(grep -m1 ' \[FATAL\] ' "$hostlog" 2>/dev/null | tr -d '	' | head -c 240)"
    # Same marker as the VM lane: the game-thread zone-geometry load, named
    # with the server's short_name -- the "did we land in the zone we
    # seeded" assertion.
    if grep -q "LoadZoneSources: Loaded zone '${short}'" "$hostlog" 2>/dev/null; then loaded="y"; else loaded="n"; fi
    zname="$(grep -m1 "scenario 'zone_in' starting: zoned in to" "$hostlog" 2>/dev/null | sed -n "s/.*zoned in to '\([^']*\)'.*/\1/p")"
    spawns="$(grep -oE 'Loaded [0-9]+ spawns in zone' "$hostlog" 2>/dev/null | grep -oE '[0-9]+' | head -1)"; spawns="${spawns:-0}"
    doors="$(grep -oE 'Received [0-9]+ doors' "$hostlog" 2>/dev/null | grep -oE '[0-9]+' | head -1)"; doors="${doors:-0}"
    placeholder="$( { grep -c 'has no model in any S3D archive' "$hostlog" 2>/dev/null || true; grep -c 'rendered as placeholder cubes' "$hostlog" 2>/dev/null || true; } | awk '{s+=$1} END {print s+0}' )"
    late="$(grep -c 'EQE1: late-open' "$hostlog" 2>/dev/null || true)"

    # B854's memory columns, from the client's OWN [memledger] lines (T491:
    # the ledger prints at every lifecycle point -- startup, login_world,
    # ready, zone_load_*, steady_60s, shutdown -- each carrying rss_anon and
    # every category's live/peak bytes). LAST lifecycle point = the last
    # [memledger] line of the log; peak RssAnon = the max across ALL of them
    # (the ledger prints at a fixed point list, so the last line is not
    # necessarily the peak); the three largest categories at that point.
    last_point="$(grep -oE '\[memledger\] point=[a-z_0-9]+' "$hostlog" 2>/dev/null | tail -1 | sed 's/.*point=//')"
    peak_rss="$(grep -oE 'rss_anon=[0-9]+' "$hostlog" 2>/dev/null | grep -oE '[0-9]+' | sort -n | tail -1)"
    peak_rss="${peak_rss:-0}"
    top3="$(pi_top3_categories "$hostlog")"
    top3="${top3:-none}"

    # OOM evidence: the dmesg diff since the last boundary (the pre-run diff
    # already covers boot/login; this one covers everything through the end
    # of the client process).
    pi_dmesg_diff "$LOGS/${short}_dmesg.diff" "$LOGS/.dmesg_mark"
    if [ -s "$LOGS/${short}_dmesg.diff" ]; then
        oom="y"
        oomline="$(head -1 "$LOGS/${short}_dmesg.diff" | tr '	' ' ' | head -c 240)"
    else
        oom="n"
        oomline=""
    fi
    # A SIGKILL that dmesg does NOT attribute to the OOM killer is still a
    # death (our own watchdog, or an unexplained kill): record which.
    if [ "$code" -eq 137 ] && [ "$oom" = "n" ]; then
        say "WARN: $short died 137 (SIGKILL) with no OOM line in the dmesg diff -- see $LOGS/${short}_dmesg.diff"
    fi

    if [ "$code" -ge 128 ]; then status="died_by_signal_$((code-128))"
    else status="ran"; fi

    clean=0
    if [ "$code" -eq 0 ] && [ -n "$zname" ] && [ "$zname" = "$short" ] \
       && [ "${fatal_n:-0}" -eq 0 ] && [ "${error_n:-0}" -eq 0 ] \
       && [ "$loaded" = "y" ] && [ "${placeholder:-0}" -eq 0 ]; then
        clean=1
    fi
    printf '%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s\n' \
        "$short" "${expansion:-}" "${s3d_bytes:-}" "$status" "$code" "$wall" \
        "${fatal_n:-0}" "${error_n:-0}" "${error_map:-0}" "${first_fatal:-}" "$loaded" "${zname:-}" \
        "$spawns" "$doors" "${placeholder:-0}" "${late:-0}" "$clean" \
        "${last_point:-none}" "$peak_rss" "$top3" "$oom" "$oomline" >> "$TSV"
    say "row: $short exit=$code wall=${wall}s fatal=${fatal_n:-0} err=${error_n:-0} peak_rss_anon=${peak_rss}B last_point=${last_point:-none} oom=$oom clean=$clean"

    # The late-save trap, the other direction: the zone writes the character
    # back on linkdead (and again at its own shutdown). Let it land BEFORE we
    # stop the process -- killing the zone first could race the write, and
    # the next seed's readback-retry loop would then fight a stale row.
    sleep "$LATE_SAVE_S"
    # Free the zone's port NOW (see kill_zone). Runs on every exit path,
    # including a dead client: a partially-booted zone process still holds
    # its port.
    kill_zone "$short"
    [ "$clean" -eq 1 ]
}

# run_zone <zone> <expansion> <s3d_bytes>
#
# The PLACEMENT (boot the zone fresh, seed the character at its safe point,
# free the port afterwards) is lane-agnostic and shared: B853's
# boot-then-seed order, proven 46/46 on the VM lane. The character is
# OFFLINE for the whole boot+seed window in BOTH lanes (it connects only for
# the client run), so the LINKDEAD cache the boot-zone comment warns about
# (a connected character's pose is never re-read by a live zone) cannot
# affect an offline seed -- the seed's own readback-retry is what pins the
# pose, in either order. Only the CLIENT RUN differs per lane: this host
# (VM) or the Orange Pi (Pi).
run_zone() {
    local short="$1" expansion="$2" s3d_bytes="$3"
    local start end code status wall fatal_n error_n error_map first_fatal loaded zname spawns doors placeholder late clean age
    code=""
    [ "$TSV_STARTED" -eq 1 ] || new_tsv

    say "=== ${short} (${expansion:-?}, ${s3d_bytes:-?} s3d bytes, ${LANE} lane) ==="
    if ! boot_zone "$short"; then
        if [ "$PI" -eq 1 ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$short" "$expansion" "${s3d_bytes:-}" "zone_boot_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 "none" 0 "none" "n" "" >> "$TSV"
        else
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$short" "$expansion" "${s3d_bytes:-}" "zone_boot_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 >> "$TSV"
        fi
        say "row: $short zone_boot_failed"
        return 1
    fi
    if ! seed_char "$short"; then
        if [ "$PI" -eq 1 ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$short" "$expansion" "${s3d_bytes:-}" "seed_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 "none" 0 "none" "n" "" >> "$TSV"
        else
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$short" "$expansion" "${s3d_bytes:-}" "seed_failed" "-" "-" 0 0 0 "" "n" "" 0 0 0 0 0 >> "$TSV"
        fi
        say "row: $short seed_failed"
        return 1
    fi

    if [ "$PI" -eq 1 ]; then
        run_zone_pi "$short" "$expansion" "$s3d_bytes"
        return
    fi
    local log="$LOGS/${short}_client.log"

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
#
# B854 selftest fix (2026-09-21): the zone loop used <<< "$ZONE_ROWS", a
# here-string that feeds stdin. Any foreground pi_remote call inside run_zone
# (ssh) inherits that stdin and relays it to the remote shell, swallowing
# remaining loop lines. The fix: write sorted rows to a temp file and read
# the index with mapfile — the loop body never touches stdin.
ZONE_TMPFILE="$(mktemp)"
trap 'rm -f "$ZONE_TMPFILE"' EXIT
printf '%s' "$ZONE_ROWS" | sort -t"$(printf '\t')" -k3,3nr > "$ZONE_TMPFILE"
ZONE_COUNT="$(grep -c . "$ZONE_TMPFILE")"

FAILURES=0
RUN_STARTED=$(date +%s)
STOPPED_AT_BOUNDARY=0
mapfile -t ZONE_LINES < "$ZONE_TMPFILE"
for (( idx=0; idx<ZONE_COUNT; idx++ )); do
    if [ "$STOPPED_AT_BOUNDARY" -eq 1 ]; then continue; fi
    IFS=$'\t' read -r z expansion s3d <<< "${ZONE_LINES[$idx]}"
    [ -n "$z" ] || continue
    # B854 runtime budget: stop CLEANLY at the zone boundary once 80 % of
    # --pi-runtime-limit has elapsed. The TSV resumes; the remaining zones
    # are printed below and in the verdict.
    if [ "$PI_RUNTIME_LIMIT_MIN" -gt 0 ]; then
        ELAPSED_MIN=$(( ($(date +%s) - RUN_STARTED) / 60 ))
        BUDGET_80=$(( PI_RUNTIME_LIMIT_MIN * 80 / 100 ))
        if [ "$ELAPSED_MIN" -ge "$BUDGET_80" ]; then
            STOPPED_AT_BOUNDARY=1
            say "runtime budget: ${ELAPSED_MIN} min elapsed >= 80% of ${PI_RUNTIME_LIMIT_MIN} min -- stopping at the zone boundary (resume with the remaining zones)"
            continue
        fi
    fi
    if ! run_zone "$z" "${expansion:-}" "${s3d:-}"; then
        FAILURES=$((FAILURES + 1))
    fi
    say "progress: $(($(grep -c . "$TSV") - 1)) row(s) recorded, ${FAILURES} failure(s) in this run"
done

# ---- 5. verdict -------------------------------------------------------------
ROWS=$(( $(grep -c . "$TSV") - 1 ))
CLEAN_ROWS="$(awk -F'\t' 'NR>1 && $17=="1"' "$TSV" | wc -l)"
say "sweep result: ${CLEAN_ROWS}/${ROWS} rows clean in ${TSV}"
awk -F'\t' 'NR>1 && $17!="1" {printf "  %s: %s (exit %s, fatal=%s err=%s) -- %s\n", $1, $4, $5, $7, $8, ($10!=""?$10:"(no FATAL; see log)")}' "$TSV" >&2
if [ "$STOPPED_AT_BOUNDARY" -eq 1 ]; then
    # Remaining = requested zones (heavy-first) minus the rows already
    # recorded in the TSV (which also covers a resumed run's earlier rows).
    DONE="$(tail -n +2 "$TSV" | cut -f1 | tr '\n' ' ')"
    say "zones recorded so far: ${DONE}"
    REMAIN="$(printf '%s\n' "$ZONE_ROWS" | cut -f1 | while read -r zz; do grep -q "^${zz}\b" <(tail -n +2 "$TSV" | cut -f1) || printf '%s ' "$zz"; done)"
    REMAIN="$(echo "$REMAIN" | sed 's/ $//; s/ /,/g')"
    say "zones remaining (resume: --zones $REMAIN --out-dir $OUT_DIR)"
fi

if [ "$RESTORE_BAD" -eq 1 ]; then
    say "restore mismatch overrides the sweep result"
    exit 66
fi
[ "$FAILURES" -eq 0 ] && exit 0
exit 1
