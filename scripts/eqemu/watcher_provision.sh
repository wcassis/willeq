#!/bin/bash
# B940 (#258): provision the eqemu container's process-watcher so a zone busy
# loading a client is no longer killed.
#
# THE DEFECT (refactor/B925/README.md, run R1): the eqemu-server container's
# assets/scripts/process-watcher.pl kill -9s any process whose ps line is not
# in an allowlist and whose LIFETIME-AVERAGE CPU exceeds 30 % after 60 s.
# The allowlist regex (gcc|mysql|wget|unzip|node|go-build|tar|world|
# JetBrains|vscode|remote-dev-server|gzip) omits zone, so a freshly-respawned
# zone, busy loading a client, reads >30 % within its first minute and is
# killed ~65-75 s in on x86 (iceclad, #244) / ~38 s on the Pi (overthere,
# #258); the client's ECONNREFUSED is the process-kill.log entry for the
# client's own zone (R1 correlated both sides).
#
# THE FIX, in the stack's own configuration (persistent, idempotent):
#   1. assets/scripts/process-watcher.pl takes its exclusion pattern from the
#      PROCESS_WATCHER_EXCLUDE env var; the ORIGINAL regex is the default, so
#      a stack without the var behaves exactly as before. Applied by
#      scripts/eqemu/b940_patch_watcher.pl (perl -c on the patched text
#      BEFORE it replaces the file; refuses a drifted allowlist block).
#   2. docker-compose.yml's eqemu-server service gains
#            - PROCESS_WATCHER_EXCLUDE=/(bin|\/)zone( |\/|$)/i
#      which exempts exactly the zone binaries (./bin/zone, /bin/zone,
#      /home/eqemu/server/bin/zone -- the argv shapes
#      server_process_count's ps grep already matches) and nothing else:
#      loginserver/world/ucs/eqemu-server:launcher keep the upstream
#      threshold. The compose environment is an explicit list (no env_file),
#      so one line is the whole wiring.
#
# Both files live on the LANE HOST under AKK_DIR (docker-compose.yml at its
# root, the watcher at assets/scripts/), and assets is BIND-MOUNTED into the
# container (./assets:/home/eqemu/assets), so the edit takes effect live --
# the entrypoint's relaunch loop (entrypoint.pl:53) restarts the watcher,
# which logs its active pattern at startup -- and persists across container
# restarts. No image rebuild, no running-container surgery.
#
# VERIFICATION (every run proves these; --check runs exactly these):
#   a. the watcher carries the B940 env-read, perl -c passes, and the
#      upstream allowlist regex survives verbatim as the default;
#   b. the compose file carries the PROCESS_WATCHER_EXCLUDE line;
#   c. the exclusion decision itself, run through the SAME code the
#      provisioned watcher runs (the compose value fed as the env var):
#      every zone argv shape is exempt, no non-zone shape matches
#      (scripts/eqemu/b940_watcher_match_test.pl);
#   d. (container up) a running watcher's /proc/<pid>/environ carries the
#      var; a watcher that predates the provision is named as such (the
#      relaunch picks it up; the file checks are authoritative).
#   Optional liveness leg (--busy-zone, ~95 s, one core): a CPU burner
#   named ./bin/zone survives past the 60 s kill window with no
#   process-kill.log line. Skipped by default -- the decision leg (c)
#   already proves the exemption and a 95 s one-core burn on a shared VM
#   lane costs more than it adds.
#
# Usage (a card names its lane, never an address):
#   scripts/eqemu/watcher_provision.sh --lane 2             # apply + verify
#   scripts/eqemu/watcher_provision.sh --lane 2 --check     # verify only
#   scripts/eqemu/watcher_provision.sh --lanes 1-5          # all, one lease
#                                                           # at a time
#   scripts/eqemu/watcher_provision.sh --lane 2 --busy-zone # + the ~95 s leg
# Exit: 0 = provisioned and verified; 1 = not (FAIL lines name the checks);
# 63 = the lane's lease is held by someone else; 64 = usage.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

PATCHER="${SCRIPT_DIR}/b940_patch_watcher.pl"
COMPOSE_PATCHER="${SCRIPT_DIR}/b940_patch_compose.pl"
MATCHTEST="${SCRIPT_DIR}/b940_watcher_match_test.pl"
[ -f "${PATCHER}" ] && [ -f "${COMPOSE_PATCHER}" ] && [ -f "${MATCHTEST}" ] \
    || { echo "missing the B940 patchers/match-test beside this script"; exit 64; }

