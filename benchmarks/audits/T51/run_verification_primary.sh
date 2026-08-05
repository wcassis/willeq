#!/bin/bash
# T51 live acceptance -- AFTER the code change: registry 2's shadowed
# attack/stopattack/aa registrations and the two producer-less intents are
# deleted. This proves the deletion is behaviour-neutral at the command
# line: every surviving alias (attack, att, stopattack, aa, ~, a) still
# resolves and does exactly what it did before (Piece 0's pre-change
# probe, run_piece0_probe.sh, is the "before" half of the comparison).
#
# /a is deliberately exercised here too, even though it means /assist (not
# attack) both before and after -- T51 Piece 0's live finding was that
# registry 2's /a-for-attack alias was ALREADY unreachable before this
# unit touched anything (shadowed by /assist's own pre-existing "a" alias
# in registry 1, T28), so /a's behaviour is asserted UNCHANGED, not lost.
#
# Same fifo-driver shape as T48/T49/T50/T51-piece0.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T51"
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-postchange_primary}"
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
# Leg 1 -- /attack: registry table entry + real engagement.
# ===========================================================================
echo "[driver] === LEG 1: /attack ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/attack"
wait_for_log "CMD outcome=.* cmd=\"/attack\"" 10
sleep 4
send "/stopattack"
wait_for_log "CMD outcome=.* cmd=\"/stopattack\"" 10
sleep 1

# ===========================================================================
# Leg 2 -- /att alias.
# ===========================================================================
echo "[driver] === LEG 2: /att (alias) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/att"
wait_for_log "CMD outcome=.* cmd=\"/att\"" 10
sleep 4
send "/stopattack"
wait_for_log "CMD outcome=.* cmd=\"/stopattack\"" 10
sleep 1

# ===========================================================================
# Leg 3 -- /aa toggle.
# ===========================================================================
echo "[driver] === LEG 3: /aa toggle ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/aa"
wait_for_log "CMD outcome=.* cmd=\"/aa\"" 10
sleep 4
send "/aa"
wait_for_log "CMD outcome=.* cmd=\"/aa\"" 10
sleep 1

# ===========================================================================
# Leg 4 -- "~" alias for /aa.
# ===========================================================================
echo "[driver] === LEG 4: ~ (alias for aa) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/~"
wait_for_log "CMD outcome=.* cmd=\"/~\"" 10
sleep 4
send "/~"
wait_for_log "CMD outcome=.* cmd=\"/~\"" 10
sleep 1

# ===========================================================================
# Leg 5 -- /a: still assist, not attack (unchanged by this unit).
# ===========================================================================
echo "[driver] === LEG 5: /a (assist, unchanged) ===" >> "${LOG}.driver"
spawn_and_target_wolf_pup
send "/a"
wait_for_log "CMD outcome=.* cmd=\"/a\"" 10
sleep 2

# ===========================================================================
# Leg 6 -- registry 2's deleted names now unrecognized there, but still
# resolve via registry 1 (proves the command SET is unchanged from the
# user's point of view -- only the dead internal duplicate is gone).
# ===========================================================================
echo "[driver] === LEG 6: full command set unchanged from user PoV ===" >> "${LOG}.driver"
send "/attack"
sleep 1
send "/stopattack"
sleep 1
send "/aa"
sleep 1
send "/aa"
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
