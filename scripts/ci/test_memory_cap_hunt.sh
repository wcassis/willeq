#!/usr/bin/env bash
# T491 -- ctest wrapper for scripts/ci/memory_cap_hunt.sh. Runs the hunt over a
# fake command that prints two [memledger] lines and exits 0 under one
# generous cap, and asserts the report row names the LAST point and parses
# the byte fields. SKIPs (125) where systemd-run --user --scope is unusable
# (no user manager -- a container or a bare cron shell), because the script's
# own exit 3 there is the environment, not the script.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if ! systemd-run --user --scope -q -p MemoryMax=1G -p MemorySwapMax=0 -- true >/dev/null 2>&1; then
    echo "SKIP: systemd-run --user --scope not usable here"
    exit 125
fi
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
# The lines below are the REAL schema of memory_ledger.h's toLogLine(), and
# they deliberately keep the name collision that broke the inherited parser:
# "unattributed" and "unaccounted" are BOTH a scalar field and a per-category
# token on the same line. A parser that greps the whole line reports the
# CATEGORY's bytes; this fixture is red against that parser and green against
# the split-at-allocs one.
L1='[memledger] point=zone_parse zone=nro seq=1 enabled=1 rss_anon=1000 rss_file=1 ledger=900 ledger_peak=900 unattributed=100 unattributed_pct=10.0 malloc_arena=1 malloc_inuse=1 malloc_slack=40 unaccounted=60 target=67108864 over_target=0 ceiling=268435456 over_ceiling=0 allocs=5 unattributed=0/0 zone_parse=900/900'
L2='[memledger] point=ready zone=nro seq=1 enabled=1 rss_anon=2000 rss_file=1 ledger=1500 ledger_peak=1500 unattributed=500 unattributed_pct=25.0 malloc_arena=1 malloc_inuse=1 malloc_slack=200 unaccounted=300 target=67108864 over_target=123 ceiling=268435456 over_ceiling=0 allocs=9 unattributed=0/0 zone_parse=400/900 zone_mesh=1100/1100'
report="$(bash "$HERE/memory_cap_hunt.sh" --caps 512M --out "$OUT" -- bash -c "printf '%s\n%s\n' \"\$1\" \"\$2\"" _ "$L1" "$L2")"
rc=$?
echo "$report"
[ "$rc" -eq 0 ] || { echo "FAIL: hunt exited $rc"; exit 1; }
row="$(grep -E '^512M ' <<< "$report")"
[ -n "$row" ] || { echo "FAIL: no row for 512M"; exit 1; }
# rss_anon=2000 ledger=1500 unaccounted=300 over_target=123, top bucket zone_mesh.
# 300 is the SCALAR unaccounted, not the "unattributed=0/0" category token.
grep -qE '^512M +ok +ready +2000 +1500 +300 +123 +zone_mesh=1100$' <<< "$row" \
    || { echo "FAIL: row did not parse the last point: '$row'"; exit 1; }
[ -f "$OUT/cap_512M.log" ] || { echo "FAIL: per-cap log missing"; exit 1; }
# A command that dies must be reported, not hidden: exit 137 is the OOM shape.
L3='[memledger] point=entities zone=x seq=1 enabled=1 rss_anon=7 rss_file=0 ledger=6 ledger_peak=6 unattributed=1 unattributed_pct=14.3 malloc_arena=0 malloc_inuse=0 malloc_slack=0 unaccounted=1 target=67108864 over_target=0 ceiling=268435456 over_ceiling=0 allocs=1 entities=6/6'
report2="$(bash "$HERE/memory_cap_hunt.sh" --caps 512M --out "$OUT" -- bash -c 'echo "$1"; kill -9 $$' _ "$L3")"
grep -qE '^512M +OOM-killed +entities +7 +6 +1 +0 +entities=6$' <<< "$report2" \
    || { echo "FAIL: killed command not reported: $report2"; exit 1; }
# An OLD log with no unaccounted= field must still parse (fall back to
# unattributed=), so a pre-addendum binary is readable.
L4='[memledger] point=ready zone=x seq=1 enabled=1 rss_anon=90 rss_file=0 ledger=70 ledger_peak=70 unattributed=20 unattributed_pct=22.2 malloc_arena=0 malloc_inuse=0 malloc_slack=0 allocs=1 ui=70/70'
report3="$(bash "$HERE/memory_cap_hunt.sh" --caps 512M --out "$OUT" -- bash -c 'echo "$1"' _ "$L4")"
grep -qE '^512M +ok +ready +90 +70 +20 +- +ui=70$' <<< "$report3" \
    || { echo "FAIL: legacy line not parsed: $report3"; exit 1; }
echo "test_memory_cap_hunt: OK"
