#!/bin/bash
# T52 Step 1c -- zone-line control, return direction: commons -> befallen.
# Character is currently persisted in commons (zone 21) at DB (596,-1150,-38)
# from the outbound leg. zone_points: commons (597,-1146,-39) -> befallen
# target (35.22,-75.27,3.13). Client-frame warp target = swap(DB x,y) =
# (-1146, 597, -39).
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T52"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t52.json"
LOG="${RUNDIR}/transcript_zonelinereturn_${LOG_SUFFIX}.txt"
FIFO="${RUNDIR}/cmds_zonelinereturn_${LOG_SUFFIX}.fifo"

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

mark_log() { MARK=$(wc -l < "${LOG}" 2>/dev/null || echo 0); }

wait_for_log_new() {
    local pattern="$1" max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        if tail -n +"$((MARK + 1))" "${LOG}" 2>/dev/null | grep -a -qE "${pattern}"; then
            echo "[driver] matched (new) '${pattern}' after ${waited}s" >> "${LOG}.driver"
            return 0
        fi
        if ! kill -0 "${WPID}" 2>/dev/null; then
            echo "[driver] process exited while waiting for '${pattern}'" >> "${LOG}.driver"
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "[driver] TIMEOUT waiting for (new) '${pattern}' after ${max}s" >> "${LOG}.driver"
    return 1
}

MARK=0
wait_for_log_new "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

mark_log
send "/zoning on"
sleep 1
# Character was DB-restored to sit exactly inside befallen's razor-thin
# trigger box (client x=-1163.9, y in [585.1,606.2], z in [-45,-5] --
# confirmed via the prior run's "checkExtractedZoneLines" box dump) before
# this process started, so a tiny nudge (still inside the box) is enough
# to pass CheckZoneLine's "have we moved since last check" gate without
# warping back OUT of the box the way a same-coordinate /warp would risk.
send "/warp -1163.9183349609375 595.6248779296875 -34.984375"
sleep 2
wait_for_log_new "Zone line triggered! Target zone" 15
mark_log
wait_for_log_new "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

send "/who"
sleep 2

send "/camp"
mark_log
wait_for_log_new "shutdown complete|Application shutdown" 40

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
