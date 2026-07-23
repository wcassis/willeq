#!/bin/bash
# H03: Bring up the akk-stack eqemu server for willeq integration testing.
#
# What this does (all sanctioned server *operation*, not server code changes):
#   1. docker compose up -d mariadb eqemu-server (same two services as
#      akk-stack's own `make up` target).
#   2. Wait for mariadb to accept connections.
#   3. Run ./bin/shared_memory once (populates the shared memory segments
#      other processes read; this is the normal eqemu startup step and exits
#      0 on its own once done).
#   4. Start ./bin/loginserver, ./bin/world, and ./bin/zone <shortname> in the
#      background inside the eqemu-server container — the same binaries
#      akk-stack's own spire launcher (`spire eqemu-server:launcher`) would
#      exec. That launcher got stuck in a persistent "already running" state
#      on this host that `stop`/`start` cycling could not clear (no matching
#      OS process, listening port, or discoverable DB/file state was found
#      backing the check — see refactor/H03_eqemu_recording.md's review for
#      the full investigation) so this script drives the binaries directly.
#      This does not touch /mnt/projects/akk-stack/code or any server script.
#   5. Wait for the login (5998), world (9000), and zone (7000) ports to
#      accept TCP/UDP-adjacent connections on the host.
#
# Usage: scripts/eqemu/up.sh [zone_shortname]  (default: arena)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

ZONE_SHORTNAME="${1:-${EQ_TEST_ZONE_SHORTNAME}}"

wait_for() {
    # wait_for <description> <timeout_seconds> <check command...>
    local desc="$1" timeout="$2"
    shift 2
    local waited=0
    until "$@" >/dev/null 2>&1; do
        if [ "${waited}" -ge "${timeout}" ]; then
            echo "TIMEOUT waiting for ${desc} after ${timeout}s" >&2
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "${desc}: ready (${waited}s)"
}

echo "== up.sh: docker compose up -d mariadb eqemu-server =="
compose up -d mariadb eqemu-server

echo "== up.sh: waiting for mariadb =="
wait_for "mariadb" 60 docker exec "${MARIADB_CONTAINER}" mysqladmin status \
    -uroot -p"$(mariadb_root_password)" -h localhost --silent

echo "== up.sh: waiting for eqemu-server container =="
wait_for "eqemu-server container" 30 docker exec "${EQEMU_CONTAINER}" true

echo "== up.sh: populating shared memory (bin/shared_memory) =="
server_exec "./bin/shared_memory 2>&1 | tail -5"

echo "== up.sh: starting loginserver =="
server_exec_bg "./bin/loginserver > logs/loginserver_h03.log 2>&1"
wait_for "loginserver port ${LOGIN_PORT}" 30 bash -c \
    "exec 3<>/dev/tcp/127.0.0.1/${LOGIN_PORT}"

echo "== up.sh: starting world =="
server_exec_bg "./bin/world > logs/world_h03.log 2>&1"
wait_for "world port ${WORLD_PORT}" 30 bash -c \
    "exec 3<>/dev/tcp/127.0.0.1/${WORLD_PORT}"

echo "== up.sh: starting zone ${ZONE_SHORTNAME} =="
server_exec_bg "./bin/zone ${ZONE_SHORTNAME} > logs/zone_${ZONE_SHORTNAME}_h03.log 2>&1"

# Zone doesn't open its TCP port; it opens a UDP EQ Network server port
# assigned by world (7000 for the first zone process, per launcher_zones/
# zones.ports config). Poll the zone log for the "Bootup" line instead of a
# port check since UDP ports don't refuse a connect() the way TCP does.
waited=0
until server_exec "grep -q 'Zone bootup type' logs/zone_${ZONE_SHORTNAME}_h03.log" >/dev/null 2>&1; do
    if [ "${waited}" -ge 60 ]; then
        echo "TIMEOUT waiting for zone ${ZONE_SHORTNAME} to boot after 60s" >&2
        server_exec "tail -40 logs/zone_${ZONE_SHORTNAME}_h03.log" || true
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done
echo "zone ${ZONE_SHORTNAME}: booted (${waited}s)"

echo "== up.sh: stack is up (loginserver:${LOGIN_PORT} world:${WORLD_PORT} zone:${ZONE_SHORTNAME}) =="
