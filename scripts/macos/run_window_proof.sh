#!/bin/bash
# B1044 (#364): the on-screen window + input proof for the macOS GLES2 client.
#
# Run it as the CONSOLE user, in Terminal, from a staged bundle
# (scripts/macos/stage_window_bundle.sh writes one to /Users/Shared/willeq-b1044):
#
#     bash /Users/Shared/willeq-b1044/run_window_proof.sh
#
# Two legs, both with --egl-window (the Cocoa window, CIrrDeviceFB_Cocoa.mm):
#   A. gles2_zone_benchmark renders qeynos2 in a 1280x720 window for a fixed
#      number of frames (login-free: the benchmark's offline zone load) and the
#      script screenshots it -> out/window.png (+ out/window_only.png).
#   B. willeq itself opens its 1280x720 window (loading screen / login; a live
#      server is optional, WILLEQ_PROOF_HOST) and YOU press keys, click,
#      move and scroll; the device prints one [cocoa-input] line per event
#      (WILLEQ_INPUT_TRACE=1) and the client one [INPUT-EVENT] line
#      (--log-module=INPUT:TRACE) -> out/client.log, out/client_window.png.
# On exit out/summary.txt says whether each window was created (and its
# size), the per-kind key/mouse event counts, and the exit codes.
#
# Nothing here needs sudo. Over ssh (no WindowServer session) it stops at the
# first check with "no WindowServer session: run this at the console".

set -u
# --leg a | --leg b runs one leg only (default: both).
LEG=both
[ "${1:-}" = "--leg" ] && LEG="${2:-both}"
BUNDLE="$(cd "$(dirname "$0")" && pwd)"

# 1. A window needs this user's Aqua (WindowServer) session. Checked FIRST,
#    so a run that cannot show a window (ssh) never touches out/.
MGR="$(launchctl managername 2>/dev/null || echo unknown)"
if [ "$MGR" != "Aqua" ]; then
    echo "window created (benchmark): no"
    echo "window created (client): no"
    echo "RESULT: no WindowServer session: run this at the console (launchctl managername=$MGR)"
    exit 3
fi

OUT="${WILLEQ_PROOF_OUT:-$BUNDLE/out}"
mkdir -p "$OUT" 2>/dev/null
if ! touch "$OUT/.w" 2>/dev/null; then
    OUT="$HOME/willeq-b1044-out"; mkdir -p "$OUT"
fi
rm -f "$OUT/.w"
# This user's own previous results only; another user's files stay.
find "$OUT" -maxdepth 1 -type f -user "$(id -un)" \( -name '*.png' -o -name '*.log' \
    -o -name 'summary*.txt' -o -name 'willeq_window.json' \) -delete 2>/dev/null
# Each run writes its own summary file, so a stale summary.txt owned by
# another user can never block it; summary.txt is refreshed when possible.
SUMMARY="$OUT/summary_$(date -u +%Y%m%dT%H%M%SZ).txt"
: > "$SUMMARY"
trap 'cp -f "$SUMMARY" "$OUT/summary.txt" 2>/dev/null || true' EXIT
note() { echo "$*"; echo "$*" >> "$SUMMARY"; }

note "B1044 window proof  $(date -u +%Y-%m-%dT%H:%M:%SZ)  user=$(id -un)  host=$(hostname -s)"
note "bundle=$BUNDLE out=$OUT"

# 2. The assets: /Volumes/projects (Will's console session), else ~/projects.
ASSETS=""
for d in /Volumes/projects "$HOME/projects"; do
    if [ -d "$d/EverQuestP1999" ] && [ -d "$d/EverQuest_ETC1" ]; then ASSETS="$d"; break; fi
done
if [ -z "$ASSETS" ]; then
    note "RESULT: assets not found: neither /Volumes/projects nor ~/projects has EverQuestP1999 + EverQuest_ETC1"
    note "        (mount the projects share in Finder, then re-run)"
    exit 4
fi
note "assets=$ASSETS (EverQuestP1999 + EverQuest_ETC1 found there)"