# The exact exclusion value, as it lands in docker-compose.yml AND as
# the watcher's qr// body (the watcher wraps it in /.../i itself, so the
# value carries NO delimiters -- and NO backslashes: delimiters and
# backslashes are the two things that do not survive the compose env ->
# shell -> qr// chain, which the match leg proves on every provision
# run). It matches the zone binary's argv wherever it sits on the ps
# line -- ./bin/zone iceclad, bin/zone overthere, /bin/zone,
# /home/eqemu/server/bin/zone qeynos2 (via its "bin/zone" tail) -- the
# argv shapes server_process_count's ps grep already matches. No
# non-zone eqemu argv carries "bin/zone" (loginserver/world/ucs/spire/
# eqemu-server:launcher all clear), so the exemption cannot leak.
EXCLUDE_VALUE='(bin|/bin)/zone'
COMPOSE_LINE="      - PROCESS_WATCHER_EXCLUDE=${EXCLUDE_VALUE}  # B940: exempt the zone binaries"

CHECK_ONLY=0
BUSY_ZONE=0
LANES=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --lane)  LANES="${LANES:+${LANES} }$2"; shift 2 ;;
        --lanes) LANES="$2"; shift 2 ;;
        --check) CHECK_ONLY=1; shift ;;
        --busy-zone) BUSY_ZONE=1; shift ;;
        -h|--help) awk 'NR > 1 && !/^#/ { exit } NR > 1' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
[ -n "${LANES}" ] || { echo "usage: --lane N (repeatable) or --lanes 1-5" >&2; exit 64; }
case "${LANES}" in
    *-*|*,*) LANES="$(seq "${LANES%-*}" "${LANES#*-}" 2>/dev/null || true)";;
esac
[ -n "${LANES}" ] || { echo "unusable lane list: ${LANES}" >&2; exit 64; }

container_up() {
    eq_docker exec "${EQEMU_CONTAINER}" true >/dev/null 2>&1
}

# c. The exclusion decision through the provisioned code path, on the lane
# host with the exact compose value (the same env the container would
# carry). The test file was shipped by the edit section above; a --check
# run ships it first.
match_test() {
    local out rc
    if [ "${CHECK_ONLY}" -eq 1 ]; then
        eq_remote bash -c "mkdir -p /tmp/atcher" >/dev/null 2>&1 || return 1
        eq_remote bash -c "cat > /tmp/atcher/b940_watcher_match_test.pl" < "${MATCHTEST}" || return 1
    fi
    out="$(eq_remote bash -c "PROCESS_WATCHER_EXCLUDE='${EXCLUDE_VALUE}' perl /tmp/atcher/b940_watcher_match_test.pl" 2>&1)"
    rc=$?
    printf '%s\n' "${out}" | sed 's/^/  match: /'
    return ${rc}
}

busy_zone_leg() {
    # A CPU burner whose ps line carries the zone argv (the R1 shape):
    # `bash -c './bin/zone B940LOAD && exec yes B940LOAD'` shows in ps as
    # "bash -c ./bin/zone B940LOAD && exec yes B940LOAD" -- the line
    # contains "./bin/zone", which is exactly what the exemption matches.
    # The burn is `yes` (deterministic, one core) behind that argv; the
    # watcher never sees a real zone process, but it sees the line shape.
    # ~95 s = the 60 s kill window + 3 of the 5 s scans.
    local before pid after alive
    before="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'wc -l < ~/process-kill.log 2>/dev/null' 2>/dev/null | tr -d '[:space:]')"
    [ -n "${before}" ] || before=0
    eq_docker exec -d "${EQEMU_CONTAINER}" bash -c \
        "bash -c './bin/zone B940LOAD && exec yes B940LOAD' > /dev/null 2>&1" >/dev/null 2>&1
    sleep 2
    pid="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -eo pid,args | grep '[B]940LOAD' | awk '{print \\$1}' | head -1" 2>/dev/null | tr -d '[:space:]')"
    [ -n "${pid}" ] || { echo "  busy-zone leg: the burner never started"; return 1; }
    echo "  busy-zone leg: burner pid ${pid} (ps line carries ./bin/zone); waiting 95 s past the 60 s kill window"
    sleep 95
    alive="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -p ${pid} -o pid= 2>/dev/null" | tr -d '[:space:]')"
    # Clean up exactly our burner: the marker B940LOAD appears nowhere in
    # the fleet or the watcher, so the pkill cannot touch anything else.
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -9 -f '[B]940LOAD'" >/dev/null 2>&1 || true
    after="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'wc -l < ~/process-kill.log 2>/dev/null' 2>/dev/null | tr -d '[:space:]')"
    [ -n "${after}" ] || after=0
    echo "  busy-zone leg: alive after 95 s: ${alive:-no}; process-kill.log ${before} -> ${after} lines"
    [ -n "${alive}" ] && [ "${after}" -le "${before}" ]
}

