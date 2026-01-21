#!/bin/bash
# Test script to reproduce zoning hang issue
# Usage: ./scripts/test_zoning_hang.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$PROJECT_DIR/test_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Config
WILLEQ_BIN="$PROJECT_DIR/build/bin/willeq"
CONFIG_FILE="/home/user/projects/claude/summonah.json"
DISPLAY_NUM=":99"
TIMEOUT_SECS=60

# Create log directory
mkdir -p "$LOG_DIR"

echo "=== Zoning Hang Test ==="
echo "Timestamp: $TIMESTAMP"
echo "Binary: $WILLEQ_BIN"
echo "Config: $CONFIG_FILE"
echo "Log dir: $LOG_DIR"
echo ""

# Check binary exists
if [ ! -f "$WILLEQ_BIN" ]; then
    echo "ERROR: Binary not found at $WILLEQ_BIN"
    exit 1
fi

# Check config exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Config not found at $CONFIG_FILE"
    exit 1
fi

# Check Xvfb is running
if ! xdpyinfo -display "$DISPLAY_NUM" > /dev/null 2>&1; then
    echo "ERROR: Display $DISPLAY_NUM not available"
    echo "Start Xvfb with: Xvfb :99 -screen 0 1024x768x24 &"
    exit 1
fi

echo "Step 1: Starting willeq with debug logging..."
MAIN_LOG="$LOG_DIR/willeq_${TIMESTAMP}.log"

# Start willeq in background with high debug level
DISPLAY="$DISPLAY_NUM" "$WILLEQ_BIN" -c "$CONFIG_FILE" --debug 5 > "$MAIN_LOG" 2>&1 &
WILLEQ_PID=$!

echo "Started willeq with PID: $WILLEQ_PID"
echo "Output log: $MAIN_LOG"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    if kill -0 $WILLEQ_PID 2>/dev/null; then
        echo "Killing willeq (PID: $WILLEQ_PID)"
        kill $WILLEQ_PID 2>/dev/null || true
        sleep 1
        kill -9 $WILLEQ_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "Step 2: Monitoring for zone progress..."
echo "Will timeout after $TIMEOUT_SECS seconds"
echo ""

START_TIME=$(date +%s)
LAST_PROGRESS=""

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    # Check if process is still running
    if ! kill -0 $WILLEQ_PID 2>/dev/null; then
        echo "Process exited after ${ELAPSED}s"
        break
    fi

    # Check timeout
    if [ $ELAPSED -ge $TIMEOUT_SECS ]; then
        echo "TIMEOUT after ${TIMEOUT_SECS}s"
        break
    fi

    # Check for zone progress in log
    if [ -f "$MAIN_LOG" ]; then
        # Look for loading progress
        PROGRESS=$(grep -o "Loading\.\.\. [0-9]*%" "$MAIN_LOG" 2>/dev/null | tail -1 || true)
        if [ -n "$PROGRESS" ] && [ "$PROGRESS" != "$LAST_PROGRESS" ]; then
            echo "[${ELAPSED}s] $PROGRESS"
            LAST_PROGRESS="$PROGRESS"
        fi

        # Look for zone-in complete
        if grep -q "Zone-in complete\|ZoneInComplete\|Player spawned\|ZONE_READY" "$MAIN_LOG" 2>/dev/null; then
            echo "[${ELAPSED}s] ZONE-IN COMPLETE!"
            break
        fi

        # Look for errors
        if grep -q "ERROR\|FATAL\|Connection failed\|disconnected" "$MAIN_LOG" 2>/dev/null; then
            echo "[${ELAPSED}s] Error detected in logs"
        fi
    fi

    sleep 1
done

echo ""
echo "Step 3: Test complete. Analyzing logs..."
echo ""

# Extract key events from log
echo "=== Key Events ==="
grep -E "(Connecting|Connected|Login|World|Zone|Loading|Progress|Spawn|Ready|ERROR|WARN)" "$MAIN_LOG" 2>/dev/null | head -100 || echo "No key events found"

echo ""
echo "=== Last 50 lines of log ==="
tail -50 "$MAIN_LOG"

echo ""
echo "Full log saved to: $MAIN_LOG"
