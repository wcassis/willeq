#!/usr/bin/env bash
# B1066 (#386): does a mouse press on a static-UI window stay out of the
# camera's mouse-look, and does a press on the 3D view still look (and keep
# looking when the drag crosses a window)? An x86 GLES2 build on llvmpipe in an
# Xvfb display (--egl-x11), real X button events from xdotool, the heading and
# the camera pitch read from --pose-log.
#
# Usage: scripts/eqemu/press_ownership_check.sh --bin PATH [--out DIR]
#            [--display :68] [--explore] [--unit B1066]
#            [--item X,Y] [--window-bg X,Y] [--view X,Y] [--cross X,Y]
#   Coordinates are CLIENT-WINDOW pixels (an 800x600 window):
#     --item       an occupied inventory slot (right press = item display)
#     --window-bg  a pixel of the open inventory window that is no control
#                  (left press there must not look either)
#     --view       a pixel of the 3D view with no window on it
#     --cross      where the view drag ends: inside the inventory window
#     --dismiss    optional: a button to left-click after the inventory opens
#                  (the Inventory Tip window's Close, B834, pops on first open)
#   --explore only opens the inventory and saves a screenshot (to find them).
#
# Lane: EQEMU_HOST=local, character Testbuddy (account willeq_buddy) -- the
# local stack is the server Will's XU4 logs into, so this rig never starts,
# stops or restarts it, takes no lane lock, writes nothing to the database and
# moves only Testbuddy (mouse-look turns the character; the turn is the only
# state it leaves, as a heading the server saves on logout).
#
# Gestures, in order (each 20 px of motion over ~0.6 s while the button is
# down, marked in marks.tsv with unix ms):
#   ui_right   right press on --item, drag 20 px, release   -> heading/pitch UNCHANGED
#   ui_left    left press on --window-bg, drag 20 px         -> UNCHANGED
#   view_right right press on --view, drag 20 px             -> CHANGED
#   view_cross right press on --view, drag to --cross        -> keeps changing
#              after the pointer is inside the window
# Verdict: press_ownership_verdict.py over the pose log and the marks.
# Exit: 0 PASS, 1 FAIL, 2 no verdict (never zoned / no pose rows), 64 usage.

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN=""; OUT=""; DISP=":68"; EXPLORE=0; UNIT="B1066"
ITEM=""; WBG=""; VIEW=""; CROSS=""; DISMISS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --bin) BIN="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --display) DISP="$2"; shift 2 ;;
        --explore) EXPLORE=1; shift ;;
        --unit) UNIT="$2"; shift 2 ;;
        --item) ITEM="$2"; shift 2 ;;
        --window-bg) WBG="$2"; shift 2 ;;
        --view) VIEW="$2"; shift 2 ;;
        --cross) CROSS="$2"; shift 2 ;;
        --dismiss) DISMISS="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
[ -x "$BIN" ] || { echo "no willeq binary at '$BIN' (an EQT_GLES2 x86 build)" >&2; exit 64; }
if [ "$EXPLORE" -eq 0 ] && { [ -z "$ITEM" ] || [ -z "$WBG" ] || [ -z "$VIEW" ] || [ -z "$CROSS" ]; }; then
    echo "--item, --window-bg, --view and --cross are required (run --explore first)" >&2; exit 64
fi
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/${UNIT}/press_ownership-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
export EQEMU_HOST=local
source "$REPO/scripts/eqemu/env.sh"
say() { printf '[press_ownership] %s\n' "$*" | tee -a "$OUT/rig.log" >&2; }

eqemu_stack_ready >/dev/null 2>&1 || { say "local stack not ready -- this rig never starts it"; exit 2; }

CFG="$OUT/client.json"
CLIENT_HOST="$(hostname -I | awk '{print $1}')"
sed "s/\"host\"\([[:space:]]*:[[:space:]]*\)\"[^\"]*\"/\"host\"\1\"${CLIENT_HOST}\"/" \
    "$REPO/scripts/scenario_configs/peer.json" > "$CFG"

