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
    # B1192: a faithful mini-mysql. Like the real eqemu_sql (restore_lib.sh,
    # `mysql -B -N -e`) it executes the '; '-separated statements in order
    # and STOPS at the first failure; only statements that actually ran are
    # logged -- the distinction legH3's post-SQL proved (the DELETEs before
    # its failing INSERT were real).
    local sql="$1" s old
    old=$IFS; IFS=';'
    local -a parts
    read -ra parts <<<"${sql//; /;}"
    IFS=$old
    for s in "${parts[@]}"; do
        [[ -n $s ]] || continue
        printf '%s\n' "$s" >>"$OUT/sql.log"
        case "$s" in
            "SELECT id FROM character_data WHERE name IN"*) echo 1 ;;
            "SELECT account_id FROM character_data"*)
                # B1192 early66: the login-character lookup returns nothing.
                [[ $PROBE_CASE == early66 ]] || echo 7 ;;
            "SELECT id FROM character_data WHERE name='Testchar'"*)
                if [[ $PROBE_CASE == query_failure ]]; then return 1; fi
                echo 1 ;;
            "SELECT id FROM character_data WHERE name='Testcaster'"*) echo 1 ;;
            "SELECT id,name FROM character_data WHERE name='Testchar' AND account_id="*)
                printf '1\tTestchar\n' ;;
            "SELECT id,name FROM account"*) printf '7\ttest\n' ;;
            "SELECT id,account_name FROM login_accounts"*) printf '7\ttest\n' ;;
            "SELECT class,level FROM character_data WHERE name='Testcaster'"*) printf '%s\t60\n' "$PROBE_CLASS" ;;
            "SELECT spell_id FROM character_spells"*) printf '93\n253\n' ;;
            # The class-leg's save-copy probe: absent in the early66 run
            # (the pre-SQL never ran), so it fails BEFORE the DELETE.
            "SELECT COUNT(*) FROM oracle_save_character_skills_67")
                if [[ $PROBE_CASE == early66 ]]; then return 1; fi
                echo 1 ;;
        esac
    done
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
    grep -q '^PRE_HOOK_SQL$' "$OUT/sql.log" || { echo 'pre-SQL did not run'; exit 1; }
    post=$(grep -n '^POST_HOOK_SQL$' "$OUT/sql.log" | cut -d: -f1); stop=$(grep -n '^WORLD_STOP$' "$OUT/sql.log" | head -1 | cut -d: -f1)
    [[ -n $post && -n $stop && $stop -lt $post ]] || { echo 'post-SQL did not run with the world down'; exit 1; }
    grep -q 'DELETE FROM character_spells WHERE id=67 AND spell_id NOT IN (93,253)' "$OUT/sql.log" || { echo 'spellbook not restored'; exit 1; }
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

# ---------------------------------------------------------------------------
# B1192 (#511, the legH3 leg): three nets for the failure class that left
# B1183's loot row unmeasured for a day:
#   1. early66    -- an orphaned post-SQL (no pre-SQL armed it, exactly
#                    legH3's scratch wrapper) must not turn a NAMED early
#                    exit into a 66, and its restore half must not delete
#                    live rows it cannot restore.
#   2. timeline0  -- a completed client step must leave its non-empty
#                    timeline.tsv (the measurement the poller's arm and the
#                    merge consume).
#   3. notimeline -- a remote rc 0 without a timeline.tsv is refused and
#                    named (65), never a silent pass.
# The working remote: a fake oracle host that produces what oracle_remote.sh
# produces (timeline + pcap), plus a stub oracle_report.py.
cat >"$FIXTURE/bin/ssh" <<'EOF'
#!/bin/bash
# $*: -n -o... user@oracle.invalid <command>; the last arg is the command.
cmd="${@: -1}"
case "$cmd" in
    "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh run "*)
        d="${cmd#*run }"; d="${d% *}"; d="${d//\'/}"
        local_dir="$REMOTE_ROOT/${d#/home/user/}"
        mkdir -p "$local_dir"
        if [[ ${PROBE_NO_TIMELINE:-0} != 1 ]]; then
            printf '0\tBEGIN\t1_login\n0\tEND\t1_login\n' >"$local_dir/timeline.tsv"
        fi
        head -c 64 /dev/zero >"$local_dir/capture.pcap"
        exit 0 ;;
    "mkdir -p "*)
        d=$(printf '%s' "$cmd" | tr -d "'" | awk '{print $NF}')
        [[ -n $d ]] && mkdir -p "$REMOTE_ROOT/${d#/home/user/}"
        exit 0 ;;
    *) exit 0 ;;
