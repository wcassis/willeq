#!/bin/bash
# T32 primary verification driver: pipes slash commands to a headless willeq
# client (no keyboard) against the live eqemu server, spawns a lootable mob
# adjacent to the GM player, drives combat/hunt/loot entirely automatically,
# interleaves a /who liveness probe mid-loot, and captures the full
# transcript for the LootSubState DEBUG transitions.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T32"
BIN="/mnt/projects/fable/wt/t1/build/bin/willeq"
CFG="${RUNDIR}/willeq_t32.json"
LOG="${RUNDIR}/transcript_primary.txt"  # .txt, not .log -- repo .gitignore excludes *.log
FIFO="${RUNDIR}/cmds.fifo"

rm -f "${FIFO}" "${LOG}"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (headless, piped stdin, -d 5/DEBUG) ==" | tee -a "${LOG}.driver"
# Launch willeq in the background FIRST so its open(FIFO, O_RDONLY) happens
# off the main script's thread of control -- opening a fifo for reading
# blocks until a writer connects, and opening it for writing (below) blocks
# until a reader connects. Doing both in the same sequential script would
# deadlock; backgrounding the reader here lets the two rendezvous once we
# open fd 3 for writing next.
"${BIN}" -c "${CFG}" --no-graphics -d 5 < "${FIFO}" > "${LOG}" 2>&1 &
WPID=$!
echo "willeq pid=${WPID}" | tee -a "${LOG}.driver"

# Keep the fifo open for writing across multiple `echo` appends (fd 3).
# Opened O_RDWR (not O_WRONLY) so this open() call itself never blocks
# waiting for a reader -- belt-and-suspenders on top of backgrounding
# willeq above.
exec 3<>"${FIFO}"

send() {
    echo "$1" >&3
    echo "[driver] sent: $1" >> "${LOG}.driver"
}

wait_for_log() {
    # wait_for_log <pattern> <max_seconds>
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

# 3. Spawn a lootable mob adjacent to the GM player: npc 79037
#    "a_scaled_wolf_pup" (level 1, 11 hp, non-humanoid race 42, loottable
#    103626 -> lootdrop 156971 has "Scaled Curskin" item_id 12654 at
#    probability=100/chance=100, i.e. guaranteed loot on every kill).
#    Deliberately level 1 (matches Testchar's level 1) and non-humanoid:
#    CombatManager::IsTargetSuitableForHunt() (combat.cpp:1467) requires
#    con color white/blue/lightblue AND (for non-aggressive faction 5-9)
#    a non-humanoid race, or auto-hunt's scanner silently skips it (found
#    live: npc 19001 "Chomper", level 3, conned YELLOW to level-1 Testchar
#    and was rejected -- "Chomper000 is NOT suitable for hunting" -- even
#    though it was the closest, freshest-spawned entity). #npctypespawn
#    spawns at the GM's own position (server source:
#    zone/gm_commands/npctypespawn.cpp -- c->GetPosition()).
send "/say #npctypespawn 79037"
wait_for_log "Spawned a_scaled_wolf_pup|Spawned .*79037" 15

# 4. Confirm the spawn is visible to the client.
send "/list"
sleep 2

# 5. Enable combat manager and pathfinding (flag only -- target is
#    adjacent, no real path-following needed). Do NOT enable /hunt yet --
#    see step 6.
send "/combat on"
sleep 1
send "/pathfinding on"
sleep 1

# 6. Enable auto-hunting (the ONLY live path into LootSubState::
#    AwaitingCorpse -- combat.cpp:1162 requires m_auto_loot_enabled &&
#    m_auto_hunting_enabled) and IMMEDIATELY manually target+attack the
#    wolf pup, with no delay in between. This preempts auto-hunt's own
#    background FindNextHuntTarget() scan (throttled to every 500ms,
#    first scan observed live ~900ms after /hunt on) from potentially
#    picking a different, already-present zone spawn instead of our
#    freshly-spawned, guaranteed-lootable target. SetAutoHunting(true)
#    forces state to HUNTING; EnableAutoAttack() (invoked by /attack)
#    then moves it to ENGAGED, which is what actually drives
#    ProcessCombatRound() each tick -- so ordering here matters.
send "/hunt on"
send "/target wolf_pup"
send "/attack"

# 7. Let combat resolve and poll for the kill + AwaitingCorpse entry.
wait_for_log "Target .*died|Awaiting corpse" 30

# 7. LIVENESS PROBE: fire /who and a chat line immediately, while the loot
#    state machine should still be mid-sequence (AwaitingItems/LootingItems).
#    This is the specific property T26 changed -- before T26 the game
#    thread blocked ~1.1s here and drained nothing.
send "/who"
send "/say liveness-probe-during-loot"
echo "[driver] liveness probe sent" >> "${LOG}.driver"

# 8. Wait for the loot cycle to close out (Closing -> Idle) and for the
#    liveness probe's CMD acks to appear.
wait_for_log "CloseLootWindow: Sent HC_OP_EndLootRequest" 20
sleep 1

# 9. Second /who after loot closes, to bound the "never froze" claim on
#    both sides of the loot sequence.
send "/who"
sleep 2

# 10. Clean shutdown.
send "/camp"
wait_for_log "shutdown complete|Application shutdown" 15

# Bounded wait for process exit.
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