XVFB_PID=""; CLIENT_PID=""
cleanup() {
    [ -n "$CLIENT_PID" ] && kill "$CLIENT_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

LOG="$OUT/client.log"; POSE="$OUT/pose.tsv"; MARKS="$OUT/marks.tsv"
printf 'wait 170\n/q\n' > "$OUT/script.txt"
Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2
bin_local="${PRESS_CHECK_BIN_DIR:-$HOME/.cache/willeq-press-check}/willeq"
mkdir -p "$(dirname "$bin_local")"
cp -f "$BIN" "$bin_local"
sudo -n setcap cap_net_raw,cap_net_admin+ep "$bin_local" 2>/dev/null || say "setcap failed (zone load may be flaky, B511)"
# A fresh cache dir per run: the per-character UI ini (T432) remembers an
# open inventory, so a shared cache would make the 'i' below CLOSE it.
mkdir -p "$OUT/cache"
( cd "$REPO" && DISPLAY="$DISP" WILLEQ_DATA_ROOT="$REPO" WILLEQ_CACHE_DIR="$OUT/cache" "$bin_local" -c "$CFG" \
    --renderer gles2 --egl-x11 -r 800 600 --no-audio --log-level=INFO --log-module=UI:DEBUG \
    --eqe1-path /mnt/projects/EverQuest_ETC1 \
    --pose-log "$POSE" --pose-every 1 --pose-max-frames 200000 \
    --script "$OUT/script.txt" --timeout 240 > "$LOG" 2>&1 ) &
CLIENT_PID=$!
for _ in $(seq 1 180); do
    grep -q "Graphics loading complete. Game ready" "$LOG" 2>/dev/null && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 1
done
grep -q "Graphics loading complete. Game ready" "$LOG" || { say "never reached Game ready"; exit 2; }
sleep 5
WID="$(DISPLAY="$DISP" xdotool search --name . 2>/dev/null | tail -1)"
X() { DISPLAY="$DISP" xdotool "$@"; }
X windowfocus "$WID" 2>/dev/null
sleep 1
shot() { DISPLAY="$DISP" import -window "$WID" "$OUT/$1.png" 2>/dev/null; }
mark() { printf '%s\t%s\n' "$1" "$(date +%s%3N)" >> "$MARKS"; }
at() { X mousemove --window "$WID" "${1%,*}" "${1#*,}"; }
# 20 px in 20 one-pixel steps, 30 ms apart, button held.
drag20() { for _ in $(seq 1 20); do X mousemove_relative -- 1 0; sleep 0.03; done; }

printf 'mark\tunix_ms\n' > "$MARKS"
X key i
sleep 2
if [ -n "$DISMISS" ]; then
    at "$DISMISS"; sleep 0.3; X click 1; sleep 1
fi
shot inventory_open
if [ "$EXPLORE" -eq 1 ]; then
    say "explore: screenshot $OUT/inventory_open.png (window $WID)"
    exit 0
fi

# --- ui_right: the #386 gesture ---
at "$ITEM"; sleep 0.5
mark ui_right_start
X mousedown 3; sleep 0.1
drag20
sleep 0.3; shot ui_right_held      # the item display, held open
X mouseup 3
sleep 0.5; mark ui_right_end
sleep 0.5; shot ui_right_released

# --- ui_left: a left drag on the window's background ---
at "$WBG"; sleep 0.5
mark ui_left_start
X mousedown 1; sleep 0.1
drag20
X mouseup 1
sleep 0.5; mark ui_left_end

# --- view_right: a right drag on the 3D view ---
at "$VIEW"; sleep 0.5
mark view_right_start
X mousedown 3; sleep 0.1
drag20
X mouseup 3
sleep 0.5; mark view_right_end

# --- view_cross: start on the view, end inside the window ---
at "$VIEW"; sleep 0.5
mark view_cross_start
X mousedown 3; sleep 0.1
vx="${VIEW%,*}"; vy="${VIEW#*,}"; cx="${CROSS%,*}"; cy="${CROSS#*,}"
for i in $(seq 1 20); do
    X mousemove --window "$WID" $(( vx + (cx - vx) * i / 20 )) $(( vy + (cy - vy) * i / 20 ))
    sleep 0.03
done
mark view_cross_inside
drag20
X mouseup 3
sleep 0.5; mark view_cross_end
shot after_all

say "gestures done; waiting for the client to quit"
wait "$CLIENT_PID" 2>/dev/null
CLIENT_PID=""
python3 -I "$REPO/scripts/eqemu/press_ownership_verdict.py" --pose "$POSE" --marks "$MARKS" \
    | tee "$OUT/verdict.txt"
exit "${PIPESTATUS[0]}"
