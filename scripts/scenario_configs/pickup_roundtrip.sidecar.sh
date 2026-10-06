# UIGAP-live (#151): SOURCED by scripts/scenario.sh in a background subshell
# for the length of the pickup_roundtrip client run (eqemu_sql is in scope).
# Polls the travelling item's `inventory` row once a second, so the post-check
# can assert the SERVER saw every hop the client made, in order.
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
while true; do
    printf '%s\t%s\n' "$(date +%s.%N)" \
        "$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(slot_id ORDER BY slot_id),'none') FROM inventory WHERE character_id=${_tc} AND item_id=7007;")"
    sleep 1
done
