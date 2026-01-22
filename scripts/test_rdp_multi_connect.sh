#!/bin/bash
# Test script for multiple RDP connections
# Verifies the use-after-free bug is fixed

set -e

RDP_PORT=13399
WILLEQ_PID=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cleanup() {
    echo "[TEST] Cleaning up..."
    if [ -n "$WILLEQ_PID" ] && kill -0 "$WILLEQ_PID" 2>/dev/null; then
        kill "$WILLEQ_PID" 2>/dev/null || true
        wait "$WILLEQ_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT

echo "[TEST] Starting willeq with RDP on port $RDP_PORT..."
cd "$PROJECT_DIR"
DISPLAY=:99 ./build/bin/willeq -c /home/user/projects/claude/summonah.json --soundfont /home/user/projects/claude/willeq-rdp/data/1mgm.sf2 --rdp --rdp-port $RDP_PORT -d 1 > /tmp/willeq_rdp_test.log 2>&1 &
WILLEQ_PID=$!

echo "[TEST] Waiting for RDP server to initialize (PID: $WILLEQ_PID)..."

# Wait for RDP to start listening (up to 15 seconds)
for i in {1..15}; do
    if grep -q "RDP.*Server started" /tmp/willeq_rdp_test.log 2>/dev/null; then
        echo "[TEST] RDP server started"
        break
    fi
    if ! kill -0 "$WILLEQ_PID" 2>/dev/null; then
        echo "[TEST] ERROR: Process exited early"
        cat /tmp/willeq_rdp_test.log | tail -30
        exit 1
    fi
    sleep 1
done

echo "[TEST] Testing multiple RDP connections..."

# Make 3 connection attempts
for i in 1 2 3; do
    echo "[TEST] Connection attempt $i..."

    if ! kill -0 "$WILLEQ_PID" 2>/dev/null; then
        echo "[TEST] FAILED: willeq crashed before connection $i"
        echo "[TEST] Last 30 lines of log:"
        tail -30 /tmp/willeq_rdp_test.log
        exit 1
    fi

    timeout 5 xfreerdp3 /v:localhost:$RDP_PORT /cert:ignore /sec:rdp +auth-only /u:test /p:test 2>&1 || true

    sleep 1

    if ! kill -0 "$WILLEQ_PID" 2>/dev/null; then
        echo "[TEST] FAILED: willeq crashed after connection $i"
        echo "[TEST] Last 30 lines of log:"
        tail -30 /tmp/willeq_rdp_test.log
        exit 1
    fi

    echo "[TEST] Connection $i completed, server still running"
done

echo ""
echo "[TEST] Checking for errors in log..."
if grep -q "invalid private key" /tmp/willeq_rdp_test.log; then
    echo "[TEST] FAILED: Found 'invalid private key' error"
    exit 1
fi

echo "[TEST] SUCCESS: All 3 RDP connections completed without crash!"
echo ""
echo "[TEST] RDP connection log entries:"
grep -i "Peer initialized\|Client connected\|Client disconnected\|Generated self-signed" /tmp/willeq_rdp_test.log || true
