#!/bin/bash
# Start WillEQ with virtual framebuffer and VNC server
# Usage: ./start-with-vnc.sh [client options]
#
# This script will:
# 1. Start Xvfb (virtual X11 server) on display :99
# 2. Start x11vnc to export the display over VNC (port 5999)
# 3. Run willeq with graphics enabled
#
# Connect to the VNC server at: vnc://localhost:5999

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Configuration
DISPLAY_NUM=90
VNC_PORT=5950
RESOLUTION="800x600x24"

# Check if willeq exists
if [ ! -f "$BUILD_DIR/bin/willeq" ]; then
    echo "Error: willeq not found at $BUILD_DIR/bin/willeq"
    echo "Please build the project first with: cd $BUILD_DIR && cmake --build ."
    exit 1
fi

# Kill any existing Xvfb/x11vnc processes on our display
pkill -f "Xvfb :$DISPLAY_NUM" 2>/dev/null || true
pkill -f "x11vnc -display :$DISPLAY_NUM" 2>/dev/null || true
sleep 1

echo "Starting virtual display :$DISPLAY_NUM ($RESOLUTION)..."
Xvfb :$DISPLAY_NUM -screen 0 $RESOLUTION &
XVFB_PID=$!
sleep 1

# Verify Xvfb started
if ! kill -0 $XVFB_PID 2>/dev/null; then
    echo "Error: Failed to start Xvfb"
    exit 1
fi
echo "Xvfb started (PID: $XVFB_PID)"

# Start x11vnc
echo "Starting VNC server on port $VNC_PORT..."
x11vnc -display :$DISPLAY_NUM -rfbport $VNC_PORT -forever -shared -bg -nopw -quiet 2>/dev/null
sleep 1

echo ""
echo "=========================================="
echo " VNC Server ready!"
echo " Connect using: vnc://localhost:$VNC_PORT"
echo "=========================================="
echo ""

# Function to cleanup on exit
cleanup() {
    echo ""
    echo "Shutting down..."
    pkill -f "x11vnc -display :$DISPLAY_NUM" 2>/dev/null || true
    kill $XVFB_PID 2>/dev/null || true
    echo "Cleanup complete"
}
trap cleanup EXIT

# Export display
export DISPLAY=:$DISPLAY_NUM

# Run willeq with passed arguments
echo "Starting WillEQ..."
cd "$PROJECT_DIR"
"$BUILD_DIR/bin/willeq" "$@"
