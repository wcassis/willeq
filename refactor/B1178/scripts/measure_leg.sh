#!/usr/bin/env bash
# B1178 v3: measure one leg's spells from video-caster.mp4 (caster display, the
# measurement surface per C2). Per spell: cut a clip around the cast (stream
# copy, -copyts keeps original pts), run fx_measure (B1118 crop 150,60,900,540)
# + fx_timing (model 214 px: head row 388 motion / feet 602 figure_rows,
# cross-checked by vision 385/600). Prints LANDING coverage/end + ROI p90s.
set -uo pipefail
LEG=$1; MROOT=$2; MODEL=${3:-214}
T0=$(cat "$LEG/video-caster.mp4.t0")
S=/home/user/kb-evidence/kb-b1178-oracle-refs/scripts
M="$MROOT/$(basename "$LEG")"; mkdir -p "$M"

# usage: measure <spell> <t_bc> <t_act>   (epochs)
measure() {
  local s=$1 tbc=$2 tact=$3
  local clip="$M/clip$s.mp4"
  local clipstart clipend off
  clipstart=$(python3 -c "print($tbc - $T0 - 2.5)")
  clipend=$(python3 -c "print($tact - $T0 + 12.0)")
  off=$(python3 -c "print($tbc - 2.5)")
  ffmpeg -nostdin -loglevel error -ss "$clipstart" -to "$clipend" \
    -i "$LEG/video-caster.mp4" -c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p "$clip" || { echo "clip $s FAILED"; return 1; }
  python3 /mnt/projects/fable/review/B1118/scripts/fx_measure.py "$clip" "$off" "$tbc" "$tact" "$(python3 -c "print($tact+10)")" \
    "150,60,900,540" "$M/spell$s" --label "v3$s" >"$M/spell$s.measure.txt" 2>&1
  python3 /mnt/projects/fable/review/B1118/scripts/fx_timing.py "$M/spell$s" "$MODEL" | tee "$M/spell$s.timing.txt"
  local CAST; CAST=$(python3 -c "print($tact-$tbc-0.05)")
  echo "--- roi (model $MODEL, oracle-crop box 114,765,240,590):"
  ROI=114,765,240,590 python3 "$S/fx_sprite_roi.py" "$M/spell$s" "$MODEL" SOURCE 0.3 "$CAST" >>"$M/spell$s.timing.txt" 2>&1 | tee -a "$M/spell$s.timing.txt"
  ROI=114,765,240,590 python3 "$S/fx_sprite_roi.py" "$M/spell$s" "$MODEL" LANDING 1.2 1.8 >>"$M/spell$s.timing.txt" 2>&1 | tee -a "$M/spell$s.timing.txt"
  rm -f "$M/spell$s"/f*.png
}

# spells passed as: spell tbc tact ...
shift 3 2>/dev/null || true
for args in "$@"; do
  measure $args
done
echo "=== $LEG done ==="
