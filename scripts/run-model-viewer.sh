#!/bin/bash
# Run model_viewer with Xvfb and x11vnc for remote viewing
# Connect via VNC to localhost:5996

DISPLAY_NUM=96
VNC_PORT=5996

# Kill any existing instances
pkill -9 Xvfb 2>/dev/null
pkill -9 x11vnc 2>/dev/null
pkill -9 model_viewer 2>/dev/null
sleep 1

echo "Starting Xvfb on display :${DISPLAY_NUM}..."
Xvfb :${DISPLAY_NUM} -screen 0 1024x768x24 &
XVFB_PID=$!
sleep 3

echo "Starting x11vnc on port ${VNC_PORT}..."
x11vnc -display :${DISPLAY_NUM} -forever -shared -rfbport ${VNC_PORT} -bg -q
sleep 3

echo "Starting model_viewer..."
cd "$(dirname "$0")/.."
DISPLAY=:${DISPLAY_NUM} ./build/bin/model_viewer &
MODEL_PID=$!
sleep 5

echo ""
echo "============================================"
echo "Model viewer running on display :${DISPLAY_NUM}"
echo "Connect via VNC: localhost:${VNC_PORT}"
echo "Press Ctrl+C to stop"
echo "============================================"
echo ""

# Wait for model_viewer to exit or user interrupt
trap "pkill -9 model_viewer; pkill -9 x11vnc; pkill -9 Xvfb; exit 0" SIGINT SIGTERM
wait $MODEL_PID

# Cleanup
pkill -9 x11vnc 2>/dev/null
pkill -9 Xvfb 2>/dev/null