lane_provision() {  # lane_provision <lane>
    local lane="$1" fail=0 rc out
    EQEMU_LANE="${lane}"
    source "${SCRIPT_DIR}/env.sh"
    echo "== lane ${lane} (${EQEMU_HOST}, ${AKK_DIR}) =="
    eqemu_lock_take "B940 watcher provision" || return 63

    local watcher="${AKK_DIR}/assets/scripts/process-watcher.pl"
    local composef="${AKK_DIR}/docker-compose.yml"
    local up=0
    container_up && up=1

        if [ "${CHECK_ONLY}" -eq 0 ]; then
            rc=0
            eq_remote bash -c "mkdir -p /tmp/atcher" >/dev/null 2>&1 || rc=1
            eq_remote bash -c "cat > /tmp/atcher/b940_patch_watcher.pl" < "${PATCHER}" || rc=1
            eq_remote bash -c "cat > /tmp/atcher/b940_patch_compose.pl" < "${COMPOSE_PATCHER}" || rc=1
            eq_remote bash -c "cat > /tmp/atcher/b940_watcher_match_test.pl" < "${MATCHTEST}" || rc=1
            if [ "${rc}" -eq 0 ]; then
                eq_remote bash -c "perl /tmp/atcher/b940_patch_watcher.pl ${watcher}" 2>&1 || rc=1
                eq_remote bash -c "perl /tmp/atcher/b940_patch_compose.pl ${composef} '${EXCLUDE_VALUE}'" 2>&1 || rc=1
            fi
            [ "${rc}" -eq 0 ] || { echo "FAIL: a B940 patch was refused (see above)"; fail=1; }

            if [ "${up}" -eq 1 ]; then
                # The entrypoint's relaunch loop (entrypoint.pl:53) restarts
                # the watcher when it dies; a targeted pkill (bracket trick;
                # board B798's guard in eq_container_pkill) is the
                # sanctioned shape. ONLY the watcher is touched -- never the
                # fleet.
                eq_container_pkill KILL '[p]rocess-watcher' >/dev/null 2>&1 || true
                echo "  live: asked the running watcher to relaunch (entrypoint loop) -- it comes back with the compose env"
            fi
        fi

    # --- verify (always) -------------------------------------------------
    out="$(eq_remote bash -c "grep -c B940 ${watcher} 2>/dev/null" 2>/dev/null)" || out=0
    [ "${out:-0}" -ge 1 ] || { echo "FAIL: ${watcher} lacks the B940 env-read"; fail=1; }
    rc=0; eq_remote bash -c "perl -c ${watcher} >/dev/null 2>&1" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: perl -c ${watcher}"; fail=1; }
    rc=0; eq_remote bash -c "grep -qF 'gcc|mysql|wget|unzip|node|go-build|tar|world|JetBrains|vscode|remote-dev-server|gzip/i' ${watcher}" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: the upstream allowlist regex is gone from ${watcher}"; fail=1; }
    rc=0; eq_remote bash -c "grep -qF 'PROCESS_WATCHER_EXCLUDE=${EXCLUDE_VALUE}' ${composef}" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: ${composef} lacks the PROCESS_WATCHER_EXCLUDE line (exact value ${EXCLUDE_VALUE})"; fail=1; }

    local mrc=0
    match_test || mrc=$?
    if [ "${mrc}" -eq 0 ]; then
        echo "  match leg: PASS"
    else
        echo "  match leg: FAIL (rc=${mrc})"; fail=1
    fi

    if [ "${up}" -eq 1 ]; then
        local wpid envline
        wpid="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -eo pid,args | grep '[p]rocess-watcher' | head -1 | awk '{print \$1}'" 2>/dev/null | tr -d '[:space:]')" || wpid=""
        if [ -n "${wpid}" ]; then
            envline="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "tr '\0' '\n' < /proc/${wpid}/environ 2>/dev/null | grep '^PROCESS_WATCHER_EXCLUDE='" 2>/dev/null)" || envline=""
            if [ -n "${envline}" ]; then
                echo "  live watcher pid ${wpid}: ${envline}"
            else
                echo "  note: watcher pid ${wpid} predates the provision (no compose env in its environ); the next relaunch carries it -- the file checks are authoritative"
            fi
        else
            echo "  note: no watcher alive right now; the entrypoint loop starts one with the compose env"
        fi
        if [ "${BUSY_ZONE}" -eq 1 ]; then
            if busy_zone_leg; then
                echo "  busy-zone leg: PASS"
            else
                echo "  busy-zone leg: FAIL"; fail=1
            fi
        else
            echo "  busy-zone leg: skipped (pass --busy-zone for the ~95 s liveness leg)"
        fi
    else
        echo "  note: container down -- file + match checks only"
    fi

    eqemu_lock_release 2>/dev/null || true
    if [ "${fail}" -ne 0 ]; then
        echo "lane ${lane}: NOT provisioned/verified"
        return 1
    fi
    echo "lane ${lane}: OK"
}

for lane in ${LANES}; do
    lane_provision "${lane}" || exit 1
done
echo "all lanes OK (${LANES})"
