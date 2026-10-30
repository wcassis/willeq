#!/usr/bin/env bash
# B1129 (#449): offline net for oracle_class_leg.sh, the multi-class provisioning
# helper. A stub oracle_session.sh records the hooks it was handed and writes
# the after-census the real driver's cleanup would; no lane, no ssh, no SQL.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/oracle-class-leg.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT
O=$FIXTURE/scripts/eqemu/oracle
mkdir -p "$O" "$FIXTURE/eq"
cp "$REPO/scripts/eqemu/oracle/oracle_class_leg.sh" "$REPO/scripts/eqemu/oracle/oracle_plan_check.py" \
   "$REPO/scripts/eqemu/oracle/gem_pitch.tsv" "$O/"
# The lease is free; the helper never takes it itself (the driver does).
printf 'eqemu_lock_show() { return 1; }\n' >"$FIXTURE/scripts/eqemu/env.sh"
cat >"$O/oracle_session.sh" <<'EOF'
OUT=$3; mkdir -p "$OUT"; echo stub >"$OUT/session.started"
{ echo "PRE=$ORACLE_PRE_SQL"; echo "POST=$ORACLE_POST_SQL"; echo "STATE=$ORACLE_STATE_CHAR"
  echo "EXPECT=$ORACLE_EXPECT_CLASS"; echo "OWNER=$ORACLE_OWNER"; } >"$OUT/hooks.txt"
printf '67\t12\t60\t2\n' >"$OUT/char-state-before.tsv"
printf '67\t%s\t1\t2\n' "${PROBE_AFTER_CLASS:-12}" >"$OUT/char-state-after.tsv"
printf '0\t93\n1\t38\n' >"$OUT/char-memmed-after.tsv"
exit 0
EOF
python3 - "$FIXTURE/eq/spells_us.txt" <<'PY'
import sys
rows = []
for sid, name, lv in (('93', 'Burst of Flame', {6: 1, 10: 1, 13: 1}), ('38', 'Lightning Bolt', {12: 16})):
    f = ['0'] * 160; f[0], f[1] = sid, name
    for c in range(1, 17): f[103 + c] = str(lv.get(c, 255))
    rows.append('^'.join(f))
open(sys.argv[1], 'w').write('\n'.join(rows) + '\n')
PY
export EQ_CLIENT_PATH=$FIXTURE/eq
printf 'login\tTestcaster\t0\tselect\nclick\t25,243\t0\tgem1_cast93\n' >"$FIXTURE/plan.tsv"
leg() { set +e; bash "$O/oracle_class_leg.sh" 5 "$1" "$FIXTURE/plan.tsv" "$FIXTURE/$2" --gems 93,38 >"$FIXTURE/$2.log" 2>&1; rc=$?; set -e; }

# 1. a wizard leg casting the druid/shaman/mage gem: refused offline, the lane untouched.
leg 12 wiz
[[ $rc == 64 && ! -e $FIXTURE/wiz/session.started ]] || { echo "wizard leg returned $rc or reached the driver"; exit 1; }
grep -q 'REFUSED.*93 "Burst of Flame".*WIZ (class 12)' "$FIXTURE/wiz.log" || { echo 'offline refusal does not name spell + class'; exit 1; }

# 2. a druid leg: the class switch only from the home class, the undo puts back
#    class 12, exactly the --gems (slot 0 = gem 1) and no buffs.
leg 6 dru
[[ $rc == 0 ]] || { echo "druid leg returned $rc"; cat "$FIXTURE/dru.log"; exit 1; }
H=$FIXTURE/dru/hooks.txt
grep -q '^PRE=.*UPDATE character_data SET class=6 WHERE id=67 AND class=12;$' "$H" || { echo 'pre-SQL wrong'; cat "$H"; exit 1; }
grep -q '^POST=UPDATE character_data SET class=12 WHERE id=67; DELETE FROM character_memmed_spells WHERE id=67; INSERT INTO character_memmed_spells (id,slot_id,spell_id) VALUES (67,0,93),(67,1,38); DELETE FROM character_buffs WHERE character_id=67;' "$H" || { echo 'post-SQL wrong'; cat "$H"; exit 1; }
# B1134: skills, disciplines and timers are saved BEFORE the class switch and
# put back exactly (delete + insert from the copy, copy dropped) in the undo.
for t in character_skills:id character_disciplines:id timers:char_id; do
    tb=${t%%:*} col=${t##*:}
    grep -q "^PRE=.*CREATE TABLE oracle_save_${tb}_67 AS SELECT \* FROM $tb WHERE $col=67;.*UPDATE character_data SET class=6" "$H" || { echo "pre-SQL does not save $tb before the switch"; exit 1; }
    grep -q "^POST=.*DELETE FROM $tb WHERE $col=67; INSERT INTO $tb SELECT \* FROM oracle_save_${tb}_67; DROP TABLE oracle_save_${tb}_67;" "$H" || { echo "post-SQL does not restore $tb"; exit 1; }
done
# B1192 (legH3): the restore half probes the copy before deleting the live
# rows (mysql stops at the first error), so an early-exit leg's orphaned
# post-SQL -- save tables never created -- fails at the probe and can never
# delete the character's live rows without a restore source.
for tb in character_skills character_disciplines timers; do
    grep -q "SELECT COUNT(\*) FROM oracle_save_${tb}_67; DELETE FROM $tb" "$H" || { echo "post-SQL does not probe oracle_save_${tb}_67 before deleting $tb"; exit 1; }
done
grep -qx 'STATE=67' "$H" && grep -qx 'EXPECT=6' "$H" && grep -qx 'OWNER=oracle_class_c6' "$H" || { echo 'state/expect/owner wrong'; exit 1; }

# 3. an after-census that does not show the home class is a restore failure.
PROBE_AFTER_CLASS=6 leg 6 dru_bad
[[ $rc == 66 ]] || { echo "unrestored class returned $rc, expected 66"; exit 1; }
echo 'PASS: class leg refuses an uncastable gem offline, hooks switch only from the home class, undo restores class + gems + buffs + skills/disciplines/timers, an unrestored census is 66'
