#!/usr/bin/env bash
# B1199 (#518) -- live poller driver for the caster client's stand byte.
# Same arm protocol and poller as B1183 (refactor/B1183/scripts/b1183_poll.sh
# + b1183_poll.py, the /proc/pid/mem poller): wait for the leg's `gdb_arm`
# timeline line in a session dir NEWER than this poller's start, then poll
# the caster spawn's +0x251 to TMAX.
#
# Session prefix: kb-b1199-stun-feed-a_* (ORACLE_OWNER for this card's leg),
# so a sibling task's oracle session cannot consume this poller.
#
# Runs on the ORACLE HOST (lane 5: the ThinkPad). Usage:
#   b1199_poll.sh <evidence_dir> <tmax_seconds>
# Output: <evidence_dir>/poll/{poll.log,poll.out}
set -u
EV="${1:?evidence dir}"
TMAX="${2:?tmax seconds}"
OUT="$EV/poll"
mkdir -p "$OUT"
REPO="${REPO:-/home/user/src/willeq-wt/kb-b1199-stun-feed}"
SCRIPT="$REPO/refactor/B1183/scripts/b1183_poll.py"

# 1. Wait for the arm line in a session dir NEWER than this poller's start
#    (a stale dir from a previous leg must not consume the poller).
touch "$OUT/poll.started"
LATEST=""
for _ in $(seq 1 240); do
    # Only our own sessions (ORACLE_OWNER prefix kb-b1199-stun-feed-a_):
    # a sibling task's oracle session must not consume this poller.
    d="$(ls -td /home/user/eq-oracle/sessions/kb-b1199-stun-feed-a_* 2>/dev/null | head -1 || true)"
    if [[ -n $d && -f $d/timeline.tsv ]] \
            && [[ "$d" -nt "$OUT/poll.started" ]] \
            && grep -qP '\tEND\t[0-9]+_gdb_arm$' "$d/timeline.tsv"; then
        LATEST="$d"
        break
    fi
    sleep 5
done
[[ -n $LATEST ]] || { echo "b1199_poll: no gdb_arm line within 20 min" >&2; exit 1; }
echo "b1199_poll: session $LATEST"

chmod 777 "$OUT"
sudo -n env S_OUT="$OUT/poll" S_TMAX="$TMAX" \
    timeout -s INT $((TMAX + 60)) \
    python3 "$SCRIPT" >"$OUT/poll.out" 2>&1
rc=$?
echo "b1199_poll: rc=$rc"
sudo -n chown -R user:user "$OUT" 2>/dev/null || true
exit $rc
