#!/usr/bin/env bash
# B1089 (#409) integration proof (B) on ONE named eqemu lane -- REAL
# wall-clock holds, logged with timestamps:
#
#   (i)   idle: take, no activity -> at the 900 s TTL a second taker takes
#               over (the lane frees; nothing is killed by expiry).
#   (ii)  active: a holder that renews every 60 s keeps the lane for
#               20+ minutes; a second taker is refused throughout.
#   (iii) reap: a run's tracked pid (a planted `sleep 300` on the lane
#               host) is killed when that run is reaped -- and ONLY its
#               pids; a live holder's lease and tracked pids are untouched.
#
# Run:  nohup bash lease_integration_proof.sh <lane> > <out>/proof.log 2>&1 &
#       (writes <out>/exit when done; the parent polls the exit file)
#
# Lane 4's stack is left DOWN (as found). The proof takes/releases the
# LEASE only; it never brings the server stack up.

set -u
LANE="${1:?usage: lease_integration_proof.sh <lane 1-4>}"
OUT="${2:?usage: lease_integration_proof.sh <lane> <out-dir>}"
mkdir -p "$OUT"
REPO=/home/user/src/willeq-wt/kb-b1089-lane-lease
L="$REPO/scripts/eqemu/lease"
IDLE="kb-b1089-idle"
ACTIVE="kb-b1089-active"
REAPRUN="kb-b1089-reap"
LIVE="kb-b1089-live"
TTL=900   # the default, deliberately: the idle hold IS the default TTL.
export LANE_LEASE_TTL_S="$TTL"

say() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/proof.log"; }
leases_snap() { # <label>
    bash "$REPO/scripts/eqemu/leases" >>"$OUT/proof.log" 2>&1
    bash "$REPO/scripts/eqemu/leases" > "$OUT/leases_$1.txt" 2>&1
    say "leases snapshot: $1"
}
stack_running() {
    bash -c "source '$REPO/scripts/eqemu/env.sh' >/dev/null 2>&1; compose ps --status running 2>/dev/null | tail -n +2 | wc -l"
}
stack_down() {
    # The lane must be left DOWN; the proof never brings it up.
    bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/down.log" 2>&1 || say "down.sh rc=$? (stack was probably already down)"
}

cleanup() {
    [ -n "${PLANT:-}" ] && ssh -n -o BatchMode=yes "user@172.16.200.${LANE}" "kill ${PLANT} 2>/dev/null" 2>/dev/null
    for i in "${IDLE}" "${ACTIVE}" "${REAPRUN}" "${LIVE}"; do
        bash "$L" release --lane "$LANE" --card "$i" --run "$i" --pid "$$" >/dev/null 2>&1
    done
    stack_down
    echo 0 > "$OUT/exit"
    say "DONE (stack left down)"
}
trap 'cleanup' INT TERM
trap 'echo 1 > "$OUT/exit"' ERR

say "PROOF START lane=$LANE out=$OUT ttl=${TTL}s (pid $$)"
say "stack pre-state: $(stack_running) running container(s)"
stack_down
leases_snap "0-before"

# ---------------------------------------------------------------- (i) idle
say "(i) IDLE: take by ${IDLE}, then zero renewals"
bash "$L" take --lane "$LANE" --card "$IDLE" --run "$IDLE" --pid $$ --purpose "B1089 idle proof" >/dev/null 2>&1 || { say "FAIL: idle take"; exit 1; }
bash "$L" take --lane "$LANE" --card "$ACTIVE" --run "$ACTIVE" --pid $$ --purpose x 2>>"$OUT/proof.log" && { say "FAIL: taker NOT refused at t=0"; exit 1; }
say "t=0: second taker refused (rc=1) -- expected"
# No renewal from here on: the next touch is at ~17.5 min, past the 900 s TTL.
sleep 1050
say "t=17m30s: leases before the idle takeover"
leases_snap "1-idle-before-takeover"
bash "$L" show --lane "$LANE" 2>&1 | grep -q "state=EXPIRED" && say "record is EXPIRED -- expected"
bash "$L" take --lane "$LANE" --card "$ACTIVE" --run "$ACTIVE" --pid $$ --purpose "B1089 takeover after idle" >/dev/null 2>&1 \
    || { say "FAIL: idle taker did not take over at TTL"; exit 1; }
say "t=17m30s: (i) PASS -- the idle lease freed at the TTL, the taker took over"
leases_snap "2-idle-takenover"

# --------------------------------------------------------------- (ii) active
say "(ii) ACTIVE: ${ACTIVE} holds 20 min, renewing every 60 s"
for r in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21; do
    bash "$L" renew --lane "$LANE" --card "$ACTIVE" --run "$ACTIVE" --pid $$ --purpose "B1089 active proof" >/dev/null 2>&1 \
        || { say "FAIL: active renew $r lost the lane"; exit 1; }
    if [ $(( r % 8 )) -eq 4 ]; then  # t ~ 4m, 12m, 20m
        out=$(bash "$L" take --lane "$LANE" --card "$IDLE" --run "$IDLE" --pid 4242 --purpose x 2>&1); rc=$?
        echo "  t=${r}m steal attempt: rc=$rc" >>"$OUT/proof.log"
        [ $rc -eq 1 ] && echo "$out" | grep -q "HELD by ${ACTIVE}/" \
            || { say "FAIL: active lease STOLEN at renew $r"; exit 1; }
        say "t=${r}m: steal attempt refused (rc=1, holder named) -- expected"
    fi
    sleep 60
