# UIGAP-live (#151): SOURCED by scripts/scenario.sh after the pickup_roundtrip
# client exits, world still up. Exit status 0 = the database agrees.
#
#  1. the sidecar's 1 Hz samples of the Rusty Dagger's `inventory.slot_id`,
#     de-duplicated, contain 24 -> 4010 -> 2000 -> 24 in that order (the
#     cursor, 33, may or may not be sampled between hops);
#  2. the final row is slot 24 and only slot 24;
#  3. the client's own MODEL line says 24=7007 -- client and server agree.
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_seq="$(awk -F'\t' 'NF>=2 && $2!=prev {print $2; prev=$2}' "$OUT/${SCENARIO}_sidecar.log" | tr '\n' ' ')"
echo "DB slot_id samples of item 7007 (de-duplicated): ${_seq}"
_want="24 4010 2000 24"
_i=0
_need=($_want)
for _s in $_seq; do
    [ "$_i" -lt "${#_need[@]}" ] && [ "$_s" = "${_need[$_i]}" ] && _i=$((_i + 1))
done
_final="$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(slot_id),'none') FROM inventory WHERE character_id=${_tc} AND item_id=7007;" | head -1)"
_model="$(grep -o 'MODEL [0-9=]* [0-9=]* [0-9=]* [0-9=]*' "$LOG" | tail -1)"
echo "final DB row(s): ${_final}; client ${_model:-<no MODEL line>}"
if [ "$_i" -lt "${#_need[@]}" ]; then
    echo "FAIL: the database never showed the hop sequence ${_want} (matched ${_i} of ${#_need[@]})"
    return 1 2>/dev/null || exit 1
fi
if [ "$_final" != "24" ]; then
    echo "FAIL: the dagger's final DB slot is ${_final}, not 24"
    return 1 2>/dev/null || exit 1
fi
case "$_model" in
    *"24=7007"*) ;;
    *) echo "FAIL: the client's MODEL line does not hold the dagger in 24"; return 1 2>/dev/null || exit 1 ;;
esac
echo "PASS: DB hops ${_want} seen in order; final DB slot 24 = client model 24=7007"
