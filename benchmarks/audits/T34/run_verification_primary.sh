#!/bin/bash
# T34 primary verification driver: real corpse, real /loot <id> command,
# driven entirely by piped stdin (no keyboard, no shift+click) -- must show
# the ACTION-LAYER path (the one T23 measured stalling the game thread
# ~1.1s/corpse, and the one T26's sleep_for removal was actually built for)
# reached live for the first time:
#
#   /loot <corpseId>
#     -> CommandProcessor::cmdLoot()               (T34, this unit)
#     -> ActionDispatcher::lootCorpse(corpseId)     (action_dispatcher.cpp:473)
#     -> EqActionHandler::lootCorpse(corpseId)      (eq_action_handler.cpp:353)
#     -> CombatManager::LootCorpse() -> BeginLootRequest()
#        -> COMBAT_STATE_LOOTING, m_player_looting_corpse_id STAYS 0
#           (BeginLootRequest sends its own HC_OP_LootRequest; it never
#           touches EverQuest::m_player_looting_corpse_id -- that field is
#           only set by the OTHER loot route, RequestLootCorpse(), which
#           this path deliberately does not call)
#     -> zone dispatch: HC_OP_ItemPacket handler, m_player_looting_corpse_id
#        == 0 branch -> m_combat_manager->IsLooting() branch (eq.cpp, the
#        "headless mode looting" comment) -> ZoneProcessLootItem()
#        -> CombatManager::CheckAutoLoot()
#     -> (T26) LootSubState::AwaitingItems -> LootingItems -> Closing
#
# Reuses T32/T33's guaranteed-drop mob (npc 79037 a_scaled_wolf_pup, item
# 12654 "Scaled Curskin" at 100% chance, reconfirmed against the live DB
# before this run) and T33's fifo-driver shape
# (benchmarks/audits/T33/run_verification_primary.sh), adapted per T34's
# plan.
#
# CRITICAL DIFFERENCE FROM T33: auto-hunting is deliberately left OFF
# (never send "/hunt on"). NotifyTargetDied()'s gate is
# `if (!m_auto_loot_enabled || !m_auto_hunting_enabled) return;` -- with
# hunting off, the death event does NOT enter AwaitingCorpse/looting on its
# own, so the corpse sits untouched with its item until THIS script's
# explicit "/loot <id>" is the first and only thing that drives the loot
# state machine. That isolates the action-layer path from T33's
# already-verified death-event path instead of racing/duplicating it.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T34"
BIN="/mnt/projects/fable/wt/t1/build/bin/willeq"
CFG="${RUNDIR}/willeq_t34.json"
LOG="${RUNDIR}/transcript_primary.txt"
FIFO="${RUNDIR}/cmds_primary.fifo"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (headless, piped stdin, -d 5/DEBUG) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 5 < "${FIFO}" > "${LOG}" 2>&1 &
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

# 1. Wait for full zone-in.
wait_for_log "Zone connection complete! Player position confirmed" 60

# 2. Baseline liveness probe before any combat.
send "/who"
sleep 2

# 3. Spawn the guaranteed-drop mob at the GM's own position (no pathfinding
#    needed -- distance=0 the whole fight, same as T32/T33).
send "/say #npctypespawn 79037"
wait_for_log "Spawned a_scaled_wolf_pup|Spawned .*79037" 15

send "/list"
sleep 2

# T33 finding carried forward: befallen accumulates persistent
# corpses/spawns across runs, so "a_scaled_wolf_pup" is not a unique
# substring for /target. Read the exact "New spawn: a_scaled_wolf_pup<N>"
# name this run's #npctypespawn just produced out of the transcript.
FRESH_NAME=$(grep -a -oE "New spawn: a_scaled_wolf_pup[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh spawn name resolved to '${FRESH_NAME}'" >> "${LOG}.driver"
if [ -z "${FRESH_NAME}" ]; then
    echo "[driver] FATAL: could not resolve fresh spawn name from transcript" >> "${LOG}.driver"
fi

# 4. Enable combat (auto-attack only) -- deliberately NO "/hunt on". This
#    keeps the death-event auto-loot route (T33) from firing, so the
#    corpse's item sits untouched until this script's own "/loot" call is
#    the only thing that drives the state machine.
send "/combat on"
sleep 1
send "/target ${FRESH_NAME}"
send "/attack"

# 5. Let melee combat resolve. Same generous 60s timeout T33 used --
#    level-1-vs-level-1 melee can run 30-40s.
wait_for_log "died, disabling auto-attack" 60

# 6. Confirm the corpse did NOT auto-enter the loot machine (auto-hunting
#    off -- NotifyTargetDied's gate should have returned early). Give it a
#    couple seconds to prove a negative, then resolve the corpse's id from
#    the "Entity <id> became corpse" rename line (entity rename/is_corpse
#    tracking fires unconditionally in ZoneProcessDeath, independent of
#    the auto-hunt gate -- same technique T33 used to get an authoritative
#    id rather than guessing). Note: transcript log files in this project
#    have shown up as "Non-ISO extended-ASCII text" to `file`(1) -- grep -a
#    is required or plain `grep` silently returns zero matches.
sleep 2
CORPSE_LINE=$(grep -a -E "became corpse" "${LOG}" | tail -n1)
echo "[driver] corpse rename line: ${CORPSE_LINE}" >> "${LOG}.driver"
CORPSE_ID=$(echo "${CORPSE_LINE}" | grep -oE 'Entity [0-9]+ became corpse' | grep -oE '[0-9]+')
echo "[driver] resolved corpse id: ${CORPSE_ID}" >> "${LOG}.driver"
if [ -z "${CORPSE_ID}" ]; then
    echo "[driver] FATAL: could not resolve corpse id from transcript" >> "${LOG}.driver"
fi

# 7. THE ACTION-LAYER PATH: explicit /loot <id>, the exact same
#    ActionDispatcher::lootCorpse() entry the renderer's shift+click
#    overlay callback uses. This is the headline event of this unit.
send "/loot ${CORPSE_ID}"

# 8. LIVENESS PROBE mid-loot -- sent immediately after /loot, no delay, to
#    land inside the AwaitingItems->LootingItems->Closing window (T33's
#    baseline: that window is ~1s under T26's event-driven cadence, vs.
#    T23's documented ~1.1s BLOCKING stall pre-T26). If the game thread
#    were blocked the way T23 measured, these would not be answered until
#    after the loot sequence closes.
send "/who"
send "/say liveness-probe-during-loot"
echo "[driver] liveness probe sent ($(date +%s.%N))" >> "${LOG}.driver"

# 9. Wait for loot cycle to close.
wait_for_log "CloseLootWindow: Sent HC_OP_EndLootRequest" 20
sleep 1

# 10. Second /who after loot closes, to bookend liveness.
send "/who"
sleep 2

# 11. Clean shutdown. /camp has a real 30s in-game timer before the
#     server logs the character out (confirmed live in this unit's first
#     run: "You have begun to camp. You will log out in 30 seconds."), so
#     this needs a longer timeout than T33's 15s or it always falls
#     through to the /quit+SIGTERM fallback below (harmless, but noisy).
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
