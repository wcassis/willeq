#!/usr/bin/env bash
# B1180 (#499): arm the half-width probe (b1180_hw_gdb.py) on the read client's
# eqgame.exe and hold TMAX s across the cast + render window. Read-only (gdb
# int3s only, removed at detach). Runs ON THE THINKPAD.
#
# The leg (oracle_class_leg.sh, run from hermes-ext with ORACLE_OWNER=b1180hw)
# creates /home/user/eq-oracle/sessions/b1180hw_* with a `timeline.tsv` of
#   <epoch>\tBEGIN|END\t<NN>_<name>
# rows. We attach at the END of the `time_settle` step: the target is spawned
# and stunned, and the cast (~12-15 s later, via tgt_gdb/stun2/gdb_arm/
# gdb_attached) + 15.5 s fx window all fall inside TMAX. Attaching early gives
# gdb the full lead to set the breakpoint before the burst.
#
#   bash b1180_hw_watch.sh            # TMAX=60, probe from this dir
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMAX="${TMAX:-60}"
SESS_GLOB="${SESS_GLOB:-b1180hw_}"

# find (or wait for) the session dir the leg creates
SESSDIR="$(ls -dt /home/user/eq-oracle/sessions/${SESS_GLOB}* 2>/dev/null | head -1)"
for _ in $(seq 1 180); do
  [[ -n "$SESSDIR" && -f "$SESSDIR/plan.tsv" ]] && break
  SESSDIR="$(ls -dt /home/user/eq-oracle/sessions/${SESS_GLOB}* 2>/dev/null | head -1)"
  sleep 2
done
[[ -n "$SESSDIR" && -f "$SESSDIR/plan.tsv" ]] || { echo "watch: no session dir with plan.tsv" >&2; exit 64; }
TL="$SESSDIR/timeline.tsv"
echo "watch: sessdir=$SESSDIR tmax=$TMAX timeline=$TL"

# wait for the END of the time_settle step (fallback: gdb_arm)
for _ in $(seq 1 1200); do
  if [[ -f "$TL" ]] && grep -qE 'END[[:space:]]+[0-9]+_time_settle' "$TL"; then
    echo "watch: saw time_settle END at $(date -u +%T); attaching"; break
  fi
  if [[ -f "$TL" ]] && grep -qE 'END[[:space:]]+[0-9]+_gdb_arm' "$TL"; then
    echo "watch: saw gdb_arm END (fallback) at $(date -u +%T); attaching"; break
  fi
  sleep 0.4
done
if ! { [[ -f "$TL" ]] && { grep -qE 'END[[:space:]]+[0-9]+_time_settle' "$TL" || grep -qE 'END[[:space:]]+[0-9]+_gdb_arm' "$TL"; }; }; then
  echo "watch: trigger never reached" >&2; exit 65
fi

# attach to the CASTER eqgame.exe (the plan runs it in $SESSDIR)
pid="$(cat "$SESSDIR/wine.pid" 2>/dev/null)"
if [[ -z "$pid" ]] || ! kill -0 "$pid" 2>/dev/null; then
  for p in $(pgrep -f "eqgame.exe patchme"); do grep -q wine "/proc/$p/maps" 2>/dev/null && { pid="$p"; break; }; done
fi
[[ -n "$pid" ]] || { echo "watch: no eqgame pid" >&2; exit 66; }
echo "watch: attaching gdb to eqgame pid=$pid at $(date -u +%T)"

OUT="$HERE/b1180_hw_pin.tsv"
: > "$OUT"
# ptrace_scope=1 blocks a same-user gdb attach; root (CAP_SYS_PTRACE) overrides
# yama. Run gdb elevated, then chown the outputs back so the leg/scp can read them.
# sudo's env_reset strips H_OUT/H_TMAX/H_EVERY; hand them to the child via `env`
# (env sets them in the child's environment before exec'ing gdb, so sudo's reset
# is irrelevant).
sudo -n env "H_OUT=$OUT" "H_TMAX=$TMAX" "H_EVERY=50" "H_PID=$pid" \
  gdb -p "$pid" --batch -x "$HERE/b1180_hw_gdb.py" 2>"$HERE/b1180_hw_gdb.err"
rc=$?
sudo -n chown "$(id -un)" "$OUT" "$HERE/b1180_hw_gdb.err" 2>/dev/null || :
echo "watch: gdb exited rc=$rc; HW lines: $(grep -c '^HW' "$OUT" 2>/dev/null || echo 0) at $(date -u +%T)"
exit $rc
