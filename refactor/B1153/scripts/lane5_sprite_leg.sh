#!/usr/bin/env bash
# B1153 (#473) step 4: ONE lane-5 observer leg -- 93 Burst of Flame on the NPC
# at B1130's pose, observer = char 103 Oraclewatch, caster = char 67
# Testcaster (class 6 DRU for the leg, gems the driver default). The driver
# (oracle_class_leg.sh) takes/holds/releases the lane-5 lease itself, brings
# the stack up, snapshots, restores with the world DOWN and brings it down;
# it re-checks the plan offline before touching the lane.
# usage: lane5_sprite_leg.sh <plan.tsv> <out_dir>
set -uo pipefail
EV=/home/user/kb-evidence/kb-b1153-retire
REPO=/home/user/src/willeq-wt/kb-b1153-retire
PLAN=${1:-$EV/lane5/plan_b1153_sprite.tsv}
OUT=${2:-$EV/lane5/oracle_b1153_sprite}
export EQEMU_LANE=5
cd "$REPO"
source scripts/eqemu/env.sh
echo "$(date -u +%FT%TZ) B1153 sprite leg ($PLAN): lease holders before" >> "$EV/lane5/driver.log"
bash scripts/eqemu/leases 2>/dev/null >> "$EV/lane5/driver.log" || true
bash scripts/eqemu/oracle/oracle_class_leg.sh 5 6 \
    "$PLAN" "$OUT" \
    > "$EV/lane5/leg.out" 2>&1
rc=$?
echo "LEG_RC=$rc" >> "$EV/lane5/driver.log"
echo "$(date -u +%FT%TZ) B1153 sprite leg done rc=$rc" >> "$EV/lane5/driver.log"
exit "$rc"
