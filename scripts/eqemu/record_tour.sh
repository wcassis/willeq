#!/bin/bash
# H03: End-to-end scripted recording tour.
#
#   server up (up.sh) -> seed test account/character (seed.sh) -> run
#   headless willeq with --record-session + --script through a short tour
#   (zone in, spawn a target via GM #npctypespawn, list/target/attack it,
#   move between a few waypoints, /camp) -> verify the recording has a
#   healthy number of zone-stream records -> server down (down.sh).
#
# Usage: scripts/eqemu/record_tour.sh [output.sleq]
#   default output: data/captures/auto_session.sleq (relative to repo root)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${SCRIPT_DIR}/env.sh"

OUTPUT="${1:-${REPO_ROOT}/data/captures/auto_session.sleq}"
mkdir -p "$(dirname "${OUTPUT}")"

WILLEQ_BIN="${REPO_ROOT}/build/bin/willeq"
EQ_CLIENT_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"

CONFIG_FILE="${REPO_ROOT}/data/captures/tour_willeq.json"
TOUR_FILE="${REPO_ROOT}/data/captures/tour.txt"
RUN_LOG="${REPO_ROOT}/data/captures/tour_run.log"

cleanup() {
    local exit_code=$?
    echo "== record_tour.sh: bringing server down =="
    "${SCRIPT_DIR}/down.sh" || true
    exit "${exit_code}"
}
trap cleanup EXIT

if [ ! -x "${WILLEQ_BIN}" ]; then
    echo "STOP: willeq binary not found at ${WILLEQ_BIN} — build first (cmake --build ${REPO_ROOT}/build)." >&2
    exit 1
fi
if [ ! -d "${EQ_CLIENT_PATH}" ]; then
    echo "STOP: EQ client files not found at ${EQ_CLIENT_PATH} (set EQ_CLIENT_PATH env var to override)." >&2
    exit 1
fi

echo "== record_tour.sh: server up =="
"${SCRIPT_DIR}/up.sh" "${EQ_TEST_ZONE_SHORTNAME}"

echo "== record_tour.sh: seeding test account/character =="
"${SCRIPT_DIR}/seed.sh"

echo "== record_tour.sh: writing client config =="
cat > "${CONFIG_FILE}" <<EOF
{
  "clients": [
    {
      "host": "127.0.0.1",
      "port": ${LOGIN_PORT},
      "user": "${EQ_TEST_ACCOUNT}",
      "pass": "${EQ_TEST_PASSWORD}",
      "server": "${EQ_TEST_SERVER_LONGNAME}",
      "character": "${EQ_TEST_CHARACTER}",
      "eq_client_path": "${EQ_CLIENT_PATH}"
    }
  ]
}
EOF

echo "== record_tour.sh: writing tour script =="
# npctypespawn 351043 = "a_rat" (level 1, 11 hp) — a harmless GM-spawned
# target so /target + /attack are exercised even though the arena zone
# ships with 0 spawn2 entries of its own (confirmed via
# `SELECT COUNT(*) FROM spawn2 WHERE zone='arena'` -> 0). #npctypespawn is
# a real eqemu zone GM command (zone/gm_commands/npctypespawn.cpp),
# reachable from willeq's existing /say command because eqemu's
# Client::ChannelMessageReceived() treats any chat message starting with
# '#' as a GM command regardless of channel, for GM-status accounts.
cat > "${TOUR_FILE}" <<'EOF'
wait 10
/say #npctypespawn 351043
wait 2
/list
wait 1
/target
wait 1
/attack
wait 4
/stopattack
wait 1
/move 200 -1009 51
wait 3
/move 200 -960 51
wait 3
/move 146 -1009 51
wait 2
/camp
EOF

echo "== record_tour.sh: running willeq tour (recording to ${OUTPUT}) =="
rm -f "${OUTPUT}"
(
    cd "${REPO_ROOT}"
    timeout 120 "${WILLEQ_BIN}" \
        -c "${CONFIG_FILE}" \
        --no-graphics \
        -d 4 \
        --record-session "${OUTPUT}" \
        --script "${TOUR_FILE}" \
        > "${RUN_LOG}" 2>&1
)
WILLEQ_EXIT=$?
tail -20 "${RUN_LOG}"
if [ "${WILLEQ_EXIT}" -ne 0 ]; then
    echo "STOP: willeq exited with code ${WILLEQ_EXIT} (see ${RUN_LOG})" >&2
    exit 1
fi

echo "== record_tour.sh: verifying recording =="
if [ ! -f "${OUTPUT}" ]; then
    echo "STOP: no recording produced at ${OUTPUT}" >&2
    exit 1
fi

ZONE_RECORDS=$(python3 "${SCRIPT_DIR}/count_zone_records.py" "${OUTPUT}")
echo "Zone-stream records: ${ZONE_RECORDS}"
if [ "${ZONE_RECORDS}" -le 200 ]; then
    echo "STOP: only ${ZONE_RECORDS} zone-stream records (need > 200)" >&2
    exit 1
fi

echo "== record_tour.sh: OK — ${OUTPUT} has ${ZONE_RECORDS} zone-stream records =="
