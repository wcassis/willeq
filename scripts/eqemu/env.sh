#!/bin/bash
# H03: shared config for scripts/eqemu/*.sh — sourced, not executed directly.
#
# All state this touches lives in the akk-stack repo (containers + its
# mariadb data volume) or willeq's own data/captures/ directory. Nothing here
# modifies akk-stack server code (/mnt/projects/akk-stack/code) or the server
# binaries/scripts shipped in the eqemu-server image.

# The server runs on a dedicated VM (2026-09-12), NOT on the machine running
# these scripts. Every docker/compose/.env access goes through eq_remote,
# which ssh-es to EQEMU_SSH; ports are probed on EQEMU_HOST. Set
# EQEMU_HOST=local to drive a stack on this machine the old way.
# AKK_DIR is the akk-stack path ON the eqemu host.
#
# B1008 (2026-10-08): FOUR independent lanes, each an eqemu VM (a clone of the
# original, same stack, same accounts, its own lock on its own disk) paired
# with a Wine official-client oracle VM pointed at it. `EQEMU_LANE=N` picks the
# pair; an explicit EQEMU_HOST still wins. Lane 1 is the original VM. A card
# names its lane, never an address.
#   lane  eqemu           oracle
#   1     172.16.200.99   172.16.200.91  (eq-oracle-1)
#   2     172.16.200.98   172.16.200.92  (eq-oracle-2)
#   3     172.16.200.97   172.16.200.93  (eq-oracle-3)
#   4     172.16.200.96   172.16.200.94  (eq-oracle-4)
#   5     172.16.200.205  172.16.200.205 (the ThinkPad: eqemu AND the oracle on one x86 box with a
#                                         real GPU, B1055; its akk-stack is at /opt/akk-stack because
#                                         /mnt/projects is the NFS mount there, not local disk)
case "${EQEMU_LANE:-}" in
    ""|1) EQEMU_LANE_HOST=172.16.200.99 ;;
    2)    EQEMU_LANE_HOST=172.16.200.98 ;;
    3)    EQEMU_LANE_HOST=172.16.200.97 ;;
    4)    EQEMU_LANE_HOST=172.16.200.96 ;;
    5)    EQEMU_LANE_HOST=172.16.200.205 ;;
    *)    echo "scripts/eqemu/env.sh: unknown EQEMU_LANE '${EQEMU_LANE}' (1-5)" >&2
          return 1 2>/dev/null || exit 1 ;;
esac
EQEMU_HOST="${EQEMU_HOST:-${EQEMU_LANE_HOST}}"
# The lane id follows the HOST that won (an explicit EQEMU_HOST beats EQEMU_LANE),
# so scenario.sh's per-lane output dir and derived configs key on it; `local`
# and an unknown host keep whatever EQEMU_LANE said, else lane 1.
case "${EQEMU_HOST}" in
    172.16.200.99) EQEMU_LANE=1 ;;
    172.16.200.98) EQEMU_LANE=2 ;;
    172.16.200.97) EQEMU_LANE=3 ;;
    172.16.200.96) EQEMU_LANE=4 ;;
    172.16.200.205) EQEMU_LANE=5 ;;
    *) EQEMU_LANE="${EQEMU_LANE:-1}" ;;
esac
case "${EQEMU_LANE}" in
    1) EQEMU_ORACLE_HOST="${EQEMU_ORACLE_HOST:-172.16.200.91}" ;;
    2) EQEMU_ORACLE_HOST="${EQEMU_ORACLE_HOST:-172.16.200.92}" ;;
    3) EQEMU_ORACLE_HOST="${EQEMU_ORACLE_HOST:-172.16.200.93}" ;;
    4) EQEMU_ORACLE_HOST="${EQEMU_ORACLE_HOST:-172.16.200.94}" ;;
    5) EQEMU_ORACLE_HOST="${EQEMU_ORACLE_HOST:-172.16.200.205}" ;;
