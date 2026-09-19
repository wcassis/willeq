#!/usr/bin/env bash
# B488 -- airplane zone-in stuck. One live rig:
#   1. take the eqemu lock (or fail)
#   2. stack up via up.sh --launcher (the launcher fleet is what serves dynamic
#      zones like airplane; the plain path only boots one static zone)
#   3. launch willeq on a private Xvfb with --egl-x11
#   4. drive it with xdotool: #level 50, /loc, #zone airplane
#   5. leave the stack UP + lock HELD -- the caller inspects, then calls
#      run_b488.sh down.
#
#   run_b488.sh up     -- lock + stack up + client running (prints PIDs)
#   run_b488.sh say "<text>"
#   run_b488.sh wait-phase [n]  -- poll the client log for a loading phase
#   run_b488.sh server-logs -- dump world/zone/launcher tails
#   run_b488.sh down    -- kill client, stack down, release lock
set -uo pipefail
REPO=/home/user/src/willeq-wt/kb-b488-airplane-zone-stuck
OUT=/mnt/projects/fable/review/kb-b488
CFG="$OUT/willeq_b488_Testchar.json"
LOG="$OUT/client.log"
XLOG="$OUT/xvfb.log"
DISP=:77
WILLEQ="$REPO/build/bin/willeq"

mkdir -p "$OUT"

cat > "$CFG" <<EOF
{
  "clients": [
    {
      "host": "172.16.200.99",
      "port": 5998,
      "user": "willeq_test",
      "pass": "WillEqH03Pass",
      "server": "WillEQ",
      "character": "Testchar",
      "eq_client_path": "/mnt/projects/EverQuestP1999",
      "maps_path": "/mnt/projects/maps/base",
      "navmesh_path": "/mnt/projects/maps/nav"
    }
  ]
}
EOF

say() {
  # type one chat line into the fable window (Return opens input, text, Return submits)
  local text="$1"; shift
  local wid
  wid=$(DISPLAY="$DISP" xdotool search --name 'WillEQ' | head -1)
  [ -z "$wid" ] && { echo "FATAL: no fable window on $DISP" >&2; return 1; }
  DISPLAY="$DISP" xdotool key --window "$wid" --clearmodifiers Return 2>/dev/null
  sleep 0.8
  DISPLAY="$DISP" xdotool type --window "$wid" --delay 40 "$text" 2>/dev/null
  sleep 0.8
  DISPLAY="$DISP" xdotool key --window "$wid" --clearmodifiers Return 2>/dev/null
  sleep "${1:-2}"
  echo "said: $text"
}

case "${1:-}" in
  up)
    source "$REPO/scripts/eqemu/env.sh"
    if ! eqemu_lock_take "kb-b488-airplane-zone-stuck"; then
      echo "LOCK HELD by: $(eqemu_lock_show)" >&2; exit 64
    fi
    echo "lock taken"
    if ! bash "$REPO/scripts/eqemu/up.sh" --launcher > "$OUT/up.log" 2>&1; then
      echo "up.sh failed:"; tail -30 "$OUT/up.log" >&2
      eqemu_lock_release; exit 65
    fi
    pkill -f "Xvfb $DISP " 2>/dev/null; rm -f "/tmp/.X${DISP#:}-lock"
    Xvfb "$DISP" -screen 0 1024x768x24 +extension GLX +render -noreset > "$XLOG" 2>&1 &
    sleep 3
    DISPLAY="$DISP" "$WILLEQ" -c "$CFG" --egl-x11 -r 1024 768 \
      --log-level=DEBUG > "$LOG" 2>&1 &
    echo "WILLEQ_PID=$!" | tee "$OUT/willeq.pid"
    echo "client launched on $DISP"
    ;;
  say)
    say "${2:?}" "${3:-3}"
    ;;
  wait-phase)
    local_n="${2:-15}"
    for i in $(seq 1 120); do
      line=$(grep -E "\[LOADING\]\[Phase ${local_n}/15\]" "$LOG" | tail -1)
      [ -n "$line" ] && { echo "$line"; exit 0; }
      sleep 2
    done
    echo "TIMEOUT waiting for Phase ${local_n}/15; last phases:" >&2
    grep -E '\[LOADING\]\[Phase' "$LOG" | tail -5 >&2
    exit 2
    ;;
  server-logs)
    source "$REPO/scripts/eqemu/env.sh"
    server_exec "ls -la logs/ | head -40"
    echo "=== world (tail 60) ==="
    server_exec "tail -60 logs/world_h03.log" 2>&1 || server_exec "tail -60 \$(ls -t logs/world*.log 2>/dev/null | head -1)"
    echo "=== zones ==="
    server_exec "ls logs/zone_*.log 2>/dev/null"
    server_exec "ps ax | grep -E 'bin/(zone|world|loginserver|spire)' | grep -v grep"
    ;;
  down)
    source "$REPO/scripts/eqemu/env.sh"
    pkill -f "$WILLEQ" 2>/dev/null; sleep 2
    pkill -f "Xvfb $DISP " 2>/dev/null
    bash "$REPO/scripts/eqemu/down.sh" > "$OUT/down.log" 2>&1
    eqemu_lock_release
    echo "stack down, lock released: $(eqemu_lock_show; echo '(free)' )"
    ;;
  *)
    echo "usage: $0 {up|say <text> [settle]|wait-phase [n]|server-logs|down}" >&2
    exit 64
    ;;
esac
