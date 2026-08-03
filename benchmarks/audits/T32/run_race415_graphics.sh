#!/bin/bash
# T32 secondary check: does race=415 still crash willeq in GRAPHICS mode?
# T29 could not test graphics mode at all because eq_client_path was stale
# (fixed by this unit's scoping, see refactor/T32_loot_verification.md).
# npc 351043 "a_rat" (race=415, gender=2) is the exact asset T29 hit.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T32"
BIN="/mnt/projects/fable/wt/t1/build-gles2/bin/willeq"
CFG="${RUNDIR}/willeq_t32.json"
LOG="${RUNDIR}/transcript_race415_graphics.txt"  # .txt, not .log -- repo .gitignore excludes *.log
FIFO="${RUNDIR}/cmds_race415.fifo"

rm -f "${FIFO}" "${LOG}"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (GRAPHICS mode, surfaceless EGL, piped stdin) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" -d 5 < "${FIFO}" > "${LOG}" 2>&1 &
WPID=$!
echo "willeq pid=${WPID}" | tee -a "${LOG}.driver"

exec 3<>"${FIFO}"

send() {
    echo "$1" >&3
    echo "[driver] sent: $1" >> "${LOG}.driver"
}

wait_for_log() {
    local pattern="$1" max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        if grep -qE "${pattern}" "${LOG}" 2>/dev/null; then
            echo "[driver] matched '${pattern}' after ${waited}s" >> "${LOG}.driver"
            return 0
        fi
        if ! kill -0 "${WPID}" 2>/dev/null; then
            echo "[driver] process exited while waiting for '${pattern}'" >> "${LOG}.driver"
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "[driver] TIMEOUT waiting for '${pattern}' after ${max}s" >> "${LOG}.driver"
    return 1
}

# Wait for zone connection AND graphics zone load to finish (or crash).
wait_for_log "Zone connection complete! Player position confirmed" 60
sleep 3
# Spawn the exact race=415 asset T29's finding was about.
send "/say #npctypespawn 351043"
wait_for_log "Spawned a_rat|Spawned .*351043|FATAL.*race=415|Failed to load race model" 20
sleep 3

send "/camp"
wait_for_log "shutdown complete|Application shutdown" 15

waited=0
while kill -0 "${WPID}" 2>/dev/null && [ "${waited}" -lt 15 ]; do
    sleep 1
    waited=$((waited + 1))
done
if kill -0 "${WPID}" 2>/dev/null; then
    send "/quit"
    sleep 3
    kill "${WPID}" 2>/dev/null || true
    sleep 2
fi

exec 3>&-
wait "${WPID}" 2>/dev/null
EXITCODE=$?
echo "[driver] willeq exit code: ${EXITCODE}" | tee -a "${LOG}.driver"
echo "DONE exit=${EXITCODE}"
