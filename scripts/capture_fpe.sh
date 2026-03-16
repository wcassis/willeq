#!/bin/bash
# Run willeq and capture FPE backtrace via built-in signal handler (no GDB needed).
# Usage: bash capture_fpe.sh

BIN="${1:-./build-arm-noble/bin/willeq}"
ARGS="-c ../summonah.json --no-audio --drm --renderer gles2 --constrained orangepi -r 1280 720 -d 5"

$BIN $ARGS 2>&1 | tee fpe_bt.txt
echo ""
echo "Output saved to fpe_bt.txt"
