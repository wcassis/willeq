#!/usr/bin/env bash
# B1170 (#489): the re-gate renders -- tour-7 + the 40 records on --camera
# b1118 and the 40 records on B1155's b1130 rig -- with ONE model_viewer
# binary, exactly the plans and command lines B1157 used
# (refactor/B1157/scripts/run_sweep_b1118.sh / run_sweep_b1130.sh).
# RUN ON THE THINKPAD: ssh 172.16.200.205 bash <this> <model_viewer> <side>
# Frames land in /home/user/kb-frames/kb-b1170-missile-exact/<side>/<tag>/
# (local NVMe; regate_identity.py compares the two sides there and only the
# runs that differ are copied to the NFS evidence tree and measured).
set -u
BIN=$1; SIDE=$2
WT=/mnt/projects/fable/main/wt/kb-b1170-missile-exact
BASE=/home/user/kb-frames/kb-b1170-missile-exact/$SIDE
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
mkdir -p "$BASE/plans"
LOG=$BASE/regate.log
: > "$LOG"
render() {  # tag spell target cast_ms level camera secs [sim_hz]
  local tag=$1 sid=$2 tgt=$3 cms=$4 lvl=$5 cam=$6 secs=$7 sim=${8:-}
  local plan=$BASE/plans/$tag.plan run=$BASE/$tag
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' \
    "$sid" "$tgt" "$cms" "$lvl" > "$plan"
  rm -rf "$run"; mkdir -p "$run"
  ( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$plan" --frames-dir "$run" --fps 30 --seconds "$secs" ${sim:+--sim-hz $sim} \
      --seed 1 --headless --camera "$cam" > "$run/run.log" 2>&1 )
  echo "$tag rc=$? frames=$(ls "$run" | grep -c '^frame_')" >> "$LOG"
}
# b1118 tour-7 (B1157's rows: the record representatives' cast_ms).
while read -r sid tgt cms; do
  render "b1118_tour_${sid}" "$sid" "$tgt" "$cms" 60 b1118 "$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')"
done <<'ROWS'
93 other 1500
38 other 1500
54 other 1500
656 other 1500
41 other 1500
253 self 1750
458 other 3150
ROWS
# b1118 40 records (B1152's plan list).
while IFS=$'\t' read -r rec sid tgt cms; do
  [ -z "${rec:-}" ] && continue
  render "b1118_r${rec}_${sid}" "$sid" "$tgt" "$cms" 60 b1118 "$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')"
done < "$WT/refactor/B1152/plans/sweep/list.tsv"
# b1130 40 records (B1157's run_sweep_b1130.sh rules).
awk -F'\t' 'NR > 1 && $1 != "51" {print $1"\t"$6"\t"$12"\t"$11"\t"$10"\t"$9}' "$TABLE" |
while IFS=$'\t' read -r rec sid captures targettype cms lvl; do
  kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
  tgt=other; case "$targettype" in self|group|group-teleport) tgt=self ;; esac
  render "b1130_${kind}_r${rec}" "$sid" "$tgt" "$cms" "$lvl" "$cam" \
    "$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')" 60
done
echo "REGATE DONE $SIDE" >> "$LOG"
