#!/bin/bash
# test-audio-hang.sh - Test audio setup without ffmpeg streaming
#
# This tests if the hang is caused by:
# 1. The null sink alone
# 2. The ffmpeg capture

set -e

DISPLAY_NUM=90
RESOLUTION="800x600x24"
SINK_NAME="test_audio_$$"

cleanup() {
    echo "Cleaning up..."
    [ -n "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null || true
    [ -n "$VNC_PID" ] && kill $VNC_PID 2>/dev/null || true
    [ -n "$ORIGINAL_SINK" ] && pactl set-default-sink "$ORIGINAL_SINK" 2>/dev/null || true
    [ -n "$SINK_MODULE" ] && pactl unload-module $SINK_MODULE 2>/dev/null || true
    rm -f /tmp/.X${DISPLAY_NUM}-lock 2>/dev/null || true
}
trap cleanup EXIT

# Start Xvfb
pkill -f "Xvfb :${DISPLAY_NUM}" 2>/dev/null || true
rm -f /tmp/.X${DISPLAY_NUM}-lock 2>/dev/null || true
Xvfb :${DISPLAY_NUM} -screen 0 ${RESOLUTION} &
XVFB_PID=$!
sleep 1
export DISPLAY=:${DISPLAY_NUM}

# Start VNC
x11vnc -display :${DISPLAY_NUM} -forever -shared -rfbport 5910 -nopw -q &
VNC_PID=$!
sleep 1

echo ""
echo "=== TEST 1: Audio with null sink but NO ffmpeg capture ==="
echo ""

# Save and set default sink
ORIGINAL_SINK=$(pactl get-default-sink 2>/dev/null) || true
SINK_MODULE=$(pactl load-module module-null-sink sink_name="$SINK_NAME")
pactl set-default-sink "$SINK_NAME"

echo "Null sink created: $SINK_NAME"
echo "Default sink set (no ffmpeg capture)"
echo ""
echo "VNC: vnc://localhost:5910"
echo ""
echo "Running willeq... (Ctrl+C to stop)"
echo ""

./build/bin/willeq "$@"
