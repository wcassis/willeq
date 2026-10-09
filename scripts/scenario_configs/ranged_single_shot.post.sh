# B900 (#216): SOURCED by scripts/scenario.sh after the ranged_single_shot
# client exits, with the world still UP (before the restore takes the seed
# out again). The client asserted the SERVER fired the shot (a combat hit or
# miss line naming the target); this asserts the SERVER's own record of the
# shot: the seeded 10-arrow ammo stack in the DB ammo slot dropped by exactly
# one -- one shot consumed exactly one arrow
# (zone/special_attacks.cpp:986-1002, DeleteItemInInventory(ammo_slot, 1)).
#
# Slots are the DB's (RoF2) numbering: 11 = range, 22 = ammo.

_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_ammo="$(eqemu_sql "SELECT charges FROM inventory WHERE character_id=${_tc} AND item_id=8005;" | head -1)"
_bow="$(eqemu_sql "SELECT item_id FROM inventory WHERE character_id=${_tc} AND slot_id=11;" | head -1)"

# The client's own words, from the run log. Two shapes: the SCENARIO verdict
# reason line carries the matched combat line in single quotes ("server fired
# one shot: 'combat_self||You hit Ranged Dummy for 1 points of damage.'"),
# and a MOD_COMBAT log line at DEBUG carries the raw text.
_shotline="$(grep -oE "combat(_self|_miss)?\|[^|]*\|[^']*" "$LOG" | grep -E "points of damage|but miss" | tail -1)"
[ -z "$_shotline" ] && _shotline="$(grep -oE "You [a-z]+ Ranged Dummy for [0-9]+ points of damage\.|You try to [a-z]+ Ranged Dummy but miss!" "$LOG" | tail -1)"

echo "DB ammo slot (item 8005) charges = ${_ammo:-<absent>}; range slot item = ${_bow:-<absent>}"
echo "client combat line: ${_shotline:-<none in client log>}"

# The client's own words: a hit or miss line must be in the run log. The
# client only passes when it saw one (the scenario's pass condition IS that
# line), but this is the post-check's own half of the assertion -- a client
# pass whose log carries no combat line is a FAIL here, not a print.
if [ -z "$_shotline" ]; then
    echo "FAIL: no hit/miss combat line in the client log -- the client never saw the server fire (see $LOG's MOD_SCENARIO verdict)"
    return 1 2>/dev/null || exit 1
fi

# The seed gave 10; one shot leaves 9. A shot the server consumed but the
# client never saw, or a stack that never moved, both fail.
if [ "${_ammo}" != "9" ]; then
    echo "FAIL: the DB arrow count is ${_ammo:-absent}, not 9 -- the seed gave 10, so the server consumed ${_ammo:+$((10 - _ammo))} arrow(s) (expected exactly 1); if <absent>, the shot took the last arrow in a different slot (the quiver fallback, special_attacks.cpp:912-956) or the seed did not land"
    return 1 2>/dev/null || exit 1
fi
echo "PASS: the server consumed exactly one arrow (10 -> 9 in the DB ammo slot), matching the client's single /range"
