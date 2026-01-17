#!/bin/bash
# Run WillEQ for testing with virtual display
# Usage: ./scripts/run-test.sh [timeout_seconds] [willeq args...]
# Example: ./scripts/run-test.sh 30 -c casterella.json --constrained-mode voodoo2
#
# Output saved to: build/test_run.log

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
LOG_FILE="$BUILD_DIR/test_run.log"

# Default timeout (0 = no timeout)
TIMEOUT=${1:-0}
shift 2>/dev/null || true

# Check if willeq exists
if [ ! -f "$BUILD_DIR/bin/willeq" ]; then
    echo "Error: willeq not found. Build first with: cd build && cmake --build ."
    exit 1
fi

# Start Xvfb on :99 if not running
if ! pgrep -f "Xvfb :99" > /dev/null; then
    echo "Starting Xvfb on :99..."
    Xvfb :99 -screen 0 800x600x24 &
    sleep 1
fi

export DISPLAY=:99

echo "Running WillEQ (timeout: ${TIMEOUT}s, log: $LOG_FILE)"
echo "Args: $@"
echo "---"

cd "$PROJECT_DIR"

if [ "$TIMEOUT" -gt 0 ]; then
    timeout "$TIMEOUT" "$BUILD_DIR/bin/willeq" "$@" 2>&1 | tee "$LOG_FILE"
    EXIT_CODE=${PIPESTATUS[0]}
else
    "$BUILD_DIR/bin/willeq" "$@" 2>&1 | tee "$LOG_FILE"
    EXIT_CODE=$?
fi

echo "---"
echo "Exit code: $EXIT_CODE"
echo "Output saved to: $LOG_FILE"
