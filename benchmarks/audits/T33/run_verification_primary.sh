#!/bin/bash
# T33 primary verification driver: real kill against a real server, driven
# entirely by piped stdin (no keyboard), that must show the loot state
# machine entering AwaitingCorpse from the DEATH EVENT (not the dead
# hp_percent poll) and advancing AwaitingItems -> LootingItems -> Closing,
# with the guaranteed-drop item actually moving into inventory. Reuses
# T32's method verbatim (benchmarks/audits/T32/run_verification.sh) --
# same mob (npc 79037 a_scaled_wolf_pup, guaranteed 100%-chance item 12654
# "Scaled Curskin"), same command ordering, same liveness probes.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T33"
BIN="/mnt/projects/fable/wt/t1/build/bin/willeq"
CFG="${RUNDIR}/willeq_t33.json"
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

# 1. Wait for full zone-in.
wait_for_log "Zone connection complete! Player position confirmed" 60

# 2. Baseline liveness probe before any combat.
send "/who"
sleep 2

# 3. Check starting inventory slot 23 (general 1) is empty, so a post-kill
#    item there is unambiguously new -- confirm via /inventory or /iteminfo
#    isn't scripted here; instead we rely on grepping for AddLootItem /
#    inventory-slot-changed log lines plus the corpse's known guaranteed
#    drop (item 12654, confirmed 100% chance against the live DB before
#    this run).
send "/say #npctypespawn 79037"
wait_for_log "Spawned a_scaled_wolf_pup|Spawned .*79037" 15

send "/list"
sleep 2

# T33 finding (not T32): befallen accumulates persistent corpses/spawns
# across runs, so the base name "a_scaled_wolf_pup" is not unique -- both
# stale corpses ("...`s_corpse<id>") and prior live spawns
# ("...pup000", "...pup001", ...) can already be in the zone.
# "/target wolf_pup" partial-matches whichever one the entity map (sorted
# by ascending spawn_id) reaches first, which is not necessarily today's
# fresh spawn. Read the exact "New spawn: a_scaled_wolf_pup..." name this
# run's #npctypespawn just produced out of the transcript instead of
# guessing a suffix, and target that literal string.
FRESH_NAME=$(grep -oE "New spawn: a_scaled_wolf_pup[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh spawn name resolved to '${FRESH_NAME}'" >> "${LOG}.driver"
if [ -z "${FRESH_NAME}" ]; then
    echo "[driver] FATAL: could not resolve fresh spawn name from transcript" >> "${LOG}.driver"
fi

# 4. Enable combat + pathfinding flag (target is adjacent; no real
#    pathfinding exercised).
send "/combat on"
sleep 1
send "/pathfinding on"
sleep 1

# 5. Auto-hunt on, then immediately target+attack -- preempts auto-hunt's
#    own background scan from grabbing a different pre-existing zone spawn.
send "/hunt on"
send "/target ${FRESH_NAME}"
send "/attack"

# 6. Let combat resolve. T33's headline claim: the DEATH EVENT
#    (ZoneProcessDeath -> NotifyTargetDied) enters AwaitingCorpse, not the
#    dead ProcessCombatRound poll. 60s, not 30 -- a level-1-vs-level-1
#    fight with low melee skill can run 30-40s, and the loot window itself
#    (AwaitingCorpse->Closing) is only ~1s, so undershooting this timeout
#    makes the liveness probe below land BEFORE the kill instead of during
#    the loot sequence (found live in this unit's first attempt).
wait_for_log "Target .*died, disabling auto-attack|Awaiting corpse" 60

# 7. LIVENESS PROBE mid-loot.
send "/who"
send "/say liveness-probe-during-loot"
echo "[driver] liveness probe sent" >> "${LOG}.driver"

# 8. Wait for loot cycle to close.
wait_for_log "CloseLootWindow: Sent HC_OP_EndLootRequest" 20
sleep 1

# 9. Second /who after loot closes.
send "/who"
sleep 2

# 10. Clean shutdown.
send "/camp"
wait_for_log "shutdown complete|Application shutdown" 15

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
