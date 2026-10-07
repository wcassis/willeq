# UIGAP-live2 (#212): SOURCED by scripts/scenario.sh after the trade_roundtrip
# client exits, world still up. The DATABASE half of the trade:
#  1. the dagger (7007) is Testbuddy's and no longer Testchar's;
#  2. the cancelled bottle (16598) is still Testchar's, not Testbuddy's;
#  3. the 5 pp moved: Testchar 10 -> 5, Testbuddy 0 -> 5;
#  4. the partner process passed its own half (trade_peer).
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_tb="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testbuddy';" | head -1)"
_rows() { eqemu_sql "SELECT IFNULL(GROUP_CONCAT(slot_id ORDER BY slot_id),'none') FROM inventory WHERE character_id=$1 AND item_id=$2;" | head -1; }
_d_tc="$(_rows "$_tc" 7007)"; _d_tb="$(_rows "$_tb" 7007)"
_b_tc="$(_rows "$_tc" 16598)"; _b_tb="$(_rows "$_tb" 16598)"
_pp_tc="$(eqemu_sql "SELECT platinum FROM character_currency WHERE id=$_tc;" | head -1)"
_pp_tb="$(eqemu_sql "SELECT platinum FROM character_currency WHERE id=$_tb;" | head -1)"
echo "dagger 7007: Testchar=${_d_tc} Testbuddy=${_d_tb}; bottle 16598: Testchar=${_b_tc} Testbuddy=${_b_tb}; pp: Testchar=${_pp_tc} Testbuddy=${_pp_tb}"
grep -E '^SCENARIO ' "$PEERLOG" | tail -1
_bad=0
_fail() { echo "FAIL: $*"; _bad=1; }
[ "$_d_tc" = none ] || _fail "the dagger is still Testchar's (slots ${_d_tc})"
[ "$_d_tb" != none ] || _fail "the dagger never reached Testbuddy"
[ "$_b_tc" != none ] || _fail "the cancelled bottle is not Testchar's"
[ "$_b_tb" = none ] || _fail "the cancelled bottle reached Testbuddy (${_b_tb})"
[ "$_pp_tc" = 5 ] || _fail "Testchar holds ${_pp_tc} pp, expected 5"
[ "$_pp_tb" = 5 ] || _fail "Testbuddy holds ${_pp_tb} pp, expected 5"
grep -qE '^SCENARIO trade_peer pass' "$PEERLOG" || _fail "the partner (trade_peer) did not pass"
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "PASS: dagger Testchar -> Testbuddy (${_d_tb}), 5 pp moved, the cancelled bottle stayed home (${_b_tc}), trade_peer passed"
