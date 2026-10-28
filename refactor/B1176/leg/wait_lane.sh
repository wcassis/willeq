#!/bin/bash
# B1176 live leg: wait until the lane-5 lease is free (sibling task holds it),
# then take our own lease. Polls eqemu_lock_show every 20 s, max 2 h.
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
for i in $(seq 1 360); do
    if [[ $(eqemu_lock_show 2>/dev/null | grep -c 'state=HELD') -eq 0 ]]; then
        echo "lock free at poll $i, taking"
        eqemu_lock_take kb-b1176-player-scale >>"$0.take.log" 2>&1
        RC=$?
        echo "take rc=$RC"
        eqemu_lock_show
        echo "rc=$RC" > /home/user/kb-evidence/kb-b1176-player-scale/lock.wait.exit
        exit $RC
    fi
    sleep 20
done
echo "lock still held after 2h" > /home/user/kb-evidence/kb-b1176-player-scale/lock.wait.exit
exit 64
