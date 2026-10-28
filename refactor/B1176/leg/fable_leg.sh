#!/bin/bash
# B1176 host-side fable leg driver (run on hermes-ext, lane 5). The caller
# holds the lane lease (this script runs only under the leg runner's
# LOCK_HELD discipline: fable legs share one lease and one stack-up).
#
#   1. seeds the poses for A (willeqwatch char) and B (Testcaster, 67) in
#      qeynos2 -- A at (-74,428,A_Z,0), B at (-64.4,451.1,3.125,448) (the
#      same geometry as the oracle legs, o_human.tsv);
#   2. ships the tour + remote script to the ThinkPad and runs the leg;
#   3. pulls the evidence back (frames are kept on the ThinkPad except a
#      manifest; the runner pulls the selected frames).
#
# usage: fable_leg.sh <run_name> <build_slug> <a_char> <a_z>
#   build_slug: kb-b1176-player-scale (fable AFTER) | kb-b1176-fable-base (BEFORE)
set -u
RUN=$1 SLUG=$2 ACHAR=$3 AZ=$4
WT=$(cd "$(dirname "$0")/../../.." && pwd)
LEG_DIR=$WT/refactor/B1176/leg
EVID=/home/user/kb-evidence/kb-b1176-player-scale
OUT_LOCAL=$EVID/fable/$RUN
OUT_REMOTE=/home/user/kb-evidence-remote/kb-b1176-player-scale/fable/$RUN
BUILD=/home/user/willeq-builds/$SLUG/build
mkdir -p "$OUT_LOCAL"
cd "$WT"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
source scripts/eqemu/restore_lib.sh
# B1176 (#495): the pose seeds and the client's zone-in both talk to the eqemu
# stack, and the preceding oracle legs leave it DOWN (their down.sh) -- that is
# why these legs used to die at the zone-id query with rc=65. Bring it up here
# if it isn't already (up.sh adopts a running stack quickly). Track whether WE
# brought it up so the cleanup tears down only what we started: a queue that
# keeps the stack up across the four fable legs pays one up/down, not four.
WE_UP=0
if ! eqemu_stack_ready; then
  bash "$WT/scripts/eqemu/up.sh" qeynos2 >>"$OUT_LOCAL/stack-up.log" 2>&1 \
    || { echo "stack up failed" >&2; exit 65; }
  WE_UP=1
fi
eqemu_stack_ready || { echo "stack not ready after up.sh" >&2; exit 65; }
trap '[[ $WE_UP == 1 ]] && bash "$WT/scripts/eqemu/down.sh" >>"$OUT_LOCAL/stack-down.log" 2>&1 || :' EXIT
# B1176 (#495): zone_id is the zone's STABLE zoneidnumber (the Zones:: enum
# space: qeynos2=2), NOT the zone table's auto-increment PK `id` -- the
# column trap documented in scripts/eqemu/zone_place_lib.sh (seed_char) and
# scripts/eqemu/roster_zone_seed.sql. The PK 303 is causeway's zoneidnumber,
# so a PK seed zoned every leg into causeway (no causeway.s3d -> A rc=4).
ZID=$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='qeynos2' LIMIT 1;")
[ -n "$ZID" ] || { echo "no qeynos2 zone id" >&2; exit 65; }
ZID=${ZID//[[:space:]]/}
# B1176 (#495): the client loads <zone>.s3d from its eq_client_path; a zone
# whose file is absent is an instant FATAL (A rc=4). Pre-check the target.
eq_remote test -f /mnt/projects/EverQuestP1999/qeynos2.s3d \
  || { echo "qeynos2.s3d missing on the eqemu host" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-74, y=428, z=$AZ, heading=0 WHERE name='$ACHAR';" \
  || { echo "A seed failed" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-64.4, y=451.1, z=3.125, heading=448 WHERE name='Testcaster';" \
  || { echo "B seed failed" >&2; exit 65; }
# B1176 (#495): verify the seeds landed in the DB this world will read. One
# mid-migration lane-5 DB state served stale rows (both chars in zone 303 =
# 'causeway' in the client's table, whose .s3d is absent) and every fable leg
# of the first run zoned into causeway and FATALed (A rc=4). SELECT-back both
# rows and abort on any mismatch instead of shipping a doomed client launch.
seed_zone_got() {
  local got
  got=$(eqemu_sql "SELECT zone_id FROM character_data WHERE name='$1' LIMIT 1;")
  got=${got//[[:space:]]/}
  [ "$got" = "$ZID" ] || { echo "$1 seed not verified: zone_id=$got want=$ZID" >&2; exit 65; }
}
seed_zone_got "$ACHAR"
seed_zone_got "Testcaster"
mkdir -p "$OUT_LOCAL"
ssh thinkpad "mkdir -p '$OUT_REMOTE'" || { echo "remote mkdir failed" >&2; exit 65; }
scp -q "$LEG_DIR/fable_leg_remote.sh" "$LEG_DIR/fable_tour.txt" "$LEG_DIR/fable_b_tour.txt" \
  "thinkpad:$OUT_REMOTE/" || { echo "ship failed" >&2; exit 65; }
ssh thinkpad "cd '$OUT_REMOTE' && OUT='$OUT_REMOTE' LEG_DIR='$OUT_REMOTE' BUILD='$BUILD' ACHAR='$ACHAR' bash '$OUT_REMOTE/fable_leg_remote.sh' '$OUT_REMOTE' '$BUILD' '$ACHAR'" \
  2>&1 | tee "$OUT_LOCAL/remote.stdout"
RC=${PIPESTATUS[0]}
echo "remote rc=$RC" | tee -a "$OUT_LOCAL/run.log"
# pull the small evidence (logs, pcap, inis). The cfgs stay on the ThinkPad:
# cfgA.json carries the willeqwatch password and is never pulled or committed.
# Frames stay on the ThinkPad too; the runner pulls the measured ones.
ssh thinkpad "cd '$OUT_REMOTE' && for f in A.log B.log capture.pcap pcap.log pcap.filter ui-after.ini rig.txt willeqA.exit willeqB.pid willeqA.pid willeq.sha256 A.launch.epoch tour.txt b_tour.txt leg.done B.early; do [ -f \"\$f\" ] && echo \"\$f\"; done | tar cf - -T -" \
  | tar xf - -C "$OUT_LOCAL" 2>/dev/null || :
ssh thinkpad "ls '$OUT_REMOTE/frames' | sort" > "$OUT_LOCAL/frames.list" 2>/dev/null || :
exit $RC
