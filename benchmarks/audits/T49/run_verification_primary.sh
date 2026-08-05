#!/bin/bash
# T49 live acceptance driver: real vendor + banker + trainer NPC sessions,
# opened/closed ENTIRELY BY PIPED STDIN (no keyboard, no renderer mouse
# click), read back through the new /npcstate reader command
# (action::CommandProcessor, T49) which is fed by GameStateSyncBridge from
# the six window-open/close bridge events -- syncNPCInteractionState() (the
# poll this unit deletes) is gone by the time this runs.
#
# Same fifo-driver shape as T48's run_verification_primary.sh
# (benchmarks/audits/T48/), adapted for T49's three legs:
#
#   Vendor: /vendor (T48) -> /npcstate (T49) shows real sellRate (not the
#           poll's hardcoded 1.0) and toDisplayName()'d name -> /vendor
#           (close) -> /npcstate shows none.
#   Banker: /bank (pre-existing registry-1 command, toggles open/close via
#           OpenBankWindow()/CloseBankWindow()) -> /npcstate -> /bank (close)
#           -> /npcstate shows none.
#   Trainer: /train (pre-existing registry-1 command, targets a guildmaster
#           NPC class 20-35) -> /npcstate shows real name/id. NOTE: there is
#           no command-channel path to CloseTrainerWindow() anywhere in the
#           registries (grepped eq_commands*.cpp) -- trainer is open-only
#           from automation. Recorded as a finding, not worked around.
#
# Vendor NPC: id 771 "SummonResupplyAgent" (same as T48). Trainer NPC: id
# 1007 "Miltiades_Tal`Azroth", class=22 (in the WarriorGM..BerserkerGM
# 20..35 range /train requires), confirmed live against the DB before this
# run.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T49"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t49.json"
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
#    three -- the un-migrated state's rest condition.
send "/npcstate"
sleep 2

# ===========================================================================
# Vendor leg
# ===========================================================================

send "/say #npctypespawn 771"
wait_for_log "Spawned SummonResupplyAgent|Spawned .*771" 15
sleep 1

send "/list"
sleep 2

VENDOR_NAME=$(grep -a -oE "New spawn: SummonResupplyAgent[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh vendor spawn name resolved to '${VENDOR_NAME}'" >> "${LOG}.driver"

send "/target ${VENDOR_NAME}"
sleep 1
send "/vendor"
wait_for_log "Opened vendor window for" 15
sleep 1

# THE HEADLINE READ: /npcstate against the migrated (event-fed) state,
# while the ShopRequest packet's real sell_rate is also visible in the same
# log (LOG_DEBUG "ShopRequest response: ... sell_rate=...") for direct
# comparison -- the poll this unit deletes hardcoded sellRate=1.0f
# regardless of what the packet said.
send "/npcstate"
sleep 2

send "/vendor"
wait_for_log "Closing vendor window for NPC" 15
sleep 1

send "/npcstate"
sleep 2

# ===========================================================================
# Banker leg
# ===========================================================================

send "/bank"
wait_for_log "Bank window opened" 15
sleep 1

send "/npcstate"
sleep 2

send "/bank"
wait_for_log "Bank window closed" 15
sleep 1

send "/npcstate"
sleep 2

# ===========================================================================
# Trainer leg
# ===========================================================================

send "/say #npctypespawn 1007"
wait_for_log "Spawned Miltiades|Spawned .*1007" 15
sleep 1

send "/list"
sleep 2

TRAINER_NAME=$(grep -a -oE "New spawn: Miltiades_Tal.Azroth[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh trainer spawn name resolved to '${TRAINER_NAME}'" >> "${LOG}.driver"

send "/target ${TRAINER_NAME}"
sleep 1
send "/train"
wait_for_log "Trainer window opened for" 15
sleep 1

send "/npcstate"
sleep 2

# No command-channel path to CloseTrainerWindow() exists (finding, recorded
# in the Review, not worked around) -- leave the trainer session open and
# proceed to shutdown rather than inventing a hook to close it.

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
