#!/bin/bash
# T104 live verification driver: pipes slash commands to a headless willeq
# client (no keyboard) against the live eqemu server. Spawns a mob adjacent
# to the GM player (pathfinding-avoidance, same technique as T32), targets
# it, damages it via repeated /consider-free real melee rounds under
# /combat on, and captures the [T104] GameStateSyncBridge debug lines that
# prove Application::m_gameState->combat() keeps updating under damage --
# the specific correlation risk the plan named.
set -u

RUNDIR="/mnt/projects/fable/wt/slot2/benchmarks/audits/T104"
BIN="/mnt/projects/fable/wt/slot2/build/bin/willeq"
CFG="${RUNDIR}/willeq_t104.json"
LOG="${RUNDIR}/transcript_primary.txt"
FIFO="${RUNDIR}/cmds.fifo"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/slot2

echo "== starting willeq (headless, piped stdin, -d 4/DEBUG) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 4 --log-level=DEBUG < "${FIFO}" > "${LOG}" 2>&1 &
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
wait_for_log "Zone connection complete! Player position confirmed" 90

# 2. Force daylight BEFORE any capture-relevant activity (not capturing
#    screenshots this unit, but keep the habit -- also verifies the client
#    survives a GM command round-trip early, per protocol).
send "/say #set time 11 0"
sleep 2

# 3. Baseline liveness probe.
send "/who"
sleep 2

# 4. Spawn a mob ADJACENT to the GM player (distance=0.0, no pathfinding
#    needed) -- same npc_type T32 verified: 79037 a_scaled_wolf_pup,
#    level 1, matches Testchar, non-humanoid race 42.
#    NOTE (round 2 fix): this zone already had ~15 leftover
#    a_scaled_wolf_pup000..014 from an earlier, unrelated session sitting at
#    the same coordinates (never cleaned up). Round 1 of this driver used
#    the bare substring "wolf_pup" and silently locked onto stale entity 135
#    instead of this run's own fresh spawn -- combat never landed a hit in
#    30s against it (round 1's transcript, kept for the record). Fix:
#    extract THIS run's exact spawn name/id from the log and target that
#    exact string, which cannot substring-match any of the 000..014 leftovers.
send "/say #npctypespawn 79037"
wait_for_log "New spawn: a_scaled_wolf_pup[0-9]+ \(ID: [0-9]+\)" 15
sleep 1
SPAWN_LINE=$(grep -oE "New spawn: a_scaled_wolf_pup[0-9]+ \(ID: [0-9]+\)" "${LOG}" | tail -1)
SPAWN_NAME=$(echo "${SPAWN_LINE}" | sed -E 's/New spawn: (a_scaled_wolf_pup[0-9]+).*/\1/')
echo "[driver] fresh spawn resolved: ${SPAWN_LINE} -> targeting '${SPAWN_NAME}'" >> "${LOG}.driver"

# 5. Target it BY EXACT NAME -- proves TargetChanged publishes from
#    CombatManager::SetTarget() (the /target command path, NOT the
#    mouse-click TargetIntent path) and GameStateSyncBridge's [T104] log
#    line fires.
send "/target ${SPAWN_NAME}"
wait_for_log "\[T104\] GameStateSyncBridge: TargetChanged -> projection" 15

# 6. /combat on enables CombatManager itself (a separate flag from
#    auto-attack, per T102's finding -- CombatManager::Enable(), not
#    EnableAutoAttack()). Sent for completeness/parity with the plan's live
#    leg wording, but the AutoAttackChanged publish this unit cares about
#    comes from /attack below, which is what actually calls
#    CombatManager::EnableAutoAttack().
send "/combat on"
sleep 1

# Round 3 note: tried also matching T32's /pathfinding on + /hunt on before
# targeting -- that REGRESSED (auto-hunt's background FindNextHuntTarget()
# scan raced ahead of the sleep between /hunt on and /attack and left
# HasTarget() false, so /attack's own EnableAutoAttack() guard silently
# no-op'd -- confirmed in transcript_round3_unarmed_noattack.driver.txt: no
# AutoAttackChanged publish at all that round). Dropped /hunt entirely --
# not needed for a plain manual /target + /attack, and the real blocker
# (see below) turned out to be unrelated to hunting.

# 7. Manual /attack to start real auto-attack rounds against the target
#    (target is adjacent, distance=0.0, so no "too far away" refusal is
#    expected here -- T102's refusal was against a non-adjacent arena NPC).
#    This is the call that publishes AutoAttackChanged(enabled=true).
send "/attack"
wait_for_log "\[T104\] GameStateSyncBridge: AutoAttackChanged -> projection enabled=true" 15

