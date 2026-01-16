#!/bin/bash
# Test script for F-key input in willeq
# Usage: ./test_fkey_input.sh [timeout_seconds]
#
# This script:
# 1. Starts willeq with debug logging
# 2. Waits for zone-in
# 3. Sends F1-F8 key presses via xdotool
# 4. Captures output to a log file

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
CONFIG_FILE="/home/user/projects/claude/summonah.json"
LOG_FILE="$PROJECT_ROOT/fkey_test_output.log"
TIMEOUT_SECONDS="${1:-120}"  # Default 120 seconds

echo "=== F-Key Input Test Script ===" | tee "$LOG_FILE"
echo "Config: $CONFIG_FILE" | tee -a "$LOG_FILE"
echo "Log file: $LOG_FILE" | tee -a "$LOG_FILE"
echo "Timeout: ${TIMEOUT_SECONDS}s" | tee -a "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

# Check prerequisites
if ! command -v xdotool &> /dev/null; then
    echo "ERROR: xdotool is required but not installed" | tee -a "$LOG_FILE"
    exit 1
fi

if [ ! -f "$BUILD_DIR/bin/willeq" ]; then
    echo "ERROR: willeq binary not found at $BUILD_DIR/bin/willeq" | tee -a "$LOG_FILE"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Config file not found at $CONFIG_FILE" | tee -a "$LOG_FILE"
    exit 1
fi

# Export display
export DISPLAY=:99

# Check if display is available
if ! xdpyinfo &> /dev/null; then
    echo "ERROR: Cannot connect to display $DISPLAY" | tee -a "$LOG_FILE"
    exit 1
fi

echo "Starting willeq with debug level 5..." | tee -a "$LOG_FILE"

# Start willeq in background, capturing stdout and stderr
"$BUILD_DIR/bin/willeq" -c "$CONFIG_FILE" --debug 5 >> "$LOG_FILE" 2>&1 &
WILLEQ_PID=$!

echo "willeq started with PID: $WILLEQ_PID" | tee -a "$LOG_FILE"

# Function to cleanup on exit
cleanup() {
    echo "" | tee -a "$LOG_FILE"
    echo "Cleaning up..." | tee -a "$LOG_FILE"
    if kill -0 $WILLEQ_PID 2>/dev/null; then
        echo "Sending SIGTERM to willeq (PID $WILLEQ_PID)" | tee -a "$LOG_FILE"
        kill $WILLEQ_PID 2>/dev/null || true
        sleep 2
        if kill -0 $WILLEQ_PID 2>/dev/null; then
            echo "Sending SIGKILL to willeq" | tee -a "$LOG_FILE"
            kill -9 $WILLEQ_PID 2>/dev/null || true
        fi
    fi
    echo "Test completed. Output saved to: $LOG_FILE" | tee -a "$LOG_FILE"
}
trap cleanup EXIT

# Wait for willeq window to appear
echo "Waiting for willeq window..." | tee -a "$LOG_FILE"
WINDOW_WAIT_START=$(date +%s)
WINDOW_ID=""
while [ -z "$WINDOW_ID" ]; do
    sleep 1
    WINDOW_ID=$(xdotool search --name "WillEQ" 2>/dev/null | head -1 || true)

    ELAPSED=$(($(date +%s) - WINDOW_WAIT_START))
    if [ $ELAPSED -gt 30 ]; then
        echo "ERROR: Timeout waiting for willeq window" | tee -a "$LOG_FILE"
        exit 1
    fi

    # Check if process is still running
    if ! kill -0 $WILLEQ_PID 2>/dev/null; then
        echo "ERROR: willeq process exited unexpectedly" | tee -a "$LOG_FILE"
        exit 1
    fi
done

echo "Found willeq window: $WINDOW_ID" | tee -a "$LOG_FILE"

# Wait for zone-in (look for "Zone connection complete" in the log)
echo "Waiting for zone-in..." | tee -a "$LOG_FILE"
ZONE_WAIT_START=$(date +%s)
while ! grep -q "Zone connection complete" "$LOG_FILE" 2>/dev/null; do
    sleep 2

    ELAPSED=$(($(date +%s) - ZONE_WAIT_START))
    if [ $ELAPSED -gt $TIMEOUT_SECONDS ]; then
        echo "ERROR: Timeout waiting for zone-in" | tee -a "$LOG_FILE"
        echo "Last 50 lines of log:" | tee -a "$LOG_FILE"
        tail -50 "$LOG_FILE"
        exit 1
    fi

    # Check if process is still running
    if ! kill -0 $WILLEQ_PID 2>/dev/null; then
        echo "ERROR: willeq process exited unexpectedly during zone-in" | tee -a "$LOG_FILE"
        exit 1
    fi

    echo "  Still waiting... (${ELAPSED}s elapsed)" | tee -a "$LOG_FILE"
done

echo "Zone-in detected!" | tee -a "$LOG_FILE"

# Wait for zone to be fully ready (zone line expansion, etc)
echo "Waiting for zone to fully load..." | tee -a "$LOG_FILE"
FULL_READY_START=$(date +%s)
while ! grep -q "Zone line expansion complete" "$LOG_FILE" 2>/dev/null; do
    sleep 1
    ELAPSED=$(($(date +%s) - FULL_READY_START))
    if [ $ELAPSED -gt 30 ]; then
        echo "Zone line expansion not found, continuing anyway..." | tee -a "$LOG_FILE"
        break
    fi
done

# Give extra time for everything to settle
echo "Waiting 5 seconds for game to settle..." | tee -a "$LOG_FILE"
sleep 5

# Focus the window
echo "Focusing willeq window..." | tee -a "$LOG_FILE"
xdotool windowactivate --sync "$WINDOW_ID" 2>/dev/null || true
sleep 1

# Send F-key test sequence
echo "" | tee -a "$LOG_FILE"
echo "=== Sending F-key test sequence ===" | tee -a "$LOG_FILE"

for KEY in F1 F2 F3 F4 F5 F6 F7 F8; do
    echo "Sending $KEY..." | tee -a "$LOG_FILE"
    xdotool key --window "$WINDOW_ID" "$KEY"
    sleep 0.5
done

echo "" | tee -a "$LOG_FILE"
echo "=== Sending Ctrl+F5 (camera mode toggle) ===" | tee -a "$LOG_FILE"
xdotool key --window "$WINDOW_ID" ctrl+F5
sleep 0.5

echo "" | tee -a "$LOG_FILE"
echo "=== Sending Tab (cycle targets) ===" | tee -a "$LOG_FILE"
xdotool key --window "$WINDOW_ID" Tab
sleep 0.5

# Wait a moment for processing
sleep 2

echo "" | tee -a "$LOG_FILE"
echo "=== Test sequence complete ===" | tee -a "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

# Show relevant log entries
echo "=== Relevant log entries (INPUT module) ===" | tee -a "$LOG_FILE"
grep -i "INPUT\|Key pressed\|target\|TargetSelf\|F1\|F2\|F3\|F4\|F5\|F6\|F7\|F8" "$LOG_FILE" | tail -100 || echo "No matching entries found"

echo "" | tee -a "$LOG_FILE"
echo "Full log saved to: $LOG_FILE"
