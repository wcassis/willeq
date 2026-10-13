#!/usr/bin/env bash
# H04 -- compare a Pi timing run (or bless it as the new baseline).
# B1024: this is the H04 TIMING compare (pi_bench_compare.py); the PIXEL
# checks live in scripts/ci/visual_gate.py --platform pi (the Pi golden set).
#
# Usage:
#   scripts/pi/compare_pi.sh [--run-name NAME]              # compare (default: latest run)
#   scripts/pi/compare_pi.sh --run-name NAME --bless        # bless NAME as the baseline
#
# Exit codes: 0 PASS, 1 FAIL, 2 usage error.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./env.sh
source "$SCRIPT_DIR/env.sh"

exec python3 "$SCRIPT_DIR/pi_bench_compare.py" "$@"
