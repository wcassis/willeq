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
  geom)  xwininfo -id "$WID" | grep -E 'Absolute|Width|Height' ;;
  ocr)   screen_text "${2:-}" ;;
  video) f="$P1/video/$2.mkv"; mark "video begin $2 $3s"
         ffmpeg -y -f x11grab -framerate 30 -video_size 1024x768 -i "${EQO_XVFB_DISPLAY}" \
             -t "$3" -c:v libx264 -preset ultrafast -qp 0 "$f" >/dev/null 2>&1
         mark "video end $2"; echo "$f $(stat -c %s "$f" 2>/dev/null)" ;;
  *) echo "unknown"; exit 2 ;;
esac
