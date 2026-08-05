#!/bin/bash
# T52 Step 1a -- isolated #goto wire probe (no follow-up /warp so the DB
# row read after this process exits reflects ONLY the #goto's outcome).
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T52"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t52.json"
LOG="${RUNDIR}/transcript_goto_${LOG_SUFFIX}.txt"
FIFO="${RUNDIR}/cmds_goto_${LOG_SUFFIX}.fifo"
GOTO_ARGS="${GOTO_ARGS:-100 200 5}"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (headless, piped stdin, -d 6/TRACE) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 6 < "${FIFO}" > "${LOG}" 2>&1 &
WPID=$!
echo "willeq pid=${WPID}" | tee -a "${LOG}.driver"

exec 3<>"${FIFO}"

send() {
    echo "$1" >&3
    echo "[driver] sent: $1 ($(date +%s.%N))" >> "${LOG}.driver"
}

wait_for_log() {
    local pattern="$1" max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        if grep -a -qE "${pattern}" "${LOG}" 2>/dev/null; then
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

wait_for_log "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

send "/say #goto ${GOTO_ARGS}"
sleep 4

send "/loc"
sleep 4

# One real movement tick's worth of settle time (SendPositionUpdate cadence)
# so the "sent back" wire leg has data.
sleep 3

send "/loc"
sleep 2

send "/camp"
wait_for_log "shutdown complete|Application shutdown" 40

waited=0
while kill -0 "${WPID}" 2>/dev/null && [ "${waited}" -lt 20 ]; do
    sleep 1
    waited=$((waited + 1))
done
if kill -0 "${WPID}" 2>/dev/null; then
    echo "[driver] process still alive after camp+20s -- sending /quit and then SIGTERM" >> "${LOG}.driver"
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