esac
export EQEMU_LANE
EQEMU_SSH="${EQEMU_SSH:-user@${EQEMU_HOST}}"
# Lane 5 (B1055) keeps its stack on local disk at /opt/akk-stack: /mnt/projects is NFS there.
if [ "${EQEMU_LANE}" = 5 ]; then AKK_DIR="${AKK_DIR:-/opt/akk-stack}"; else AKK_DIR="${AKK_DIR:-/mnt/projects/akk-stack}"; fi
COMPOSE_FILE="${AKK_DIR}/docker-compose.yml"
# Cross-session exclusive use of a lane, kept ON the lane's host so every
# dev machine (NFS or not) sees the same record. B1089 (#409): the old
# noclobber lock (EQEMU_LOCK, no TTL, no liveness, a dead holder held
# forever) is now a LEASE-BY-ACTIVITY: scripts/eqemu/lease_core.sh keeps
# one key=value record per lane (card/run/host/pid/acquired_at/
# last_touched/purpose), renewed by every lane operation, and a quiet lane
# is free again after LANE_LEASE_TTL_S (default 900 s) -- expiry releases
# the lane only, never kills anything. Use eqemu_lock_take/_show/_release
# (thin wrappers, every existing caller unchanged) or the `lease` CLI.
# SCRIPTS ARE MANDATORY: a hand-rolled ssh gets no renewal and loses the
# lane at TTL.
EQEMU_LOCK="${EQEMU_LOCK:-/mnt/projects/.eqemu-lock}"   # retired: the lease replaces it

# The lease client (the lane table above is all it needs from this file;
# it is defined AFTER the case blocks so EQEMU_LANE/EQEMU_SSH exist).

MARIADB_CONTAINER="akk-stack-mariadb-1"
EQEMU_CONTAINER="akk-stack-eqemu-server-1"

# Loaded from akk-stack/.env at up.sh time; hardcoded fallback matches the
# checked-in default so status.sh/down.sh work even without re-sourcing .env.

# Test account/character seeded once (see seed.sh for how/why). These match
# the willeq.json client config used by record_tour.sh.
EQ_TEST_ACCOUNT="willeq_test"
EQ_TEST_PASSWORD="WillEqH03Pass"
EQ_TEST_CHARACTER="Testchar"
EQ_TEST_SERVER_LONGNAME="WillEQ"

# Second account for two-client tours (trade/group/duel) — created by Will
# via the official client 2026-07-23; password normalized by seed.sh
EQ_BUDDY_ACCOUNT="willeq_buddy"
EQ_BUDDY_PASSWORD="WillEqBuddyPass"
# Characters (created via official client): Testchar+Testmelee(warrior)+
# Testcaster(wizard) on willeq_test; Testbuddy(magician) on willeq_buddy
EQ_TEST_ZONE_SHORTNAME="arena"

LOGIN_PORT=5998
WORLD_PORT=9000
ZONE_PORT=7000

eqemu_is_local() {
    [ "${EQEMU_HOST}" = "local" ]
}

# eq_remote below renews the lease on use (B1089); the lease client's own
# operations (lease_lib.sh's _lease_client_ssh) use raw ssh/scp so a renew
# can never recurse through eq_remote.
eq_remote() {
    # eq_remote <argv...> — run one command on the eqemu host, argv preserved.
    # ssh -n: never read this shell's stdin (callers loop over `read` input).
    # B1089 (#409): a shell that HOLDS the lane's lease renews it before
    # EVERY lane touch (renew-on-use), so a long wait loop keeps the lane
    # alive without a dedicated renewer; the lease client's own operations
    # use raw ssh (_lease_client_ssh) so a renew can never recurse through
    # here. Shells that never took the lease (LANE_LEASE_HELD unset) never
    # renew, so read-only scripts (status.sh, the selftests' probes) work
    # untouched.
    if [ "${LANE_LEASE_HELD:-0}" = 1 ] && [ "${LANE_LEASE_AUTO_RENEW:-1}" = 1 ]; then
        eqemu_lease_renew "use: $1 $2 $3" || return "${LEASE_EXPIRED:-63}"
    fi
    if eqemu_is_local; then
        "$@"
    else
        ssh -n -o BatchMode=yes -o ConnectTimeout=10 "${EQEMU_SSH}" -- "$(printf '%q ' "$@")"
    fi
}

