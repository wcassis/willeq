#!/usr/bin/env bash
# B504 (issue #90): the regression net for up.sh's start-zone branch.
#
# The defect it pins: up.sh once gated "the zone is really booted" on a single
# grep string (`Zone bootup type`) with no liveness check and nothing in the
# tree to keep the string honest. When the eqemu build stopped printing it, a
# COLD up.sh timed out after 60 s on a zone that booted perfectly (the original
# T457 evidence; scenario.sh turns the exit 1 into operator code 65). When the
# build started printing it again (binaries 2026-02-24, re-measured 2026-09-19)
# the line went GREEN silently -- nothing in the tree had noticed either way.
#
# So this net RE-MEASURES the marker on every run, the same way
# ready_selftest.sh re-measures the B797 premise:
#
#   1. take the lock, bring the stack up (up.sh adopts the launcher fleet)
#   2. kill the FULL supervisor chain (eqemu_kill_supervisor_chain) so that no
#      zone process exists and nothing can respawn one -- the only way to reach
#      up.sh's start-zone branch, which in normal operation is almost never
#      taken because the container's process-watcher respawns the fleet
#   3. run up.sh AGAIN: with no zone process it must take its start-zone
#      branch and come back 0, which is only possible if the marker it waits
#      for (eqemu_zone_booted) still matches what this build prints. If the
#      string ever rots (the B504 failure mode), this step fails, naming the
#      lines the zone actually printed; a return-0 without the start-zone log
#      means up.sh adopted a respawned fleet and is a setup failure, not a
#      verdict pass.
#   4. KILL the zone process up.sh just proved ready, and require
#      eqemu_zone_booted to go FALSE (the liveness half of the check: a marker
#      line in the log is not enough if the process is gone)
#   5. VERDICT: a deliberately BOGUS shortname must fail eqemu_zone_booted
#      quickly with the `Zone Bootup failed` hard stop (MEASURED 2026-09-19:
#      the zone prints the failure line) -- not a 60 s timeout
#   6. bring the stack back up and assert ready again, leaving the lane as
#      found (stack down, lock released)
#
# Usage:
#   scripts/eqemu/zone_booted_selftest.sh [--unit T###] [--no-lock]
#                                         [--keep-server] [--kill-deadline N]
#
# Exit: 0 pass | 64 lock held | 65 setup failure (up.sh, or the supervisor won
#       the kill race) | 70 VERDICT: a bogus shortname was reported as booted
#       | 71 the B504 premise did not reproduce (marker check said "booted"
#       with the zone process dead, or the failure line was not caught)
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"

UNIT="B504"
TAKE_LOCK=1
KEEP_SERVER=0
KILL_DEADLINE=60
while [ $# -gt 0 ]; do
    case "$1" in
        --unit) UNIT="$2"; shift 2 ;;
        --no-lock) TAKE_LOCK=0; shift ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --kill-deadline) KILL_DEADLINE="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

# Evidence goes to the cache dir (T402's rule for generated files), never into
# the checkout and never to an NFS path.
OUTDIR="${WILLEQ_CACHE_DIR:-${TMPDIR:-/tmp}}/eqemu_zone_booted_selftest"
mkdir -p "$OUTDIR"
PSLOG="$OUTDIR/ps.txt"
RESULT="$OUTDIR/result.txt"
: > "$PSLOG"

say() { printf '[zone_booted_selftest] %s %s\n' "$(date '+%H:%M:%S')" "$*" >&2; }
census() {
    {
        echo "---- $1 ($(date -Iseconds)) ----"
        echo "loginserver=$(server_process_count loginserver) world=$(server_process_count world) zone=$(server_process_count zone)"
        eq_docker exec "${EQEMU_CONTAINER}" bash -c 'ps -eo pid,ppid,args' 2>&1 | head -40
    } >> "$PSLOG" 2>&1
}

if [ "$TAKE_LOCK" -eq 1 ]; then
    if ! eqemu_lock_take "${UNIT} zone_booted_selftest"; then
        say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
        exit 64
    fi
    say "lock taken"
