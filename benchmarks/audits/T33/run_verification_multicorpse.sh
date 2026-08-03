#!/bin/bash
# T33 multi-corpse check: the specific hazard scope item 3 exists for.
# Kills TWO lootable mobs in the same session so that when the second one
# dies, the first one's corpse is still present in the zone (EQ corpses
# persist for minutes) -- exactly the "some other corpse is lying around"
# scenario the plan calls the *normal* case, not an edge case. Confirms
# the second kill's AwaitingCorpse resolves to the second corpse's own
# spawn_id, not the first (stale) one, and that its own guaranteed item
# gets looted (not a re-loot of the first corpse).
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T33"
BIN="/mnt/projects/fable/wt/t1/build/bin/willeq"
CFG="${RUNDIR}/willeq_t33.json"
LOG="${RUNDIR}/transcript_multicorpse.txt"
FIFO="${RUNDIR}/cmds_multicorpse.fifo"

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

# wait_for_log <pattern> <max_seconds>: greps the WHOLE log each call, so
# only safe for patterns expected to occur exactly once in the run, or for
# a pattern already made specific enough (e.g. by embedding a spawn/corpse
# id) that "already matched earlier for a different id" cannot happen.
wait_for_log() {
    local pattern="$1" max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        if grep -aqE "${pattern}" "${LOG}" 2>/dev/null; then
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

# Waits for a CLIENT-side "New spawn: a_scaled_wolf_pup<N> (ID: <id>)" line
# whose id is NOT already in the space-separated $1 (ids to exclude --
# i.e. every id seen so far this run), then echoes "<name> <id>". Distinct
# from the server's GM-chat "Spawned a scaled wolf pup" broadcast, which
# (found live in this unit's first attempt) can arrive up to ~100ms before
# the client's own NewSpawn zone packet -- targeting off the chat broadcast
# raced the client's entity table and silently missed ("No target found
# matching"), letting auto-hunt's background scan grab an unrelated,
# already-present zone mob instead of the one we just spawned.
wait_for_fresh_client_spawn() {
    local exclude_ids=" $1 " max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        local line
        line=$(grep -aoE "New spawn: a_scaled_wolf_pup[0-9]+ \(ID: [0-9]+\)" "${LOG}" 2>/dev/null | tail -n5)
        if [ -n "${line}" ]; then
            while IFS= read -r l; do
                local name id
                name=$(echo "${l}" | sed -E 's/New spawn: (a_scaled_wolf_pup[0-9]+).*/\1/')
                id=$(echo "${l}" | sed -E 's/.*\(ID: ([0-9]+)\).*/\1/')
                if [[ "${exclude_ids}" != *" ${id} "* ]]; then
                    echo "${name} ${id}"
                    return 0
                fi
            done <<< "${line}"
        fi
        if ! kill -0 "${WPID}" 2>/dev/null; then
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

wait_for_log "Zone connection complete! Player position confirmed" 60
send "/combat on"
sleep 1
send "/pathfinding on"
sleep 1
send "/hunt on"
sleep 1

# --- Kill #1: establishes the first corpse, which will stay in the zone. ---
send "/say #npctypespawn 79037"
read -r NAME1 ID1 < <(wait_for_fresh_client_spawn "" 15)
echo "[driver] kill #1 target: '${NAME1}' id=${ID1}" >> "${LOG}.driver"
send "/target ${NAME1}"
send "/attack"
wait_for_log "Our combat target ${ID1} died, disabling auto-attack" 60
wait_for_log "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId=${ID1}" 20
sleep 1

# --- Kill #2: corpse #1 (id=$ID1) is still present and still
#     is_corpse=true (EQ corpses persist for minutes -- not despawned by
#     looting). This is the actual multi-corpse hazard: TickAwaitingCorpse
#     must resolve to the SECOND kill's own spawn_id, not fall back to the
#     first is_corpse entity it finds (the pre-T33 bug). ---
send "/say #npctypespawn 79037"
read -r NAME2 ID2 < <(wait_for_fresh_client_spawn "${ID1}" 15)
echo "[driver] kill #2 target: '${NAME2}' id=${ID2}" >> "${LOG}.driver"
send "/target ${NAME2}"
send "/attack"
wait_for_log "Our combat target ${ID2} died, disabling auto-attack" 60
wait_for_log "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId=${ID2}" 20
sleep 1

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
echo "[driver] kill1 id=${ID1} kill2 id=${ID2}" >> "${LOG}.driver"
echo "DONE exit=${EXITCODE}"
