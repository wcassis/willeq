#!/bin/bash
# Cross-compile willeq for ARM (armhf) targeting Orange Pi One / Armbian Noble
# Uses Lima open-source GPU driver (Mesa GL 2.1) - no gl4es/Mali blob needed
#
# Usage:
#   ./scripts/build-arm-noble.sh                # Build with graphics + audio (default)
#   ./scripts/build-arm-noble.sh --headless     # Build without graphics
#   ./scripts/build-arm-noble.sh --no-audio     # Build without audio
#   ./scripts/build-arm-noble.sh --headless --no-audio  # Minimal build
#
# Output: build-arm-noble/bin/willeq

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ENABLE_GRAPHICS=ON
ENABLE_AUDIO=ON
IMAGE_NAME="willeq-arm-noble"

for arg in "$@"; do
    case "$arg" in
        --headless)
            ENABLE_GRAPHICS=OFF
            ;;
        --no-audio)
            ENABLE_AUDIO=OFF
            ;;
        --help|-h)
            echo "Usage: $0 [--headless] [--no-audio]"
            echo ""
            echo "Cross-compile willeq for ARM (armhf) targeting Orange Pi One / Armbian Noble."
            echo "Uses Lima open-source GPU driver (Mesa GL 2.1) - no gl4es needed."
            echo ""
            echo "Options:"
            echo "  --headless    Build without graphics (Irrlicht)"
            echo "  --no-audio    Build without audio (OpenAL/FluidSynth)"
            echo ""
            echo "Output: build-arm-noble/bin/willeq"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--headless] [--no-audio]"
            exit 1
            ;;
    esac
done

echo "=== WillEQ ARM Cross-Compilation (Armbian Noble) ==="
echo "Graphics: ${ENABLE_GRAPHICS}"
echo "Audio:    ${ENABLE_AUDIO}"
echo ""

# Build the Docker image (caches layers between runs)
echo "--- Building Docker cross-compilation image ---"
docker build \
    -f "$PROJECT_DIR/docker/Dockerfile.arm-noble" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

# Create output directory
OUTPUT_DIR="$PROJECT_DIR/build-arm-noble"
mkdir -p "$OUTPUT_DIR/bin"

# Run the build inside Docker
echo ""
echo "--- Cross-compiling willeq ---"
docker run --rm \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$OUTPUT_DIR/bin:/output" \
    -e "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    -e "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    "$IMAGE_NAME"

echo ""
if [ -f "$OUTPUT_DIR/bin/willeq" ]; then
    echo "=== Success ==="
    file "$OUTPUT_DIR/bin/willeq"
    ls -lh "$OUTPUT_DIR/bin/willeq"
    echo ""
    echo "Binary: $OUTPUT_DIR/bin/willeq"
    echo ""
    echo "Deploy to Orange Pi (Armbian Noble):"
    echo "  scp $OUTPUT_DIR/bin/willeq orangepi:~/willeq/"
    echo ""
    echo "Run on Orange Pi (DRM/KMS, Lima GPU, Mesa GL 2.1 - no X11 needed):"
    echo "  ./willeq -c config.json --drm --opengl --constrained orangepi -r 800 600"
    echo ""
    echo "Run on Orange Pi (with X11, if Xorg is running):"
    echo "  DISPLAY=:0 ./willeq -c config.json --opengl --constrained orangepi -r 800 600"
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
