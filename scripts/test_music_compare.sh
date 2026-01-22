#!/bin/bash
# Script to test music loopback capture vs reference capture
# This compares the RDP audio path with the known-working FluidSynth path

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

XMI_FILE="${1:-/home/user/projects/claude/EverQuestP1999/qeynos2.xmi}"
SOUNDFONT="${2:-$PROJECT_DIR/data/1mgm.sf2}"
DURATION="${3:-10}"

WORK_DIR="$BUILD_DIR/music_test"
mkdir -p "$WORK_DIR"

echo "=== Music Loopback Comparison Test ==="
echo "XMI file: $XMI_FILE"
echo "SoundFont: $SOUNDFONT"
echo "Duration: ${DURATION}s"
echo "Work dir: $WORK_DIR"
echo ""

# Step 1: Convert XMI to MIDI
echo "=== Step 1: Converting XMI to MIDI ==="
if [ ! -f "$BUILD_DIR/xmi_to_midi" ]; then
    echo "Building xmi_to_midi tool..."
    cat > "$WORK_DIR/xmi_to_midi.cpp" << 'CPPEOF'
#define WITH_AUDIO
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "client/audio/xmi_decoder.h"

using namespace EQT::Audio;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.xmi> <output.mid>" << std::endl;
        return 1;
    }
    XmiDecoder decoder;
    std::vector<uint8_t> midiData = decoder.decodeFile(argv[1]);
    if (midiData.empty()) {
        std::cerr << "Failed to decode: " << decoder.getError() << std::endl;
        return 1;
    }
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(midiData.data()), midiData.size());
    std::cerr << "Converted " << argv[1] << " to " << argv[2] << " (" << midiData.size() << " bytes)" << std::endl;
    return 0;
}
CPPEOF
    g++ -std=c++17 -DWITH_AUDIO -I"$PROJECT_DIR/include" \
        "$WORK_DIR/xmi_to_midi.cpp" \
        "$PROJECT_DIR/src/client/audio/xmi_decoder.cpp" \
        -o "$BUILD_DIR/xmi_to_midi"
fi

"$BUILD_DIR/xmi_to_midi" "$XMI_FILE" "$WORK_DIR/test.mid"
echo ""

# Step 2: Create reference capture using FluidSynth file driver
echo "=== Step 2: Creating reference capture (FluidSynth file driver) ==="
rm -f "$WORK_DIR/reference_capture.wav"

# Run fluidsynth with file output
timeout "${DURATION}s" fluidsynth -a file -F "$WORK_DIR/reference_capture.wav" -r 44100 \
    "$SOUNDFONT" "$WORK_DIR/test.mid" 2>&1 || true

if [ ! -f "$WORK_DIR/reference_capture.wav" ]; then
    echo "ERROR: Reference capture failed"
    exit 1
fi

echo "Reference capture created: $WORK_DIR/reference_capture.wav"
sox "$WORK_DIR/reference_capture.wav" -n stat 2>&1 | head -10
echo ""

# Step 3: Create loopback capture (simulates RDP audio path)
echo "=== Step 3: Creating loopback capture (RDP audio path) ==="
rm -f "$WORK_DIR/loopback_capture.wav"

"$BUILD_DIR/bin/test_music_loopback_compare" capture \
    "$XMI_FILE" "$SOUNDFONT" "$WORK_DIR/loopback_capture.wav" "$DURATION" 2>&1

if [ ! -f "$WORK_DIR/loopback_capture.wav" ]; then
    echo "ERROR: Loopback capture failed"
    exit 1
fi

echo ""
echo "Loopback capture created: $WORK_DIR/loopback_capture.wav"
sox "$WORK_DIR/loopback_capture.wav" -n stat 2>&1 | head -10
echo ""

# Step 4: Compare the two captures
echo "=== Step 4: Comparing captures ==="
"$BUILD_DIR/bin/test_music_loopback_compare" compare \
    "$WORK_DIR/reference_capture.wav" "$WORK_DIR/loopback_capture.wav"

echo ""
echo "=== Test Complete ==="
echo "Files created in: $WORK_DIR"
echo "  - reference_capture.wav (FluidSynth file driver - known working)"
echo "  - loopback_capture.wav (OpenAL loopback - RDP audio path)"
echo ""
echo "You can listen to these files to hear the difference:"
echo "  aplay $WORK_DIR/reference_capture.wav"
echo "  aplay $WORK_DIR/loopback_capture.wav"
