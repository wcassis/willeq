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
EQEMU_HOST="${EQEMU_HOST:-172.16.200.99}"
EQEMU_SSH="${EQEMU_SSH:-user@${EQEMU_HOST}}"
AKK_DIR="${AKK_DIR:-/mnt/projects/akk-stack}"
COMPOSE_FILE="${AKK_DIR}/docker-compose.yml"
# Cross-session exclusive lock, kept ON the eqemu host so every dev machine
# (NFS or not) sees the same file. Use eqemu_lock_take/_show/_release.
EQEMU_LOCK="${EQEMU_LOCK:-/mnt/projects/.eqemu-lock}"

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

eq_remote() {
    # eq_remote <argv...> — run one command on the eqemu host, argv preserved.
    # ssh -n: never read this shell's stdin (callers loop over `read` input).
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

eqemu_lock_take() {
    # eqemu_lock_take <owner, e.g. T###> — ATOMIC (noclobber) on the eqemu
    # host. Exit 0 = you own it; non-zero = someone else does: STOP.
    eq_remote bash -c "set -o noclobber; echo \"\$1 \$(date -Iseconds) from \$2\" > '${EQEMU_LOCK}'" _ "$1" "$(hostname)" 2>/dev/null
}

eqemu_lock_show() {
    # Prints the lock line, or nothing (exit 1) when the lock is free.
    eq_remote cat "${EQEMU_LOCK}" 2>/dev/null
}

eqemu_lock_release() {
    eq_remote rm -f "${EQEMU_LOCK}"
}

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
