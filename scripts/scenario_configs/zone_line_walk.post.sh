# B1068 (#388): zone_line_walk's post-check -- the SERVER half. The client
# must have logged its own trigger, and the source zone's log must show the
# server receiving that OP_ZoneChange for the character (eqemu's LogZoning
# line in Client::Handle_OP_ZoneChange, zone/zoning.cpp). Sourced by
# scripts/scenario.sh with LOG / OUT set, the world still up.
CHAR="$(sed -n 's/.*"character"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CONFIG" | head -1)"
grep -m1 "\[ZONE\] Zone line triggered!" "$LOG" || { echo "FAIL: the client never logged [ZONE] Zone line triggered! (the request decision, not zone_lines.cpp's [MAP] box match)"; exit 1; }
grep -m1 "Zone line crossing:" "$LOG"
ZLOG="$(eq_remote sh -c "ls -t ${AKK_DIR}/server/logs/zone/qeynos_version_0_inst_id_0_port_*.log 2>/dev/null | head -1")"
[ -n "$ZLOG" ] || { echo "FAIL: no qeynos zone log under ${AKK_DIR}/server/logs/zone"; exit 1; }
LINE="$(eq_remote grep -a "char_name \[${CHAR}\] zoning to \[qeynos2\]" "$ZLOG" | tail -1)"
[ -n "$LINE" ] || { echo "FAIL: $ZLOG has no OP_ZoneChange from ${CHAR} to qeynos2"; exit 1; }
echo "$LINE"
echo "PASS: client trigger + server OP_ZoneChange for ${CHAR} (qeynos -> qeynos2)"
