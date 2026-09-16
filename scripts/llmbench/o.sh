#!/usr/bin/env bash
# T492 official-client driver -- a COPY of review/T478/scripts/t478_o.sh with
# three changes, nothing else:
#   * output root and marks file come from the environment (LLMB_P1_DIR),
#     so one driver serves the dry run and the recording;
#   * marks are LOCAL time, not UTC. T478's driver stamped `date -u`, while
#     every willeq run.log stamps local time -- on a host that is not UTC the
#     two halves of the corpus cannot be joined at all (board B784). Both
#     hosts here happen to be UTC, so this is a latent-trap fix, not a
#     correction of T478's numbers.
#   * `pair <label>` also writes the OCR of the chat crop next to the frames.
#
# Usage: o.sh cmd "<chat text>" ["<expect regex>"]
#        o.sh key <xdotool keyspec>...       (1.5 s between keys)
#        o.sh hold <keyspec> <seconds>
#        o.sh click <x> <y>                  (SCREEN coords; window at -1,+30)
#        o.sh pair <label>                   (window 1023x738 + root 1024x768 + chat OCR)
#        o.sh ocr [crop]
#        o.sh video <label> <seconds>
#        o.sh geom
set -uo pipefail
source /mnt/projects/eq-oracle/env.sh
source /mnt/projects/eq-oracle/scripts/ui-lib.sh
export DISPLAY="$EQO_XVFB_DISPLAY"
P1="${LLMB_P1_DIR:?LLMB_P1_DIR (player p1 corpus dir) must be set}"
mkdir -p "$P1/frames" "$P1/chat" "$P1/video"
WID=$(xdotool search --name '^EverQuest$' | head -1)
CHATCROP="670x190+95+570"
MARKS="$P1/marks.tsv"
ts()   { date '+%H:%M:%S.%3N'; }
mark() { printf '%s\t%s\n' "$(ts)" "$*" >> "$MARKS"; }
focus() { xdotool windowfocus --sync "$WID" 2>/dev/null; xdotool windowactivate "$WID" 2>/dev/null; sleep 1; }
case "${1:?}" in
  cmd)
    text="$2"; expect="${3:-}"
    for a in 1 2 3; do
      focus
      xdotool key Return; sleep 1.5
      xdotool type --delay 70 "$text"; sleep 1.5
      xdotool key Return; sleep 3
      if [ -z "$expect" ]; then mark "cmd UNVERIFIED: $text"; echo "UNVERIFIED: $text"; exit 0; fi
      if wait_for_text "$expect" 6 "$CHATCROP"; then mark "cmd OK: $text"; echo "OK(attempt $a): $text"; exit 0; fi
      echo "RETRY(attempt $a): $text"; xdotool key Escape; sleep 1
    done
    mark "cmd FAILED: $text"; echo "FAILED: $text"; exit 1 ;;
  key)   focus; shift; for k in "$@"; do xdotool key "$k"; mark "key $k"; sleep 1.5; done; echo "keys sent: $*" ;;
  hold)  focus; xdotool keydown "$2"; sleep "$3"; xdotool keyup "$2"; mark "hold $2 $3"; echo "held $2 for $3 s" ;;
  click) focus; jclick "$2" "$3"; sleep 2; mark "click $2 $3"; echo "clicked $2 $3" ;;
  pair)  t="$(ts)"; lab="$2"
         timeout 25 import -window "$WID" "$P1/frames/${t}_${lab}_window.png" 2>/dev/null
         timeout 25 import -window root  "$P1/frames/${t}_${lab}_root.png"   2>/dev/null
         screen_text "$CHATCROP" > "$P1/chat/${t}.txt" 2>/dev/null
         printf '%s\t%s\n' "$t" "pair $lab" >> "$MARKS"
         echo "$t $(identify -format '%wx%h' "$P1/frames/${t}_${lab}_window.png" 2>/dev/null) $(identify -format '%wx%h' "$P1/frames/${t}_${lab}_root.png" 2>/dev/null)" ;;
  # T492 Leg F.  Drive the camera CLOSED LOOP: press F9 until a root grab
  # SCORES as the named camera against T466's direct-mode still, instead of
  # pressing once and writing down what the count implies.  Leg C/E drove it
  # open loop and one dropped keypress put the official client a whole step
  # behind the truth line for the rest of the cycle -- Overhead where the truth
  # said Chase, which is what Will found on the sheets.  The cycle itself is
  # measured and sound (review/T492/f9_cycle/: period 6, every preset
  # identified 0.058-0.191 with the next-best >= 0.784).
  camto) want="$2"; tries="${3:-9}"; thr="${4:-0.30}"
         refs=/mnt/projects/fable/review/T466/oracle
         ref="$refs/T466_oracle_h160_direct_${want}_noui_root_1024x768.png"
         [ -f "$ref" ] || { echo "camto: no reference still for $want"; exit 2; }
         tmp="$P1/.camto_probe.png"
         for a in $(seq 1 "$tries"); do
             timeout 25 import -window root "$tmp" 2>/dev/null
             sc=$(python3 /mnt/projects/fable/wt/t3/scripts/camera_match_score.py \
                    --oracle "$ref" --candidate "$tmp" \
                    --crop 1000x480+12+40 --candidate-crop 1000x480+12+40 2>/dev/null \
                  | awk 'END{print $3}')
             ok=$(python3 -c "print(1 if ${sc:-9} < $thr else 0)" 2>/dev/null)
             if [ "${ok:-0}" = "1" ]; then
                 mark "camto $want OK after $((a-1)) presses score=$sc"
                 echo "camto $want OK presses=$((a-1)) score=$sc"; exit 0
             fi
             focus; xdotool key F9; sleep 3
         done
         mark "camto $want FAILED after $tries probes last=$sc"
         echo "camto $want FAILED last=$sc"; exit 1 ;;
  # The official TARGET window's name field.  MEASURED on
  # review/T492/llmbench/players/p1/frames/15:27:43.497_C_target_p1_p2_window.png:
  # the box Will took for the target is the PLAYER's own window; the target name
  # sits at window crop 140x24+885+470 and OCRs there as "Testcaster".
  target) timeout 25 import -window "$WID" "$P1/.target_probe.png" 2>/dev/null
         t=$(convert "$P1/.target_probe.png" -crop 140x24+885+470 +repage \
               -colorspace Gray -resize 500% -threshold 50% png:- 2>/dev/null \
             | tesseract - - --psm 7 2>/dev/null | tr -d '\r\n' | sed 's/[^A-Za-z_ ]//g' | xargs)
         [ -n "$t" ] || t="unreadable"
         mark "target window reads: $t"; echo "$t" ;;
  geom)  xwininfo -id "$WID" | grep -E 'Absolute|Width|Height' ;;
  ocr)   screen_text "${2:-}" ;;
  video) f="$P1/video/$2.mkv"; mark "video begin $2 $3s"
         ffmpeg -y -f x11grab -framerate 30 -video_size 1024x768 -i "${EQO_XVFB_DISPLAY}" \
             -t "$3" -c:v libx264 -preset ultrafast -qp 0 "$f" >/dev/null 2>&1
         mark "video end $2"; echo "$f $(stat -c %s "$f" 2>/dev/null)" ;;
  *) echo "unknown"; exit 2 ;;
esac