# 8. Let combat run for a bit, watching for real melee damage.
sleep 5
# NOTE: `grep -c` prints "0" AND exits 1 on zero matches -- `|| echo 0` on
# top of that produces a two-line "0\n0" value, silently breaking the
# string-equality check below. Just take grep's own count, exit code
# ignored (bounded by `set -u`, not `-e`).
DAMAGE_SO_FAR=$(grep -ac "ZoneProcessDamage" "${LOG}" 2>/dev/null)
DAMAGE_SO_FAR="${DAMAGE_SO_FAR:-0}"
echo "[driver] ZoneProcessDamage count after plain /attack: ${DAMAGE_SO_FAR}" >> "${LOG}.driver"

# Round 3 finding: Testchar's DB inventory (character_id=65) has ZERO items
# in equippable slots 0-22 -- completely unarmed, only unequipped loot
# stacked in general slots 23-30 (leftover Scaled Curskins from a prior
# session's kills, never worn). Rounds 1-3 confirm zero ZoneProcessDamage
# packets ever arrive under a plain unarmed /attack against an adjacent,
# correctly-targeted, freshly-spawned mob -- an environmental gap (no
# weapon on file for this test character), not a defect in this unit or in
# CombatManager. Equipping a weapon from here would need a working
# /equip-from-cursor round trip, out of this unit's scope to build, and the
# protocol's DB-write allowance is explicitly position-only, not inventory.
#
# Substitute, still a REAL server round trip exercising the EXACT mechanism
# under test (an authoritative HP-changed packet for the current target,
# server-driven, not client-fabricated): the sanctioned GM command
# `#set hp <amount>` (akk-stack/code/zone/gm_commands/set/set_hp.cpp)
# targets whatever the GM's current target is, calls Mob::SetHP() +
# Mob::SendHPUpdate() -- the identical wire path (OP_HPUpdate ->
# ZoneProcessMobHealth -> EntityStatsChanged + the TargetChanged re-publish
# this unit added) that a real melee round's periodic HP tick would take.
# This is the plan's own correlation risk (does the projection's target HP
# keep updating from a genuine server-driven HP change for the CURRENT
# target), exercised honestly via a sanctioned GM command rather than
# fabricated client-side.
if [ "${DAMAGE_SO_FAR}" = "0" ]; then
    echo "[driver] no real melee damage observed -- falling back to sanctioned #set hp GM command to exercise the same HP-update wire path" >> "${LOG}.driver"
    send "/say #set hp 12"
    sleep 3
    send "/say #set hp 8"
    sleep 3
    send "/say #set hp 4"
    sleep 3
    send "/say #set hp 0"
    sleep 3
fi

# 9. Wait for the kill (real HP hitting 0 disables auto-attack) OR the last
#    of the explicit #set hp steps above to have landed.
wait_for_log "Target .*died|has been slain|AutoAttackChanged -> projection enabled=false|ZoneProcessDamage|hp=0%" 20

# 10. NOTE: there is no slash command that reaches EqActionHandler::
#     clearTarget() -- ActionDispatcher::clearTarget()'s only production
#     caller is InputActionBridge (a keybinding), which piped-stdin headless
#     cannot trigger, and ZoneProcessDeath() (eq.cpp) deliberately does NOT
#     call ClearTarget() on a kill (T32's finding, unchanged by this unit --
#     it only calls DisableAutoAttack()). This is a real, pre-existing
#     command-surface gap, independent of this unit's projection work;
#     flagged in the Review rather than worked around here. Retarget to
#     self instead, as a second live identity-change proof (still exercises
#     SetTarget()'s publish end to end for a different spawnId).
send "/target Testchar"
sleep 3

# 11. Final liveness probe -- proves the client never froze across the
#     whole sequence.
send "/who"
sleep 2

echo "[driver] sequence complete, sending /quit" >> "${LOG}.driver"
send "/quit"

# Give the process a bounded window to exit cleanly (T30).
waited=0
while kill -0 "${WPID}" 2>/dev/null && [ "${waited}" -lt 15 ]; do
    sleep 1
    waited=$((waited + 1))
done

if kill -0 "${WPID}" 2>/dev/null; then
    echo "[driver] willeq (pid ${WPID}) still alive after /quit + 15s -- leaving for the caller to inspect/kill by real PID" >> "${LOG}.driver"
else
    echo "[driver] willeq exited cleanly" >> "${LOG}.driver"
fi

exec 3>&-
echo "[driver] done. WPID=${WPID}" >> "${LOG}.driver"
echo "WPID=${WPID}"
