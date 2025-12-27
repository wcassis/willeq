#!/bin/bash
# WillEQ Docker Entrypoint
# Handles X11 forwarding and VNC modes

set -e

# Configuration defaults
: "${DISPLAY:=:0}"
: "${VNC_PORT:=5900}"
: "${RESOLUTION:=800x600x24}"
: "${VNC:=0}"

# Function to cleanup on exit
cleanup() {
    echo "Shutting down..."
    # Kill VNC and Xvfb if running
    pkill -f "x11vnc" 2>/dev/null || true
    pkill -f "Xvfb" 2>/dev/null || true
    pkill -f "openbox" 2>/dev/null || true
    exit 0
}
trap cleanup SIGTERM SIGINT EXIT

# VNC mode: start virtual display and VNC server
if [ "$VNC" = "1" ] || [ "$VNC" = "true" ]; then
    echo "Starting VNC mode..."

    # Extract display number from DISPLAY variable
    DISPLAY_NUM="${DISPLAY#:}"

    # Start Xvfb (virtual framebuffer)
    echo "Starting virtual display $DISPLAY ($RESOLUTION)..."
    Xvfb $DISPLAY -screen 0 $RESOLUTION &
    XVFB_PID=$!
    sleep 1

    # Verify Xvfb started
    if ! kill -0 $XVFB_PID 2>/dev/null; then
        echo "Error: Failed to start Xvfb"
        exit 1
    fi
    echo "Xvfb started (PID: $XVFB_PID)"

    # Start openbox window manager if available (improves VNC experience)
    if command -v openbox &> /dev/null; then
        DISPLAY=$DISPLAY openbox &
        sleep 0.5
    fi

    # Start x11vnc
    echo "Starting VNC server on port $VNC_PORT..."
    x11vnc -display $DISPLAY -rfbport $VNC_PORT -forever -shared -nopw -quiet &
    X11VNC_PID=$!
    sleep 1

    if ! kill -0 $X11VNC_PID 2>/dev/null; then
        echo "Error: Failed to start x11vnc"
        exit 1
    fi

    echo ""
    echo "=========================================="
    echo " VNC Server ready!"
    echo " Connect using: vnc://localhost:$VNC_PORT"
    echo "=========================================="
    echo ""

    export DISPLAY
fi

# Check if we have a valid display
if [ -z "$DISPLAY" ]; then
    echo "Warning: No DISPLAY set, running in headless mode"
    exec "$@" --no-graphics
fi

# Check X11 socket availability for forwarding mode
if [ "$VNC" != "1" ] && [ "$VNC" != "true" ]; then
    if [ ! -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && [ ! -e "/tmp/.X11-unix/X0" ]; then
        echo "Warning: X11 socket not found at /tmp/.X11-unix/"
        echo "For X11 forwarding, run with: -e DISPLAY=\$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix"
        echo "Falling back to headless mode..."
        exec "$@" --no-graphics
    fi
fi

# Run the command
exec "$@"
