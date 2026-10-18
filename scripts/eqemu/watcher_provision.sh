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
#            - PROCESS_WATCHER_EXCLUDE=(bin|/bin)/zone
#      which exempts exactly the zone binaries (./bin/zone, bin/zone,
#      /bin/zone, /home/eqemu/server/bin/zone -- the argv shapes
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
#   Optional liveness leg (--busy-zone, ~3 min, one core): a CPU burner
#   that REALLY carries the ./bin/zone argv (a copy of /usr/bin/yes
#   installed as /tmp/b940scratch/bin/zone in the container, run as
#   ./bin/zone B940LOAD) -- the exact argv the R1 kill came from. Two
#   phases, both proven against the LIVE watcher's own process-kill.log:
#   with the compose env (the provisioned exclusion) the burner survives
#   past the 60 s kill window untouched; with the env removed (the
#   upstream default, same binary, restored right after) the live watcher
#   kills it and the kill line lands in the log. Skipped by default --
#   the decision leg (c) already proves the exemption and a one-core
#   burn on a shared VM lane costs more than it adds.
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

# eq_remote is `ssh -n` (env.sh:111 -- stdin is deliberately NOT forwarded
# to a lane, so a caller's `read` loops stay intact): `cat > f < local`
# across it ships nothing. The sanctioned transfer is base64 over the
# command line: one file per call, size-checked on the other end.
ship_file() {  # ship_file <local-path> <remote-path>
    local b64 size rsize
    b64="$(base64 -w0 < "$1")"
    eq_remote bash -c "mkdir -p $(dirname $2) && echo '${b64}' | base64 -d > '$2'" || return 1
    size="$(wc -c < "$1" | tr -d '[:space:]')"
    rsize="$(eq_remote bash -c "wc -c < '$2' 2>/dev/null" | tr -d '[:space:]')"
    [ -n "${rsize}" ] && [ "${rsize}" = "${size}" ] || { echo "ship_file: $1 -> $2 size mismatch (${rsize:-0}/${size})"; return 1; }
}

# c. The exclusion decision through the provisioned code path, on the lane
# host with the exact compose value (the same env the container would
# carry). The test file was shipped by the edit section above; a --check
# run ships it first.
match_test() {
    local out rc
    if [ "${CHECK_ONLY}" -eq 1 ]; then
        ship_file "${MATCHTEST}" "/tmp/atcher/b940_watcher_match_test.pl" || return 1
    fi
    out="$(eq_remote bash -c "PROCESS_WATCHER_EXCLUDE='${EXCLUDE_VALUE}' perl /tmp/atcher/b940_watcher_match_test.pl" 2>&1)"
    rc=$?
    printf '%s\n' "${out}" | sed 's/^/  match: /'
    return ${rc}
}

# Start the CPU burner with a REAL zone argv: /usr/bin/yes copied to
# /tmp/b940scratch/bin/zone inside the container, run from that dir as
# `./bin/zone B940LOAD`. That is the exact argv the R1 kill came from
# (refactor/B925/README.md: `./bin/zone <zone>`) -- no `bash -c` wrapper,
# so the ps line IS the zone shape, not a string that happens to contain
# it. Echoes the burner's ps line, or nothing if it never started.
start_burner() {  # start_burner <ps-line-marker>
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "rm -rf /tmp/b940scratch && mkdir -p /tmp/b940scratch/bin && cp /usr/bin/yes /tmp/b940scratch/bin/zone" || return 1
    eq_docker exec -d "${EQEMU_CONTAINER}" bash -c \
        "cd /tmp/b940scratch && exec ./bin/zone '${1}' > /dev/null 2>&1" >/dev/null 2>&1
    sleep 2
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "ps -eo pid,args | grep '[${1:0:1}]${1:1}' | head -1" 2>/dev/null
}

# Wait (up to 150 s) for a NEW line in process-kill.log after <before>
# lines. Echoes the new line(s) to stdout; returns 0 if one appeared,
# 1 on timeout.
wait_kill_log_line() {  # wait_kill_log_line <before-lines>
    local before="$1" waited=0 n newlines
    while [ "${waited}" -lt 150 ]; do
        n="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'wc -l < ~/process-kill.log 2>/dev/null' 2>/dev/null | tr -d '[:space:]')"
        [ -n "${n}" ] || n=0
        if [ "${n}" -gt "${before}" ]; then
            newlines="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "tail -n +$((before + 1)) ~/process-kill.log" 2>/dev/null)"
            printf '%s\n' "${newlines}"
            return 0
        fi
        sleep 5
        waited=$((waited + 5))
    done
    return 1
}

# Kill exactly the burner and the default-watchers, and clean the scratch
# dir. The markers B940LOAD / B940DEF appear nowhere in the fleet or the
# provisioned watcher, so neither pkill can touch anything else.
busy_zone_cleanup() {
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -9 -f '[B]940LOAD'" >/dev/null 2>&1 || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -9 -f '[b]940def'" >/dev/null 2>&1 || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "rm -rf /tmp/b940scratch /tmp/b940def.out" >/dev/null 2>&1 || true
}

