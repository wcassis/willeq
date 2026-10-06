# UIGAP-live2 (#26): SOURCED after bank_change. Each store went up to the
# fewest coins on its own: carried 0/25/37/123 -> 2/9/9/3, banked 3/14/9/50 ->
# 4/5/4/0 (the arithmetic is in scenario_ui_trade.cpp; this is the SERVER's).
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_p="$(eqemu_sql "SELECT CONCAT_WS('/',platinum,gold,silver,copper) FROM character_currency WHERE id=$_tc;" | head -1)"
_b="$(eqemu_sql "SELECT CONCAT_WS('/',platinum_bank,gold_bank,silver_bank,copper_bank) FROM character_currency WHERE id=$_tc;" | head -1)"
echo "DB carried ${_p}; banked ${_b}"
_bad=0
_fail() { echo "FAIL: $*"; _bad=1; }
[ "$_p" = "2/9/9/3" ] || _fail "carried coin is ${_p}, expected 2/9/9/3"
[ "$_b" = "4/5/4/0" ] || _fail "banked coin is ${_b}, expected 4/5/4/0"
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "PASS: the server holds carried 2/9/9/3 and banked 4/5/4/0"
