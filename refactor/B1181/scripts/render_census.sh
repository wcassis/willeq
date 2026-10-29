#!/usr/bin/env bash
# B1181 fix leg: render the gate census (tour7 + b1130, 47 tags) on the
# ThinkPad for BOTH the pre-B1181 base (ac8c26bc6) and the MR tip, frames to
# /home/user/kb-frames/kb-b1181-item-missile/gate/{base,tip}/<tag>.
# Render command = refactor/B1186/scripts/run_b1186.sh row_job render step
# (no measure step), plans from gate_census/gen_plans.sh (same recipe).
set -u
G=/home/user/kb-frames/kb-b1181-item-missile/gate
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
ROWS=/home/user/kb-evidence/kb-b1181-item-missile/gate_census/rows.tsv
LOG=/home/user/kb-evidence/kb-b1181-item-missile/gate_census/render.log
: > "$LOG"
export G CLIENT PACK LOG

job() { # $1=tree $2=tag $3=cam $4=secs $5=drm
  local tree=$1 tag=$2 cam=$3 secs=$4 drm=$5
  local WT BIN
  case $tree in
    base) WT=/home/user/src/willeq-wt/kb-b1181-gate-base BIN=/home/user/willeq-builds/kb-b1181-gate-base/build/bin/model_viewer ;;
    tip)  WT=/home/user/src/willeq-wt/kb-b1181-item-missile BIN=/home/user/willeq-builds/kb-b1181-item-missile/build/bin/model_viewer ;;
  esac
  local drmcmd=""; [ "$drm" = 1 ] && drmcmd="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"
  echo "[$tree $tag] render $(date -u +%T)" >> "$LOG"
  if ssh -n thinkpad "mkdir -p '$G/$tree/$tag' && cd '$WT' && $drmcmd '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
      --plan '$G/plans/$tag.plan' --frames-dir '$G/$tree/$tag' --fps 30 --seconds $secs \
      --sim-hz 60 --seed 1 --headless --camera $cam > '$G/$tree/$tag/run.log' 2>&1"; then
    local n
    n=$(ssh -n thinkpad "ls '$G/$tree/$tag' | grep -c '\.png$'" < /dev/null)
    echo "[$tree $tag] done frames=$n $(date -u +%T)" >> "$LOG"
  else
    echo "[$tree $tag] RENDER FAILED (see $G/$tree/$tag/run.log on ThinkPad)" >> "$LOG"
  fi
}
export -f job

render_tree() { # $1=base|tip
  local tree=$1
  xargs -P 3 -n 4 bash -c 'job "$0" "$@"' "$tree" < <(awk -F'\t' '{print $1"\t"$2"\t"$3"\t"$4}' "$ROWS")
}

pids=()
render_tree base & pids+=($!)
render_tree tip  & pids+=($!)
for p in "${pids[@]}"; do wait "$p"; done
echo "all renders finished $(date -u)" >> "$LOG"
echo "fails: $(grep -c FAILED "$LOG" || true)"
