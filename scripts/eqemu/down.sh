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

# T497 (board B798): the pattern is unchanged -- it is the one that does NOT
# match its own `bash -c` wrapper -- but it now goes through the one helper
# that refuses a self-matching pattern, so the next container-side pkill
# cannot repeat T493's "the exec killed itself" defect.
echo "== down.sh: stopping loginserver/world/zone processes =="
eq_container_pkill TERM '/bin/(loginserver|world|zone)( |$)' || true
sleep 2
eq_container_pkill KILL '/bin/(loginserver|world|zone)( |$)' || true

echo "== down.sh: docker compose down (mariadb + eqemu-server + deps) =="
compose down --timeout 3

echo "== down.sh: done =="
