#!/bin/bash
# H03: Report status of the akk-stack eqemu server (containers + processes).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

echo "== containers =="
compose ps

echo
echo "== server processes (inside ${EQEMU_CONTAINER}) =="
eq_docker exec "${EQEMU_CONTAINER}" bash -c \
    "ps aux | grep -E '/bin/(loginserver|world|zone)' | grep -v grep" \
    || echo "(none running)"

echo
echo "== readiness (T497, board B797) =="
# A HOST port that answers proves only that the CONTAINER is up: 5998/9000 are
# published, so docker-proxy completes the connect() with the fleet at zero
# processes. Print both facts so the difference is never invisible again.
for svc in "loginserver ${LOGIN_PORT}" "world ${WORLD_PORT}"; do
    set -- ${svc}
    name="$1"; port="$2"
    procs="$(server_process_count "${name}")"
    if eqemu_service_ready "${name}" "${port}"; then
        echo "  ${name}: READY -- running (${procs}) and listening on :${port} inside the container"
    elif eqemu_host_port_reachable "${port}"; then
        echo "  ${name}: DOWN -- host port ${port} answers (docker-proxy), nothing is listening inside the container (processes: ${procs})"
    else
        echo "  ${name}: DOWN -- host port ${port} does not answer either (processes: ${procs})"
    fi
done
echo "  zone: $(server_process_count zone) process(es) (UDP port assigned by world; no TCP probe)"
echo
echo "== stack ready? =="
if eqemu_stack_ready; then echo "  eqemu_stack_ready: YES"; else echo "  eqemu_stack_ready: NO"; fi
