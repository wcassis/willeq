#!/bin/bash
# H03: Report status of the akk-stack eqemu server (containers + processes).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

echo "== containers =="
compose ps

echo
echo "== server processes (inside ${EQEMU_CONTAINER}) =="
docker exec "${EQEMU_CONTAINER}" bash -c \
    "ps aux | grep -E '/bin/(loginserver|world|zone)' | grep -v grep" \
    || echo "(none running)"

echo
echo "== host ports =="
for port in "${LOGIN_PORT}" "${WORLD_PORT}"; do
    if bash -c "exec 3<>/dev/tcp/127.0.0.1/${port}" 2>/dev/null; then
        echo "  ${port}: open"
    else
        echo "  ${port}: closed"
    fi
done
