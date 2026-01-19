#!/bin/bash
# test-midi-streaming.sh
# Test FluidSynth MIDI streaming via HTTP for VLC playback
#
# Usage: ./scripts/test-midi-streaming.sh [xmi_file]
#
# Connect with VLC: vlc http://localhost:8085

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

SINK_NAME="midi_stream_$$"
AUDIO_PORT=8085
SOUNDFONT="${SOUNDFONT:-$PROJECT_DIR/data/1mgm.sf2}"
EQ_PATH="/home/user/projects/claude/EverQuestP1999"
XMI_FILE="${1:-qeynos.xmi}"

# Resolve XMI path
if [[ "$XMI_FILE" == /* ]]; then
    XMI_PATH="$XMI_FILE"
else
    XMI_PATH="$EQ_PATH/$XMI_FILE"
fi

echo "========================================"
echo "  MIDI Streaming Test"
echo "========================================"
echo ""
echo "SoundFont: $SOUNDFONT"
echo "XMI File:  $XMI_PATH"
echo ""

# Check files exist
if [ ! -f "$SOUNDFONT" ]; then
    echo "ERROR: SoundFont not found: $SOUNDFONT"
    exit 1
fi

if [ ! -f "$XMI_PATH" ]; then
    echo "ERROR: XMI file not found: $XMI_PATH"
    echo "Available: $(ls "$EQ_PATH"/*.xmi 2>/dev/null | head -5 | xargs -n1 basename)"
    exit 1
fi

if [ ! -f "./build/bin/test_fluidsynth_midi" ]; then
    echo "ERROR: test_fluidsynth_midi not built"
    exit 1
fi

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    jobs -p | xargs -r kill 2>/dev/null
    pactl unload-module $SINK_MODULE_ID 2>/dev/null
    echo "Done"
}
trap cleanup EXIT

# Create PulseAudio sink
echo "Creating audio sink..."
SINK_MODULE_ID=$(pactl load-module module-null-sink sink_name="$SINK_NAME" sink_properties=device.description=MIDI_Stream)
echo "Sink: $SINK_NAME (module $SINK_MODULE_ID)"

# Start ffmpeg in a loop (reconnects when VLC disconnects)
echo "Starting HTTP stream on port $AUDIO_PORT..."
(
    while true; do
        ffmpeg -f pulse -i "${SINK_NAME}.monitor" \
            -acodec libmp3lame -ab 192k -ar 48000 -ac 2 \
            -f mp3 -listen 1 "http://0.0.0.0:${AUDIO_PORT}" \
            -loglevel error 2>&1
        sleep 1
    done
) &

sleep 2

# Start MIDI playback
echo ""
echo "Starting MIDI playback..."
echo ""
echo "========================================"
echo "Connect with: vlc http://localhost:$AUDIO_PORT"
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
[ -n "$LOCAL_IP" ] && echo "       or:    vlc http://$LOCAL_IP:$AUDIO_PORT"
echo ""
echo "Press Ctrl+C to stop"
echo "========================================"
echo ""

PULSE_SINK="$SINK_NAME" ./build/bin/test_fluidsynth_midi "$SOUNDFONT" "$XMI_PATH"
