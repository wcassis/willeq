#!/usr/bin/env bash
# Three-state B1086 probe. Each oracle_session owns its lane lease, starts and
# stops its stack, and restores its database snapshot. The INI is separately
# restored byte-for-byte here because the official client edits it on login.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:?usage: luclin_ab.sh <evidence-directory>}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
export EQEMU_LANE=5
source "$REPO/scripts/eqemu/env.sh"
ORACLE="user@$EQEMU_ORACLE_HOST"
CLIENT=/home/user/eq-oracle/client
REMOTE=/home/user/eq-oracle/b1086-ab-$(date -u +%Y%m%dT%H%M%S)-$$
SCRIPT=/home/user/eq-oracle/set_facepick_luclin.py
ORIGINAL="$REMOTE/eqclient.ini.original"
RESTORE_ARMED=0
cleanup() {
    local rc=$?
    trap - EXIT
    if (( RESTORE_ARMED )); then
        ssh -o BatchMode=yes "$ORACLE" "cp '$ORIGINAL' '$CLIENT/eqclient.ini' && cmp '$ORIGINAL' '$CLIENT/eqclient.ini'" || rc=66
    fi
    printf 'cleanup exit=%s original=%s\n' "$rc" "$ORIGINAL" | tee -a "$OUT/ab.log"
    exit "$rc"
}
trap cleanup EXIT
if eqemu_lock_show >/dev/null 2>&1; then echo 'lane 5 lease held' >&2; exit 64; fi
ssh -o BatchMode=yes "$ORACLE" "mkdir -p '$REMOTE' && cp '$CLIENT/eqclient.ini' '$ORIGINAL'"
RESTORE_ARMED=1
scp -q "$REPO/refactor/B1086/set_facepick_luclin.py" "$ORACLE:$SCRIPT"
ssh -o BatchMode=yes "$ORACLE" "cmp '$ORIGINAL' '$CLIENT/eqclient.ini'"
for state in false_before true false_after; do
    if [[ -e $OUT/$state/session.started ]]; then
        [[ -f $OUT/$state/02_barbarian-male.png ]] || { echo "incomplete $state still" >&2; exit 65; }
        rg -q 'cleanup exit=[0-9]+ lane=5 lock=state=FREE' "$OUT/$state/driver.log" || { echo "incomplete $state cleanup" >&2; exit 66; }
        printf 'state=%s reused_verified_capture=1\n' "$state" | tee -a "$OUT/ab.log"
        continue
    fi
    ssh -o BatchMode=yes "$ORACLE" "cp '$ORIGINAL' '$CLIENT/eqclient.ini' && cmp '$ORIGINAL' '$CLIENT/eqclient.ini'"
    if [[ $state == true ]]; then
        ssh -o BatchMode=yes "$ORACLE" "python3 '$SCRIPT'"
    fi
    printf 'state=%s begin=%s\n' "$state" "$(date -u +%FT%TZ)" | tee -a "$OUT/ab.log"
    rc=0
    bash "$REPO/scripts/eqemu/oracle/oracle_session.sh" 5 "$REPO/refactor/B1086/luclin_ab_plan.tsv" "$OUT/$state" >"$OUT/$state.log" 2>&1 || rc=$?
    printf 'state=%s driver_exit=%s end=%s\n' "$state" "$rc" "$(date -u +%FT%TZ)" | tee -a "$OUT/ab.log"
    [[ $rc != 66 ]] || { echo 'lane cleanup failed; stop' >&2; exit 66; }
    [[ -f $OUT/$state/02_barbarian-male.png ]] || { echo "missing $state still" >&2; exit 65; }
    if eqemu_lock_show >/dev/null 2>&1 || eqemu_stack_ready; then
        echo "lane not restored after $state" >&2
        exit 66
    fi
done
ssh -o BatchMode=yes "$ORACLE" "cp '$ORIGINAL' '$CLIENT/eqclient.ini' && cmp '$ORIGINAL' '$CLIENT/eqclient.ini'"
printf 'lane_restored=1\n' | tee -a "$OUT/ab.log"
