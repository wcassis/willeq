#!/usr/bin/env bash
# Offline regression for lock contention and pre-existing create names.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/oracle-safety.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT
mkdir -p "$FIXTURE/scripts/eqemu/oracle"
cp "${ORACLE_SESSION_SOURCE:-$REPO/scripts/eqemu/oracle/oracle_session.sh}" "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh"
cat >"$FIXTURE/scripts/eqemu/env.sh" <<'EOF'
eqemu_lock_take() { [[ $PROBE_CASE != held ]]; }
# B1089 review: emit the lease's REAL key=value record. The old free-text
# "$OWNER held" stand-in never matched what eqemu_lock_show really returns
# (a multi-line card=/run=/purpose=/state= record, owner newline-terminated),
# which is exactly how finding 1 slipped past this net. The shape here is what
# the real eqemu_lock_show / eqemu_lease_show produces, so a format-dependent
# gate would have been asserted against the gate's real input.
eqemu_lock_show() {
    if [[ $PROBE_CASE == held ]]; then
        printf 'card=other-owner\nrun=other-owner\npurpose=other\nstate=HELD\n'
    elif [[ ! -e $OUT/released ]]; then
        printf 'card=%s\nrun=%s\npurpose=oracle_safety_probe\nstate=HELD\n' "$OWNER" "$OWNER"
    else
        return 1
    fi
}
eqemu_lock_release() { touch "$OUT/released"; }
# B1089: the driver now starts/stops the lease renewer around the capture;
# the offline fixture has no lease, so these are no-ops.
eqemu_lease_start_renewer() { :; }
eqemu_lease_stop_renewer() { :; }
eqemu_stack_ready() { [[ ! -e $OUT/down ]]; }
EOF
cat >"$FIXTURE/scripts/eqemu/restore_lib.sh" <<'EOF'
eqemu_snapshot() { printf 'snapshot\n' >"$1"; }
eqemu_inventory_snapshot() { printf 'inventory\n' >"$1"; }
eqemu_world_stop() { :; }
eqemu_restore_snapshot() { cp "$1" "$2"; }
eqemu_inventory_restore() { :; }
eqemu_sql() {
    printf '%s\n' "$1" >>"$OUT/sql.log"
    case "$1" in
        "SELECT id FROM character_data WHERE name IN"*) echo 1 ;;
        "SELECT account_id FROM character_data"*) echo 7 ;;
        "SELECT id FROM character_data WHERE name='Testchar';")
            if [[ $PROBE_CASE == query_failure ]]; then return 1; fi
            echo 1 ;;
        "SELECT id,name FROM character_data WHERE name='Testchar' AND account_id="*)
            printf '1\tTestchar\n' ;;
        "SELECT id,name FROM account"*) printf '7\ttest\n' ;;
        "SELECT id,account_name FROM login_accounts"*) printf '7\ttest\n' ;;
    esac
}
EOF
printf 'exit 0\n' >"$FIXTURE/scripts/eqemu/up.sh"
printf 'touch "$OUT/down"\n' >"$FIXTURE/scripts/eqemu/down.sh"
printf 'login\tTestchar\t0\tselect\ncreate\tTestchar\tHuman\tWarrior\tMale\texisting\n' >"$FIXTURE/plan.tsv"
for PROBE_CASE in ${PROBE_CASES:-held existing query_failure}; do
    export PROBE_CASE
    export ORACLE_OWNER=oracle_safety_probe
    OUT="$FIXTURE/$PROBE_CASE"; export OUT
    set +e
    bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 3 "$FIXTURE/plan.tsv" "$OUT" >"$FIXTURE/$PROBE_CASE.log" 2>&1
    rc=$?
    set -e
    if [[ $PROBE_CASE == held ]]; then
        [[ $rc == 64 ]] || { echo "held lock returned $rc, expected 64"; exit 1; }
        [[ ! -e $OUT/released && ! -e $OUT/down ]] || { echo 'other owner was disturbed'; exit 1; }
    else
        [[ $rc == 65 ]] || { echo "existing name returned $rc, expected 65"; exit 1; }
        [[ $(cat "$OUT/session.started") == oracle_safety_probe_* ]] || { echo 'owner prefix ignored'; exit 1; }
        ! grep -q 'DELETE FROM character_data' "$OUT/sql.log" || { echo 'existing character deleted'; exit 1; }
        [[ ! -s $OUT/absent-names.tsv ]] || { echo 'cleanup armed before validation'; exit 1; }
    fi
done
# B1057 (#377): lane 5 (the ThinkPad) is a valid oracle lane; 0 and 6 are not.
# A refused lane must stop at usage, before any output dir or lock.
export PROBE_CASE=held ORACLE_OWNER=oracle_safety_probe
for lane in 0 6 5; do
    OUT="$FIXTURE/lane$lane"; export OUT
    set +e
    bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" "$lane" "$FIXTURE/plan.tsv" "$OUT" >"$FIXTURE/lane$lane.log" 2>&1
    rc=$?
    set -e
    [[ $rc == 64 ]] || { echo "lane $lane returned $rc, expected 64"; exit 1; }
    if [[ $lane == 5 ]]; then
        grep -q 'lane lease held' "$FIXTURE/lane$lane.log" || { echo 'lane 5 refused before the lock'; exit 1; }
    else
        grep -q '^usage: ' "$FIXTURE/lane$lane.log" || { echo "lane $lane not refused at usage"; exit 1; }
        [[ ! -e $OUT ]] || { echo "lane $lane created its output dir"; exit 1; }
    fi
done
echo 'PASS: held lock untouched; existing and failed-query names never arm cleanup or DELETE; lane 5 accepted, lanes 0/6 refused'