eq_docker() {
    # eq_docker <docker args...> — `docker` on the eqemu host.
    eq_remote docker "$@"
}

eqemu_port_host() {
    # Where a client (and a port probe) reaches the server.
    if eqemu_is_local; then echo 127.0.0.1; else echo "${EQEMU_HOST}"; fi
}

eqemu_host_port_reachable() {
    # eqemu_host_port_reachable <port> — a host-side TCP connect probe.
    #
    # T497 (board B797). This proves REACHABILITY ONLY and must NEVER gate
    # readiness. 5998/9000/7000 are PUBLISHED container ports
    # (/mnt/projects/akk-stack/docker-compose.yml:35-46), so `docker-proxy`
    # binds them on the host for as long as the CONTAINER runs and completes
    # the connect() whatever is — or is not — listening inside it. MEASURED
    # T493 and again T497: with the eqemu-server container up and world, zone
    # and loginserver all at ZERO processes, this returns 0 for both 5998 and
    # 9000. It was renamed from `eqemu_port_open` precisely so that every call
    # site had to be revisited. Use `eqemu_stack_ready` for readiness.
    bash -c "exec 3<>/dev/tcp/$(eqemu_port_host)/$1" 2>/dev/null
}

eqemu_container_port_open() {
    # eqemu_container_port_open <port> — the same connect probe, but INSIDE the
    # eqemu-server container's network namespace, where docker-proxy cannot
    # answer for a process that is not there. The bash-less fallback reads
    # /proc/net/tcp (listening sockets are state 0A, port in hex).
    local port="$1" hex
    hex="$(printf '%04X' "${port}")"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "exec 3<>/dev/tcp/127.0.0.1/${port} 2>/dev/null || grep -Eq ':${hex} .* 0A ' /proc/net/tcp" \
        >/dev/null 2>&1
}

eqemu_service_ready() {
    # eqemu_service_ready <basename> <tcp port> — the honest per-service check:
    # the process is running AND its port answers from inside the container.
    server_process_running "$1" && eqemu_container_port_open "$2"
}

eqemu_stack_ready() {
    # THE readiness definition. Everything that used to gate on a host port
    # probe gates on this instead (T497, board B797).
    #
    # zone is deliberately absent: its port is UDP and assigned by world, and
    # a UDP port does not refuse a connect() the way a TCP one does — up.sh
    # polls the zone's own bootup log line instead.
    eqemu_service_ready loginserver "${LOGIN_PORT}" \
        && eqemu_service_ready world "${WORLD_PORT}"
}

eqemu_zone_booted() {
    # eqemu_zone_booted <shortname> <log> <timeout_seconds> — the honest "zone
    # <short> is really booted" check for the process up.sh's start-zone branch
    # launches into its own stdout redirect (<log>), e.g.
    # logs/zone_arena_h03.log.
    #
    # B504 (issue #90). This build used to be pinned to a single grep string,
    # `Zone bootup type`, in up.sh: a marker this eqemu build prints at the END
    # of a successful boot (line 97 of a cold arena boot, MEASURED 2026-09-19),
    # and nothing else in the tree verified it. If the server team ever stops
    # printing it, a cold up.sh times out after 60 s on a zone that booted
    # perfectly (scenario.sh turns that into 65) — the original T457 evidence
    # was exactly that. So the check here is the marker PLUS a liveness check
    # scoped to THIS shortname (a respawned launcher fleet must not satisfy it):
    # the `./bin/zone <short>` process is still running AND the marker line
    # exists; a `Zone Bootup failed` line in the log is a hard stop (MEASURED
    # 2026-09-19: a bogus shortname prints `Zone Bootup failed :: Zone::Bootup`
    # and the process EXITs — without the failure line, the liveness check
    # alone would eventually catch that, but the marker alone would NOT,
    # because a dying process can have printed it).
    #
    # scripts/eqemu/zone_booted_selftest.sh re-measures this on every run —
    # that is the net that keeps the marker string from rotting again.
    local short="$1" log="$2" timeout="${3:-90}"
    local waited=0
    until server_exec "grep -q 'Zone bootup type' '${log}'" >/dev/null 2>&1; do
        if server_exec "grep -q 'Zone Bootup failed' '${log}'" >/dev/null 2>&1; then
            echo "zone ${short} did not boot (log says 'Zone Bootup failed')" >&2
            return 1
        fi
        if [ "${waited}" -ge "${timeout}" ]; then
            echo "TIMEOUT waiting for zone ${short} boot marker after ${timeout}s (log: ${log})" >&2
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    # The marker was printed; now prove the specific process is still alive
    # with it. Scoped to the shortname: a respawned launcher fleet (which runs
    # the static zones, not this shortname, unless it has one) must not satisfy
    # it. server_process_count anchors on `bin/<name>` and excludes its own
    # grep, so this is the same liveness probe the rest of the lane uses.
    local n
    n="$(server_process_count "zone ${short}")"
    if [ -z "${n}" ] || [ "${n}" -le 0 ] 2>/dev/null; then
        echo "zone ${short} printed its boot marker but its process is no longer running" >&2
        return 1
    fi
    return 0
}

