#!/usr/bin/env bash
# B1167 (#486): the per-emitter matrix for record 35 (spell 253, record 104) on --camera b1130
# at caster level 60 (and 5): each run keeps only the masked slots (WILLEQ_FX_SLOTMASK_0 = Source
# bits slot0..3 = em428/429/430/431, WILLEQ_FX_SLOTMASK_2 = Target bits slot0..2 = em432/433/434),
# an env-gated instrument built ONLY in the throwaway worktrees wt/kb-b1167-instrbefore (fable
# 070a9215 + the mask) and wt/kb-b1167-instr (this branch + the mask + the
# WILLEQ_FX_B1167_HEADINGS variant: capture headings, fit on them, ratchet 245). Never merged.
# usage: run_matrix.sh   (3 renders at a time; results in runs/<tag>/measure/b1130_rows.tsv)
D=$(cd "$(dirname "$0")" && pwd)
jobs_=()
run() { # tag slug level env...
  local tag=$1 slug=$2 lvl=$3; shift 3
  RENV="$*" "$D/run_r35.sh" "$slug" "$tag" b1130 "$lvl" > /dev/null 2>&1 &
  while [ "$(jobs -rp | wc -l)" -ge 3 ]; do wait -n; done
}
for v in before after; do
  slug=kb-b1167-instrbefore; [ $v = after ] && slug=kb-b1167-instr
  run mx_${v}_all $slug 60
  run mx_${v}_l5 $slug 5
  run mx_${v}_S0_T0 $slug 60 WILLEQ_FX_SLOTMASK_0=0 WILLEQ_FX_SLOTMASK_2=0
  run mx_${v}_Sall $slug 60 WILLEQ_FX_SLOTMASK_2=0
  run mx_${v}_Tall $slug 60 WILLEQ_FX_SLOTMASK_0=0
  for k in 0 1 2 3; do run mx_${v}_S$((428+k)) $slug 60 WILLEQ_FX_SLOTMASK_0=$((1<<k)) WILLEQ_FX_SLOTMASK_2=0; done
  for k in 0 1 2; do run mx_${v}_T$((432+k)) $slug 60 WILLEQ_FX_SLOTMASK_0=0 WILLEQ_FX_SLOTMASK_2=$((1<<k)); done
done
run mx_afterA_all kb-b1167-instr 60 WILLEQ_FX_B1167_HEADINGS=1
run mx_afterA_Sall kb-b1167-instr 60 WILLEQ_FX_B1167_HEADINGS=1 WILLEQ_FX_SLOTMASK_2=0
run mx_afterA_Tall kb-b1167-instr 60 WILLEQ_FX_B1167_HEADINGS=1 WILLEQ_FX_SLOTMASK_0=0
run mx_branch_all kb-b1167-record104 60
run mx_pristine_before_all kb-b1167-before 60
wait
echo matrix done
