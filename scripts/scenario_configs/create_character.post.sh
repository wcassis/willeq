# UIGAP-live2 (B888, #204): SOURCED by scripts/scenario.sh after the
# create_character client exits (world up). Run with --create-character.
#  1. the DATABASE has the character, in one of its city's home zones
#     (data/char_create.tsv `city` row), never the tutorial (zone 189);
#  2. the same client binary + config, with --delete-character, deletes it
#     at character select (OP_DeleteCharacter) and exits 0;
#  3. the DATABASE no longer has a live row for it (eqemu soft-deletes:
#     name -> '<name>-deleted-<ts>', deleted_at set).
# Then the soft-deleted rows THIS run created are removed: the lane's restore
# compares whole character tables before/after, and a new character is the
# one thing a run adds rather than moves.
_spec="${CREATE_CHARACTER:-}"
_bad=0
_fail() { echo "FAIL: $*"; _bad=1; }
[ -n "$_spec" ] || _fail "no --create-character spec"
_name="${_spec%%,*}"
_city="$(echo "$_spec" | cut -d, -f6 | tr '[:lower:]' '[:upper:]')"
_zones="$(awk -F'\t' -v c="$_city" '$1=="city" && toupper($3)==c {print $5}' "$REPO/data/char_create.tsv")"
_row="$(eqemu_sql "SELECT c.id, c.zone_id, z.short_name, IFNULL(c.deleted_at,'live') FROM character_data c LEFT JOIN zone z ON z.zoneidnumber=c.zone_id AND z.version=0 WHERE c.name='${_name}';" | head -1)"
echo "created: '${_row}' (city ${_city} -> home zones ${_zones})"
[ -n "$_row" ] || _fail "no character_data row named ${_name}"
_id="$(echo "$_row" | cut -f1)"; _zid="$(echo "$_row" | cut -f2)"; _zsn="$(echo "$_row" | cut -f3)"
[ "$_zid" != 189 ] || _fail "${_name} is in the tutorial (zone 189)"
case ",${_zones}," in *",${_zsn},"*) ;; *) _fail "${_name} is in ${_zsn} (zone ${_zid}), not one of ${_zones}" ;; esac
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "-- --list-characters before the delete:"
timeout 120 "$BIN" -c "$CONFIG" --list-characters > "$OUT/create_character_list_before.log" 2>&1
grep -iE "character|${_name}" "$OUT/create_character_list_before.log" | grep -v DEBUG | tail -5
timeout 120 "$BIN" -c "$CONFIG" --delete-character "$_name" > "$OUT/create_character_delete.log" 2>&1
_drc=$?
echo "--delete-character ${_name}: exit ${_drc}"
grep -iE "delet" "$OUT/create_character_delete.log" | grep -v DEBUG | tail -5
_live="$(eqemu_sql "SELECT COUNT(*) FROM character_data WHERE name='${_name}' AND deleted_at IS NULL;" | head -1)"
_soft="$(eqemu_sql "SELECT IFNULL(GROUP_CONCAT(CONCAT(name,' @ ',deleted_at)),'none') FROM character_data WHERE id=${_id};" | head -1)"
echo "after the delete: live rows named ${_name} = ${_live}; row ${_id} = ${_soft}"
[ "$_drc" = 0 ] || _fail "--delete-character exited ${_drc}"
[ "$_live" = 0 ] || _fail "${_name} still has a live row after --delete-character"
eqemu_sql "DELETE FROM character_currency WHERE id=${_id}; DELETE FROM character_data WHERE id=${_id} AND deleted_at IS NOT NULL;" >/dev/null
if [ "$_bad" != 0 ]; then return 1 2>/dev/null || exit 1; fi
echo "PASS: ${_name} created in ${_zsn} (zone ${_zid}, city ${_city}), then deleted by --delete-character (row ${_id} soft-deleted, removed by the lane)"