esac
EOF
cat >"$FIXTURE/bin/scp" <<'EOF'
#!/bin/bash
src="${@: -2:1}"
if [[ $src == user@oracle.invalid:* ]]; then
    p="${src#user@oracle.invalid:}"
    cp -r "$REMOTE_ROOT/${p#/home/user/}/." "${@: -1}/" 2>/dev/null || :
fi
exit 0
EOF
chmod +x "$FIXTURE/bin/ssh" "$FIXTURE/bin/scp"
printf 'import sys\nsys.exit(0)\n' >"$FIXTURE/scripts/eqemu/oracle/oracle_report.py"
export REMOTE_ROOT="$FIXTURE/remote"
mkdir -p "$REMOTE_ROOT"
printf 'login\tTestcaster\t0\tselect\nwait\t100\t0\tw1\n' >"$FIXTURE/logplan.tsv"
echo 'EQEMU_ORACLE_HOST=oracle.invalid' >>"$FIXTURE/scripts/eqemu/env.sh"

# 1. early66: legH3's shape -- no pre-SQL, an orphaned post-SQL (the
# class-leg's undo, save-table half included), and the login-character
# lookup returns nothing.
export PROBE_CASE=early66 ORACLE_OWNER=b1192_early66 PROBE_CLASS=12 PROBE_NO_TIMELINE=0
unset ORACLE_STATE_CHAR
export ORACLE_PRE_SQL=""
export ORACLE_POST_SQL="UPDATE character_data SET class=12 WHERE id=67; SELECT COUNT(*) FROM oracle_save_character_skills_67; DELETE FROM character_skills WHERE id=67;"
OUT="$FIXTURE/early66"; export OUT
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/gemplan.tsv" "$OUT" >"$FIXTURE/early66.log" 2>&1
rc=$?
set -e
[[ $rc == 65 ]] || { echo "early exit + orphaned post-SQL returned $rc, expected 65 (66 was legH3's misreport)"; cat "$OUT/driver.log" 2>/dev/null; exit 1; }
grep -q 'has no account_id' "$OUT/driver.log" || { echo 'early exit not named in driver.log'; cat "$OUT/driver.log"; exit 1; }
grep -q 'post-SQL failed on a leg that already failed' "$OUT/driver.log" || { echo 'orphaned post-SQL failure not named'; exit 1; }
grep -q '^SELECT COUNT(\*) FROM oracle_save_character_skills_67$' "$OUT/sql.log" || { echo 'save-table probe did not run'; cat "$OUT/sql.log"; exit 1; }
! grep -q 'DELETE FROM character_skills' "$OUT/sql.log" || { echo 'live rows deleted without a restore source'; exit 1; }
[[ ! -e $OUT/timeline.tsv && -e $OUT/released ]] || { echo 'client ran or lease not released'; exit 1; }
echo 'PASS: B1192 early66 -- a named early exit stays 65; the orphaned post-SQL cannot delete live rows'

