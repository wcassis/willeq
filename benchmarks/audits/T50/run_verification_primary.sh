#!/bin/bash
# T50 live acceptance driver: real banker + trainer NPC sessions driven
# ENTIRELY BY PIPED STDIN (no keyboard, no renderer mouse click, no scratch
# hooks -- T48's standard), closing the two live caveats T49 recorded but
# could not exercise:
#
#   Banker: /bank now pushes BankerInteractIntent{target_id} (T50 Piece 1)
#           instead of bypassing it with a no-arg OpenBankWindow() that fell
#           back to a hardcoded dummy npc id (T49's self-closing-bank bug,
#           reproduced with npc id 1 == zone_controller000, 31690 units
#           away). Targeting a REAL banker fixes the distance the intent
#           handler checks, so the window should OPEN and STAY OPEN across
#           several seconds of movement ticks, and /npcstate (T49) should
#           report it live for the first time ever in this project.
#   Trainer: /train now pre-validates distance and class match (T50 Piece
#           2) before sending HC_OP_GMTraining, instead of sending a request
#           the server drops in total silence (T49's reproduction: Testchar
#           is class 1/Warrior, Miltiades_Tal`Azroth is class 22/PaladinGM,
#           trains class 3/Paladin -- a cross-class mismatch, not a range
#           issue, per Client::OPGMTraining verified against the real eqemu
#           source). Targeting a real WarriorGM (class 20, trains class 1 =
#           Warrior = Testchar's own class) should make the window actually
#           open for the first time.
#
# Same fifo-driver shape as T48/T49's run_verification_primary.sh.
#
# NPCs used (confirmed live against the DB before this run):
#   Banker:  id 772  "SummonClockworkBanker" (class=40 == CLASS_BANKER)
#   Trainer: id 1063 "Brin_Stolunger"       (class=20 == WarriorGM, trains
#            class 20-19=1=Warrior, matching Testchar's own class=1)
#   Wrong-class trainer (negative leg): id 1007 "Miltiades_Tal`Azroth"
#            (class=22 == PaladinGM, T49's exact reproduction NPC)
#
# #npctypespawn spawns at the GM's own current position (0 units away,
# same technique T48/T49 used) -- so the too-far negative leg uses the real
# GM command "#goto x y z" (command_add("goto", ...), zone/command.cpp) to
# teleport the PLAYER away from the still-targeted trainer, rather than
# inventing any client-side distance fakery.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T50"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t50.json"
LOG="${RUNDIR}/transcript_${LOG_SUFFIX}.txt"
FIFO="${RUNDIR}/cmds_${LOG_SUFFIX}.fifo"

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

# 2. Baseline: before any interaction, /npcstate must report none for all
#    three (banker/trainer are this unit's subject; vendor untouched).
send "/npcstate"
sleep 2

# ===========================================================================
# Banker leg -- the window that OPENS AND STAYS OPEN
# ===========================================================================

send "/say #npctypespawn 772"
wait_for_log "Spawned SummonClockworkBanker|Spawned .*772" 15
sleep 1

send "/list"
sleep 2

BANKER_NAME=$(grep -a -oE "New spawn: SummonClockworkBanker[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh banker spawn name resolved to '${BANKER_NAME}'" >> "${LOG}.driver"

send "/target ${BANKER_NAME}"
sleep 1
send "/bank"
wait_for_log "Bank window opened" 15
sleep 1

send "/npcstate"
sleep 2

# THE HEADLINE READ: several seconds of movement ticks pass (the player is
# stationary, but GameTick()'s movement/proximity logic still runs every
# tick) -- T49's dummy-id bug closed the window on the very NEXT tick. If
# it is still open after this sleep, the fix is proven, not just the first
# read.
sleep 6
send "/npcstate"
sleep 2

send "/bank"
wait_for_log "Bank window closed" 15
sleep 1

send "/npcstate"
sleep 2

# ===========================================================================
# Trainer leg (positive) -- a WarriorGM matching Testchar's own class
# ===========================================================================

send "/say #npctypespawn 1063"
wait_for_log "Spawned Brin_Stolunger|Spawned .*1063" 15
sleep 1

send "/list"
sleep 2

TRAINER_NAME=$(grep -a -oE "New spawn: Brin_Stolunger[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh WarriorGM spawn name resolved to '${TRAINER_NAME}'" >> "${LOG}.driver"

send "/target ${TRAINER_NAME}"
sleep 1
send "/train"
wait_for_log "Trainer window opened for" 15
sleep 1

send "/npcstate"
sleep 2

# ===========================================================================
# Negative leg 1 -- wrong-class trainer (T49's exact reproduction NPC)
# ===========================================================================

send "/say #npctypespawn 1007"
wait_for_log "Spawned Miltiades|Spawned .*1007" 15
sleep 1

send "/list"
sleep 2

WRONGCLASS_NAME=$(grep -a -oE "New spawn: Miltiades_Tal.Azroth[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh wrong-class trainer spawn name resolved to '${WRONGCLASS_NAME}'" >> "${LOG}.driver"

send "/target ${WRONGCLASS_NAME}"
sleep 1
send "/train"
sleep 2

# ===========================================================================
# Negative leg 2 -- too-far target.
#
# Two approaches were tried and rejected before this one:
#  - The GM command "#goto x y z" (command_goto -> Client::MovePC,
#    zone/gm_commands/goto.cpp) triggers a full same-zone client re-entry
#    (OP_ZoneChange round trip) whose server-approved position turned out
#    to just be Testchar's DB-persisted character_data row, NOT the
#    requested destination, for both an out-of-bounds guess (1000,1000,3)
#    and befallen's own real safe point (zone table, zoneidnumber=36:
#    safe_x=35, safe_y=-82) -- confirmed live both times (see driver log
#    history); a real eqemu same-zone-teleport quirk, not investigated
#    further (server-side, out of this unit's territory).
#  - Disconnecting (/camp), updating character_data's x/y directly (a
#    sanctioned "documented character-position write"), and reconnecting
#    DOES land the new client session at the written DB coordinates
#    (confirmed live) -- but a probe /who after reconnecting showed the
#    #npctypespawn'd NPCs from the prior session gone: they do not survive
#    a full client disconnect/reconnect cycle in this environment, so a
#    fresh session would have no trainer left to re-target.
#
# So this leg uses "/warp x y z" (eq_commands_movement.cpp) instead: a
# pre-existing, PURELY CLIENT-SIDE command (sets m_x/m_y/m_z directly, then
# SendPositionUpdate() informs the server) -- not a scratch hook added for
# this unit, and not a GM/server operation at all. It moves only the local
# position /train's own distance check reads (CalculateDistance2D(m_x, m_y,
# target.x, target.y)), leaving the still-targeted, still-alive WarriorGM
# from the positive leg above exactly where it was spawned.
# ===========================================================================

send "/warp 2000 2000 3"
sleep 2

send "/target ${TRAINER_NAME}"
sleep 1
send "/train"
sleep 2

# Warp back before /camp -- the world server persists the in-memory
# position to character_data on disconnect, and this run's final restore
# step (Testchar -> zone_id=36, x=-77.9232, y=21.9086, z=3, h=190) should
# not have to fight a stale warped-away position.
send "/warp -77.9232 21.9086 3"
sleep 2

send "/who"
sleep 2

# 11. Clean shutdown. /camp has a real 30s in-game timer (T33/T34 finding).
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
