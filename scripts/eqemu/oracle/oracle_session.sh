#!/usr/bin/env bash
# One official-client capture, with all server state returned to its starting point.
set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
if [[ ! -f $REPO/scripts/eqemu/env.sh && -f $SCRIPT_DIR/.oracle-repo ]]; then
    REPO=$(cat "$SCRIPT_DIR/.oracle-repo")
fi
[[ -f $REPO/scripts/eqemu/env.sh ]] || { echo "oracle repo not found; rerun install.sh" >&2; exit 65; }
if [[ $# != 3 || ! $1 =~ ^[1-5]$ ]]; then
    echo "usage: $0 <lane 1..5> <plan.tsv> <out_dir>" >&2
    exit 64
fi
LANE=$1 PLAN=$(realpath "$2") OUT=$(realpath -m "$3")
mkdir -p "$OUT"
if [[ -e $OUT/session.started ]]; then echo "output already used: $OUT" >&2; exit 64; fi
export EQEMU_LANE=$LANE
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
OWNER="${ORACLE_OWNER:-oracle_session}_$(date -u +%Y%m%dT%H%M%S)_$$"
[[ $OWNER =~ ^[A-Za-z0-9_-]+$ ]] || { echo "invalid ORACLE_OWNER" >&2; exit 64; }
STATUS=0 LOCKED=0 SNAPSHOT=0 SERVER_DATA=0 REMOTE_STARTED=0 CREATE_ACCOUNT_ID= BROKER_PID= STATE_CHAR= OBS_ID= VITALS_IDS= PROVISION= PROV_ACC= PROV_LOGIN_ID= PRE_SQL_RAN=0
REMOTE_DIR="/home/user/eq-oracle/sessions/$OWNER"
say() { printf '%s\n' "$*" | tee -a "$OUT/driver.log"; }
remote() { ssh -n -o BatchMode=yes -o ConnectTimeout=10 "user@$EQEMU_ORACLE_HOST" "$@"; }
stop_broker() {
    if [[ -n $BROKER_PID ]]; then
        kill "$BROKER_PID" 2>/dev/null || :
        wait "$BROKER_PID" 2>/dev/null || :
        BROKER_PID=
    fi
}
# B1129: one character's provisioning state (class/level/race, memorised
# gems, spellbook ids, buff count) to $OUT/char-*-<before|after>.
char_state() {
    eqemu_sql "SELECT id,class,level,race FROM character_data WHERE id=$1;" >"$OUT/char-state-$2.tsv" &&
    eqemu_sql "SELECT slot_id,spell_id FROM character_memmed_spells WHERE id=$1 ORDER BY slot_id;" >"$OUT/char-memmed-$2.tsv" &&
    eqemu_sql "SELECT spell_id FROM character_spells WHERE id=$1 ORDER BY spell_id;" >"$OUT/char-book-$2.txt" &&
    eqemu_sql "SELECT COUNT(*) FROM character_buffs WHERE character_id=$1;" >"$OUT/char-buffs-$2.txt"
}
cleanup() {
    # B1129: a `set -u` abort (unbound variable) reaches this trap with $? != 0
    # but STATUS still 0; it used to exit 0. Every deliberate exit sets STATUS.
    local aborted=$?
    if (( STATUS == 0 && aborted != 0 )); then STATUS=65; fi
    local prior=$STATUS
    stop_broker
    if (( REMOTE_STARTED )); then
        remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh stop '$REMOTE_DIR'" >>"$OUT/driver.log" 2>&1 || :
        scp -q -r "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/." "$OUT/" 2>>"$OUT/driver.log" || STATUS=65
    fi
    if (( LOCKED )); then
        if (( SNAPSHOT )); then
            # Only remove a plan name that was absent before this locked run,
            # exists now with an id, and belongs to the login character's account.
            : >"$OUT/created-ids.tsv"
            while IFS= read -r name; do
                [[ -n $name ]] || continue
                eqemu_sql "SELECT id,name FROM character_data WHERE name='$name' AND account_id=$CREATE_ACCOUNT_ID;" >>"$OUT/created-ids.tsv" || STATUS=66
            done <"$OUT/absent-names.tsv"
            if [[ -s $OUT/created-ids.tsv ]]; then
                eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
                while IFS=$'\t' read -r id name; do
                    [[ $id =~ ^[0-9]+$ ]] || { STATUS=66; continue; }
                    eqemu_sql "DELETE FROM character_data WHERE id=$id AND name='$name' AND account_id=$CREATE_ACCOUNT_ID;" >/dev/null || STATUS=66
                done <"$OUT/created-ids.tsv"
            fi
            if [[ -n $PROVISION ]]; then
                # The provisioned character's rows are the only new ones.
                PROV_ID=$(eqemu_sql "SELECT id FROM character_data WHERE name='$PROVISION';")
                PROV_ACC=$(eqemu_sql "SELECT account_id FROM character_data WHERE name='$PROVISION';")
                if [[ $PROV_ID =~ ^[0-9]+$ && $PROV_ACC =~ ^[0-9]+$ ]]; then
                    PROV_ACC_NAME=$(eqemu_sql "SELECT name FROM account WHERE id=$PROV_ACC;")
                    PROV_LOGIN_ID=$(eqemu_sql "SELECT id FROM login_accounts WHERE account_name='$PROV_ACC_NAME';")
                    printf 'character\t%s\t%s\naccount\t%s\t%s\nlogin_account\t%s\t%s\n' "$PROV_ID" "$PROVISION" \
                        "$PROV_ACC" "$PROV_ACC_NAME" "$PROV_LOGIN_ID" "$PROV_ACC_NAME" >"$OUT/provisioned.tsv"
                    say "provisioned: character $PROV_ID $PROVISION on account $PROV_ACC $PROV_ACC_NAME (login $PROV_LOGIN_ID)"
                else
                    say "provision=$PROVISION: no character was created"; STATUS=66
                fi
                eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
                eqemu_sql "$(eqemu_restore_sql_from "$OUT/db-before.tsv")" >/dev/null || STATUS=66
                eqemu_snapshot "$OUT/db-after-raw.tsv" || STATUS=66
                awk -F'\t' -v id="${PROV_ID:-x}" '$1 != id' "$OUT/db-after-raw.tsv" >"$OUT/db-after.tsv"
                diff -q "$OUT/db-before.tsv" "$OUT/db-after.tsv" >>"$OUT/driver.log" 2>&1 || { say "SNAPSHOT MISMATCH (provision)"; STATUS=66; }
            else
                eqemu_restore_snapshot "$OUT/db-before.tsv" "$OUT/db-after.tsv" say >>"$OUT/driver.log" 2>&1 || STATUS=66
            fi
            # B1129 (#449): the provisioning hooks' undo, with the WORLD DOWN
            # (the zone writes the character back at shutdown, so an earlier
            # undo is overwritten). The snapshot covers level, not class,
            # memorised gems, buffs or the spellbook: ORACLE_POST_SQL puts
            # those back (oracle_class_leg.sh builds it), then any spellbook
            # row the leg added (a #memspell scribes) is removed.
            if [[ -n ${ORACLE_POST_SQL:-} || $STATE_CHAR != "" ]]; then
                eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
            fi
            if [[ -n ${ORACLE_POST_SQL:-} ]]; then
                say "post-SQL (world down): $ORACLE_POST_SQL"
                eqemu_sql "$ORACLE_POST_SQL" >>"$OUT/driver.log" 2>&1 || {
                    STATUS=66
                    if [[ $prior != 0 && $prior != 66 ]]; then
                        # B1192 (legH3): the post-SQL restores what the pre-SQL
                        # armed (the oracle_save_* tables above all); when the
                        # leg died before the client step, that state never
                        # existed and this failure is a CONSEQUENCE of the
                        # early exit, not a restore failure of a finished leg.
                        # It is named here and the early-exit code wins below.
                        say "post-SQL failed on a leg that already failed (early exit $prior); the state it restores may never have existed -- the early-exit cause is logged above"
                    fi
                }
            fi
            if [[ -n $STATE_CHAR ]]; then
                book=$(paste -sd, - <"$OUT/char-book-before.txt")
                if [[ -n $book ]]; then
                    eqemu_sql "DELETE FROM character_spells WHERE id=$STATE_CHAR AND spell_id NOT IN ($book);" >>"$OUT/driver.log" 2>&1 || STATUS=66
                fi
                char_state "$STATE_CHAR" after || STATUS=66
            fi
            # B1134 (#454): HP/mana/endurance and the PvP flag of the login
            # character and the observer (an observer leg may nuke the observer
            # or flag both PvP), then the observer's buffs (none before: the
            # session refused otherwise) and its after-census.
            if [[ -n $VITALS_IDS ]]; then
                eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
                vsql=$(awk -F'\t' 'NF==5 && $1 ~ /^[0-9]+$/ {printf "UPDATE character_data SET cur_hp=%s,mana=%s,endurance=%s,pvp_status=%s WHERE id=%s;", $2,$3,$4,$5,$1}' "$OUT/vitals-before.tsv")
                [[ -z $vsql ]] || eqemu_sql "$vsql" >>"$OUT/driver.log" 2>&1 || STATUS=66
                eqemu_sql "SELECT id,cur_hp,mana,endurance,pvp_status FROM character_data WHERE id IN ($VITALS_IDS) ORDER BY id;" >"$OUT/vitals-after.tsv" || STATUS=66
                diff -q "$OUT/vitals-before.tsv" "$OUT/vitals-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
            fi
            if [[ -n $OBS_ID ]]; then
                eqemu_sql "DELETE FROM character_buffs WHERE character_id=$OBS_ID;" >>"$OUT/driver.log" 2>&1 || STATUS=66
                char_state "$OBS_ID" observer-after || STATUS=66
                for f in state memmed book buffs; do
                    ext=tsv; [[ $f == book || $f == buffs ]] && ext=txt
                    cmp -s "$OUT/char-$f-observer-before.$ext" "$OUT/char-$f-observer-after.$ext" || { say "observer $OBS_ID $f changed"; STATUS=66; }
                done
            fi
            eqemu_inventory_restore "$OUT/inventory-before.tsv" "$CHAR_IDS" >>"$OUT/driver.log" 2>&1 || STATUS=66
            eqemu_inventory_snapshot "$OUT/inventory-after.tsv" "$CHAR_IDS" >>"$OUT/driver.log" 2>&1 || STATUS=66
            diff -q "$OUT/inventory-before.tsv" "$OUT/inventory-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
            # A mistyped Wine login can auto-create account rows. Remove only
            # new, unowned test accounts, then require the complete censuses to
            # round-trip; never delete a row present before this session.
            account_ids=$(cut -f1 "$OUT/accounts-before.tsv" | paste -sd, -)
            login_ids=$(cut -f1 "$OUT/login-accounts-before.tsv" | paste -sd, -)
            eqemu_sql "DELETE a FROM account a LEFT JOIN character_data c ON c.account_id=a.id WHERE a.id NOT IN ($account_ids) AND a.name LIKE 'willeq_test%' AND c.id IS NULL;" >/dev/null || STATUS=66
            eqemu_sql "DELETE FROM login_accounts WHERE id NOT IN ($login_ids) AND account_name LIKE 'willeq_test%';" >/dev/null || STATUS=66
            eqemu_sql "SELECT id,name FROM account ORDER BY id;" >"$OUT/accounts-after.tsv" || STATUS=66
            eqemu_sql "SELECT id,account_name FROM login_accounts ORDER BY id;" >"$OUT/login-accounts-after.tsv" || STATUS=66
            if [[ -n $PROVISION && -s $OUT/provisioned.tsv ]]; then
                # the provisioned account + login rows are the only new rows
                mv "$OUT/accounts-after.tsv" "$OUT/accounts-after-raw.tsv"
                mv "$OUT/login-accounts-after.tsv" "$OUT/login-accounts-after-raw.tsv"
                awk -F'\t' -v id="$PROV_ACC" '$1 != id' "$OUT/accounts-after-raw.tsv" >"$OUT/accounts-after.tsv"
                awk -F'\t' -v id="$PROV_LOGIN_ID" '$1 != id' "$OUT/login-accounts-after-raw.tsv" >"$OUT/login-accounts-after.tsv"
            fi
            diff -q "$OUT/accounts-before.tsv" "$OUT/accounts-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
            diff -q "$OUT/login-accounts-before.tsv" "$OUT/login-accounts-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
        fi
        # B1174 (#493): the server-data undo runs on EVERY exit path once the
        # change was armed (a failed login or capture included), with the
        # world down, then the dump must equal the one taken before the change.
        if (( SERVER_DATA )); then
            eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
            say "server-data undo (world down): $ORACLE_SERVER_UNDO_SQL"
            eqemu_sql "$ORACLE_SERVER_UNDO_SQL" >>"$OUT/driver.log" 2>&1 || STATUS=66
            eqemu_sql "$ORACLE_SERVER_DUMP_SQL" >"$OUT/server-data-after.tsv" 2>>"$OUT/driver.log" || STATUS=66
            if cmp -s "$OUT/server-data-before.tsv" "$OUT/server-data-after.tsv"; then
                say "server data restored BYTE-IDENTICALLY ($(wc -l <"$OUT/server-data-after.tsv") dump lines)"
            else
                say "SERVER DATA MISMATCH: server-data-before.tsv vs server-data-after.tsv"; STATUS=66
            fi
        fi
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/driver.log" 2>&1 || STATUS=66
        eqemu_lease_stop_renewer
        # B1089 review (finding 1): eqemu_lock_release is already owner-checked
        # by the lease's identity (card/run/pid; a takeover is refused with
        # 63 -> STATUS=66), so the free-text gate that compared $OWNER to a
        # space-terminated prefix of eqemu_lock_show was both broken (the
        # lease's key=value record never matched) and redundant. A genuine
        # takeover still surfaces through the release's own refusal.
        eqemu_lock_release || STATUS=66
        if eqemu_lock_show >/dev/null 2>&1 || eqemu_stack_ready; then STATUS=66; fi
    fi
    # B1192 (legH3): an early exit (the client step never completed) stays the
    # leg's reported code -- a cleanup failure CAUSED by the early exit (the
    # orphaned post-SQL over save tables the pre-SQL never created) must not
    # masquerade as the primary failure with 66. A cleanup failure on a leg
    # that reached its client step (prior 0) stays 66.
    if (( prior != 0 && prior != 66 && STATUS == 66 )); then STATUS=$prior; fi
    if (( prior == 67 && STATUS != 66 )); then STATUS=67; fi
    say "cleanup exit=$STATUS lane=$LANE lock=$(eqemu_lock_show 2>/dev/null || echo free)"
    trap - EXIT
    exit "$STATUS"
}
trap 'cleanup' EXIT
trap 'STATUS=65; exit 65' INT TERM
if ! eqemu_lock_take "$OWNER"; then say "lane lease held: $(eqemu_lock_show || :)"; STATUS=64; exit "$STATUS"; fi
LOCKED=1
# B1089: a capture can run 20+ min; the renewer keeps the lease live while
# the oracle leg runs, and dies with this script (never after it).
eqemu_lease_start_renewer
printf '%s\n' "$OWNER" >"$OUT/session.started"
# B1174 (#493): a leg-only change to SERVER data (e.g. one spells_new row)
# that the zone reads only when it boots (spells live in shared memory, which
# up.sh's bin/shared_memory and the container's launcher build at start). So
# it runs with the stack DOWN, before up.sh, with only mariadb started:
# ORACLE_SERVER_DUMP_SQL is dumped before (server-data-before.tsv) and after
# the change (server-data-leg.tsv), ORACLE_SERVER_SQL makes it, and the
# cleanup above runs ORACLE_SERVER_UNDO_SQL with the world down and requires
# the dump to match server-data-before.tsv byte for byte. All three or none.
if [[ -n ${ORACLE_SERVER_SQL:-}${ORACLE_SERVER_UNDO_SQL:-}${ORACLE_SERVER_DUMP_SQL:-} ]]; then
    [[ -n ${ORACLE_SERVER_SQL:-} && -n ${ORACLE_SERVER_UNDO_SQL:-} && -n ${ORACLE_SERVER_DUMP_SQL:-} ]] ||
        { say "server-data hook needs ORACLE_SERVER_SQL, _UNDO_SQL and _DUMP_SQL"; STATUS=64; exit "$STATUS"; }
    bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/driver.log" 2>&1 || { STATUS=65; exit "$STATUS"; }
    compose up -d mariadb >>"$OUT/driver.log" 2>&1 || { STATUS=65; exit "$STATUS"; }
    for _ in $(seq 1 60); do eqemu_sql "SELECT 1;" >/dev/null 2>&1 && break; sleep 1; done
    eqemu_sql "$ORACLE_SERVER_DUMP_SQL" >"$OUT/server-data-before.tsv" 2>>"$OUT/driver.log" || { STATUS=65; exit "$STATUS"; }
    [[ -s $OUT/server-data-before.tsv ]] || { say "server-data dump is empty; refusing"; STATUS=65; exit "$STATUS"; }
    SERVER_DATA=1
    say "server-data SQL (stack down): $ORACLE_SERVER_SQL"
    eqemu_sql "$ORACLE_SERVER_SQL" >>"$OUT/driver.log" 2>&1 || { STATUS=65; exit "$STATUS"; }
    eqemu_sql "$ORACLE_SERVER_DUMP_SQL" >"$OUT/server-data-leg.tsv" 2>>"$OUT/driver.log" || { STATUS=65; exit "$STATUS"; }
fi
if ! bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/driver.log" 2>&1 || ! eqemu_stack_ready; then STATUS=65; exit "$STATUS"; fi
# B1047 (#367): keep the roster out of timorous, BEFORE the snapshot so the
# restore keeps it (scripts/eqemu/roster_zone_seed.sql); a plan's zone row
# below still seeds any zone per run.
eqemu_roster_zone_seed >>"$OUT/driver.log" 2>&1 || { STATUS=65; exit "$STATUS"; }
eqemu_snapshot "$OUT/db-before.tsv" || { STATUS=65; exit "$STATUS"; }
CHAR_IDS=$(eqemu_sql "SELECT id FROM character_data WHERE name IN ('Testcaster','Testchar','Testbuddy') ORDER BY id;" | paste -sd, -)
[[ -n $CHAR_IDS ]] || { STATUS=65; exit "$STATUS"; }
# B1134 (#454): `# observer=<Character>` = a second official client logged in
# as another PLAYER character (docs/oracle_driver.md "The observer"). Its
# inventory joins the snapshot; its zone and `# observer-pose=` are seeded
# below like the login character's and come back with the snapshot restore.
# `# provision=<Observer>` is the one-time leg that MAKES the observer: its
# account does not exist yet (the loginserver auto-creates it at the first
# login, oracle_observer_setup.sh wrote its credentials on the oracle host)
# and an `obs create <Observer> ...` row creates the character. That account,
# its login row and that character are the only rows the restore and the
# census checks let through, and they are kept (provisioned.tsv).
OBSERVER=$(awk '/^# observer=/ {sub(/^# observer=/, ""); print; exit}' "$PLAN")
PROVISION=$(awk '/^# provision=/ {sub(/^# provision=/, ""); print; exit}' "$PLAN")
if [[ -n $OBSERVER ]]; then
    [[ $OBSERVER =~ ^[A-Za-z]+$ ]] || { STATUS=64; exit "$STATUS"; }
    OBS_ID=$(eqemu_sql "SELECT id FROM character_data WHERE name='$OBSERVER';")
    if [[ -n $PROVISION ]]; then
        [[ $PROVISION == "$OBSERVER" && -z $OBS_ID ]] || { say "provision=$PROVISION: needs observer=$PROVISION and no such character yet"; STATUS=64; exit "$STATUS"; }
        awk -F'\t' -v n="$PROVISION" '$1=="obs" && $2=="create" && $3==n {f=1} END {exit !f}' "$PLAN" || { STATUS=64; exit "$STATUS"; }
    else
        [[ $OBS_ID =~ ^[0-9]+$ ]] || { say "observer $OBSERVER not found"; STATUS=65; exit "$STATUS"; }
        [[ ,$CHAR_IDS, == *,$OBS_ID,* ]] || CHAR_IDS+=",$OBS_ID"
    fi
elif [[ -n $PROVISION ]]; then
    STATUS=64; exit "$STATUS"
fi
eqemu_inventory_snapshot "$OUT/inventory-before.tsv" "$CHAR_IDS" || { STATUS=65; exit "$STATUS"; }
eqemu_sql "SELECT id,name FROM account ORDER BY id;" >"$OUT/accounts-before.tsv" || { STATUS=65; exit "$STATUS"; }
eqemu_sql "SELECT id,account_name FROM login_accounts ORDER BY id;" >"$OUT/login-accounts-before.tsv" || { STATUS=65; exit "$STATUS"; }
SNAPSHOT=1
# A plan's zone row is an explicit pre-login seed, never a persistent change.
ZONE=$(awk -F'\t' '$1=="zone" {print $2; exit}' "$PLAN")
CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$PLAN")
# B1192 (legH3): the plan is TAB-delimited (action/target/hold_ms/still_name).
# B1183's legH_loot.tsv was hand-written SPACE-delimited: this awk found no
# login row, CHARACTER was empty, and the leg died at the account_id check --
# silently, pre-fix -- so the reported failure was the orphaned post-SQL's 66.
# Name the empty/illegal name here, at the only place it is detectable.
[[ $CHARACTER =~ ^[A-Za-z]+$ ]] || { say "plan has no parseable TAB-delimited login row (got '$CHARACTER'); the plan columns are action/target/hold_ms/still_name, tab-separated (legH3's plan was space-delimited)"; STATUS=64; exit "$STATUS"; }
# B1192 (run 2): a plan with `obs` rows but no `# observer=<Character>`
# directive passes every session-side check and then dies at the FIRST obs
# row in oracle_remote.sh's `[[ -n $OBSERVER ]] || exit 65` -- silently,
# with an empty remote.log and a timeline stuck on the last caster step
# (B1183's legH plan had exactly this shape). Name it here, where both
# facts (the obs rows and the missing directive) are visible.
if grep -qE $'^obs\t' "$PLAN" && [[ -z ${OBSERVER:-} ]]; then
    say "the plan has obs rows but no '# observer=<Character>' directive (obs rows also need '# obs-ini-set=Defaults.LastCharSel=N' on a two-character account); the remote step would die at the first obs row with no log; see refactor/B1176/leg/o_human.tsv:32-38 for the canonical form"; STATUS=64; exit "$STATUS"; fi
awk -F'\t' '$1=="create" {print $2}' "$PLAN" >"$OUT/plan-names.tsv"
: >"$OUT/absent-names.tsv"
: >"$OUT/validated-names.tsv"
CREATE_ACCOUNT_ID=$(eqemu_sql "SELECT account_id FROM character_data WHERE name='$CHARACTER' LIMIT 1;")
# B1192 (legH3): these exits used to be SILENT -- no log line, no file, so
# the leg died in this stretch (between the snapshot and the first remote
# artifact) invisibly: legH3's driver.log has no pre-SQL line, no remote
# artifacts, and the only failure recorded is the post-SQL 66 from cleanup.
# Every early exit below the snapshot now names itself in driver.log.
[[ $CREATE_ACCOUNT_ID =~ ^[0-9]+$ ]] || { say "login character '$CHARACTER' has no account_id row (or the lookup failed); refusing before the client starts"; STATUS=65; exit "$STATUS"; }
while IFS= read -r name; do
    [[ $name =~ ^[A-Za-z]{3,15}$ ]] || { say "plan create name '$name' is not a legal character name; refusing"; STATUS=65; exit "$STATUS"; }
    if ! existing_id=$(eqemu_sql "SELECT id FROM character_data WHERE name='$name';"); then
        say "name lookup failed for $name; refusing to arm cleanup"
        STATUS=65; exit "$STATUS"
    fi
    [[ -z $existing_id ]] || { STATUS=65; exit "$STATUS"; }
    if grep -Fxq "$name" "$OUT/validated-names.tsv"; then STATUS=65; exit "$STATUS"; fi
    printf '%s\n' "$name" >>"$OUT/validated-names.tsv"
done <"$OUT/plan-names.tsv"
cp "$OUT/validated-names.tsv" "$OUT/absent-names.tsv"
# B1129 (#449): provisioning hooks, run with the lock held and the snapshot
# taken. ORACLE_STATE_CHAR=<id> records that character's class, gems,
# spellbook and buffs before ORACLE_PRE_SQL (e.g. a class switch) and again
# after the world-down undo; oracle_class_leg.sh sets all four variables.
if [[ -n ${ORACLE_STATE_CHAR:-} ]]; then
    [[ $ORACLE_STATE_CHAR =~ ^[0-9]+$ ]] || { say "ORACLE_STATE_CHAR '$ORACLE_STATE_CHAR' is not a character id; refusing"; STATUS=64; exit "$STATUS"; }
    char_state "$ORACLE_STATE_CHAR" before || { say "char_state before-census failed for char $ORACLE_STATE_CHAR"; STATUS=65; exit "$STATUS"; }
    STATE_CHAR=$ORACLE_STATE_CHAR
fi
if [[ -n $OBS_ID ]]; then
    char_state "$OBS_ID" observer-before || { say "observer before-census failed for char $OBS_ID"; STATUS=65; exit "$STATUS"; }
    [[ $(cat "$OUT/char-buffs-observer-before.txt") == 0 ]] || { say "observer $OBSERVER has buffs; refusing"; STATUS=65; exit "$STATUS"; }
    LOGIN_ID=$(eqemu_sql "SELECT id FROM character_data WHERE name='$CHARACTER';")
    [[ $LOGIN_ID =~ ^[0-9]+$ && $LOGIN_ID != "$OBS_ID" ]] || { say "login character '$CHARACTER' has no usable id (got '$LOGIN_ID', observer is $OBS_ID)"; STATUS=65; exit "$STATUS"; }
    VITALS_IDS="$LOGIN_ID,$OBS_ID"
    eqemu_sql "SELECT id,cur_hp,mana,endurance,pvp_status FROM character_data WHERE id IN ($VITALS_IDS) ORDER BY id;" >"$OUT/vitals-before.tsv" || { say "vitals before-census failed for $VITALS_IDS"; STATUS=65; exit "$STATUS"; }
fi
if [[ -n ${ORACLE_PRE_SQL:-} ]]; then
    say "pre-SQL: $ORACLE_PRE_SQL"
    # B1192: the pre-SQL is armed NOW (before it runs): the cleanup's
    # post-SQL/after-census decisions key off this flag, and the save tables
    # the post-SQL's restore half references only exist once the pre-SQL has
    # started (oracle_class_leg.sh builds both halves).
    PRE_SQL_RAN=1
    eqemu_sql "$ORACLE_PRE_SQL" >>"$OUT/driver.log" 2>&1 || { say "pre-SQL failed (see driver.log above)"; STATUS=65; exit "$STATUS"; }
fi
# B1129 (#449): the official client refuses a gem cast its class cannot use
# ("You are not high enough level to cast this spell!", no OP_CastSpell);
# check every gem cast against the login character's REAL class, level and
# gems now, before Wine starts (oracle_plan_check.py; exit 64 = refused).
if [[ -n $CHARACTER ]]; then
    [[ $CHARACTER =~ ^[A-Za-z]+$ ]] || { say "plan login name '$CHARACTER' is not a legal character name; refusing"; STATUS=65; exit "$STATUS"; }
    eqemu_sql "SELECT class,level FROM character_data WHERE name='$CHARACTER' LIMIT 1;" >"$OUT/plan-character.tsv" || { say "plan-character lookup failed for '$CHARACTER'"; STATUS=65; exit "$STATUS"; }
    eqemu_sql "SELECT m.slot_id,m.spell_id FROM character_memmed_spells m JOIN character_data c ON c.id=m.id WHERE c.name='$CHARACTER' ORDER BY m.slot_id;" >"$OUT/plan-memmed.tsv" || { say "plan-memmed lookup failed for '$CHARACTER'"; STATUS=65; exit "$STATUS"; }
    read -r CHAR_CLASS CHAR_LEVEL <"$OUT/plan-character.tsv"
    [[ $CHAR_CLASS =~ ^[0-9]+$ && $CHAR_LEVEL =~ ^[0-9]+$ ]] || { say "character '$CHARACTER' class/level unreadable ('$CHAR_CLASS' '$CHAR_LEVEL')"; STATUS=65; exit "$STATUS"; }
    if [[ -n ${ORACLE_EXPECT_CLASS:-} && $CHAR_CLASS != "$ORACLE_EXPECT_CLASS" ]]; then
        say "$CHARACTER is class $CHAR_CLASS after the pre-SQL, expected $ORACLE_EXPECT_CLASS; refusing"
        STATUS=65; exit "$STATUS"
    fi
    python3 "$REPO/scripts/eqemu/oracle/oracle_plan_check.py" check "$PLAN" --class "$CHAR_CLASS" \
        --level "$CHAR_LEVEL" --lane "$LANE" --memmed-file "$OUT/plan-memmed.tsv" >"$OUT/plan-check.txt" 2>&1
    PLAN_RC=$?
    cat "$OUT/plan-check.txt" >>"$OUT/driver.log"
    if (( PLAN_RC != 0 )); then
        say "plan refused (gem casts the official client would refuse; $OUT/plan-check.txt):"
        grep -E '^REFUSED' "$OUT/plan-check.txt" | tee -a "$OUT/driver.log" >&2
        STATUS=64; exit "$STATUS"
    fi
fi
if [[ -n $ZONE && -n $CHARACTER ]]; then
    [[ $ZONE =~ ^[a-z0-9_]+$ && $CHARACTER =~ ^[A-Za-z]+$ ]] || { say "plan zone '$ZONE' / character '$CHARACTER' is not legal; refusing"; STATUS=65; exit "$STATUS"; }
    ZONE_ID=$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='$ZONE' LIMIT 1;")
    [[ $ZONE_ID =~ ^[0-9]+$ ]] || { say "zone '$ZONE' has no zoneidnumber row (got '$ZONE_ID')"; STATUS=65; exit "$STATUS"; }
    eqemu_sql "UPDATE character_data SET zone_id=$ZONE_ID WHERE name='$CHARACTER';" >/dev/null || { say "zone seed update for '$CHARACTER' failed"; STATUS=65; exit "$STATUS"; }
    # B1065: `# pose=x,y,z,heading` (heading in EQ units, 0-512) seeds the
    # login character's position with the zone, so a still is taken from a
    # pose willeq can be put at byte-for-byte. x/y/z/heading are in
    # eqemu_snapshot, so the restore puts the old row back.
    POSE=$(awk '/^# pose=/ {sub(/^# pose=/, ""); print; exit}' "$PLAN")
    if [[ -n $POSE ]]; then
        [[ $POSE =~ ^-?[0-9.]+,-?[0-9.]+,-?[0-9.]+,[0-9.]+$ ]] || { STATUS=64; exit "$STATUS"; }
        IFS=, read -r PX PY PZ PH <<<"$POSE"
        eqemu_sql "UPDATE character_data SET x=$PX, y=$PY, z=$PZ, heading=$PH WHERE name='$CHARACTER';" >/dev/null || { say "pose seed update for '$CHARACTER' failed"; STATUS=65; exit "$STATUS"; }
    fi
    # B1134: the observer stands in the same zone at its own pose.
    if [[ -n $OBS_ID ]]; then
        eqemu_sql "UPDATE character_data SET zone_id=$ZONE_ID WHERE id=$OBS_ID;" >/dev/null || { say "observer zone seed update for char $OBS_ID failed"; STATUS=65; exit "$STATUS"; }
        OPOSE=$(awk '/^# observer-pose=/ {sub(/^# observer-pose=/, ""); print; exit}' "$PLAN")
        if [[ -n $OPOSE ]]; then
            [[ $OPOSE =~ ^-?[0-9.]+,-?[0-9.]+,-?[0-9.]+,[0-9.]+$ ]] || { STATUS=64; exit "$STATUS"; }
            IFS=, read -r PX PY PZ PH <<<"$OPOSE"
            eqemu_sql "UPDATE character_data SET x=$PX, y=$PY, z=$PZ, heading=$PH WHERE id=$OBS_ID;" >/dev/null || { say "observer pose seed update for char $OBS_ID failed"; STATUS=65; exit "$STATUS"; }
        fi
    fi
fi
remote "mkdir -p ~/eq-oracle/uigap-scripts '$REMOTE_DIR'" || { say "remote mkdir of $REMOTE_DIR failed"; STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_remote.sh" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_remote.sh" || { say "scp of oracle_remote.sh failed"; STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_login_patch.py" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_login_patch.py" || { say "scp of oracle_login_patch.py failed"; STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_ini_seed.py" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py" || { say "scp of oracle_ini_seed.py failed"; STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/set_facepick_luclin.py" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/set_facepick_luclin.py" || { say "scp of set_facepick_luclin.py failed"; STATUS=65; exit "$STATUS"; }
scp -q "$PLAN" "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/plan.tsv" || { say "scp of the plan failed"; STATUS=65; exit "$STATUS"; }
REMOTE_STARTED=1
# B1058: a plan with template clicks needs the locate broker on THIS host (the
# oracle box has no NumPy/OpenCV): one ssh exchange per locate, shots and the
# per-locate table in $OUT/broker/ (docs/oracle_driver.md "Template clicks").
# B1059: the same broker answers text reads with lane$LANE's glyph atlas
# (glyphs/rigs.tsv; none listed = tesseract / the legacy checks only).
if grep -qE '^# create-clicks=template$|^(click|wait)-template'$'\t' "$PLAN"; then
    python3 "$REPO/scripts/eqemu/oracle/oracle_locate.py" broker --remote "user@$EQEMU_ORACLE_HOST" \
        --remote-dir "$REMOTE_DIR" --out "$OUT/broker" --rig "lane$LANE" 2>>"$OUT/broker.log" &
    BROKER_PID=$!
fi
remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh run '$REMOTE_DIR' $LANE" >"$OUT/remote.log" 2>&1
RC=$?
stop_broker
if (( RC == 67 )); then STATUS=67; else STATUS=$RC; fi
remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh stop '$REMOTE_DIR'" >>"$OUT/remote.log" 2>&1 || STATUS=65
scp -q -r "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/." "$OUT/" 2>>"$OUT/driver.log" || STATUS=65
REMOTE_STARTED=0
# B1192 (legH3): a completed client step MUST have produced its timeline --
# the per-step measurement every downstream step (the poller's arm line, the
# merge, the report) consumes. A remote rc 0 with no timeline.tsv is the
# legH3 failure class ("the client step never produced a timeline") wearing a
# success code: refuse it.
if [[ $STATUS -eq 0 && ! -s $OUT/timeline.tsv ]]; then
    say "the client step completed (remote rc 0) but produced no timeline.tsv; refusing a leg with no measurement"
    STATUS=65
fi
if (( STATUS == 67 )); then
    printf '# Official-client disconnect\n\nPlan: `%s`. Partial timeline: [timeline.tsv](timeline.tsv).\nRaw capture: [capture.pcap](capture.pcap); video: [video.mp4](video.mp4).\n' "$PLAN" >"$OUT/summary.md"
elif [[ -s $OUT/capture.pcap ]]; then
    python3 "$REPO/scripts/eqemu/oracle/oracle_report.py" "$OUT" "$PLAN" "$REPO" || STATUS=65
fi
if (( STATUS == 67 )); then
    eq_remote bash -lc "for kind in world zone; do file=\$(ls -t '$AKK_DIR/server/logs'/\${kind}_*.log '$AKK_DIR/server/logs/zone'/\${kind}_*.log 2>/dev/null | head -1); if [[ -n \$file ]]; then echo \"== \$file ==\"; tail -n 40 \"\$file\"; fi; done" >"$OUT/disconnect-logs.txt" 2>&1 || :
    printf '\n## Disconnect server log\n\n' >>"$OUT/summary.md"
    if [[ -s $OUT/disconnect-logs.txt ]]; then
        sed -n '1,82p' "$OUT/disconnect-logs.txt" >>"$OUT/summary.md"
    else
        printf 'No world/zone log lines were found.\n' >>"$OUT/summary.md"
        STATUS=65
    fi
fi
exit "$STATUS"
