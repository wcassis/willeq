#!/bin/bash
# H03: Shut down the akk-stack eqemu server started by up.sh.
#
# Stops the loginserver/world/zone processes inside the eqemu-server
# container (started directly by up.sh, not via akk-stack's spire launcher —
# see up.sh's header comment), then brings down the whole compose project
# (mirrors akk-stack's own `make down`: `docker compose down --timeout 3`).

set -uo pipefail  # not -e: best-effort teardown, keep going on individual failures

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

echo "== down.sh: stopping loginserver/world/zone processes =="
docker exec "${EQEMU_CONTAINER}" bash -c \
    "pkill -TERM -f '/bin/(loginserver|world|zone)( |\$)'" 2>/dev/null || true
sleep 2
docker exec "${EQEMU_CONTAINER}" bash -c \
    "pkill -KILL -f '/bin/(loginserver|world|zone)( |\$)'" 2>/dev/null || true

echo "== down.sh: docker compose down (mariadb + eqemu-server + deps) =="
compose down --timeout 3

echo "== down.sh: done =="
