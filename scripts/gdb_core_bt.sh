#!/bin/bash
# Run on Orange Pi to extract backtrace from core dump
# Usage: ./scripts/gdb_core_bt.sh [core_file] [binary]
CORE="${1:-core}"
BIN="${2:-./willeq}"

if [ ! -f "$CORE" ]; then
    echo "Core file not found: $CORE"
    exit 1
fi
if [ ! -f "$BIN" ]; then
    echo "Binary not found: $BIN"
    exit 1
fi

gdb -batch \
    -ex "set print frame-arguments all" \
    -ex "bt full" \
    -ex "info registers" \
    -ex "thread apply all bt" \
    "$BIN" "$CORE" 2>&1 | tee gdb_core_bt.log

echo ""
echo "Output saved to gdb_core_bt.log"
