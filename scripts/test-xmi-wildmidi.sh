#!/bin/bash
# test-xmi-wildmidi.sh
# Stream XMI music using wildmidi (plays XMI directly, no conversion needed)
#
# Usage: ./scripts/test-xmi-wildmidi.sh [xmi_file]
#
# Connect with VLC: vlc http://localhost:8085

EQ_PATH="/home/user/projects/claude/EverQuestP1999"
XMI_FILE="${1:-qeynos.xmi}"
SINK_NAME="xmi_stream_$$"
AUDIO_PORT=8085

# Resolve path
if [[ "$XMI_FILE" == /* ]]; then
    XMI_PATH="$XMI_FILE"
else
    XMI_PATH="$EQ_PATH/$XMI_FILE"
fi

echo "========================================"
echo "  XMI Streaming Test (wildmidi)"
echo "========================================"
echo ""
echo "XMI File: $XMI_PATH"
echo ""

if [ ! -f "$XMI_PATH" ]; then
    echo "ERROR: File not found: $XMI_PATH"
    exit 1
fi

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
SINK_MODULE_ID=$(pactl load-module module-null-sink sink_name="$SINK_NAME" sink_properties=device.description=XMI_Stream)
echo "Sink: $SINK_NAME"

# Start ffmpeg HTTP stream
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

echo ""
echo "========================================"
echo "Connect with: vlc http://localhost:$AUDIO_PORT"
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
[ -n "$LOCAL_IP" ] && echo "       or:    vlc http://$LOCAL_IP:$AUDIO_PORT"
echo ""
echo "Press Ctrl+C to stop"
echo "========================================"
echo ""

# Play with wildmidi, output to our sink
PULSE_SINK="$SINK_NAME" wildmidi -l "$XMI_PATH"
