#!/usr/bin/env bash
# B1214 spike launcher on eq-oracle-1: Xvfb :99, MQ2 loader, then eqgame from the COPIED prefix/client.
set -uo pipefail
B=/home/user/eq-oracle/b1214
RUN=$B/run; mkdir -p "$RUN"
export DISPLAY=:99 WINEPREFIX=$B/wineprefix WINEARCH=win32 WINEDEBUG=${WINEDEBUG:--all} LP_NUM_THREADS=1
if ! xdpyinfo -display :99 >/dev/null 2>&1; then
  Xvfb :99 -screen 0 1280x1024x24 +extension GLX +render -noreset >"$RUN/xvfb.log" 2>&1 &
  echo $! >"$RUN/xvfb.pid"; sleep 2
fi
date -u +%FT%T.%3NZ >"$RUN/launch.epoch"
if [[ ${NO_MQ2:-0} != 1 ]]; then
  ( cd "$WINEPREFIX/drive_c/MQ2" && nohup wine MacroQuest2.exe >"$RUN/mq2-loader.log" 2>&1 </dev/null & echo $! >"$RUN/mq2.pid" )
  sleep 8
fi
( cd "$B/client" && nohup wine eqgame.exe patchme >"$RUN/wine.log" 2>&1 </dev/null & echo $! >"$RUN/wine.pid" )
echo launched
