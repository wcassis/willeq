#!/bin/bash
# H03: shared config for scripts/eqemu/*.sh — sourced, not executed directly.
#
# All state this touches lives in the akk-stack repo (containers + its
# mariadb data volume) or willeq's own data/captures/ directory. Nothing here
# modifies akk-stack server code (/mnt/projects/akk-stack/code) or the server
# binaries/scripts shipped in the eqemu-server image.

AKK_DIR="/mnt/projects/akk-stack"
COMPOSE_FILE="${AKK_DIR}/docker-compose.yml"

MARIADB_CONTAINER="akk-stack-mariadb-1"
EQEMU_CONTAINER="akk-stack-eqemu-server-1"

# Loaded from akk-stack/.env at up.sh time; hardcoded fallback matches the
# checked-in default so status.sh/down.sh work even without re-sourcing .env.

# Test account/character seeded once (see seed.sh for how/why). These match
# the willeq.json client config used by record_tour.sh.
EQ_TEST_ACCOUNT="willeq_test"
EQ_TEST_PASSWORD="WillEqH03Pass"
EQ_TEST_CHARACTER="Testchar"
EQ_TEST_SERVER_LONGNAME="Akkas Docker PEQ Installer"
EQ_TEST_ZONE_SHORTNAME="arena"

LOGIN_PORT=5998
WORLD_PORT=9000
ZONE_PORT=7000

compose() {
    docker compose -f "${COMPOSE_FILE}" --project-directory "${AKK_DIR}" "$@"
}

mariadb_root_password() {
    local env_file="${AKK_DIR}/.env"
    if [ -f "${env_file}" ]; then
        local pw
        pw=$(grep -E '^MARIADB_ROOT_PASSWORD=' "${env_file}" | tail -n1 | cut -d= -f2-)
        if [ -n "${pw}" ]; then
            echo "${pw}"
            return
        fi
    fi
    echo "ERROR: could not read MARIADB_ROOT_PASSWORD from ${env_file} — akk-stack .env missing or unreadable" >&2
    return 1
}

mysql_exec() {
    # mysql_exec <sql>
    docker exec "${MARIADB_CONTAINER}" mysql -uroot -p"$(mariadb_root_password)" peq -e "$1"
}

server_exec() {
    # server_exec <shell command, run with cwd=/home/eqemu/server>
    docker exec -w /home/eqemu/server "${EQEMU_CONTAINER}" bash -c "$1"
}

server_exec_bg() {
    # server_exec_bg <shell command> — detached (docker exec -d), for
    # long-running server processes (loginserver/world/zone).
    docker exec -w /home/eqemu/server -d "${EQEMU_CONTAINER}" bash -c "$1"
}