export WILLEQ_CACHE_DIR="$HOME/Library/Caches/willeq-b1044"
mkdir -p "$WILLEQ_CACHE_DIR"
export WILLEQ_INPUT_TRACE=1
BIN="$BUNDLE/bin"

# Wait (up to $3 s) for a line matching $2 in log $1; print the line.
wait_line() {
    local log="$1" pat="$2" limit="$3" i=0
    while [ "$i" -lt "$limit" ]; do
        if grep -q "$pat" "$log" 2>/dev/null; then grep -m1 "$pat" "$log"; return 0; fi
        sleep 1; i=$((i + 1))
    done
    return 1
}
win_number() { sed -n 's/^\[cocoa-window\] created window=\([0-9]*\).*/\1/p' "$1" | head -1; }
stamp() { date -u +%H:%M:%SZ; }
# shoot <window number> <name>: full screen <name>.png + the window alone
# <name>_only.png; notes each file that was written, with the time.
shoot() {
    local t; t="$(stamp)"
    screencapture -x "$OUT/$2.png" 2>/dev/null
    [ -s "$OUT/$2.png" ] && note "  screenshot $t out/$2.png (full screen)"
    if [ -n "$1" ]; then
        screencapture -x -o -l "$1" "$OUT/${2}_only.png" 2>/dev/null
        [ -s "$OUT/${2}_only.png" ] && note "  screenshot $t out/${2}_only.png (window $1 only)"
    fi
    return 0
}

# ---------------------------------------------------------------- leg A
if [ "$LEG" != b ]; then
echo
echo "=== Leg A: gles2_zone_benchmark in a window (hands off, about 30 s) ==="
echo "leg A: the window renders qeynos2, then HOLDS the last frame for 20 s and"
echo "       closes by itself -- that close is the end of leg A, not a crash."
BLOG="$OUT/benchmark.log"
note "benchmark start $(stamp)"
"$BIN/gles2_zone_benchmark" --egl-window --hold-seconds 20 --zone qeynos2 \
    --eq-path "$ASSETS/EverQuestP1999" --eqe1-path "$ASSETS/EverQuest_ETC1" \
    --frames 60 --warmup 5 --res 1280 720 \
    --occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count \
    --player-light-level 9 --camera-pose 201.9,393.4,5.0,180.0,-5.0 \
    > "$BLOG" 2>&1 &
BPID=$!
if wait_line "$BLOG" '^\[window-hold\] holding' 120 >/dev/null; then
    WIN="$(win_number "$BLOG")"
    note "benchmark frames presented; hold started $(stamp)"
    sleep 2; shoot "$WIN" window
    sleep 3; shoot "$WIN" window_2
else
    note "benchmark never reached its hold (see out/benchmark.log)"
fi
wait "$BPID"; BRC=$?
note "benchmark end $(stamp)"
BWIN="$(grep -m1 '^\[cocoa-window\] created' "$BLOG")"
if [ -n "$BWIN" ]; then note "window created (benchmark): yes  ${BWIN#\[cocoa-window\] created }"
else note "window created (benchmark): no  (see out/benchmark.log; EGL lines: $(grep -m2 'EGL (' "$BLOG" | tr '\n' ' '))"; fi
note "benchmark frames: $(grep -m1 'Zone Benchmark Summary' "$BLOG")  $(grep -m1 'Avg FPS' "$BLOG" | sed 's/^ *//')"
note "benchmark exit code: $BRC  screenshots: $(cd "$OUT" && ls window*.png 2>/dev/null | tr '\n' ' ')"
fi
[ "$LEG" = a ] && { echo "Done (leg A only). Summary: $SUMMARY"; exit 0; }

