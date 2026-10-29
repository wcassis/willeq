#!/bin/bash
# B1181 host-side fable leg driver (run on hermes-ext, lane 5). One leg = one
# stack up/down: the OBSERVER is the willeq under test (A, windowed on the
# ThinkPad's :99, the ledger video + the frame dump), the CASTER is a
# headless willeq GM (B, Testcaster on willeq_test) running the archery
# auto-attack and the two bolt #castspells from fable_b_tour.txt.
#
# The caller holds NO lease: the leg takes and releases its own (the oracle
# legs in the queue do the same per leg). The GFX rule
# (Spells:UseLiveSpellProjectileGFX) is leg-limited: ON from here until the
# post-SQL after the world-stop.
#
# usage: fable_leg.sh <run_name>
#   run_name: e.g. fable-b1181 (the leg dir name under the evidence dir)
set -u
RUN=${1:?run name}
WT=$(cd "$(dirname "$0")/../../.." && pwd)
LEG_DIR=$WT/refactor/B1181/leg
EVID=/home/user/kb-evidence/kb-b1181-item-missile
OUT_LOCAL=$EVID/fable/$RUN
OUT_REMOTE=/home/user/kb-evidence-remote/kb-b1181-item-missile/fable/$RUN
BUILD=/home/user/willeq-builds/kb-b1181-item-missile/build
SLUG=kb-b1181-item-missile
mkdir -p "$OUT_LOCAL"
cd "$WT"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
source scripts/eqemu/restore_lib.sh
eqemu_lock_take kb-b1181-item-missile "B1181 fable leg $RUN" || { echo "lease busy" >&2; exit 64; }
eqemu_lease_start_renewer 60 2>/dev/null || :
cleanup_lease() { eqemu_lease_stop_renewer 2>/dev/null || :; eqemu_lock_release kb-b1181-item-missile 2>/dev/null || :; }
trap cleanup_lease EXIT
# --- stack (bring up only if we have to; the queue may keep it up) -----------
WE_UP=0
if ! eqemu_stack_ready; then
  bash "$WT/scripts/eqemu/up.sh" qeynos2 >"$OUT_LOCAL/stack-up.log" 2>&1 \
    || { echo "stack up failed" >&2; exit 65; }
  WE_UP=1