eqemu_kill_supervisor_chain() {
    # eqemu_kill_supervisor_chain — kill EVERYTHING in the eqemu-server
    # container that can respawn the server fleet, so a script can observe a
    # "no zone process" state long enough to exercise up.sh's start-zone
    # branch (B504, issue #90).
    #
    # The tree (MEASURED 2026-09-19): entrypoint.pl -> process-watcher.pl
    # (respawns /tmp/spire-launcher when it dies) -> spire http:serve (run in
    # a `sh -c while true` loop) -> /tmp/spire-launcher (re-execs `eqemu-
    # server:launcher start`) -> world/ucs + the static zone fleet.
    #
    # Order matters: the respawners go first, or they resurrect what the later
    # pkills take. Every pattern uses the bracket trick (board B798) so none of
    # them matches this exec's own command line; eq_container_pkill's guard
    # rejects the other two shapes.
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '[p]rocess-watcher'" 2>/dev/null || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f 'while true; do nohup'" 2>/dev/null || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '[s]h -c while true; do cd ~/server'" 2>/dev/null || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '[s]pire http:serve'" 2>/dev/null || true
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '[s]pire-launcher'" 2>/dev/null || true
    eq_container_pkill KILL '[e]qemu-server:launcher' || true
    eq_container_pkill KILL '/bin/(loginserver|world|zone|ucs)( |$)' || true
}

eqemu_fleet_at_zero() {
    # eqemu_fleet_at_zero <deadline_seconds> — poll until loginserver, world,
    # zone AND ucs are all absent; return 1 (after printing the ps dump) if
    # the deadline passes, i.e. something is still respawning them.
    local deadline="$1" waited=0
    until [ "$(server_process_count loginserver)" = "0" ] \
       && [ "$(server_process_count world)" = "0" ] \
       && [ "$(server_process_count zone)" = "0" ] \
       && [ "$(server_process_count ucs)" = "0" ]; do
        if [ "${waited}" -ge "${deadline}" ]; then
            eq_docker exec "${EQEMU_CONTAINER}" bash -c 'ps -eo pid,ppid,args' 2>&1 | head -40 >&2
            return 1
        fi
        sleep 2
        waited=$((waited + 2))
    done
    return 0
}

