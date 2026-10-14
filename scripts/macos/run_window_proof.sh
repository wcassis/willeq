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
BUNDLE="$(cd "$(dirname "$0")" && pwd)"
OUT="${WILLEQ_PROOF_OUT:-$BUNDLE/out}"
mkdir -p "$OUT" 2>/dev/null
if ! touch "$OUT/.w" 2>/dev/null; then
    OUT="$HOME/willeq-b1044-out"; mkdir -p "$OUT"
fi
rm -f "$OUT/.w"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
note() { echo "$*"; echo "$*" >> "$SUMMARY"; }

note "B1044 window proof  $(date -u +%Y-%m-%dT%H:%M:%SZ)  user=$(id -un)  host=$(hostname -s)"
note "bundle=$BUNDLE out=$OUT"

# 1. A window needs this user's Aqua (WindowServer) session.
MGR="$(launchctl managername 2>/dev/null || echo unknown)"
if [ "$MGR" != "Aqua" ]; then
    note "window created (benchmark): no"
    note "window created (client): no"
    note "RESULT: no WindowServer session: run this at the console (launchctl managername=$MGR)"
    exit 3
fi

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

# Wait for a [cocoa-window] line in $1 (up to $2 s); print the window number.
wait_window() {
    local log="$1" limit="$2" i=0
    while [ "$i" -lt "$limit" ]; do
        if grep -q '^\[cocoa-window\] created' "$log" 2>/dev/null; then
            sed -n 's/^\[cocoa-window\] created window=\([0-9]*\).*/\1/p' "$log" | head -1
            return 0
        fi
        sleep 1; i=$((i + 1))
    done
    return 1
}
shoot() {  # shoot <window number> <name>
    screencapture -x "$OUT/$2.png" 2>/dev/null
    [ -n "$1" ] && screencapture -x -o -l "$1" "$OUT/${2}_only.png" 2>/dev/null
}

# ---------------------------------------------------------------- leg A
echo
echo "=== Leg A: gles2_zone_benchmark in a window (about 30 s, no input needed) ==="
BLOG="$OUT/benchmark.log"
"$BIN/gles2_zone_benchmark" --egl-window --zone qeynos2 \
    --eq-path "$ASSETS/EverQuestP1999" --eqe1-path "$ASSETS/EverQuest_ETC1" \
    --frames 3000 --warmup 5 --res 1280 720 \
    --occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count \
    --player-light-level 9 --camera-pose 201.9,393.4,5.0,180.0,-5.0 \
    > "$BLOG" 2>&1 &
BPID=$!
WIN="$(wait_window "$BLOG" 60)"
# The frame rate decides how long the 3000 frames last (vsync'd: about 50 s;
# uncapped: a few s), so shoot every 2 s while it runs; the last one is kept.
if [ -n "$WIN" ]; then
    n=0
    while [ "$n" -lt 10 ] && kill -0 "$BPID" 2>/dev/null; do
        sleep 2; n=$((n + 1))
        kill -0 "$BPID" 2>/dev/null && shoot "$WIN" "window_$n" && cp -f "$OUT/window_$n.png" "$OUT/window.png" \
            && { [ -s "$OUT/window_${n}_only.png" ] && cp -f "$OUT/window_${n}_only.png" "$OUT/window_only.png"; true; }
    done
fi
wait "$BPID"; BRC=$?
BWIN="$(grep -m1 '^\[cocoa-window\] created' "$BLOG")"
if [ -n "$BWIN" ]; then note "window created (benchmark): yes  ${BWIN#\[cocoa-window\] created }"
else note "window created (benchmark): no  (see out/benchmark.log; EGL lines: $(grep -m2 'EGL (' "$BLOG" | tr '\n' ' '))"; fi
note "benchmark exit code: $BRC  screenshot: $( [ -s "$OUT/window.png" ] && echo out/window.png || echo none)"

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
      "pass": "${WILLEQ_PROOF_PASS:-}",
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
echo "A 1280x720 'WillEQ' window opens. Click into it once, then:"
echo "  1. press and release W, A, S, D (one at a time), then Escape"
echo "  2. hold Shift and press W; hold Control and press A"
echo "  3. move the mouse around inside the window"
echo "  4. left-click, right-click inside the window"
echo "  5. scroll the wheel (or two-finger scroll) up and down"
echo "  6. optionally drag the window corner to resize it"
echo "  7. then quit: Command-Q in the window (or its red close button,"
echo "     or Ctrl-C here). It stops by itself after 300 s."
echo "(server: $HOST -- without a running server the login screen stays up; that is fine)"
CLOG="$OUT/client.log"
"$BIN/willeq" --egl-window -r 1280 720 -c "$CFG" --eqe1-path "$ASSETS/EverQuest_ETC1" \
    --log-level=INFO --log-module=INPUT:TRACE > "$CLOG" 2>&1 &
CPID=$!
trap 'kill -TERM "$CPID" 2>/dev/null' INT
WIN="$(wait_window "$CLOG" 90)"
if [ -n "$WIN" ]; then
    sleep 8
    kill -0 "$CPID" 2>/dev/null && shoot "$WIN" client_window
fi
( sleep 300; kill -TERM "$CPID" 2>/dev/null ) &
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
note "device summary: $(grep -m1 '^\[cocoa-input\] summary' "$CLOG" | sed 's/^\[cocoa-input\] summary //')"
note "keys seen (irr codes, down): $(sed -n 's/^\[cocoa-input\] key down code=0x\([0-9a-f]*\) irr=\([0-9]*\).*/\2/p' "$CLOG" | sort -n | uniq -c | awk '{printf "%s x%s, ", $2, $1}')"
note "client exit code: $CRC  screenshot: $( [ -s "$OUT/client_window.png" ] && echo out/client_window.png || echo none)"
echo
echo "Done. Everything is in $OUT (summary.txt first)."