done
say "t=21m: (ii) PASS -- 20+ min of renewal kept the lane; every steal refused"
leases_snap "3-active-holding"
bash "$L" release --lane "$LANE" --card "$ACTIVE" --run "$ACTIVE" --pid $$ >/dev/null 2>&1 || { say "FAIL: active release"; exit 1; }
say "active lease released for (iii)"
leases_snap "4-active-released"

# Plant a detached pid on the lane host. The child must NOT inherit ssh's
# stdout: with `&` it does, and ssh waits for that pipe to close before
# returning (the B1089 proof's first (iii) attempt died exactly there).
plant() { # <lane> -> prints the pid
    ssh -n -o BatchMode=yes "user@172.16.200.$1" \
        "setsid sleep 300 </dev/null >/dev/null 2>&1 & P=\$!; echo \$P" | tail -n1 | tr -d '[:space:]'
}

# ----------------------------------------------------------------- (iii) reap
say "(iii) REAP: the dead run holds the lane, plants a tracked pid, gets reaped"
bash "$L" take --lane "$LANE" --card "$REAPRUN" --run "$REAPRUN" --pid "$$" --purpose "B1089 dead run for reap" >/dev/null 2>&1 || { say "FAIL: dead-run take"; exit 1; }
PLANT="$(plant "$LANE")"
[ -n "${PLANT}" ] && [ "${PLANT}" -gt 0 ] 2>/dev/null || { say "FAIL: could not plant a pid (${PLANT:-})"; exit 1; }
bash "$L" track --lane "$LANE" --run "$REAPRUN" --pid "$PLANT" --purpose "planted sleep" >/dev/null 2>&1 || { say "FAIL: plant track"; exit 1; }
printf '%s\n' "$REAPRUN" > "$OUT/deadruns.txt"
say "dead run ${REAPRUN} holds the lane; planted pid ${PLANT} tracked under it"
bash "$L" reap --lane "$LANE" --dead-runs "$OUT/deadruns.txt" --dry > "$OUT/reap_dry.log" 2>&1 || { say "FAIL: dry reap rc=$?"; exit 1; }
grep -q "DRY run=${REAPRUN}: would release lease" "$OUT/reap_dry.log" || { say "FAIL: dry log missing would-release: $(cat "$OUT/reap_dry.log")"; exit 1; }
grep -q "DRY run=${REAPRUN}: would kill 1 tracked pids" "$OUT/reap_dry.log" || say "WARN: dry log missing the would-kill line"
sleep 2
ssh -n -o BatchMode=yes "user@172.16.200.$LANE" "kill -0 ${PLANT}" 2>/dev/null || { say "FAIL: DRY reap killed the planted pid"; exit 1; }
bash "$L" show --lane "$LANE" 2>&1 | grep -q "state=HELD" || { say "FAIL: DRY reap released the lease"; exit 1; }
say "dry: would-release + would-kill reported, nothing changed -- expected"
bash "$L" reap --lane "$LANE" --dead-runs "$OUT/deadruns.txt" > "$OUT/reap.log" 2>&1 || { say "FAIL: reap rc=$?"; exit 1; }
sleep 2
ssh -n -o BatchMode=yes "user@172.16.200.$LANE" "kill -0 ${PLANT}" 2>/dev/null && { say "FAIL: planted pid ${PLANT} still alive after reap"; exit 1; }
say "planted pid ${PLANT} dead -- expected"
bash "$L" show --lane "$LANE" 2>&1 | grep -q "state=FREE" || { say "FAIL: reap did not release the dead run's lease"; exit 1; }
say "(iii-a) PASS: reap released the dead run's lease and killed exactly its pid"
leases_snap "3a-after-reap"

say "(iii) REAP again with the same dead list -- a LIVE holder must survive"
bash "$L" take --lane "$LANE" --card "$LIVE" --run "$LIVE" --pid "$$" --purpose "B1089 live holder for reap" >/dev/null 2>&1 || { say "FAIL: live take"; exit 1; }
LIVEPLANT="$(plant "$LANE")"
[ -n "${LIVEPLANT}" ] && [ "${LIVEPLANT}" -gt 0 ] 2>/dev/null || { say "FAIL: could not plant live pid (${LIVEPLANT:-})"; exit 1; }
bash "$L" track --lane "$LANE" --run "$LIVE" --pid "$LIVEPLANT" --purpose "live holder's pid" >/dev/null 2>&1 || { say "FAIL: live track"; exit 1; }
bash "$L" reap --lane "$LANE" --dead-runs "$OUT/deadruns.txt" > "$OUT/reap2.log" 2>&1 || { say "FAIL: reap2 rc=$?"; exit 1; }
sleep 2
ssh -n -o BatchMode=yes "user@172.16.200.$LANE" "kill -0 ${LIVEPLANT}" 2>/dev/null || { say "FAIL: the reap killed the LIVE run's pid ${LIVEPLANT}"; exit 1; }
bash "$L" show --lane "$LANE" 2>&1 | grep -q "card=${LIVE}" && bash "$L" show --lane "$LANE" 2>&1 | grep -q "state=HELD" \
    || { say "FAIL: the reap took the LIVE holder's lease"; exit 1; }
say "(iii-b) PASS: reap with the dead list left the live holder's lease and pid untouched"
leases_snap "4-live-holding-after-reap2"
bash "$L" release --lane "$LANE" --card "$LIVE" --run "$LIVE" --pid "$$" >/dev/null 2>&1 || say "WARN: live release rc=$?"
ssh -n -o BatchMode=yes "user@172.16.200.$LANE" "kill ${LIVEPLANT} 2>/dev/null" 2>/dev/null
PLANT="" LIVEPLANT=""
leases_snap "5-final-free"
say "PROOF PASS lane=$LANE"
cleanup
exit 0
