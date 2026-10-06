# UIGAP-live (#215): SOURCED by scripts/scenario.sh after the combine_sewing
# client exits, world still up. The database must agree with the client:
# the products where the client put them, the consumed components gone.
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
_rows="$(eqemu_sql "SELECT slot_id, item_id FROM inventory WHERE character_id=${_tc} ORDER BY slot_id;" | tr '\t\n' '= ')"
echo "DB inventory rows (slot=item): ${_rows}"
_fail=""
for _want in "24=21565" "25=13782" "26=12896"; do
    case " ${_rows}" in *" ${_want} "*) ;; *) _fail="${_fail} missing ${_want};" ;; esac
done
for _gone in 13753 6027 12900; do
    case " ${_rows}" in *"=${_gone} "*) _fail="${_fail} component ${_gone} still in the DB;" ;; esac
done
case " ${_rows}" in *" 33="*) _fail="${_fail} something is still on the DB cursor (33);" ;; esac
case " ${_rows}" in *" 401"[0-7]"="*) _fail="${_fail} the kit is not empty in the DB;" ;; esac
grep -o 'MODEL [^-]*' "$LOG" | tail -1
if [ -n "$_fail" ]; then
    echo "FAIL:${_fail}"
    return 1 2>/dev/null || exit 1
fi
echo "PASS: DB has knife 24, Ruined Wolf Pelt 25, A Ruby 26; skin and ruby parts consumed; cursor and kit empty"
