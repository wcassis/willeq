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

# B795: the Tip of the Day dialog.  MEASURED T492 Leg H -- it sits at window
# (664, 234) 360x300 (its own [TipWindow] geometry in the character UI ini),
# which is exactly where the TARGET window is, so the target-window OCR read
# "ow Another Tip" instead of a character name and the Leg H gate stopped the
# run.  Two fixes, belt and braces:
#   1. PERSISTENT: `[Tips] ShowTips=0` in the CHARACTER's UI ini.  Oraclealpha's
#      had no [Tips] section at all (tips default on); other characters shipped
#      in this client copy do have `ShowTips=0`, which is where the key is from.
#   2. RUNTIME: dismiss whatever is up and PROVE the region is clear by OCR
#      before the driver is allowed to proceed.
UIINI="$EQO_CLIENT_RUN/UI_Oraclealpha_WillEQ Private Server.ini"
if [ -f "$UIINI" ]; then
    [ -f "${UIINI}.t492-pristine" ] || cp "$UIINI" "${UIINI}.t492-pristine"
    if grep -q '^\[Tips\]' "$UIINI"; then
        sed -i 's/^ShowTips=.*/ShowTips=0/' "$UIINI"
    else
        printf '[Tips]\nShowTips=0\n' >> "$UIINI"
    fi
    mark "UI ini: [Tips] ShowTips=0 set for Oraclealpha"
fi

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
# The character-select scene takes ~2 min to render the first time under
# llvmpipe and clicks issued before it is up are dropped SILENTLY (hermes-ext
# note, 2026-09-13).  MEASURED T492: without this wait, enter-world.sh burned
# all four of its attempts (~90 s each -- `wait_for_text N` is N OCR polls, not
# N seconds) and reported "failed to enter world" for a client that was fine.
mark "settling at character select for 60 s before the first Enter World click"
sleep 60
ERC=1
for w in 1 2; do
    bash /mnt/projects/eq-oracle/scripts/enter-world.sh >"$SCRATCH/enter_${LABEL}_${w}.log" 2>&1
    ERC=$?; mark "enter-world pass ${w} exit=${ERC}"
    [ $ERC -eq 0 ] && break
    sleep 20
done
[ $ERC -ne 0 ] && { tail -20 "$SCRATCH/enter_${LABEL}_2.log"; exit 1; }
WID=$(xdotool search --name '^EverQuest$' | head -1)
xdotool windowfocus --sync "$WID" 2>/dev/null
xdotool windowactivate "$WID" 2>/dev/null
sleep 15

# --- B795 runtime half: dismiss the tip dialog and PROVE the region is clear --
TIPCROP="360x300+664+234"
tip_text() {
    timeout 25 import -window "$WID" "$SCRATCH/tip_probe.png" 2>/dev/null
    convert "$SCRATCH/tip_probe.png" -crop "$TIPCROP" +repage -colorspace Gray \
        -resize 300% -threshold 55% png:- 2>/dev/null | tesseract - - --psm 6 2>/dev/null | tr '\n' ' '
}
TIPRE='Another Tip|Tip of the|TipWindow|Show Another'
for a in 1 2 3 4; do
    t="$(tip_text)"
    if ! printf '%s' "$t" | grep -qiE "$TIPRE"; then
        mark "tip dialog: region clear on probe ${a} (OCR: $(printf '%s' "$t" | cut -c1-60))"
        break
    fi
    mark "tip dialog PRESENT on probe ${a} (OCR: $(printf '%s' "$t" | cut -c1-60)) -- dismissing"
    xdotool windowfocus --sync "$WID" 2>/dev/null
    xdotool key Escape; sleep 2
    # its close box is the top-right of the 360x300 panel at (664,234)
    jclick 1010 246; sleep 2
    [ "$a" -eq 4 ] && { echo "FATAL: tip dialog would not dismiss (OCR: $t)"; mark "tip dialog UNDISMISSED"; exit 1; }
done
timeout 25 import -window "$WID" "$P1/frames/$(date '+%H:%M:%S.%3N')_login_camera_untouched_window.png" 2>/dev/null
echo "WID=$WID"
mark "in world WID=$WID"
