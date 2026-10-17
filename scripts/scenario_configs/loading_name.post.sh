# B1088 (#408): loading_name's post-check -- the CLIENT half of the arm. The
# lane's assertion is the loading screen's own INFO lines, greppable out of a
# plain log without pixels (step 4 iii of the card): the initial zone-in's
# "Entering <era long name>..." (the character's home zone, era table) and
# the zone change's (the GM zone request's target, era table). Sourced by
# scripts/scenario.sh with LOG / REPO set, the world still up.
#
# Lane invocation (the lane's own --scenario-zone-id names the target; the
# short name below is only the scenario's Pass check):
#   SCENARIO_CLIENT_ARGS="--scenario-zone-id 2 --scenario-zone-name qeynos2" \
#       EQEMU_LANE=1 bash scripts/scenario.sh loading_name --timeout 300 --unit T4xx
ERA="$REPO/data/era_zones.tsv"
[ -f "$ERA" ] || { echo "FAIL: $ERA is not in the tree"; exit 1; }
# Initial zone: the runner's start line names the short name; map it to the
# era table's long name (the exact string the loading screen draws).
SHORT="$(grep -o "zoned in to '[^']*'" "$LOG" | head -1 | cut -d"'" -f2)"
[ -n "$SHORT" ] || { echo "FAIL: the client never reported zoned in (no 'zoned in to' line)"; exit 1; }
INIT_EXPECTED="$(awk -F'\t' -v z="$SHORT" '$3 == z {print $4; exit}' "$ERA")"
[ -n "$INIT_EXPECTED" ] || { echo "FAIL: the initial zone '$SHORT' has no era-table row"; exit 1; }
grep -F "[LOADING] Entering ${INIT_EXPECTED}..." "$LOG" >/dev/null || { echo "FAIL: the initial zone-in's loading line '[LOADING] Entering ${INIT_EXPECTED}...' is missing from the log"; exit 1; }
# Zone change: the target's era-table long name. The lane's invocation runs
# the scenario with --scenario-zone-id 2 (the documented default below); a
# different id overrides it via B1088_ZONE_ID (the lane's own rig).
ZONE_ID="${B1088_ZONE_ID:-2}"
TARGET_EXPECTED="$(awk -F'\t' -v id="$ZONE_ID" '$2 == id {print $4; exit}' "$ERA")"
[ -n "$TARGET_EXPECTED" ] || { echo "FAIL: the target zone id $ZONE_ID has no era-table row"; exit 1; }
grep -F "[LOADING] Entering ${TARGET_EXPECTED}..." "$LOG" >/dev/null || { echo "FAIL: the zone change's loading line '[LOADING] Entering ${TARGET_EXPECTED}...' is missing from the log"; exit 1; }
echo "PASS: both loading-screen lines -- initial zone-in ($INIT_EXPECTED) and the zone change ($TARGET_EXPECTED) -- are in the client's own log"