fi

CLEANED=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" || true
        say "server down"
    fi
    [ "$TAKE_LOCK" -eq 1 ] && { eqemu_lock_release; say "lock released"; }
}
trap cleanup EXIT INT TERM

SHORT="${EQ_TEST_ZONE_SHORTNAME:-arena}"

# ---- 1. the stack up -------------------------------------------------------
say "bringing eqemu up (up.sh adopts anything already running)"
bash "$REPO/scripts/eqemu/up.sh" || { say "up.sh failed"; exit 65; }
census "after up.sh"

# ---- 2. no zone process, and nothing can respawn one -----------------------
say "killing the FULL supervisor chain (process-watcher -> spire -> launcher -> fleet)"
eqemu_kill_supervisor_chain
if ! eqemu_fleet_at_zero "$KILL_DEADLINE"; then
    say "SETUP FAIL: the fleet is still up after ${KILL_DEADLINE}s -- a respawner survived, see $PSLOG"
    census "kill deadline"
    exit 65
fi
sleep 10
if [ "$(server_process_count zone)" != "0" ] || [ "$(server_process_count world)" != "0" ]; then
    say "SETUP FAIL: the fleet RESPAWNED during the 10 s grace -- the supervisor chain is not dead"
    census "respawned"
    exit 65
fi
say "no zone process, and the supervisor chain stayed dead for 10 s"

# ---- 3. cold up.sh through its OWN start-zone branch -----------------------
# The most faithful pin (card AC #2: "cold up.sh exits 0 only when the zone is
# really ready"): with no zone process and no respawner, up.sh itself must take
# the start-zone branch and come back 0. That is only possible if the marker it
# waits for (eqemu_zone_booted) still matches what this build prints — so if
# the string ever rots, THIS step fails, naming the lines the zone actually
# printed. Guard: if up.sh instead ADOPTED a respawned fleet (log file never
# created), that is a setup problem, not a verdict pass -- fail loudly.
say "running up.sh (its start-zone branch) with the supervisor chain dead"
server_exec "rm -f logs/zone_${SHORT}_h03.log"
if ! bash "$REPO/scripts/eqemu/up.sh" >> "$OUTDIR/up_startzone.log" 2>&1; then
    MARKER_LINES="$(server_exec "grep -E 'Bootup' logs/zone_${SHORT}_h03.log" 2>/dev/null | head -10 || true)"
    say "VERDICT FAIL (B504 failure mode): up.sh's start-zone branch did not come back 0"
    say "the lines the zone actually printed:"
    printf '%s\n' "${MARKER_LINES}" >&2
    say "up.sh's own tail:"
    tail -20 "$OUTDIR/up_startzone.log" >&2 || true
    exit 71
fi
if ! server_exec "test -s logs/zone_${SHORT}_h03.log" >/dev/null 2>&1 \
   || ! server_exec "grep -q 'Zone bootup type' logs/zone_${SHORT}_h03.log" >/dev/null 2>&1; then
    say "SETUP FAIL: up.sh returned 0 but the start-zone log ${SHORT} is missing/empty -- it likely ADOPTED a respawned fleet, not a clean start-zone"
    tail -20 "$OUTDIR/up_startzone.log" >&2 || true
    exit 65
fi
if [ "$(server_process_count "zone ${SHORT}")" = "0" ]; then
    say "VERDICT FAIL: up.sh returned 0 but no zone ${SHORT} process is running"
    exit 71
fi
census "after up.sh start-zone branch"
say "cold up.sh took the start-zone branch, came back 0, and the zone ${SHORT} process is alive"

# ---- 4. the liveness half --------------------------------------------------
say "killing the zone process to prove the liveness half of the check"
eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '[b]in/zone ${SHORT}'" 2>/dev/null || true
w=0
until [ "$(server_process_count "zone ${SHORT}")" = "0" ]; do
    if [ "$w" -ge 30 ]; then say "SETUP FAIL: zone ${SHORT} would not die"; exit 65; fi
    sleep 1; w=$((w+1))
