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
#      background inside the eqemu-server container.
#
#      NOTE (correcting H03's initial diagnosis): the sanctioned way to run
#      the full stack is `spire spire:launcher start` (the container's
#      `start` alias) — it re-execs /tmp/spire-launcher which runs
#      `eqemu-server:launcher start`. Invoking `spire eqemu-server:launcher
#      start` DIRECTLY always fails with "already running" because its
#      process check matches its own command line — it was never wedged
#      state. This script still defaults to driving the binaries directly
#      because CI wants ONE zone process, not the launcher's full static-zone
#      fleet (~15 zone processes). Pass --launcher to use the sanctioned
#      spire:launcher path instead (boots the fleet per eqemu_config's
#      web-admin.launcher block).
#   5. Wait until each service is READY -- the process is running AND its
#      port answers from INSIDE the container (`eqemu_service_ready`, env.sh).
#      T497/board B797: this used to be a host-side connect() on 5998/9000,
#      which proves nothing at all. Those are published container ports, so
#      docker-proxy answers them for as long as the CONTAINER runs; this
#      script printed "world port 9000: ready (0s)" and exited 0 against a
#      fleet at ZERO processes. `scripts/eqemu/ready_selftest.sh` is the net.
#
# Usage: scripts/eqemu/up.sh [zone_shortname]  (default: arena)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

USE_LAUNCHER=0
if [ "${1:-}" = "--launcher" ]; then
    USE_LAUNCHER=1
    shift
fi
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
wait_for "mariadb" 60 eq_docker exec "${MARIADB_CONTAINER}" mysqladmin status \
    -uroot -p"$(mariadb_root_password)" -h localhost --silent

echo "== up.sh: waiting for eqemu-server container =="
wait_for "eqemu-server container" 30 eq_docker exec "${EQEMU_CONTAINER}" true

echo "== up.sh: populating shared memory (bin/shared_memory) =="
server_exec "./bin/shared_memory 2>&1 | tail -5"

if [ "${USE_LAUNCHER}" = "1" ]; then
    # Sanctioned path: the container's `start` alias. Boots world/ucs + the
    # full static-zone fleet per eqemu_config's web-admin.launcher block.
    # The launcher does NOT start loginserver on this config; start it
    # ourselves either way.
    echo "== up.sh: starting via sanctioned spire:launcher =="
    server_exec_bg "./bin/loginserver > logs/loginserver_h03.log 2>&1"
    server_exec "./bin/spire spire:launcher start 2>&1 | head -3"
    wait_for "loginserver ready (process + :${LOGIN_PORT} in-container)" 60 \
        eqemu_service_ready loginserver "${LOGIN_PORT}"
    wait_for "world ready (process + :${WORLD_PORT} in-container)" 120 \
        eqemu_service_ready world "${WORLD_PORT}"
    echo "== up.sh: launcher mode up (fleet booting in background) =="
    exit 0
fi

# T457 (board B483): ADOPT the container's own fleet instead of duplicating it.
#
# The eqemu-server image auto-starts /tmp/spire-launcher on boot, so by the time
# `compose up -d` has settled there is usually already a world (and the static
# zone fleet) running -- with no `up.sh --launcher` anywhere in sight. Starting
# a second `bin/world` was not merely wasteful: both bind the same ports, only
# one wins, and the client's login handshake then reaches a world that is not
# the one holding the zone list. T449 lost two `cast_lands` runs and one
# `zone_in` run to exactly that, each reported as "could not find world server
# WillEQ".
#
# So each of the three processes is started only if it is not already running.
# The checks are independent because the launcher's coverage is uneven: on this
# config it brings up world and the zones but NOT loginserver, which is why
# --launcher above starts loginserver by hand too.
if server_process_running loginserver; then
    echo "== up.sh: loginserver already running ($(server_process_count loginserver)) -- adopting =="
else
    echo "== up.sh: starting loginserver =="
    server_exec_bg "./bin/loginserver > logs/loginserver_h03.log 2>&1"
fi
wait_for "loginserver ready (process + :${LOGIN_PORT} in-container)" 60 \
    eqemu_service_ready loginserver "${LOGIN_PORT}"

if server_process_running world; then
    echo "== up.sh: world already running ($(server_process_count world)) -- adopting =="
else
    echo "== up.sh: starting world =="
    server_exec_bg "./bin/world > logs/world_h03.log 2>&1"
fi
wait_for "world ready (process + :${WORLD_PORT} in-container)" 120 \
    eqemu_service_ready world "${WORLD_PORT}"

# The zone check is deliberately for ANY zone process, not for this shortname.
# When the launcher fleet is up it is already serving the static zones and the
# client will be handed one of those; adding a sixteenth process for a zone the
# fleet already covers is the same duplication one level down. A bare container
# with no fleet has zero, and the branch below starts exactly the one zone CI
# wants.
if server_process_running zone; then
    echo "== up.sh: zone fleet already running ($(server_process_count zone) processes) -- adopting, not starting ${ZONE_SHORTNAME} =="
    echo "== up.sh: stack is up (adopted: loginserver:${LOGIN_PORT} world:${WORLD_PORT} + running zone fleet) =="
    exit 0
fi

echo "== up.sh: starting zone ${ZONE_SHORTNAME} =="
server_exec_bg "./bin/zone ${ZONE_SHORTNAME} > logs/zone_${ZONE_SHORTNAME}_h03.log 2>&1"

# Zone doesn't open a TCP port; its EQ Network port is UDP, assigned by world
# (7000 for the first zone process, per launcher_zones/zones.ports config), and
# a UDP port does not refuse a connect() the way TCP does. So readiness is the
# zone's own boot log, checked by eqemu_zone_booted (env.sh): the boot marker
# line AND the process still alive, with `Zone Bootup failed` as a hard stop.
# B504/issue #90: this used to be a bare `grep 'Zone bootup type'` — a single
# string with no liveness check and nothing to pin it. If it times out here the
# log tail below names the line the zone actually printed.
if ! eqemu_zone_booted "${ZONE_SHORTNAME}" "logs/zone_${ZONE_SHORTNAME}_h03.log" 90; then
    echo "zone ${ZONE_SHORTNAME}: FAILED to boot -- last 40 log lines:" >&2
    server_exec "tail -40 logs/zone_${ZONE_SHORTNAME}_h03.log" 2>&1 || true
    exit 1
fi
echo "zone ${ZONE_SHORTNAME}: booted"

echo "== up.sh: stack is up (loginserver:${LOGIN_PORT} world:${WORLD_PORT} zone:${ZONE_SHORTNAME}) =="
