#!/bin/bash
# Cross-compile willeq for ARM (armhf) targeting Orange Pi One / Debian Jessie
#
# Usage:
#   ./scripts/build-arm.sh                # Build with graphics + audio (default)
#   ./scripts/build-arm.sh --headless     # Build without graphics
#   ./scripts/build-arm.sh --no-audio     # Build without audio
#   ./scripts/build-arm.sh --headless --no-audio  # Minimal build
#
# Output: build-arm/bin/willeq

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ENABLE_GRAPHICS=ON
ENABLE_AUDIO=ON
IMAGE_NAME="willeq-arm-cross"

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
            echo "Cross-compile willeq for ARM (armhf) targeting Orange Pi One."
            echo ""
            echo "Options:"
            echo "  --headless    Build without graphics (Irrlicht)"
            echo "  --no-audio    Build without audio (OpenAL/FluidSynth)"
            echo ""
            echo "Output: build-arm/bin/willeq"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--headless] [--no-audio]"
            exit 1
            ;;
    esac
done

echo "=== WillEQ ARM Cross-Compilation ==="
echo "Graphics: ${ENABLE_GRAPHICS}"
echo "Audio:    ${ENABLE_AUDIO}"
echo ""

# Build the Docker image (caches layers between runs)
echo "--- Building Docker cross-compilation image ---"
docker build \
    -f "$PROJECT_DIR/docker/Dockerfile.arm-cross" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

# Create output directories
OUTPUT_DIR="$PROJECT_DIR/build-arm"
mkdir -p "$OUTPUT_DIR/bin" "$OUTPUT_DIR/lib" "$OUTPUT_DIR/xorg"

# Run the build inside Docker
echo ""
echo "--- Cross-compiling willeq ---"
docker run --rm \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$OUTPUT_DIR:/output" \
    -e "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    -e "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    "$IMAGE_NAME"

echo ""
if [ -f "$OUTPUT_DIR/bin/willeq" ]; then
    echo "=== Success ==="
    file "$OUTPUT_DIR/bin/willeq"
    ls -lh "$OUTPUT_DIR/bin/willeq"
    if [ -f "$OUTPUT_DIR/lib/libGL.so.1" ]; then
        echo ""
        echo "gl4es: $OUTPUT_DIR/lib/libGL.so.1"
        ls -lh "$OUTPUT_DIR/lib/libGL.so.1"
    fi
    echo ""
    echo "Binary: $OUTPUT_DIR/bin/willeq"
    echo ""
    echo "Deploy to Orange Pi:"
    echo "  scp $OUTPUT_DIR/bin/willeq orangepi:~/willeq-orangepi/"
    if [ -f "$OUTPUT_DIR/lib/libGL.so.1" ]; then
        echo "  scp $OUTPUT_DIR/lib/libGL.so.1 orangepi:~/willeq-orangepi/lib/"
    fi
    if [ -f "$OUTPUT_DIR/xorg/fbturbo_drv.so" ]; then
        echo "  scp $OUTPUT_DIR/xorg/fbturbo_drv.so orangepi:~/willeq-orangepi/xorg/"
        echo "  scp $OUTPUT_DIR/xorg/xorg.conf orangepi:~/willeq-orangepi/xorg/"
        echo ""
        echo "One-time Mali GPU setup on Orange Pi (requires reboot):"
        echo "  sudo cp ~/willeq-orangepi/xorg/fbturbo_drv.so /usr/lib/xorg/modules/drivers/"
        echo "  sudo cp ~/willeq-orangepi/xorg/xorg.conf /etc/X11/xorg.conf"
        echo "  sudo reboot"
    fi
    if [ -f "$OUTPUT_DIR/lib/libGL.so.1" ]; then
        echo ""
        echo "Run with Mali 400 GPU (via gl4es):"
        echo "  cd ~/willeq-orangepi && LD_LIBRARY_PATH=./lib ./willeq -c willeq.json --opengl --constrained orangepi -r 800 600"
    fi
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