done
# The log still contains the marker line; the check must now be FALSE because
# the process is gone. (eqemu_zone_booted waits for the marker first, so it
# returns at the liveness check -- give it a short budget so a FALSE comes back
# promptly rather than after the full 120 s.)
if eqemu_zone_booted "$SHORT" "logs/zone_${SHORT}_h03.log" 5; then
    say "VERDICT FAIL: eqemu_zone_booted says BOOTED with the zone process dead"
    exit 71
fi
say "marker check is FALSE with the marker in the log but the process dead (liveness half holds)"

# ---- 5. the bogus-shortname verdict ----------------------------------------
# world (step 3) is still running; a bogus zone must be REJECTED quickly by the
# `Zone Bootup failed` hard stop, not accepted and not waited out for the full
# timeout. (MEASURED 2026-09-19: `./bin/zone doesnotexist` prints
# `Zone Bootup failed :: Zone::Bootup` and exits.)
say "starting zone with a BOGUS shortname (must fail fast, clearly)"
server_exec "rm -f logs/zone_doesnotexist_h03.log"
server_exec_bg "./bin/zone doesnotexist > logs/zone_doesnotexist_h03.log 2>&1"
t0=$(date +%s)
if eqemu_zone_booted "doesnotexist" "logs/zone_doesnotexist_h03.log" 60; then
    say "VERDICT FAIL: a bogus shortname was reported as booted"
    exit 70
fi
t1=$(date +%s)
say "bogus shortname rejected after $((t1-t0))s (the Zone Bootup failed hard stop; a full timeout would be 60s)"
# Prove the REJECTION came from the failure line, not a timeout: the line must
# be in the log. (A failed zone process may still be lingering in the
# container — MEASURED: `./bin/zone <bogus>` prints the failure, keeps
# "listening" on a port world assigned it, and does not always exit — so the
# rejection's proof is the log line, not the process's death. down.sh's
# `/bin/(loginserver|world|zone)( |$)` pkill reaps it at teardown.)
if ! server_exec "grep -q 'Zone Bootup failed' logs/zone_doesnotexist_h03.log" >/dev/null 2>&1; then
    say "SETUP FAIL: bogus shortname was rejected, but the 'Zone Bootup failed' line is not in the log -- the hard stop did not fire"
    exit 65
fi
server_exec "grep -E 'Bootup failed' logs/zone_doesnotexist_h03.log" >> "$PSLOG" 2>&1 || true

# ---- 6. and it comes back --------------------------------------------------
# up.sh (step 1) already proved the bring-up path once; here we only re-check
# the readiness definition after tearing the hand-started trio down: kill the
# trio, confirm eqemu_stack_ready is FALSE (no world/loginserver), then bring
# the stack up again and confirm TRUE.
say "bringing the stack back up"
eq_container_pkill KILL '/bin/(loginserver|world|zone|ucs)( |$)' || true
eqemu_fleet_at_zero "$KILL_DEADLINE" || { say "SETUP FAIL: could not bring the trio down"; exit 65; }
if eqemu_stack_ready; then
    say "SETUP FAIL: eqemu_stack_ready is TRUE with the fleet at zero"
    exit 65
fi
say "eqemu_stack_ready is FALSE with the fleet at zero (as it must be)"
bash "$REPO/scripts/eqemu/up.sh" || { say "up.sh failed on the second bring-up"; exit 65; }
if ! eqemu_stack_ready; then
    say "SETUP FAIL: not ready after the second up.sh"
    exit 65
fi
census "after second up.sh"

{
    echo "unit=${UNIT} verdict=PASS"
    echo "marker=TRUE for ${SHORT}, FALSE when the process died, bogus shortname rejected in $((t1-t0))s"
    echo "eqemu_host=${EQEMU_HOST} container=${EQEMU_CONTAINER}"
    date -Iseconds
} > "$RESULT"
say "SELFTEST PASS: marker re-measured, liveness half holds, bogus shortname rejected"
say "evidence: $RESULT"
exit 0
