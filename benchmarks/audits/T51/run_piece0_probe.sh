#!/bin/bash
# T51 Piece 0 -- BEFORE any code changes: establish live, headless, which
# registry actually serves /attack, /stopattack, /aa, /a, and /att. Also
# prove auto-attack GENUINELY ENGAGES (real melee rounds / damage against a
# real mob), not just that a command was accepted.
#
# Method: spawn a fresh a_scaled_wolf_pup (npc 79037, the same mob T32 used
# for its own real kill -- level 1, matches Testchar, non-humanoid so no
# faction/aggro surprises) per alias tested, target it, send the command,
# capture the "CMD outcome=applied registry=..." ack line AND watch for real
# ProcessCombatRound / damage lines in the log (not just the ack). One fight
# (via bare /attack) is allowed to run to a real kill, matching T32's own
# evidence shape; the other three (/aa, /a, /att) are stopped early via
# /stopattack once real damage lines are observed, to conserve run time --
# stopping still proves engagement (swings/damage already logged) and
# additionally proves /stopattack's own registry.
#
# Same fifo-driver shape as T48/T49/T50's run_verification_primary.sh.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T51"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-piece0_primary}"
CFG="${RUNDIR}/willeq_t51.json"
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

spawn_and_target_wolf_pup() {
    # spawns npc 79037 at GM's position, resolves the fresh spawn name via
    # /list, targets it. Echoes the resolved name to stdout.
    send "/say #npctypespawn 79037"
    wait_for_log "Spawned a_scaled_wolf_pup|Spawned .*79037" 15
    sleep 1
    send "/list"
    sleep 2
    local name
    name=$(grep -a -oE "New spawn: a_scaled_wolf_pup[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
    echo "[driver] fresh wolf pup spawn name resolved to '${name}'" >> "${LOG}.driver"
    send "/target ${name}"
    sleep 1
}

# 1. Wait for full zone-in.
wait_for_log "Zone connection complete! Player position confirmed" 60

# ===========================================================================
# Leg 1 -- /attack (registry table entry) + full kill (real damage, per T32)
# ===========================================================================
echo "[driver] === LEG 1: /attack (fight to a real kill) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/attack"
wait_for_log "CMD outcome=.* cmd=\"/attack\"" 10
wait_for_log "ProcessCombatRound" 15
sleep 1
send "/npcstate"
wait_for_log "was killed by Testchar|You have slain" 60

# ===========================================================================
# Leg 2 -- /stopattack (registry table entry), against a fresh target,
# engaged first via /attack so real damage is observed before stopping.
# ===========================================================================
echo "[driver] === LEG 2: /attack then /stopattack (mid-fight) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/attack"
wait_for_log "CMD outcome=.* cmd=\"/attack\"" 10
wait_for_log "ProcessCombatRound.*in_range=true" 15
sleep 3
send "/stopattack"
wait_for_log "CMD outcome=.* cmd=\"/stopattack\"" 10
sleep 1

# ===========================================================================
# Leg 3 -- /aa (toggle on then off), fresh target, real engagement.
# ===========================================================================
echo "[driver] === LEG 3: /aa toggle ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/aa"
wait_for_log "CMD outcome=.* cmd=\"/aa\"" 10
wait_for_log "ProcessCombatRound.*in_range=true" 15
sleep 3
send "/aa"
wait_for_log "CMD outcome=.* cmd=\"/aa\"" 10
sleep 1

# ===========================================================================
# Leg 4 -- /a (registry-2-only alias, THE ALIAS TRAP), fresh target.
# ===========================================================================
echo "[driver] === LEG 4: /a (alias) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/a"
wait_for_log "CMD outcome=.* cmd=\"/a\"" 10
wait_for_log "ProcessCombatRound.*in_range=true" 15
sleep 3
send "/stopattack"
wait_for_log "CMD outcome=.* cmd=\"/stopattack\"" 10
sleep 1

# ===========================================================================
# Leg 5 -- /att (registry-1-only alias), fresh target.
# ===========================================================================
echo "[driver] === LEG 5: /att (alias) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/att"
wait_for_log "CMD outcome=.* cmd=\"/att\"" 10
wait_for_log "ProcessCombatRound.*in_range=true" 15
sleep 3
send "/stopattack"
wait_for_log "CMD outcome=.* cmd=\"/stopattack\"" 10
sleep 1

send "/who"
sleep 2

# Clean shutdown. /camp has a real 30s in-game timer (T33/T34 finding).
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
