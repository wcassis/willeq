# UIGAP-live2 (#151): SOURCED after drop_destroy_coin. The dropped bottle and
# the destroyed dagger have no inventory rows; the coin ended 8 pp carried,
# 5 pp banked, nothing on the cursor (10 carried / 3 banked at the start).
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_rows="$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(CONCAT(item_id,'@',slot_id)),'none') FROM inventory WHERE character_id=$_tc AND item_id IN (7007,16598);" | head -1)"
_c="$(eqemu_sql "SELECT CONCAT(platinum,' ',platinum_bank,' ',platinum_cursor) FROM character_currency WHERE id=$_tc;" | head -1)"
echo "dagger/bottle rows: ${_rows}; platinum carried/bank/cursor: ${_c}"
_bad=0
_fail() { echo "FAIL: $*"; _bad=1; }
[ "$_rows" = none ] || _fail "the dropped/destroyed items still have rows: ${_rows}"
[ "$_c" = "8 5 0" ] || _fail "coin is '${_c}', expected '8 5 0' (carried bank cursor)"
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "PASS: bottle dropped and dagger destroyed (no rows); coin 8 carried / 5 banked / 0 cursor"
