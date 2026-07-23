#!/usr/bin/env bash
# H04 -- run one full timed pass of the GLES2 zone benchmark on the Orange
# Pi over ssh (three golden zones x RGBA/EQE1 variants, 60 static-pose
# frames each), collecting CSVs to benchmarks/pi_runs/<run-name>/.
#
# Usage:
#   scripts/pi/bench_pi.sh [--run-name NAME] [--no-eqe1]
#
# All env in scripts/pi/env.sh (PI_HOST, paths, etc.) can be overridden by
# exporting before invocation.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./env.sh
source "$SCRIPT_DIR/env.sh"

exec python3 "$SCRIPT_DIR/pi_bench_run.py" "$@"
