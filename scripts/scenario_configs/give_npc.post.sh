# UIGAP-live2 (#212): SOURCED after give_npc. The fang (13915) left Testchar's
# rows and the Moonstone (10070) the quest summoned is there.
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_f="$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(slot_id),'none') FROM inventory WHERE character_id=$_tc AND item_id=13915;" | head -1)"
_m="$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(slot_id),'none') FROM inventory WHERE character_id=$_tc AND item_id=10070;" | head -1)"
echo "Gnoll Fang rows: ${_f}; Moonstone rows: ${_m}"
_bad=0
_fail() { echo "FAIL: $*"; _bad=1; }
[ "$_f" = none ] || _fail "the fang is still Testchar's (${_f})"
[ "$_m" != none ] || _fail "no Moonstone row: the quest did not pay"
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "PASS: the fang was given to Captain Tillin and the Moonstone is in slot ${_m}"
