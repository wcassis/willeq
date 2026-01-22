#!/bin/bash
# Capture audio from both paths and compare
# 1. Reference: timidity MIDI -> WAV (standard MIDI renderer)
# 2. RDP/loopback path: FluidSynth -> OpenAL loopback -> capture

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

XMI_FILE="${1:-/home/user/projects/claude/EverQuestP1999/qeynos2.xmi}"
SOUNDFONT="${2:-$PROJECT_DIR/data/1mgm.sf2}"
DURATION="${3:-10}"

WORK_DIR="$BUILD_DIR/music_test"
mkdir -p "$WORK_DIR"

echo "=== Audio Path Comparison Test ==="
echo "XMI: $XMI_FILE"
echo "SoundFont: $SOUNDFONT"
echo "Duration: ${DURATION}s"
echo ""

# Step 1: Convert XMI to MIDI
echo "=== Step 1: Converting XMI to MIDI ==="
if [ ! -f "$BUILD_DIR/xmi_to_midi" ]; then
    echo "ERROR: xmi_to_midi not found. Build it first."
    exit 1
fi
"$BUILD_DIR/xmi_to_midi" "$XMI_FILE" "$WORK_DIR/test.mid"
echo ""

# Step 2: Capture reference using timidity
echo "=== Step 2: Capturing reference (timidity render) ==="
rm -f "$WORK_DIR/reference.wav"

echo "Rendering MIDI with timidity..."
timidity -Ow -o "$WORK_DIR/reference.wav" --output-stereo --output-signed --output-16bit \
    -s 44100 "$WORK_DIR/test.mid" 2>&1 | head -10

# Trim to duration
if [ -f "$WORK_DIR/reference.wav" ]; then
    sox "$WORK_DIR/reference.wav" "$WORK_DIR/reference_trimmed.wav" trim 0 "$DURATION" 2>/dev/null || true
    if [ -f "$WORK_DIR/reference_trimmed.wav" ]; then
        mv "$WORK_DIR/reference_trimmed.wav" "$WORK_DIR/reference.wav"
    fi
    echo "Reference capture: $WORK_DIR/reference.wav"
    sox "$WORK_DIR/reference.wav" -n stat 2>&1 | head -5
else
    echo "ERROR: Reference capture failed"
    exit 1
fi
echo ""

# Step 3: Capture loopback (RDP path simulation)
echo "=== Step 3: Capturing loopback (OpenAL loopback path) ==="
rm -f "$WORK_DIR/loopback.wav"

"$BUILD_DIR/bin/test_music_loopback_compare" capture \
    "$XMI_FILE" "$SOUNDFONT" "$WORK_DIR/loopback.wav" "$DURATION" 2>&1 | \
    grep -v "^fluidsynth:" | grep -v "^\[MUSIC\] fillBuffer" | head -30

if [ -f "$WORK_DIR/loopback.wav" ]; then
    echo ""
    echo "Loopback capture: $WORK_DIR/loopback.wav"
    sox "$WORK_DIR/loopback.wav" -n stat 2>&1 | head -5
else
    echo "ERROR: Loopback capture failed"
    exit 1
fi
echo ""

# Step 4: Compare
echo "=== Step 4: Comparing captures ==="
"$BUILD_DIR/bin/test_music_loopback_compare" compare \
    "$WORK_DIR/reference.wav" "$WORK_DIR/loopback.wav"

echo ""
echo "=== Done ==="
echo "Listen to compare:"
echo "  Reference: aplay $WORK_DIR/reference.wav"
echo "  Loopback:  aplay $WORK_DIR/loopback.wav"
