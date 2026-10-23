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
EQEMU_ORACLE_HOST=oracle.invalid
EOF
cat >"$FIXTURE/scripts/eqemu/restore_lib.sh" <<'EOF'
eqemu_snapshot() { printf 'snapshot\n' >"$1"; }
eqemu_inventory_snapshot() { printf 'inventory\n' >"$1"; }
eqemu_world_stop() { printf 'WORLD_STOP\n' >>"$OUT/sql.log"; }
eqemu_restore_snapshot() { cp "$1" "$2"; }
eqemu_inventory_restore() { :; }
eqemu_roster_zone_seed() { printf 'roster_zone_seed\n' >>"$OUT/sql.log"; }
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
        "SELECT class,level FROM character_data WHERE name='Testcaster'"*) printf '%s\t60\n' "$PROBE_CLASS" ;;
        "SELECT spell_id FROM character_spells"*) printf '93\n253\n' ;;
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
# B1129 (#449): a gem cast the login character's class cannot use is refused
# (exit 64) before Wine starts, naming the spell and the class; a castable one
# passes the check (and then stops at the stubbed ssh with 65). The
# provisioning hooks run with the lock held and the post-SQL with the world
# DOWN; the spellbook keeps only its before rows.
cp "$REPO/scripts/eqemu/oracle/oracle_plan_check.py" "$REPO/scripts/eqemu/oracle/gem_pitch.tsv" "$FIXTURE/scripts/eqemu/oracle/"
mkdir -p "$FIXTURE/bin" "$FIXTURE/eq"
printf '#!/bin/sh\ntouch "$OUT/ssh.called"; exit 1\n' >"$FIXTURE/bin/ssh"; cp "$FIXTURE/bin/ssh" "$FIXTURE/bin/scp"
chmod +x "$FIXTURE/bin/ssh" "$FIXTURE/bin/scp"
python3 - "$FIXTURE/eq/spells_us.txt" <<'PY'
import sys
f = ['0'] * 160; f[0], f[1] = '93', 'Burst of Flame'
f[104:120] = ['255'] * 16; f[109] = f[113] = f[116] = '1'   # DRU SHM MAG 1 (spells_us.txt row 93)
open(sys.argv[1], 'w').write('^'.join(f) + '\n')
PY
printf 'login\tTestcaster\t0\tselect\ncommand\t#memspell 93 0\t0\tm1\nclick\t25,243\t0\tgem1\n' >"$FIXTURE/gemplan.tsv"
for PROBE_CLASS in 12 6; do
    export PROBE_CASE=gem PROBE_CLASS EQ_CLIENT_PATH="$FIXTURE/eq" ORACLE_STATE_CHAR=67
    export ORACLE_PRE_SQL="PRE_HOOK_SQL;" ORACLE_POST_SQL="POST_HOOK_SQL;"
    OUT="$FIXTURE/gem$PROBE_CLASS"; export OUT
    set +e
    PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/gemplan.tsv" "$OUT" >"$FIXTURE/gem$PROBE_CLASS.log" 2>&1
    rc=$?
    set -e
    if [[ $PROBE_CLASS == 12 ]]; then
        [[ $rc == 64 ]] || { echo "druid-only gem cast on a wizard returned $rc, expected 64"; exit 1; }
        grep -q 'REFUSED.*93 "Burst of Flame".*WIZ (class 12)' "$FIXTURE/gem12.log" || { echo 'refusal does not name spell + class'; exit 1; }
        [[ ! -e $OUT/ssh.called ]] || { echo 'refused plan reached the oracle host'; exit 1; }
    else
        [[ $rc == 65 && -e $OUT/ssh.called ]] || { echo "castable plan returned $rc without reaching the oracle host"; exit 1; }
        grep -q '^OK .*DRU (class 6)' "$OUT/plan-check.txt" || { echo 'castable gem cast not checked OK'; exit 1; }
    fi
    grep -q '^PRE_HOOK_SQL;$' "$OUT/sql.log" || { echo 'pre-SQL did not run'; exit 1; }
    post=$(grep -n '^POST_HOOK_SQL;$' "$OUT/sql.log" | cut -d: -f1); stop=$(grep -n '^WORLD_STOP$' "$OUT/sql.log" | head -1 | cut -d: -f1)
    [[ -n $post && -n $stop && $stop -lt $post ]] || { echo 'post-SQL did not run with the world down'; exit 1; }
    grep -q 'DELETE FROM character_spells WHERE id=67 AND spell_id NOT IN (93,253);' "$OUT/sql.log" || { echo 'spellbook not restored'; exit 1; }
    [[ -e $OUT/released && -e $OUT/char-state-after.tsv ]] || { echo 'lock not released or no after-census'; exit 1; }
done
unset ORACLE_STATE_CHAR ORACLE_PRE_SQL ORACLE_POST_SQL
# B1129: a `set -u` abort (an unbound variable) must not exit 0 through the
# cleanup trap: unset the oracle host and expect a failure code.
sed -i 's/^EQEMU_ORACLE_HOST=.*$//' "$FIXTURE/scripts/eqemu/env.sh"
OUT="$FIXTURE/unbound"; export OUT PROBE_CLASS=6
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/gemplan.tsv" "$OUT" >"$FIXTURE/unbound.log" 2>&1
rc=$?
set -e
[[ $rc == 65 ]] || { echo "an unbound-variable abort returned $rc, expected 65"; exit 1; }
echo 'PASS: gem cast refused for a class that cannot use it (spell + class named), castable one passes; hooks run locked, post-SQL with the world down; an abort is never exit 0'
echo 'PASS: held lock untouched; existing and failed-query names never arm cleanup or DELETE; lane 5 accepted, lanes 0/6 refused'
