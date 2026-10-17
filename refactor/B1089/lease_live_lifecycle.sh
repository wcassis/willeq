#!/usr/bin/env bash
# Live lifecycle on lane 4 (short TTL): take -> refuse -> renew -> expire ->
# takeover names former holder -> old holder renew = 63 + lane-numbered
# re-lease line -> release -> FREE.
set -u
REPO=/home/user/src/willeq-wt/kb-b1089-lane-lease
export LANE_LEASE_TTL_S=8 LANE=4
L="$REPO/scripts/eqemu/lease"
fail() { echo "LIVE-FAIL: $*" >&2; exit 1; }

say() { echo "[live] $*"; }

bash "$L" take --lane 4 --card kb-b1089 --run live-smoke --pid $$ --purpose "B1089 live lifecycle" || fail "take"
say "taken"
out=$(bash "$L" take --lane 4 --card other --run o2 --pid 4242 --purpose other 2>&1); rc=$?
[ $rc -eq 1 ] && echo "$out" | grep -q "HELD by kb-b1089/live-smoke" || fail "refusal (rc=$rc: $out)"
say "second taker refused, holder named"
lt1=$(bash "$L" show --lane 4 | sed -n 's/^last_touched=//p')
bash "$L" renew --lane 4 --card kb-b1089 --run live-smoke --pid $$ --purpose renewed || fail "renew"
lt2=$(bash "$L" show --lane 4 | sed -n 's/^last_touched=//p')
[ "${lt2}" -gt "${lt1}" ] || fail "renew did not extend (${lt1} -> ${lt2})"
say "renew extended last_touched"
# No renewal; let the lease age past TTL.
sleep 9
out=$(bash "$L" take --lane 4 --card other --run o2 --pid 4242 --purpose other 2>&1); rc=$?
[ $rc -eq 0 ] && echo "$out" | grep -q "taking over from kb-b1089/live-smoke" || fail "takeover (rc=$rc: $out)"
say "takeover after expiry named the former holder"
out=$(bash "$L" renew --lane 4 --card kb-b1089 --run live-smoke --pid $$ --purpose stale 2>&1); rc=$?
[ $rc -eq 63 ] || fail "stale renew rc=$rc (want 63): $out"
echo "$out" | grep -q "EXPIRED (held by other/o2 on hermes-ext)" || fail "holder not named: $out"
echo "$out" | grep -q "re-lease: bash scripts/eqemu/lease take --lane 4" || fail "re-lease line lacks lane number: $out"
say "stale renew -> 63 + lane-numbered re-lease one-liner"
bash "$L" release --lane 4 --card other --run o2 --pid 4242 || fail "release"
out=$(bash "$L" show --lane 4 2>&1); rc=$?
[ $rc -eq 1 ] && echo "$out" | grep -q "state=FREE" || fail "FREE (rc=$rc: $out)"
say "released, lane FREE"
echo "LIVE-PASS: full lifecycle on lane 4"
