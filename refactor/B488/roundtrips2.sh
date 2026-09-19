#!/usr/bin/env bash
# B488 repeatability (volume-safe): every check anchors on a line-number mark,
# so it is immune to how fast DEBUG lines scroll the log.
set -uo pipefail
LOG=/mnt/projects/fable/review/kb-b488/client2.log
say() {
  local wid; wid=$(DISPLAY=:77 xdotool search --name 'WillEQ' | head -1)
  [ -z "$wid" ] && { echo "FATAL: no window"; return 1; }
  DISPLAY=:77 xdotool key --window "$wid" --clearmodifiers Return; sleep 0.8
  DISPLAY=:77 xdotool type --window "$wid" --delay 40 "$1"; sleep 0.8
  DISPLAY=:77 xdotool key --window "$wid" --clearmodifiers Return
}
after() {  # after <mark> <fixed-string> -> true when it exists on a later line
  awk -v m="$1" -v s="$2" 'NR>m && index($0, s) {found=1} END {exit !found}' "$LOG"
}
SEQ="airplane qeynos2 airplane qeynos2 airplane"
i=0
for z in $SEQ; do
  i=$((i+1))
  mark=$(wc -l < "$LOG")
  say "#zone $z"
  ok=0
  for n in $(seq 1 60); do
    if after "$mark" "Game state setup complete"; then
      st=$(awk -v m="$mark" 'NR>m && index($0,"Game state setup complete") {print; exit}' "$LOG")
      case "$st" in *"Zone: $z "*) ok=1; break;; esac
    fi
    sleep 2
  done
  [ $ok -eq 1 ] || { echo "STUCK: no 'Game state setup complete ... Zone: $z' after line $mark"; exit 1; }
  ok=0
  for n in $(seq 1 60); do
    if after "$mark" "Phase 15/15"; then ok=1; break; fi
    sleep 2
  done
  [ $ok -eq 1 ] || { echo "STUCK: Phase 15/15 never followed the zone-in to $z"; exit 1; }
  echo "round $i -> $z: state+Phase 15/15 OK ($(grep -cF 'Game state setup complete' <(awk -v m="$mark" 'NR>m' "$LOG")))  [$(date +%H:%M:%S)]"
  sleep 3
done
echo "ALL ROUND TRIPS OK"
