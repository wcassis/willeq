#!/usr/bin/env bash
# B1201 (#520): the hourly no-agent cron job for the inference ledger.
# Installed (by the orchestrator, not by the MR) as ~/.hermes/scripts/ledger/ledger_cron.sh
# next to copies of build_ledger.py / report.py / summarize.py; see refactor/B1201/README.md
# "Cron". A hermes --no-agent job delivers its stdout verbatim and stays silent on empty
# stdout, so this prints NOTHING on success and the error tail on failure.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
since="${LEDGER_SINCE:-2026-10-23}"
out="${LEDGER_OUT:-/mnt/scratch/ledger}"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
if ! python3 -I "$here/build_ledger.py" --since "$since" --out "$out" >"$log" 2>&1; then
    echo "LEDGER FAILED (build_ledger.py --since $since --out $out):"
    tail -15 "$log"
    exit 1
fi
exit 0