fi
eqemu_stack_ready || { echo "stack not ready after up.sh" >&2; exit 65; }
# --- server state as-found (the rows this leg moves, BEFORE any change) ------
eqemu_sql "SELECT id, name, account_id, cur_hp, mana, endurance, pvp FROM character_data WHERE id IN (67, 103) ORDER BY id;" >"$OUT_LOCAL/chars-before.tsv" 2>&1
eqemu_sql "SELECT rule_name, rule_value FROM rule_values WHERE rule_name='Spells:UseLiveSpellProjectileGFX';" >"$OUT_LOCAL/gfx-before.tsv" 2>&1
eqemu_sql "SELECT rule_name, rule_value FROM rule_values WHERE rule_name='Combat:MinRangedAttackDist';" >"$OUT_LOCAL/range-rule-before.tsv" 2>&1
eqemu_sql "SELECT slot_id, item_id FROM inventory WHERE character_id=67 AND slot_id IN (11, 22);" >"$OUT_LOCAL/gear-before.tsv" 2>&1
# --- pre-SQL (world up): GFX on, the caster's archery gear, observer vitals,
# --- the poses. B zones in at its seeded pose (the world reads the DB row per
# --- login, B1176) and A at the observer pose, heading 220 (facing B).
ZID=$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='qeynos2' LIMIT 1;")
ZID=${ZID//[[:space:]]/}
[ -n "$ZID" ] || { echo "no qeynos2 zone id" >&2; exit 65; }
eq_remote test -f /mnt/projects/EverQuestP1999/qeynos2.s3d \
  || { echo "qeynos2.s3d missing on the eqemu host" >&2; exit 65; }
eqemu_sql "UPDATE rule_values SET rule_value='true' WHERE rule_name='Spells:UseLiveSpellProjectileGFX';" \
  || { echo "GFX rule failed" >&2; exit 65; }
# RUN 5 (2026-10-29): the archery shot is 9.05 units and the server's
# Client::RangedAttack band is MinRangedAttackDist^2 .. range^2
# (special_attacks.cpp:965-972; the rule default is 25, ruletypes.h:576),
# so the leg zeros the rule and B reloads it ('#reload rules global').
# The world is ALREADY UP when this runs, hence the reload, not a restart.
eqemu_sql "UPDATE rule_values SET rule_value='0' WHERE rule_name='Combat:MinRangedAttackDist';" \
  || { echo "range rule pre-SQL failed" >&2; exit 65; }
RANGE_RULE=$(eqemu_sql "SELECT rule_value FROM rule_values WHERE rule_name='Combat:MinRangedAttackDist';")
RANGE_RULE=${RANGE_RULE//[[:space:]]/}
[ "$RANGE_RULE" = "0" ] || { echo "MinRangedAttackDist not applied (got '$RANGE_RULE') -- name miss; the 9.05-unit shot would be refused" >&2; exit 65; }
# The server's bow/arrow type checks (special_attacks.cpp:893-906) read the
# DB itemtype: 5 = ItemTypeBow, 27 = ItemTypeArrow (B900's seed). A wrong
# item row fails the shot server-side, so gate on it, not on the log.
BOW_TYPE=$(eqemu_sql "SELECT itemtype FROM item WHERE id=8003;"); BOW_TYPE=${BOW_TYPE//[[:space:]]/}
ARR_TYPE=$(eqemu_sql "SELECT itemtype FROM item WHERE id=8005;"); ARR_TYPE=${ARR_TYPE//[[:space:]]/}
[ "$BOW_TYPE" = "5" ] || { echo "item 8003 itemtype='$BOW_TYPE' want 5 (ItemTypeBow)" >&2; exit 65; }
[ "$ARR_TYPE" = "27" ] || { echo "item 8005 itemtype='$ARR_TYPE' want 27 (ItemTypeArrow)" >&2; exit 65; }
eqemu_sql "DELETE FROM inventory WHERE character_id=67 AND slot_id=11;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES (67, 11, 8003, 0);
DELETE FROM inventory WHERE character_id=67 AND slot_id=22;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES (67, 22, 8005, 10);" \
  || { echo "gear pre-SQL failed" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET cur_hp=999999, mana=999999, endurance=999999 WHERE id=103 AND name='Oraclewatch';" \
  || { echo "observer vitals pre-SQL failed" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-67.6, y=434.4, z=3.75, heading=220 WHERE id=103 AND name='Oraclewatch';" \
  || { echo "A seed failed" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-74, y=428, z=3.75, heading=45 WHERE id=67 AND name='Testcaster';" \
  || { echo "B seed failed" >&2; exit 65; }
# B1176's SELECT-back discipline: a stale mid-migration DB row zoned the legs
# into causeway (no causeway.s3d, A rc=4). Verify both seeds landed.
seed_zone_got() {
  local got
  got=$(eqemu_sql "SELECT zone_id FROM character_data WHERE id=$1 LIMIT 1;")
  got=${got//[[:space:]]/}
  [ "$got" = "$ZID" ] || { echo "char $1 seed not verified: zone_id=$got want=$ZID" >&2; exit 65; }
}
seed_zone_got 103
seed_zone_got 67
eqemu_sql "SELECT slot_id, item_id FROM inventory WHERE character_id=67 AND slot_id IN (11, 22);" >"$OUT_LOCAL/gear-after-pre.tsv" 2>&1
grep -q 8003 "$OUT_LOCAL/gear-after-pre.tsv" && grep -q 8005 "$OUT_LOCAL/gear-after-pre.tsv" \
  || { echo "gear not verified after pre-SQL" >&2; cat "$OUT_LOCAL/gear-after-pre.tsv" >&2; exit 65; }
# --- run the leg on the ThinkPad ---------------------------------------------
ssh thinkpad "mkdir -p '$OUT_REMOTE'" || { echo "remote mkdir failed" >&2; exit 65; }
scp -q "$LEG_DIR/fable_leg_remote.sh" "$LEG_DIR/fable_a_tour.txt" "$LEG_DIR/fable_b_tour.txt" \
  "thinkpad:$OUT_REMOTE/" || { echo "ship failed" >&2; exit 65; }
ssh thinkpad "cd '$OUT_REMOTE' && OUT='$OUT_REMOTE' LEG_DIR='$OUT_REMOTE' bash '$OUT_REMOTE/fable_leg_remote.sh' '$OUT_REMOTE' '$BUILD'" \
  2>&1 | tee "$OUT_LOCAL/remote.stdout"
RC=${PIPESTATUS[0]}
echo "remote rc=$RC" | tee -a "$OUT_LOCAL/run.log"
# pull the evidence (small logs + pcap + the ledger video + a frame manifest)
ssh thinkpad "cd '$OUT_REMOTE' && for f in A.log B.log capture.pcap pcap.log pcap.filter video.mp4 video.log ui-after.ini rig.txt willeqA.exit willeqA.pid willeqB.pid willeq.sha256 A.launch.epoch tour.txt b_tour.txt leg.done B.early; do [ -f \"\$f\" ] && echo \"\$f\"; done | tar cf - -T -" \
  | tar xf - -C "$OUT_LOCAL" 2>/dev/null || :
ssh thinkpad "ls '$OUT_REMOTE/frames' | sort" > "$OUT_LOCAL/frames.list" 2>/dev/null || :
# --- teardown: the world writes character rows back on shutdown, so the post-
# --- SQL runs with the world STOPPED but mariadb still up (eqemu_world_stop,
# --- restore_lib.sh -- a full down.sh would take mariadb down with it and
# --- the post-SQL would fail, exactly archery5's "No such container" undo).
if [[ $WE_UP == 1 ]]; then
  eqemu_world_stop >>"$OUT_LOCAL/stack-down.log" 2>&1 || :
fi
# post-SQL (world down once the down.sh ran; with the stack still up the
# restore is still safe: the clients are both /q'd, no live connection).
eqemu_sql "UPDATE rule_values SET rule_value='false' WHERE rule_name='Spells:UseLiveSpellProjectileGFX';" || :
# restore the ranged-distance rule (default 25) from the as-found snapshot
RR_BEFORE=$(awk 'NR==2{print $2}' "$OUT_LOCAL/range-rule-before.tsv" 2>/dev/null)
RR_BEFORE=${RR_BEFORE//[[:space:]]/}
if [ -n "$RR_BEFORE" ]; then
  eqemu_sql "UPDATE rule_values SET rule_value='$RR_BEFORE' WHERE rule_name='Combat:MinRangedAttackDist';" || :
fi
eqemu_sql "DELETE FROM inventory WHERE character_id=67 AND item_id IN (8003, 8005);" || :
# restore the as-found vitals (cur_hp/mana/endurance/pvp) from the snapshot
python3 - "$OUT_LOCAL/chars-before.tsv" <<'PY' > "$OUT_LOCAL/restore.sql" 2>&1
import sys
rows = []
with open(sys.argv[1]) as f:
    next(f)
    for line in f:
        p = line.split()
        if len(p) >= 7 and p[0].isdigit():
            rows.append(f"UPDATE character_data SET cur_hp={p[3]}, mana={p[4]}, endurance={p[5]}, pvp={p[6]} WHERE id={p[0]} AND name='{p[1]}';")
print("\n".join(rows))
PY
eqemu_sql "$(cat "$OUT_LOCAL/restore.sql")" || :
eqemu_sql "SELECT id, name, account_id, cur_hp, mana, endurance, pvp FROM character_data WHERE id IN (67, 103) ORDER BY id;" >"$OUT_LOCAL/chars-after.tsv" 2>&1
eqemu_sql "SELECT rule_name, rule_value FROM rule_values WHERE rule_name='Spells:UseLiveSpellProjectileGFX';" >"$OUT_LOCAL/gfx-after.tsv" 2>&1
if [[ $WE_UP == 1 ]]; then
  bash "$WT/scripts/eqemu/down.sh" >>"$OUT_LOCAL/stack-down.log" 2>&1 || :
fi
if diff -q "$OUT_LOCAL/chars-before.tsv" "$OUT_LOCAL/chars-after.tsv" >/dev/null 2>&1; then
  echo "chars as-found: OK" | tee -a "$OUT_LOCAL/run.log"
else
  echo "chars as-found: DIFF (see chars-before/after.tsv)" | tee -a "$OUT_LOCAL/run.log"
fi
exit $RC
