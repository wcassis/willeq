#!/bin/bash
# T52 Step 1 -- ground-truth wire capture, before any code change.
#
# Captures, for a real #goto (server-approved teleport, "producer A") and a
# real zone-line crossing (client-triggered, "producer B"):
#   1. what the server SENT (OP_ZoneChange / OP_RequestClientZoneChange
#      raw offsets, via DumpPacket hex dump -- requires -d 6/TRACE)
#   2. what the client STORED (m_pending_zone_x/y/z, LOG_DEBUG lines)
#   3. what the client APPLIED (m_x/m_y/m_z, LOG_DEBUG lines)
#   4. what the client SENT BACK in its next SendPositionUpdate
#   5. where the character actually ENDED UP (/loc + DB row, read
#      separately by the caller after this script exits)
#
# Piped stdin only, no scratch hooks, same fifo-driver shape as
# T48/T49/T50/T51.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T52"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t52.json"
LOG="${RUNDIR}/transcript_step1_${LOG_SUFFIX}.txt"
FIFO="${RUNDIR}/cmds_step1_${LOG_SUFFIX}.fifo"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (headless, piped stdin, -d 6/TRACE for DumpPacket hex) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 6 < "${FIFO}" > "${LOG}" 2>&1 &
WPID=$!
echo "willeq pid=${WPID}" | tee -a "${LOG}.driver"

exec 3<>"${FIFO}"

send() {
    echo "$1" >&3
    echo "[driver] sent: $1 ($(date +%s.%N))" >> "${LOG}.driver"
}

# wait_for_log_new: like wait_for_log but only counts matches AFTER the
# line count captured in $MARK (set via `mark_log` before the action that
# should produce the new match) -- needed because several phases in this
# script reuse the same substring ("Zone connection complete...", "Zone
# line triggered") that already appears earlier in the same cumulative
# transcript, which would make a naive grep -q match instantly (a false
# trigger, zero real wait).
mark_log() {
    MARK=$(wc -l < "${LOG}" 2>/dev/null || echo 0)
}

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

# 1. Wait for full zone-in.
wait_for_log "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

# ===========================================================================
# Leg A -- #goto (server-solicited teleport, PRODUCER A), same zone
#          (befallen, zone_id=36). Target: a distinctive, clearly-different
#          position (x=555, y=-333, z=8) so wire values are unambiguous.
# ===========================================================================
send "/say #goto 555 -333 8"
sleep 4

send "/loc"
sleep 2

# Let a movement tick fire (SendPositionUpdate) so the "sent back" leg of
# the wire table has data even though the character is stationary.
sleep 3

# ===========================================================================
# Leg B -- real zone-line crossing (PRODUCER B): befallen -> commons
#          (zone_points: befallen (35.22,-75.27,3.75) -> commons/west
#          commonlands). This is befallen's ONLY real physical zone line
#          (it has no zone line to qeynos2 -- see Review deviation note).
#          Warp near the trigger volume and let CheckZoneLine fire it
#          (not a GM teleport -- must go through the client's own BSP
#          zone-line detection to exercise producer B).
# ===========================================================================
mark_log
send "/warp 35.22 -75.27 3.75"
sleep 2
wait_for_log_new "Zone line triggered" 15
mark_log
wait_for_log_new "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

# ===========================================================================
# Leg B return -- commons -> befallen, the other direction, second hop.
# zone_points: commons (597,-1146,-39) -> target befallen (35.22,-75.27,3.13)
# ===========================================================================
mark_log
send "/warp 597 -1146 -39"
sleep 2
wait_for_log_new "Zone line triggered" 15
mark_log
wait_for_log_new "Zone connection complete! Player position confirmed" 60

send "/loc"
sleep 2

send "/who"
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
