#!/usr/bin/env bash
# B1129 (#449): one oracle leg with a test character switched to another CLASS
# for that leg only -- the multi-class recipe of B1118 (review/B1118/scripts/
# class_run.sh) and B1130 (review/B1130/scripts/leg.sh), made a driver feature.
#
# The official client refuses a gem cast its class cannot use, so a set of
# spells from several classes needs one leg per class. This helper:
#   1. checks the plan OFFLINE first (oracle_plan_check.py, the target class,
#      the gems the leg starts with) and refuses before touching the lane;
#   2. waits for the lane lease, then runs oracle_session.sh, which TAKES the
#      lease itself; every SQL below runs only inside that locked session
#      (class_run.sh's class switch ran outside the lock -- not repeated here);
#   3. ORACLE_PRE_SQL switches the class (only from the home class);
#      the driver then re-checks the plan against the character's real class,
#      level and gems before Wine starts;
#   4. ORACLE_POST_SQL runs in cleanup with the WORLD DOWN, after the snapshot
#      restore (which covers level but NOT class, memorised gems or buffs):
#      home class back, the memorised gems rewritten from --gems (slot 0 =
#      gem 1), buffs cleared, plus --post-sql-file (e.g. B1130's
#      skills_restore_t2.sql); the driver drops spellbook rows the leg added;
#   5. verifies the after-census: home class and exactly --gems memorised.
#
# usage: oracle_class_leg.sh <lane 1..5> <class 1..16> <plan.tsv> <out_dir>
#            [--char-id 67] [--home-class 12] [--gems 93,38,54,41,253,458,656]
#            [--pre-sql-file F] [--post-sql-file F] [--no-wait]
#            [--server-sql-file F --server-undo-sql-file F --server-dump-sql-file F]
# Exit codes: oracle_session.sh's (0/64/65/66/67); 64 also = plan refused;
# 66 also = the after-census does not show the home class and the gems.
set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
usage() { sed -n '/^# usage:/,/^# Exit/p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 64; }
[[ $# -ge 4 ]] || usage
LANE=$1 CLS=$2 PLAN=$3 OUT=$4; shift 4
CHAR_ID=67 HOME_CLASS=12 GEMS=93,38,54,41,253,458,656 PRE_FILE= POST_FILE= WAIT=1
SRV_FILE= SRV_UNDO_FILE= SRV_DUMP_FILE=
while [[ $# -gt 0 ]]; do
    case $1 in
        --char-id) CHAR_ID=$2; shift 2 ;;
        --home-class) HOME_CLASS=$2; shift 2 ;;
        --gems) GEMS=$2; shift 2 ;;
        --pre-sql-file) PRE_FILE=$2; shift 2 ;;
        --post-sql-file) POST_FILE=$2; shift 2 ;;
        --no-wait) WAIT=0; shift ;;
        --server-sql-file) SRV_FILE=$2; shift 2 ;;
        --server-undo-sql-file) SRV_UNDO_FILE=$2; shift 2 ;;
        --server-dump-sql-file) SRV_DUMP_FILE=$2; shift 2 ;;
        *) usage ;;
    esac
done
[[ $LANE =~ ^[1-5]$ && $CLS =~ ^([1-9]|1[0-6])$ && $HOME_CLASS =~ ^([1-9]|1[0-6])$ && $CHAR_ID =~ ^[0-9]+$ ]] || usage
[[ $GEMS =~ ^([0-9]+(,[0-9]+)*)?$ ]] || usage
[[ -f $PLAN ]] || { echo "no plan: $PLAN" >&2; exit 64; }
[[ -z $POST_FILE || -f $POST_FILE ]] || { echo "no post-SQL file: $POST_FILE" >&2; exit 64; }
[[ -z $PRE_FILE || -f $PRE_FILE ]] || { echo "no pre-SQL file: $PRE_FILE" >&2; exit 64; }
# B1174 (#493): a leg-only server-data change (oracle_session.sh runs it with
# the stack down before up.sh and its undo on every exit path): all three or none.
if [[ -n $SRV_FILE$SRV_UNDO_FILE$SRV_DUMP_FILE ]]; then
    for f in "$SRV_FILE" "$SRV_UNDO_FILE" "$SRV_DUMP_FILE"; do
        [[ -n $f && -f $f ]] || { echo "server-data hook needs --server-sql-file, --server-undo-sql-file and --server-dump-sql-file" >&2; exit 64; }
    done
    export ORACLE_SERVER_SQL="$(cat "$SRV_FILE")" ORACLE_SERVER_UNDO_SQL="$(cat "$SRV_UNDO_FILE")" ORACLE_SERVER_DUMP_SQL="$(cat "$SRV_DUMP_FILE")"
fi
PLAN=$(realpath "$PLAN") OUT=$(realpath -m "$OUT")

