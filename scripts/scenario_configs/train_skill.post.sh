# UIGAP-live (#214): SOURCED by scripts/scenario.sh after the train_skill
# client exits, world still up. The trained skill row must be the value the
# client reports (eqemu's SetSkill saves the row at once). The training
# points are saved with the character (logout / periodic), so they are
# REPORTED, not asserted.
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_db="$(eqemu_sql "SELECT value FROM character_skills WHERE id=${_tc} AND skill_id=0;" | head -1)"
_pts="$(eqemu_sql "SELECT points FROM character_data WHERE id=${_tc};" | head -1)"
_model="$(grep -o 'MODEL skill0=[0-9]* points=[0-9]*' "$LOG" | tail -1)"
echo "DB skill 0 = ${_db}, DB points = ${_pts} (not asserted); client ${_model:-<no MODEL line>}"
case "$_model" in
    "MODEL skill0=${_db} "*) echo "PASS: the DB skill row (${_db}) equals the client's value" ;;
    *) echo "FAIL: DB skill 0 is ${_db}, the client says ${_model:-nothing}"; return 1 2>/dev/null || exit 1 ;;
esac
