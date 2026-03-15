#!/bin/bash
# Cross-compile willeq for AArch64 (arm64) targeting Rock64 / Armbian Noble
# Uses Lima open-source GPU driver (Mesa GLES2) - no X11 needed
#
# Usage:
#   ./scripts/build-arm64-noble.sh                # Build with graphics + audio (default)
#   ./scripts/build-arm64-noble.sh --headless     # Build without graphics
#   ./scripts/build-arm64-noble.sh --no-audio     # Build without audio
#   ./scripts/build-arm64-noble.sh --headless --no-audio  # Minimal build
#
# Output: build-arm64-noble/bin/willeq

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ENABLE_GRAPHICS=ON
ENABLE_AUDIO=ON
ENABLE_GLES2=ON
IMAGE_NAME="willeq-arm64-noble"

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
            echo "Cross-compile willeq for AArch64 (arm64) targeting Rock64 / Armbian Noble."
            echo "Uses Lima open-source GPU driver (Mesa GLES2) - no X11 needed."
            echo ""
            echo "Options:"
            echo "  --headless    Build without graphics (Irrlicht)"
            echo "  --no-audio    Build without audio (OpenAL/FluidSynth)"
            echo ""
            echo "Output: build-arm64-noble/bin/willeq"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--headless] [--no-audio]"
            exit 1
            ;;
    esac
done

echo "=== WillEQ AArch64 Cross-Compilation (Armbian Noble) ==="
echo "Graphics: ${ENABLE_GRAPHICS}"
echo "Audio:    ${ENABLE_AUDIO}"
echo "GLES2:    ${ENABLE_GLES2}"
echo ""

# Build the Docker image (caches layers between runs)
echo "--- Building Docker cross-compilation image ---"
docker build \
    -f "$PROJECT_DIR/docker/Dockerfile.arm64-noble" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    --build-arg "ENABLE_GLES2=${ENABLE_GLES2}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

# Create output directory and persistent build cache
OUTPUT_DIR="$PROJECT_DIR/build-arm64-noble"
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
    for bin in model_viewer gpu_texture_formats gles2_etc1_benchmark egl_image_sharing_test test_gl_points gles2_derivatives_test gles2_npot_test gles2_shader_perpixel_benchmark gles2_program_switch_benchmark gles2_fog_volume_benchmark gles2_fog_visual_compare gles2_icosphere_anim_benchmark gles2_texture_read_benchmark; do
        if [ -f "$OUTPUT_DIR/bin/$bin" ]; then
            file "$OUTPUT_DIR/bin/$bin"
            ls -lh "$OUTPUT_DIR/bin/$bin"
        fi
    done
    echo ""
    echo "Binaries: $OUTPUT_DIR/bin/"
    echo ""
    echo "Deploy to Rock64 (Armbian Noble):"
    echo "  scp $OUTPUT_DIR/bin/* rock64:~/willeq/"
    echo ""
    echo "Run on Rock64 (DRM/KMS, Lima GPU, GLES2 - no X11 needed):"
    echo "  ./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600"
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
