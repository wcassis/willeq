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
ENABLE_GLES2=ON
IMAGE_NAME="willeq-arm-noble"

for arg in "$@"; do
    case "$arg" in
        --headless)
            ENABLE_GRAPHICS=OFF
            ;;
        --no-audio)
            ENABLE_AUDIO=OFF
            ;;
        --no-gles2)
            ENABLE_GLES2=OFF
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
echo "GLES2:    ${ENABLE_GLES2}"
echo ""

# Build the Docker image (caches layers between runs)
echo "--- Building Docker cross-compilation image ---"
docker build \
    -f "$PROJECT_DIR/docker/Dockerfile.arm-noble" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    --build-arg "ENABLE_GLES2=${ENABLE_GLES2}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

# Create output directory and persistent build cache
OUTPUT_DIR="$PROJECT_DIR/build-arm-noble"
BUILD_CACHE="$OUTPUT_DIR/cache"
mkdir -p "$OUTPUT_DIR/bin" "$BUILD_CACHE"

# Run the build inside Docker
# Mount build cache for incremental compilation (only recompiles changed files)
echo ""
echo "--- Cross-compiling willeq ---"
docker run --rm \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$OUTPUT_DIR/bin:/output" \
    -v "$BUILD_CACHE:/build" \
    -e "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    -e "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    -e "ENABLE_GLES2=${ENABLE_GLES2}" \
    "$IMAGE_NAME"

echo ""
if [ -f "$OUTPUT_DIR/bin/willeq" ]; then
    echo "=== Success ==="
    file "$OUTPUT_DIR/bin/willeq"
    ls -lh "$OUTPUT_DIR/bin/willeq"
    if [ -f "$OUTPUT_DIR/bin/model_viewer" ]; then
        file "$OUTPUT_DIR/bin/model_viewer"
        ls -lh "$OUTPUT_DIR/bin/model_viewer"
    fi
    if [ -f "$OUTPUT_DIR/bin/gpu_texture_formats" ]; then
        file "$OUTPUT_DIR/bin/gpu_texture_formats"
        ls -lh "$OUTPUT_DIR/bin/gpu_texture_formats"
    fi
    if [ -f "$OUTPUT_DIR/bin/gles2_etc1_benchmark" ]; then
        file "$OUTPUT_DIR/bin/gles2_etc1_benchmark"
        ls -lh "$OUTPUT_DIR/bin/gles2_etc1_benchmark"
    fi
    if [ -f "$OUTPUT_DIR/bin/egl_image_sharing_test" ]; then
        file "$OUTPUT_DIR/bin/egl_image_sharing_test"
        ls -lh "$OUTPUT_DIR/bin/egl_image_sharing_test"
    fi
    if [ -f "$OUTPUT_DIR/bin/test_gl_points" ]; then
        file "$OUTPUT_DIR/bin/test_gl_points"
        ls -lh "$OUTPUT_DIR/bin/test_gl_points"
    fi
    if [ -f "$OUTPUT_DIR/bin/gles2_derivatives_test" ]; then
        file "$OUTPUT_DIR/bin/gles2_derivatives_test"
        ls -lh "$OUTPUT_DIR/bin/gles2_derivatives_test"
    fi
    if [ -f "$OUTPUT_DIR/bin/gles2_npot_test" ]; then
        file "$OUTPUT_DIR/bin/gles2_npot_test"
        ls -lh "$OUTPUT_DIR/bin/gles2_npot_test"
    fi
    if [ -f "$OUTPUT_DIR/bin/gles2_fog_volume_benchmark" ]; then
        file "$OUTPUT_DIR/bin/gles2_fog_volume_benchmark"
        ls -lh "$OUTPUT_DIR/bin/gles2_fog_volume_benchmark"
    fi
    echo ""
    echo "Binaries: $OUTPUT_DIR/bin/willeq"
    echo "          $OUTPUT_DIR/bin/model_viewer"
    echo "          $OUTPUT_DIR/bin/gpu_texture_formats"
    echo "          $OUTPUT_DIR/bin/gles2_etc1_benchmark"
    echo "          $OUTPUT_DIR/bin/egl_image_sharing_test"
    echo "          $OUTPUT_DIR/bin/test_gl_points"
    echo "          $OUTPUT_DIR/bin/gles2_derivatives_test"
    echo "          $OUTPUT_DIR/bin/gles2_npot_test"
    echo ""
    echo "Deploy to Orange Pi (Armbian Noble):"
    echo "  scp $OUTPUT_DIR/bin/willeq $OUTPUT_DIR/bin/model_viewer $OUTPUT_DIR/bin/gpu_texture_formats $OUTPUT_DIR/bin/gles2_etc1_benchmark $OUTPUT_DIR/bin/egl_image_sharing_test $OUTPUT_DIR/bin/test_gl_points $OUTPUT_DIR/bin/gles2_derivatives_test $OUTPUT_DIR/bin/gles2_npot_test orangepi:~/willeq/"
    echo ""
    echo "Run on Orange Pi (DRM/KMS, Lima GPU, Mesa GL 2.1 - no X11 needed):"
    echo "  ./willeq -c config.json --drm --opengl --constrained orangepi -r 800 600"
    echo ""
    echo "Run model viewer spell test (GLES2, DRM):"
    echo "  ./model_viewer --spell-test --gles2 --client /path/to/EQ/"
    echo ""
    echo "Run on Orange Pi (with X11, if Xorg is running):"
    echo "  DISPLAY=:0 ./willeq -c config.json --opengl --constrained orangepi -r 800 600"
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