# ---------------------------------------------------------------- leg B
CFG="$OUT/willeq_window.json"
HOST="${WILLEQ_PROOF_HOST:-127.0.0.1}"
cat > "$CFG" <<EOF
{
  "clients": [
    {
      "host": "$HOST",
      "port": 5998,
      "user": "${WILLEQ_PROOF_USER:-willeq_test}",
      "pass": "${WILLEQ_PROOF_PASS:-no-server-proof}",
      "server": "WillEQ",
      "character": "${WILLEQ_PROOF_CHAR:-Testchar}",
      "eq_client_path": "$ASSETS/EverQuestP1999",
      "maps_path": "$ASSETS/maps/base",
      "navmesh_path": "$ASSETS/maps/nav"
    }
  ]
}
EOF
echo
echo "=== Leg B: the willeq client in a window (you drive this one) ==="
echo "A 1280x720 'WillEQ' window opens (it shows 'Connecting to login server')."
echo "FIRST click once INSIDE the WillEQ window, so it -- not Terminal -- gets the keys; then:"
echo "  1. press and release W, A, S, D (one at a time), then Escape"
echo "  2. hold Shift and press W; hold Control and press A"
echo "  3. move the mouse around inside the window"
echo "  4. left-click, right-click inside the window"
echo "  5. scroll the wheel (or two-finger scroll) up and down"
echo "  6. optionally drag the window corner to resize it"
echo "  7. then quit: Command-Q in the window (or its red close button,"
echo "     or Ctrl-C here). It stops by itself after 15 min."
echo "(server: $HOST -- without a running server the login screen stays up; that is fine)"
CLOG="$OUT/client.log"
"$BIN/willeq" --egl-window -r 1280 720 -c "$CFG" --eqe1-path "$ASSETS/EverQuest_ETC1" \
    --log-level=INFO --log-module=INPUT:TRACE > "$CLOG" 2>&1 &
CPID=$!
trap 'kill -TERM "$CPID" 2>/dev/null' INT
if wait_line "$CLOG" '^\[cocoa-window\] created' 90 >/dev/null; then
    WIN="$(win_number "$CLOG")"
    note "client window up $(stamp); waiting for you (Command-Q ends leg B)"
    sleep 8
    kill -0 "$CPID" 2>/dev/null && shoot "$WIN" client_window
fi
( sleep 900; kill -TERM "$CPID" 2>/dev/null ) &
TPID=$!
wait "$CPID"; CRC=$?
kill "$TPID" 2>/dev/null
trap - INT

CWIN="$(grep -m1 '^\[cocoa-window\] created' "$CLOG")"
if [ -n "$CWIN" ]; then note "window created (client): yes  ${CWIN#\[cocoa-window\] created }"
else note "window created (client): no  (see out/client.log)"; fi
count() { grep -c "$1" "$CLOG" 2>/dev/null || true; }
note "client device events: key_down=$(count '^\[cocoa-input\] key down') key_up=$(count '^\[cocoa-input\] key up')" \
     "mouse_move=$(count '^\[cocoa-input\] mouse move') mouse_down=$(count '^\[cocoa-input\] mouse down')" \
     "mouse_up=$(count '^\[cocoa-input\] mouse up') wheel=$(count '^\[cocoa-input\] mouse wheel')" \
     "resize=$(count '^\[cocoa-input\] resize')"
note "client receiver lines ([INPUT-EVENT], MOD_INPUT TRACE): key=$(count 'INPUT-EVENT\] key') mouse=$(count 'INPUT-EVENT\] mouse')"
note "client window focus: became key $(count '^\[cocoa-window\] key window: yes') time(s), lost key $(count '^\[cocoa-window\] key window: no') time(s); at creation: $(grep -m1 '^\[cocoa-window\] created' "$CLOG" | sed -n 's/.*\(active=[01] key=[01]\).*/\1/p')"
note "device summary: $(grep -m1 '^\[cocoa-input\] summary' "$CLOG" | sed 's/^\[cocoa-input\] summary //')"
note "keys seen (irr codes, down): $(sed -n 's/^\[cocoa-input\] key down code=0x\([0-9a-f]*\) irr=\([0-9]*\).*/\2/p' "$CLOG" | sort -n | uniq -c | awk '{printf "%s x%s, ", $2, $1}')"
note "client end $(stamp)  exit code: $CRC  screenshots: $(cd "$OUT" && ls client_window*.png 2>/dev/null | tr '\n' ' ')"
echo
echo "Done. Everything is in $OUT (summary.txt first)."
