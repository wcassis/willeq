#!/usr/bin/env python3
"""B1159: write the specs for the three b-leg clips (M1b_38, M2b_54, M3b_93)
by running the B1158 inventory's missile_specs() over the UPDATED
refactor/B1152/missile_oracle.tsv (the repo file; LEDGER_MISSILE_TSV).

usage: b1159_specs.py <out-dir>
  gate dir: a throwaway with an empty measure/ (the willeq side comes from
  LEDGER_EXTRA_MEASURE: the B1158 sweep's m1_rig38/m2_rig54 + this unit's
  m3_rig93 harness render). Writes <out>/specs/<id>.json for the b-rows.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
B1158 = os.path.join(os.path.dirname(os.path.dirname(HERE)), "B1158", "scripts")
sys.path.insert(0, B1158)
import ledger_inventory as I

out = sys.argv[1]
gate = os.path.join(out, "_empty_gate")
os.makedirs(os.path.join(gate, "measure"), exist_ok=True)

specs = [s for s in I.missile_specs(gate)
         if s["id"] in ("M1b_38", "M2b_54", "M3b_93")]
if len(specs) != 3:
    raise SystemExit(f"expected the three b-legs, got {[s['id'] for s in specs]}")

sdir = os.path.join(out, "specs")
os.makedirs(sdir, exist_ok=True)
for s in specs:
    with open(os.path.join(sdir, s["id"] + ".json"), "w") as f:
        json.dump(s, f, indent=1)
    print(f'{s["id"]}: oracle={s["oracle"]["video"]} t0={s["oracle"]["t0"]:.3f} '
          f'bc={s["oracle"]["bc_epoch"]:.3f} act={s["oracle"]["act_epoch"]:.3f}'
          + (f' | willeq={s["willeq"]["label"]} bc_t={s["willeq"]["bc_t"]} '
             f'act_t={s["willeq"]["act_t"]}' if s["willeq"] else " | willeq: NONE"))
print(f"wrote {len(specs)} specs to {sdir}")
