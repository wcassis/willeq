#!/bin/bash
# T39 acceptance check: CombatState's target half is now populated from the
# authoritative CombatManager (Application::syncCombatState()), and
# fireTargetChangedEvent() now publishes real data on the EventBus. This
# script proves the two acceptance-bar claims live:
#   (a) /hail takes the hailTarget() branch once a target is set -- the
#       concrete user-visible payoff (cmdHail reads m_state.combat().
#       hasTarget(), which was permanently false before this unit).
#   (b) target changes reach ConsoleRenderer's output (headless/
#       --no-graphics mode, the live EventBus consumer).
#
# Runs headless (--no-graphics) so ConsoleRenderer (not IrrlichtBridge) is
# the active renderer -- that is the channel this unit populates.
#
# Evidence signals used, both direct std::cout (NOT the buffered LOG_INFO/
# LOG_DEBUG path -- see refactor/deferred.md "Non-FATAL log output never
# reaches stdout during normal operation", still open, so it is NOT relied
# on here):
#   - eq_chat.cpp's `s_debug_level >= 1` "Sending say message: '...'" line
#     (unbuffered std::cout), which shows the LITERAL text hail()/
#     hailTarget() send ("Hail" vs "Hail, <name>") -- run with -d 1.
#   - console_renderer.cpp's setCurrentTarget()/clearCurrentTarget(), which
#     print "Target: <name> ... HP: NN%" directly (unbuffered std::cout) --
#     this is the new T39 case in ConsoleRenderer::handleEvent().
#
# Testchar's canonical position is zone_id=36 (cazicthule); confirmed via DB
# query before this run. Spawns a GM npctype (same mechanism T38 used) to
# have a nameable, targetable entity.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T39"
BIN="/mnt/projects/fable/wt/t1/build/bin/willeq"
CFG="${RUNDIR}/willeq_t39.json"
LOG="${RUNDIR}/transcript_hail_target_acceptance.txt"
FIFO="${RUNDIR}/cmds_hail_target_acceptance.fifo"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (HEADLESS mode, --no-graphics, -d 1, piped stdin) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 1 < "${FIFO}" > "${LOG}" 2>&1 &
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

if ! wait_for_log "Zone connection complete! Player position confirmed" 60; then
    echo "[driver] FAILED: zone connection never completed" | tee -a "${LOG}.driver"
fi
sleep 3

# --- Step 1: baseline, no target -- /hail must take the plain hail() branch ---
send "/hail"
sleep 2

# --- Step 2: spawn a nameable, targetable NPC (same mechanism as T38) ---
send "/say #npctypespawn 351043"
wait_for_log "Spawned a_rat|Spawned .*351043" 20
sleep 3

# --- Step 3: target it by name (routes through CombatManager::SetTarget(name),
#     NOT through the eq_bridge.cpp TargetIntent push sites -- proves the
#     Application::syncCombatState() polling wiring works regardless of how
#     the target changed, not just the renderer-click path) ---
send "/target a_rat"
sleep 3

# --- Step 4: with a target set, /hail must take the hailTarget() branch ---
send "/hail"
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
