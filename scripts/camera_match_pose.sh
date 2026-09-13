#!/usr/bin/env bash
# T310 Leg C -- pin a character at an EXACT world pose, deterministically.
#
# Used by BOTH sides of the match: scripts/camera_match_sweep.sh sources it for
# fable, and the official-client oracle capture calls it standalone, so the two
# clients are put at a byte-identical position/heading by the same mechanism.
#
# TWO TRAPS IT EXISTS FOR, both paid for in T310 Leg C recon runs:
#
#  1. LATE LINKDEAD SAVE. fable's /quit drops the socket; the zone declares the
#     client linkdead and writes its IN-MEMORY position back to character_data
#     seconds after the process has exited. A pose UPDATE issued right after a
#     run is silently overwritten.
#  2. THE ZONE RESUMES THE LINKDEAD CLIENT. On the next login the zone hands
#     back the cached entity and NEVER READS THE DB. Four consecutive runs at
#     headings 480/448/416/384 -- each verified in character_data -- all
#     rendered and reported headingEqUnits=480. That is a sweep that produces a
#     perfectly flat, perfectly meaningless curve while every log line agrees
#     with itself.
#
# So: bounce the zone process (a fresh one has no cached client and any late
# save has already landed), THEN write the pose, THEN READ IT BACK. Never
# assume; the whole unit depends on the two clients actually being where they
# are said to be.
#
# Usage: camera_match_pose.sh <char> <zone_id> <x> <y> <z> <heading> [zone_short]

set -uo pipefail
_CMP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${_CMP_DIR}/eqemu/env.sh"

cmp_reset_zone() {   # cmp_reset_zone <zone_short>
    local zs="$1" i
    echo "== bouncing zone ${zs} (clears the linkdead client cache)"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -TERM -f '/bin/zone( |\$)'" >/dev/null 2>&1
    sleep 4
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '/bin/zone( |\$)'" >/dev/null 2>&1
    sleep 2
    server_exec_bg "./bin/zone ${zs} > logs/zone_${zs}_sweep.log 2>&1"
    for i in $(seq 1 40); do
        if server_exec "grep -q 'Zone bootup type' logs/zone_${zs}_sweep.log" >/dev/null 2>&1; then
            echo "   zone up (${i}s)"; return 0
        fi
        sleep 1
    done
    echo "STOP: zone ${zs} did not boot" >&2
    return 1
}

cmp_apply_pose() {   # cmp_apply_pose <char> <zone_id> <x> <y> <z> <heading>
    local ch="$1" zid="$2" px="$3" py="$4" pz="$5" ph="$6"
    local i got want
    want="${zid},$(printf '%.3f' "$px"),$(printf '%.3f' "$py"),$(printf '%.3f' "$pz"),$(printf '%.3f' "$ph")"
    for i in $(seq 1 10); do
        mysql_exec "UPDATE character_data SET zone_id=${zid}, x=${px}, y=${py}, z=${pz}, heading=${ph} WHERE name='${ch}';" >/dev/null 2>&1
        sleep 5
        got=$(mysql_exec "SELECT CONCAT_WS(',',zone_id,ROUND(x,3),ROUND(y,3),ROUND(z,3),ROUND(heading,3)) FROM character_data WHERE name='${ch}';" 2>/dev/null | tail -1 | tr -d '\r')
        if [ "$got" = "$want" ]; then
            echo "== pose confirmed in DB: ${ch} -> ${got}"
            return 0
        fi
        echo "   pose not settled (db='${got}' want='${want}'), retrying"
    done
    echo "STOP: could not pin ${ch}'s pose in character_data" >&2
    return 1
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    [ $# -lt 6 ] && { echo "usage: $0 <char> <zone_id> <x> <y> <z> <heading> [zone_short]" >&2; exit 2; }
    cmp_reset_zone "${7:-qeynos2}" || exit 1
    cmp_apply_pose "$1" "$2" "$3" "$4" "$5" "$6" || exit 1
fi
