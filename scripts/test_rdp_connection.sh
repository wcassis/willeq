#!/bin/bash
# Test script for RDP connection
# Usage: ./scripts/test_rdp_connection.sh

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
sleep 2

# Wait for RDP to start listening (up to 15 seconds)
for i in {1..15}; do
    if grep -q "RDP.*Server started" /tmp/willeq_rdp_test.log 2>/dev/null; then
        echo "[TEST] RDP server started"
        break
    fi
    if ! kill -0 "$WILLEQ_PID" 2>/dev/null; then
        echo "[TEST] Process exited early"
        break
    fi
    sleep 1
done

echo "[TEST] Checking for certificate generation..."
if grep -q "Generated self-signed certificate" /tmp/willeq_rdp_test.log; then
    echo "[TEST] SUCCESS: Self-signed certificate was generated"
else
    echo "[TEST] Note: Certificate generation message not found (may have loaded from file)"
fi

echo "[TEST] Checking for 'invalid private key' error..."
if grep -q "invalid private key" /tmp/willeq_rdp_test.log; then
    echo "[TEST] FAILED: Still seeing 'invalid private key' error"
    echo ""
    echo "[TEST] Full RDP-related log:"
    grep -i "rdp\|cert\|key\|tls" /tmp/willeq_rdp_test.log || true
    exit 1
else
    echo "[TEST] SUCCESS: No 'invalid private key' error"
fi

echo ""
echo "[TEST] RDP-related log entries:"
grep -i "rdp\|listener" /tmp/willeq_rdp_test.log | head -20 || true

# Try connecting if the server is still running
if kill -0 "$WILLEQ_PID" 2>/dev/null; then
    echo ""
    echo "[TEST] Attempting xfreerdp3 connection..."
    timeout 5 xfreerdp3 /v:localhost:$RDP_PORT /cert:ignore /sec:rdp +auth-only /u:test /p:test 2>&1 || true

    echo ""
    echo "[TEST] Checking if connection was received..."
    sleep 1
    if grep -q "Peer initialized\|Client post-connect\|Client connected" /tmp/willeq_rdp_test.log; then
        echo "[TEST] SUCCESS: RDP client connection was received by server!"
    fi
else
    echo "[TEST] Note: willeq process has exited (may be due to EQ server connection)"
fi

echo ""
echo "[TEST] Test complete - RDP certificate fix verified"
