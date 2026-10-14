# B1068 (#388): zone_line_walk's sidecar -- the KEYBOARD half. Sourced by
# scripts/scenario.sh in a background subshell with DISPLAY set to the
# scenario's private Xvfb and LOG the client's log. It waits for the
# runner's "starting" line, lets the zone settle, then holds the forward key
# (W, config/hotkeys.json MoveForward) on the client window so the walk is the
# renderer's own movement path.
WALK_SECONDS="${ZONE_LINE_WALK_SECONDS:-8}"
SETTLE_SECONDS="${ZONE_LINE_WALK_SETTLE:-10}"
for _ in $(seq 1 600); do
    grep -q "scenario 'zone_line_walk' starting" "$LOG" 2>/dev/null && break
    sleep 1
done
grep -q "scenario 'zone_line_walk' starting" "$LOG" 2>/dev/null || { echo "sidecar: the client never zoned in"; exit 1; }
sleep "$SETTLE_SECONDS"
WID="$(xdotool search --onlyvisible --name '.' 2>/dev/null | head -1)"
[ -n "$WID" ] || { echo "sidecar: no client window on $DISPLAY"; exit 1; }
xdotool windowfocus --sync "$WID" 2>/dev/null
echo "sidecar: $(date -u +%T.%N | cut -c1-12) keydown w (window $WID) for ${WALK_SECONDS}s"
xdotool keydown --window "$WID" w
sleep "$WALK_SECONDS"
xdotool keyup --window "$WID" w
echo "sidecar: $(date -u +%T.%N | cut -c1-12) keyup w"