# 1. offline: the leg starts with --gems memorised (the previous leg's undo).
MEMMED= slot=0
for sid in ${GEMS//,/ }; do MEMMED+="${MEMMED:+,}$slot:$sid"; slot=$((slot + 1)); done
if ! python3 "$SCRIPT_DIR/oracle_plan_check.py" check "$PLAN" --class "$CLS" --lane "$LANE" ${MEMMED:+--memmed "$MEMMED"} >&2; then
    echo "plan refused for class $CLS before taking lane $LANE" >&2
    exit 64
fi

# 3./4. the hooks oracle_session.sh runs inside its locked session.
VALUES= slot=0
for sid in ${GEMS//,/ }; do VALUES+="${VALUES:+,}($CHAR_ID,$slot,$sid)"; slot=$((slot + 1)); done
export ORACLE_STATE_CHAR=$CHAR_ID ORACLE_EXPECT_CLASS=$CLS
# B1134 (#454; B1137's rig copy, made a driver feature): the character's
# skills, disciplines and persistent timers are not in the snapshot, and a
# leg can change all three (a song raises a skill, #traindisc scribes
# disciplines, a /disc starts a reuse timer). Copy them aside before the class
# switch and put exactly those rows back in the post-SQL; the census lines
# (SAVED/LEG/BACK: rows and value sums) land in driver.log.
SAVE_TABLES="character_skills:id character_disciplines:id timers:char_id"
SAVE_PRE= SAVE_POST=
for tc in $SAVE_TABLES; do
    # One assignment per line. The one-line 3-word form is fine in the
    # driver (legH3's own post-SQL log shows the correct `WHERE id=67`),
    # but on this host's bash 5.2.21 the 2-word shape `t=${tc%%:*}
    # c=${tc##:*}` mis-expands c to the whole token in a standalone
    # script; the split form is immune, so the WHERE clauses stay
    # provably correct in any context.
    t=${tc%%:*}
    c=${tc##*:}
    s=oracle_save_${t}_$CHAR_ID
    SAVE_PRE+="DROP TABLE IF EXISTS $s; CREATE TABLE $s AS SELECT * FROM $t WHERE $c=$CHAR_ID; SELECT 'SAVED','$t',COUNT(*) FROM $s; "
    # B1192 (legH3): the restore half PROBES the copy before it deletes the
    # live rows (mysql -B -N -e stops at the first error). In legH3 the
    # session died before the save tables existed -- ORACLE_PRE_SQL empty
    # or the leg never reaching the pre-SQL stage (the log shows neither a
    # pre-SQL line nor the tables) -- so the post-SQL's
    # DELETE-then-INSERT deleted the live rows first (char 67's
    # character_skills happened to hold 0 rows -- the LEG census in the
    # log -- so nothing was lost that run) and only then failed on the
    # INSERT (ERROR 1146, driver.log). With non-empty rows the delete
    # would have destroyed them with no restore source. The probe fails
    # BEFORE the delete in exactly that situation.
    SAVE_POST+=" SELECT 'LEG','$t',COUNT(*) FROM $t WHERE $c=$CHAR_ID; SELECT COUNT(*) FROM $s; DELETE FROM $t WHERE $c=$CHAR_ID; INSERT INTO $t SELECT * FROM $s; DROP TABLE $s; SELECT 'BACK','$t',COUNT(*) FROM $t WHERE $c=$CHAR_ID;"
done
ORACLE_PRE_SQL="${SAVE_PRE}UPDATE character_data SET class=$CLS WHERE id=$CHAR_ID AND class=$HOME_CLASS;"
# B1134: --pre-sql-file runs after the save and the switch (e.g. a server rule
# a leg needs off; its undo goes in --post-sql-file).
[[ -n $PRE_FILE ]] && ORACLE_PRE_SQL+=" $(cat "$PRE_FILE")"
export ORACLE_PRE_SQL
ORACLE_POST_SQL="UPDATE character_data SET class=$HOME_CLASS WHERE id=$CHAR_ID; DELETE FROM character_memmed_spells WHERE id=$CHAR_ID;"
[[ -n $VALUES ]] && ORACLE_POST_SQL+=" INSERT INTO character_memmed_spells (id,slot_id,spell_id) VALUES $VALUES;"
ORACLE_POST_SQL+=" DELETE FROM character_buffs WHERE character_id=$CHAR_ID;"
ORACLE_POST_SQL+="$SAVE_POST"
[[ -n $POST_FILE ]] && ORACLE_POST_SQL+=" $(cat "$POST_FILE")"
export ORACLE_POST_SQL
export ORACLE_OWNER=${ORACLE_OWNER:-oracle_class_c$CLS}

# 2. wait for the lease (never break it); a lost take race (64 with no
# session.started) is retried, anything else is the leg's result.
export EQEMU_LANE=$LANE
# shellcheck source=/dev/null
source "$REPO/scripts/eqemu/env.sh"
for try in $(seq 1 240); do
    if (( WAIT )); then while eqemu_lock_show >/dev/null 2>&1; do sleep 30; done; fi
    bash "$SCRIPT_DIR/oracle_session.sh" "$LANE" "$PLAN" "$OUT"
    RC=$?
    if (( WAIT && RC == 64 )) && [[ ! -e $OUT/session.started ]]; then
        echo "lane $LANE lease race lost (try $try), retrying" >&2; rm -rf "$OUT"; sleep 20; continue
    fi
    break
done

# 5. the after-census (written by the driver's cleanup) must show the undo.
if [[ -e $OUT/char-state-before.tsv ]]; then
    want_mem=$(slot=0; for sid in ${GEMS//,/ }; do printf '%s\t%s\n' "$slot" "$sid"; slot=$((slot + 1)); done)
    got_class=$(cut -f2 "$OUT/char-state-after.tsv" 2>/dev/null)
    if [[ $got_class != "$HOME_CLASS" ]] || [[ $(cat "$OUT/char-memmed-after.tsv" 2>/dev/null) != "$want_mem" ]]; then
        echo "char $CHAR_ID NOT restored: class '$got_class' (want $HOME_CLASS), gems in $OUT/char-memmed-after.tsv (want $GEMS)" >&2
        RC=66
    else
        echo "char $CHAR_ID restored: class $HOME_CLASS, gems $GEMS" >&2
    fi
fi
exit "$RC"
