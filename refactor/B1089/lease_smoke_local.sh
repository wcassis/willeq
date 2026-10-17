#!/usr/bin/env bash
# Smoke: the lease client end-to-end against a LOCAL store (EQEMU_HOST=local),
# exercising env.sh's wrappers + renewer, not just the core.
set -u
REPO=/home/user/src/willeq-wt/kb-b1089-lane-lease
export EQEMU_HOST=local EQEMU_LANE=1 LANE_LEASE_TTL_S=6
# The renewer's interval floor is normally 60 s (ssh churn); override it so
# this smoke test can exercise the renewer in seconds.
export LANE_LEASE_RENEW_FLOOR_S=0
export EQEMU_LEASE_STORE="$(mktemp -d)/store"
# A fake HERMES workspace/task so eqemu_lock_take's identity path is exercised
# without the real board.
export HERMES_KANBAN_DB= HERMES_KANBAN_WORKSPACE=
source "$REPO/scripts/eqemu/env.sh"

say() { echo "[smoke] $*"; }
fail() { echo "SMOKE-FAIL: $*" >&2; exit 1; }

eqemu_lock_take "T900 smoke" || fail "take on free lane"
eqemu_lease_show | grep -q "state=HELD" || fail "show HELD"
# The renewer's interval floor is 60 s, so pass one explicitly (TTL here is 6 s).
eqemu_lease_start_renewer 2 || fail "start renewer"
# The renewer's first tick is at iv=2s; wait past one.
sleep 3
lt1=$(eqemu_lease_show | sed -n 's/^last_touched=//p')
sleep 3
lt2=$(eqemu_lease_show | sed -n 's/^last_touched=//p')
[ -n "$lt2" ] && [ "${lt2}" -gt "${lt1}" ] || fail "renewer did not extend last_touched (${lt1} ${lt2})"
say "renewer extended last_touched: ${lt1} -> ${lt2}"
# A second taker is refused while the renewer keeps the lease live.
out=$(LANE_LEASE_CARD=other LANE_LEASE_RUN=other _lease_client_cmd "$EQEMU_LEASE_STORE" take "other" 2>&1); rc=$?
[ ${rc} -eq 1 ] && say "second taker refused (rc=1)" || fail "second taker rc=${rc}: ${out}"
kill "${LANE_LEASE_RENEWER_PID}" 2>/dev/null || :
eqemu_lease_stop_renewer
eqemu_lock_release
eqemu_lock_show | grep -q "state=FREE" || fail "show FREE after release"
say "release ok"
# A renew by the old identity once the lane is free fails with 63.
out=$(LANE_LEASE_CARD=T900 LANE_LEASE_RUN=T900 LANE_LEASE_PID=$$ _lease_client_cmd "$EQEMU_LEASE_STORE" renew "stale" 2>&1); rc=$?
[ ${rc} -eq 63 ] || fail "stale renew rc=${rc} (want 63): ${out}"
echo "${out}" | grep -q "re-lease: bash scripts/eqemu/lease take" || fail "no re-lease one-liner: ${out}"
say "stale renew -> 63 + re-lease one-liner"
rm -rf "$(dirname "$EQEMU_LEASE_STORE")" 2>/dev/null || :
echo "SMOKE-PASS: client wrappers, renewer, refusal, release, 63+message"