eq_container_pkill() {
    # eq_container_pkill <signal> <ere> — pkill inside the eqemu-server
    # container, with the guard B798 asks for.
    #
    # The trap (MEASURED T493): the container-side `bash -c "pkill -f
    # /tmp/spire-launcher; ..."` matches its OWN command line, so the exec
    # kills itself (rc 143) and everything after the first pkill never runs.
    # Safe shapes are ones the wrapper cannot match: down.sh's
    # '/bin/(loginserver|world|zone)( |$)' (the literal "(" after /bin/ is not
    # one of the alternatives) and the bracket trick '[s]pire-launcher'.
    # The guard approximates pkill's per-process argv match with a match
    # against the command string this function is about to run; it catches
    # exactly the T493 shape and is deliberately conservative.
    local sig="$1" pat="$2" cmd
    cmd="pkill -${sig} -f '${pat}'"
    if printf '%s' "${cmd}" | grep -Eq -- "${pat}"; then
        echo "REFUSING self-matching pkill pattern: ${pat}" >&2
        return 2
    fi
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "${cmd}" 2>/dev/null
}

# ---------------------------------------------------------------------------
# The lease, for eqemu_lock_take/_show/_release (defined above this file, in
# lease_lib.sh, which is sourced at the BOTTOM of env.sh): the record lives
# ON the lane's host, one flock critical section per take/takeover/renew/
# release, owner-checked release, TTL from LANE_LEASE_TTL_S (default 900 s),
# and the background renewer:
#   eqemu_lease_start_renewer [interval]  -- after taking the lease, before a
#       long wait (a 30-min scenario must never be stolen); interval floor
#       LANE_LEASE_RENEW_FLOOR_S (default 60 s, an ssh round trip).
#   eqemu_lease_stop_renewer              -- in the script's cleanup trap,
#       BEFORE eqemu_lock_release (the final renew is the release's own job).
# ---------------------------------------------------------------------------

# The lease client needs eq_remote / eqemu_is_local / the lane table, all
# of which now exist. Sourced, never executed: this file's own functions
# below are the ones callers use.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lease_lib.sh"

compose() {
    eq_docker compose -f "${COMPOSE_FILE}" --project-directory "${AKK_DIR}" "$@"
}

mariadb_root_password() {
    local env_file="${AKK_DIR}/.env"
    local pw
    pw=$(eq_remote grep -E '^MARIADB_ROOT_PASSWORD=' "${env_file}" 2>/dev/null | tail -n1 | cut -d= -f2-)
    if [ -n "${pw}" ]; then
        echo "${pw}"
        return
    fi
    echo "ERROR: could not read MARIADB_ROOT_PASSWORD from ${EQEMU_HOST}:${env_file} — akk-stack .env missing or unreadable" >&2
    return 1
}

mysql_exec() {
    # mysql_exec <sql>
    eq_docker exec "${MARIADB_CONTAINER}" mysql -uroot -p"$(mariadb_root_password)" peq -e "$1"
}

server_exec() {
    # server_exec <shell command, run with cwd=/home/eqemu/server>
    eq_docker exec -w /home/eqemu/server "${EQEMU_CONTAINER}" bash -c "$1"
}

server_exec_bg() {
    # server_exec_bg <shell command> — detached (docker exec -d), for
    # long-running server processes (loginserver/world/zone).
    eq_docker exec -w /home/eqemu/server -d "${EQEMU_CONTAINER}" bash -c "$1"
}

server_process_count() {
    # server_process_count <basename> — how many processes in the eqemu-server
    # container are running ./bin/<basename>.
    #
    # T457 (board B483). This exists because the container does NOT start
    # empty: its entrypoint runs /tmp/spire-launcher, which brings up
    # `eqemu-server:launcher start` and with it a `world` and the static-zone
    # fleet, roughly 45 s after `docker compose up -d`. Anything that starts a
    # second world without looking gets TWO, and which one a client reaches is
    # a race — T449 lost two scenario runs to it ("could not find world server
    # WillEQ" against a world that was demonstrably up).
    #
    # The match is anchored on "bin/<name>" and this function's own `grep` is
    # excluded, because `ps` output includes the pipeline that reads it.
    # `pgrep` is not assumed present in the image.
    local name="$1"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c \
        "ps -eo args | grep -c '[b]in/${name}'" 2>/dev/null | tr -d '[:space:]'
}

server_process_running() {
    # server_process_running <basename> — true when at least one is running.
    local n
    n="$(server_process_count "$1")"
    [ -n "${n}" ] && [ "${n}" -gt 0 ] 2>/dev/null
}