# 2. timeline0: a completed client step leaves its non-empty timeline.
export PROBE_CASE=timeline ORACLE_OWNER=b1192_timeline ORACLE_STATE_CHAR=67
export ORACLE_PRE_SQL="PRE_B1192_SQL;" ORACLE_POST_SQL="POST_B1192_SQL;"
OUT="$FIXTURE/timeline0"; export OUT
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/logplan.tsv" "$OUT" >"$FIXTURE/timeline0.log" 2>&1
rc=$?
set -e
[[ $rc == 0 ]] || { echo "completed leg returned $rc, expected 0"; cat "$OUT/driver.log" "$FIXTURE/timeline0.log" 2>/dev/null; exit 1; }
[[ -s $OUT/timeline.tsv ]] || { echo 'no timeline.tsv from a completed client step'; exit 1; }
[[ -e $OUT/released ]] || { echo 'lease not released'; exit 1; }
echo 'PASS: B1192 timeline0 -- a completed client step leaves its timeline'

# 3. notimeline: a remote rc 0 without a timeline.tsv is a refused leg.
export PROBE_OWNER=b1192_notimeline ORACLE_OWNER=b1192_notimeline PROBE_NO_TIMELINE=1
OUT="$FIXTURE/notimeline"; export OUT
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/logplan.tsv" "$OUT" >"$FIXTURE/notimeline.log" 2>&1
rc=$?
set -e
[[ $rc == 65 ]] || { echo "rc-0 leg without a timeline returned $rc, expected 65"; cat "$OUT/driver.log" 2>/dev/null; exit 1; }
grep -q 'produced no timeline.tsv' "$OUT/driver.log" || { echo 'missing timeline not named'; cat "$OUT/driver.log"; exit 1; }
echo 'PASS: B1192 notimeline -- a remote rc 0 without timeline.tsv is refused, never a silent pass'

# 4. spaceplan: legH3's plan was SPACE-delimited; the login row must parse
# (tab-separated) or the leg is refused NAMED before the client step.
export PROBE_CASE=spaceplan ORACLE_OWNER=b1192_spaceplan PROBE_NO_TIMELINE=0
printf 'login Testcaster 0 char_select\n' >"$FIXTURE/spaceplan.tsv"
OUT="$FIXTURE/spaceplan"; export OUT
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/spaceplan.tsv" "$OUT" >"$FIXTURE/spaceplan.log" 2>&1
rc=$?
set -e
[[ $rc == 64 ]] || { echo "space-delimited plan returned $rc, expected 64"; cat "$OUT/driver.log" 2>/dev/null; exit 1; }
grep -q 'no parseable TAB-delimited login row' "$OUT/driver.log" || { echo 'unparseable plan not named'; cat "$OUT/driver.log"; exit 1; }
[[ ! -e $OUT/timeline.tsv ]] || { echo 'client step ran on an unparseable plan'; exit 1; }
echo 'PASS: B1192 spaceplan -- a space-delimited plan (legH3) is refused 64 and named, no client step'

# 5. obsnoobs: B1192 run 2 -- obs rows without a `# observer=` directive
# die silently at the first obs row in oracle_remote.sh; the session must
# refuse the shape BEFORE the client step.
export PROBE_CASE=obsnoobs ORACLE_OWNER=b1192_obsnoobs PROBE_NO_TIMELINE=0
printf 'login\tTestcaster\t0\tselect\nobs\tlogin\tOraclewatch\t0\tobs_select\n' >"$FIXTURE/obsnoobs.tsv"
OUT="$FIXTURE/obsnoobs"; export OUT
set +e
PATH="$FIXTURE/bin:$PATH" bash "$FIXTURE/scripts/eqemu/oracle/oracle_session.sh" 5 "$FIXTURE/obsnoobs.tsv" "$OUT" >"$FIXTURE/obsnoobs.log" 2>&1
rc=$?
set -e
[[ $rc == 64 ]] || { echo "obs-without-directive plan returned $rc, expected 64"; cat "$OUT/driver.log" 2>/dev/null; exit 1; }
grep -q "obs rows but no '# observer=" "$OUT/driver.log" || { echo 'missing observer directive not named'; cat "$OUT/driver.log"; exit 1; }
[[ ! -e $OUT/timeline.tsv ]] || { echo 'client step ran on an obs plan with no observer directive'; exit 1; }
echo 'PASS: B1192 obsnoobs -- obs rows without # observer= are refused 64 and named before the client step'
