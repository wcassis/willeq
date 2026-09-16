#!/usr/bin/env bash
# T492 official-client launcher -- a COPY of review/T478/scripts/t478_oracle_start.sh
# with paths taken from the environment and a pristine eqclient.ini restored
# before launch (the client writes its live camera state back into that file
# DURING play -- measured on hermes-ext 2026-09-16 -- so without this the next
# run does not start from the same camera).
# Usage: oracle_start.sh <account> <password> <label>
set -uo pipefail
source /mnt/projects/eq-oracle/env.sh
source /mnt/projects/eq-oracle/scripts/ui-lib.sh
ACCOUNT="${1:?account}"; PASSWORD="${2:?password}"; LABEL="${3:?label}"
P1="${LLMB_P1_DIR:?LLMB_P1_DIR must be set}"
SCRATCH="${LLMB_SCRATCH:?LLMB_SCRATCH must be set}/oracle"
mkdir -p "$SCRATCH" "$P1/frames"
export DISPLAY="$EQO_XVFB_DISPLAY"
export WINEARCH=win32 WINEDEBUG=fixme-all LP_NUM_THREADS=1
MARKS="$P1/marks.tsv"
mark() { printf '%s\t%s\n' "$(date '+%H:%M:%S.%3N')" "$*" | tee -a "$MARKS"; }

# Pristine eqclient.ini: kept beside the client copy the first time this runs.
PRISTINE="$EQO_CLIENT_RUN/eqclient.ini.t492-pristine"
if [ ! -f "$PRISTINE" ]; then
    cp "$EQO_CLIENT_RUN/eqclient.ini" "$PRISTINE"
    mark "eqclient.ini pristine copy CREATED from the live file"
else
    cp "$PRISTINE" "$EQO_CLIENT_RUN/eqclient.ini"
    mark "eqclient.ini restored from pristine copy"
fi
sha256sum "$PRISTINE" "$EQO_CLIENT_RUN/eqclient.ini" > "$P1/eqclient_ini_at_launch.sha256"

pkill -f "wine.*eqgame" >/dev/null 2>&1
pkill -f "Xvfb ${EQO_XVFB_DISPLAY}" >/dev/null 2>&1
sleep 5
Xvfb "$EQO_XVFB_DISPLAY" -screen 0 1024x768x24 +extension GLX +render -noreset \
    >"$SCRATCH/xvfb_${LABEL}.log" 2>&1 &
sleep 4
xdpyinfo >/dev/null 2>&1 || { echo "FATAL: no display"; exit 1; }
mark "login begin ${ACCOUNT}"
bash /mnt/projects/eq-oracle/scripts/login-as.sh "$ACCOUNT" "$PASSWORD" \
    >"$SCRATCH/login_${LABEL}.log" 2>&1
LRC=$?; mark "login exit=${LRC}"
[ $LRC -ne 0 ] && { tail -20 "$SCRATCH/login_${LABEL}.log"; exit 1; }
bash /mnt/projects/eq-oracle/scripts/enter-world.sh >"$SCRATCH/enter_${LABEL}.log" 2>&1
ERC=$?; mark "enter-world exit=${ERC}"
[ $ERC -ne 0 ] && { tail -20 "$SCRATCH/enter_${LABEL}.log"; exit 1; }
WID=$(xdotool search --name '^EverQuest$' | head -1)
xdotool windowfocus --sync "$WID" 2>/dev/null
xdotool windowactivate "$WID" 2>/dev/null
sleep 15
timeout 25 import -window "$WID" "$P1/frames/$(date '+%H:%M:%S.%3N')_login_camera_untouched_window.png" 2>/dev/null
echo "WID=$WID"
mark "in world WID=$WID"