busy_zone_leg() {
    # The exemption, proven both ways against the LIVE provisioned
    # watcher's own log:
    #   phase A (the provisioned exclusion in the watcher's environ):
    #     a burner whose ps line is the real ./bin/zone argv survives
    #     past the 60 s kill window with no kill line;
    #   phase B (the same binary, env removed -- a second watcher
    #     started in-container with PROCESS_WATCHER_EXCLUDE empty, i.e.
    #     the upstream default regex): the live default watcher kills it
    #     within the window and the kill line lands in process-kill.log.
    # ~3 min, one core. The provisioned watcher's compose env is
    # restored right after phase B (it was never changed -- phase B
    # starts its OWN second watcher).
    local live_wpid before psline alive newlines
    # The real watcher (the perl process), not its `sh -c while true`
    # wrapper: the wrapper's environ is identical (compose started both),
    # but ps -p on it survives the wrapper's restart loop -- the check
    # after phase B is about the provisioned WATCHER, not its shell.
    live_wpid="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -eo pid,args | grep 'perl.*[p]rocess-watcher' | head -1 | awk '{print \\$1}'" 2>/dev/null | tr -d '[:space:]')"
    [ -n "${live_wpid}" ] || { echo "  busy-zone leg: no live watcher to prove against (relaunch it first)"; return 1; }
    before="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'wc -l < ~/process-kill.log 2>/dev/null' 2>/dev/null | tr -d '[:space:]')"
    [ -n "${before}" ] || before=0
    psline="$(start_burner B940LOAD)"
    [ -n "${psline}" ] || { echo "  busy-zone leg: the burner never started"; return 1; }
    echo "  burner ps line: ${psline}"
    echo "  busy-zone A (provisioned exclusion, live watcher pid ${live_wpid}): waiting 100 s past the 60 s kill window"
    sleep 100
    alive="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -eo pid,args | grep '[B]940LOAD' | head -1" 2>/dev/null)"
    if [ -n "${alive}" ]; then
        echo "  busy-zone A: PASS -- alive past the window: ${alive}"
    else
        echo "  busy-zone A: FAIL -- the provisioned watcher killed the zone-argv burner"; busy_zone_cleanup; return 1
    fi
    busy_zone_cleanup
    sleep 5
    before="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'wc -l < ~/process-kill.log 2>/dev/null' 2>/dev/null | tr -d '[:space:]')"
    [ -n "${before}" ] || before=0
    # The default watcher: the SAME provisioned binary, PROCESS_WATCHER
    # _EXCLUDE EMPTY so it compiles the upstream regex (the pre-B940
    # behavior). The provisioned watcher is never touched -- it exempts
    # the zone argv, so only this one can kill the burner. Both append
    # to the same ~/process-kill.log (same user, same $HOME).
    eq_docker exec -d "${EQEMU_CONTAINER}" bash -c \
        "env PROCESS_WATCHER_EXCLUDE= /home/eqemu/assets/scripts/process-watcher.pl > /tmp/b940def.out 2>&1 &" >/dev/null 2>&1
    sleep 3
    local defline
    defline="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c 'head -1 /tmp/b940def.out 2>/dev/null')"
    [ "${defline}" = "[process-watcher] exclusion pattern: (upstream default)" ] \
        || { echo "  busy-zone B: FAIL -- the default watcher does not print the upstream-default pattern (got: ${defline:-nothing})"; busy_zone_cleanup; return 1; }
    echo "  default watcher: ${defline}"
    psline="$(start_burner B940LOAD)"
    [ -n "${psline}" ] || { echo "  busy-zone B: the burner never started"; busy_zone_cleanup; return 1; }
    echo "  busy-zone B (upstream default, env removed): waiting for the kill"
    if newlines="$(wait_kill_log_line "${before}")"; then
        echo "  busy-zone B: PASS -- the default watcher's kill line: ${newlines}"
    else
        echo "  busy-zone B: FAIL -- no kill line within 150 s (the exemption would have been unproven)"; busy_zone_cleanup; return 1
    fi
    busy_zone_cleanup
    # The provisioned watcher was never restarted or re-env'd -- the
    # compose env is authoritative and untouched; re-prove it.
    alive="$(eq_docker exec "${EQEMU_CONTAINER}" bash -c "ps -p ${live_wpid} -o pid= 2>/dev/null" | tr -d '[:space:]')"
    [ -n "${alive}" ] || { echo "  busy-zone leg: FAIL -- the live provisioned watcher is gone"; return 1; }
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "tr '\\0' '\\n' < /proc/${live_wpid}/environ | grep '^PROCESS_WATCHER_EXCLUDE='" >/dev/null 2>&1 \
        || { echo "  busy-zone leg: FAIL -- the live watcher no longer carries the compose env"; return 1; }
    echo "  busy-zone B: the provisioned watcher (pid ${live_wpid}) still carries PROCESS_WATCHER_EXCLUDE -- restored state"
    return 0
}

lane_provision() {  # lane_provision <lane>
    local lane="$1" fail=0 rc out
    # env.sh honors an exported EQEMU_HOST over the lane table (env.sh:37)
    # and only defaults the unset ones -- so a loop that re-sources it in
    # ONE shell keeps lane 1's host for every later lane. The host, the ssh
    # target and the lease namespace all follow the lane, not a sticky env.
    unset EQEMU_HOST EQEMU_SSH
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
            ship_file "${PATCHER}" "/tmp/atcher/b940_patch_watcher.pl" || rc=1
            ship_file "${COMPOSE_PATCHER}" "/tmp/atcher/b940_patch_compose.pl" || rc=1
            ship_file "${MATCHTEST}" "/tmp/atcher/b940_watcher_match_test.pl" || rc=1
            [ "${rc}" -eq 0 ] || echo "FAIL: shipping the B940 patchers to the lane host failed"
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
