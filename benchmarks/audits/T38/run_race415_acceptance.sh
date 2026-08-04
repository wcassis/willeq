#!/bin/bash
# T38 acceptance check: race=415's missing base model must no longer abort
# the whole zone load. Reuses T32's committed repro shape
# (benchmarks/audits/T32/run_race415_graphics.sh) -- Testchar's canonical
# position is zone_id=36 (befallen), the exact zone T32 confirmed race=415
# (npc 351043, "a_rat") crashes in. This script:
#   1. Zones in with graphics enabled (surfaceless EGL).
#   2. Spawns the race=415 npc that used to abort the zone load.
#   3. Waits for the zone to finish loading to completion (not crash/quit).
#   4. Drives a couple of piped slash commands afterward to prove the
#      client is still usable (T29's command channel, T31's stdout
#      visibility of replies).
#   5. Camps cleanly.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T38"
BIN="/mnt/projects/fable/wt/t1/build-gles2/bin/willeq"
CFG="${RUNDIR}/willeq_t38.json"
LOG="${RUNDIR}/transcript_race415_acceptance.txt"  # .txt, not .log -- repo .gitignore excludes *.log
FIFO="${RUNDIR}/cmds_race415_acceptance.fifo"

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
if ! wait_for_log "Zone connection complete! Player position confirmed" 60; then
    echo "[driver] FAILED: zone connection never completed" | tee -a "${LOG}.driver"
fi
sleep 3

# Spawn the exact race=415 asset (npc 351043, a_rat) that used to abort the
# entire zone load (T29's original finding, reconfirmed live by T32).
send "/say #npctypespawn 351043"
wait_for_log "Spawned a_rat|Spawned .*351043" 20
sleep 3

# The old behavior: this next log pattern is FATAL and the process quits.
# The new behavior: this is a WARN, the zone finishes loading, and the
# process keeps running. Give the (now-nonfatal) entity skip + rest of the
# zone load time to actually reach completion.
wait_for_log "SEQ: Step 8 complete|Sequential: SKIPPED|hideLoadingScreen|Graphics.*complete|loadZoneGraphicsOnThread: complete" 30
sleep 2

# Prove the client is still usable: pipe a couple of slash commands and
# confirm their replies are visible on stdout (T29 command channel, T31
# reply visibility).
send "/who"
sleep 2
send "/say hello after race415 skip"
sleep 2

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
