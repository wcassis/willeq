#!/bin/bash
LOGFILE="u15.log"
TIMEOUT=60
DISPLAY=:99 timeout $TIMEOUT ./build/bin/willeq -c ../summonah.json --no-audio --constrained 128x32x10 -r 1280 720 -d 6 > "$LOGFILE" 2>&1
echo "Exit code: $?" >> "$LOGFILE"
