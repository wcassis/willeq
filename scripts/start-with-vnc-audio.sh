#!/bin/bash
# start-with-vnc-audio.sh
# Starts WillEQ with Xvfb + x11vnc for remote graphics and audio streaming via HTTP
#
# Usage: ./scripts/start-with-vnc-audio.sh -c willeq.json
#
# Connect:
#   VNC:   vnc://hostname:5999
#   Audio: vlc http://hostname:8080

set -e

DISPLAY_NUM=90
RESOLUTION="800x600x24"
VNC_PORT=5910
AUDIO_PORT=8085
SINK_NAME="willeq_audio_$$"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."

    # Kill background processes
    [ -n "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null || true
    [ -n "$VNC_PID" ] && kill $VNC_PID 2>/dev/null || true
    [ -n "$AUDIO_PID" ] && kill $AUDIO_PID 2>/dev/null || true

    # Restore original default sink
    if [ -n "$ORIGINAL_DEFAULT_SINK" ]; then
        pactl set-default-sink "$ORIGINAL_DEFAULT_SINK" 2>/dev/null || true
        log_info "Restored default sink: $ORIGINAL_DEFAULT_SINK"
    fi

    # Unload PulseAudio sink
    if [ -n "$SINK_MODULE_ID" ]; then
        pactl unload-module $SINK_MODULE_ID 2>/dev/null || true
    fi

    # Remove lock file
    rm -f /tmp/.X${DISPLAY_NUM}-lock 2>/dev/null || true

    log_info "Cleanup complete"
}

trap cleanup EXIT INT TERM

# Check dependencies
check_dependencies() {
    local missing=()

    command -v Xvfb >/dev/null 2>&1 || missing+=("xvfb")
    command -v x11vnc >/dev/null 2>&1 || missing+=("x11vnc")
    command -v pactl >/dev/null 2>&1 || missing+=("pulseaudio-utils")
    command -v ffmpeg >/dev/null 2>&1 || missing+=("ffmpeg")
    command -v nc >/dev/null 2>&1 || missing+=("netcat")

    if [ ${#missing[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        echo "Install with: sudo apt-get install ${missing[*]}"
        exit 1
    fi
}

# Start Xvfb
start_xvfb() {
    log_info "Starting Xvfb on display :${DISPLAY_NUM} (${RESOLUTION})..."

    # Kill any existing Xvfb on this display
    pkill -f "Xvfb :${DISPLAY_NUM}" 2>/dev/null || true
    rm -f /tmp/.X${DISPLAY_NUM}-lock 2>/dev/null || true

    Xvfb :${DISPLAY_NUM} -screen 0 ${RESOLUTION} &
    XVFB_PID=$!

    # Wait for Xvfb to start
    sleep 1

    if ! kill -0 $XVFB_PID 2>/dev/null; then
        log_error "Failed to start Xvfb"
        exit 1
    fi

    export DISPLAY=:${DISPLAY_NUM}
    log_info "Xvfb started (PID: $XVFB_PID)"
}

# Start x11vnc
start_vnc() {
    log_info "Starting x11vnc on port ${VNC_PORT}..."

    x11vnc -display :${DISPLAY_NUM} -forever -shared -rfbport ${VNC_PORT} -nopw -q &
    VNC_PID=$!

    sleep 1

    if ! kill -0 $VNC_PID 2>/dev/null; then
        log_error "Failed to start x11vnc"
        exit 1
    fi

    log_info "x11vnc started (PID: $VNC_PID)"
}

# Setup PulseAudio sink for audio capture
setup_audio_sink() {
    log_info "Setting up PulseAudio virtual sink..."

    # Ensure PulseAudio is running
    pulseaudio --check 2>/dev/null || pulseaudio --start 2>/dev/null || true

    # Save original default sink so we can restore it on cleanup
    ORIGINAL_DEFAULT_SINK=$(pactl get-default-sink 2>/dev/null) || true
    log_info "Original default sink: ${ORIGINAL_DEFAULT_SINK:-none}"

    # Create a null sink for audio capture
    SINK_MODULE_ID=$(pactl load-module module-null-sink \
        sink_name=${SINK_NAME} \
        sink_properties=device.description=WillEQ_Audio_Stream 2>/dev/null) || true

    if [ -z "$SINK_MODULE_ID" ]; then
        log_warn "Could not create PulseAudio sink - audio streaming disabled"
        return 1
    fi

    # Set the virtual sink as default so OpenAL and FluidSynth use it
    pactl set-default-sink ${SINK_NAME} 2>/dev/null || true
    log_info "Set default sink to: ${SINK_NAME}"

    log_info "PulseAudio sink created: ${SINK_NAME} (module: $SINK_MODULE_ID)"
    return 0
}

# Start audio streaming
start_audio_stream() {
    log_info "Starting audio stream on port ${AUDIO_PORT}..."

    # Stream audio via HTTP using ffmpeg
    # Use ffmpeg's built-in HTTP server for proper HTTP streaming
    # -thread_queue_size: larger buffer for smoother capture
    # -fragment_size: smaller fragments for lower latency PulseAudio capture
    (
        while true; do
            ffmpeg -thread_queue_size 4096 \
                -f pulse -fragment_size 1024 -i ${SINK_NAME}.monitor \
                -acodec libmp3lame -ab 192k -ar 44100 -ac 2 \
                -flush_packets 1 \
                -f mp3 \
                -listen 1 "http://0.0.0.0:${AUDIO_PORT}" 2>/dev/null || true
            sleep 0.5
        done
    ) &
    AUDIO_PID=$!

    log_info "Audio stream started (PID: $AUDIO_PID)"
}

# Main
main() {
    echo "========================================"
    echo "  WillEQ with VNC + Audio Streaming"
    echo "========================================"
    echo ""

    check_dependencies
    start_xvfb
    start_vnc

    AUDIO_ENABLED=false
    if setup_audio_sink; then
        start_audio_stream
        AUDIO_ENABLED=true
    fi

    echo ""
    echo "========================================"
    log_info "Services started successfully!"
    echo ""
    echo "  VNC:   vnc://$(hostname -I | awk '{print $1}'):${VNC_PORT}"
    echo "         vnc://localhost:${VNC_PORT}"
    if [ "$AUDIO_ENABLED" = true ]; then
        echo ""
        echo "  Audio: vlc http://$(hostname -I | awk '{print $1}'):${AUDIO_PORT}"
        echo "         vlc http://localhost:${AUDIO_PORT}"
    fi
    echo ""
    echo "  Press Ctrl+C to stop"
    echo "========================================"
    echo ""

    # Build environment for WillEQ
    WILLEQ_ENV="DISPLAY=:${DISPLAY_NUM}"
    if [ "$AUDIO_ENABLED" = true ]; then
        WILLEQ_ENV="$WILLEQ_ENV PULSE_SINK=${SINK_NAME}"
    fi

    log_info "Starting WillEQ..."
    log_info "Command: $WILLEQ_ENV ./build/bin/willeq $*"
    echo ""

    # Run WillEQ with the configured environment
    env DISPLAY=:${DISPLAY_NUM} PULSE_SINK=${SINK_NAME} ./build/bin/willeq "$@"
}

main "$@"
