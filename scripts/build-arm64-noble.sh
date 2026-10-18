#!/bin/bash
# Cross-compile willeq for AArch64 (arm64) -- the aarch64 SBC lanes of scripts/pi/env.sh
# (B1030 #350): lane 2 Rock64 (RK3328, Mali-450/lima) and lane 3 ROCK 4D (RK3576,
# Mali-G52/panfrost). One generic armv8-a binary serves both; the system Mesa is used
# on both (no LLVM-less prefix -- that was a 483 MB Orange Pi decision, B935).
#
# This is scripts/build-arm-noble.sh for docker/Dockerfile.arm64-noble, which is in turn
# the armhf Dockerfile with the arch swapped (scripts/ci/check_arm64_dockerfile.sh keeps
# the two from drifting). Same vendor store, same options.
#
# Usage:
#   ./scripts/build-arm64-noble.sh                # Build with graphics + audio (default)
#   ./scripts/build-arm64-noble.sh --headless     # Build without graphics
#   ./scripts/build-arm64-noble.sh --no-audio     # Build without audio
#   ./scripts/build-arm64-noble.sh --t122-texprobe
#   BUILD_JOBS=4 ./scripts/build-arm64-noble.sh   # dev-u24 runs ONE C++ build at a time:
#                                                 # cap the jobs when another build is alive
#
# Output: build-arm64-noble/bin/willeq (+ the tools the Dockerfile's target list names)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ENABLE_GRAPHICS=ON
ENABLE_AUDIO=ON
ENABLE_GLES2=ON
ENABLE_T122_TEXPROBE=OFF
IMAGE_NAME="willeq-arm64-noble"

for arg in "$@"; do
    case "$arg" in
        --headless) ENABLE_GRAPHICS=OFF ;;
        --no-audio) ENABLE_AUDIO=OFF ;;
        --t122-texprobe) ENABLE_T122_TEXPROBE=ON ;;
        --help|-h)
            echo "Usage: $0 [--headless] [--no-audio] [--t122-texprobe]"
            echo "Env:   BUILD_JOBS=<n>  (default: the container's nproc)"
            echo "Output: build-arm64-noble/bin/willeq"
            exit 0 ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--headless] [--no-audio] [--t122-texprobe]"
            exit 1 ;;
    esac
done

echo "=== WillEQ AArch64 Cross-Compilation (Armbian Noble sysroot; lanes 2-3) ==="
echo "Graphics:      ${ENABLE_GRAPHICS}"
echo "Audio:         ${ENABLE_AUDIO}"
echo "T122 texprobe: ${ENABLE_T122_TEXPROBE}"
echo "Build jobs:    ${BUILD_JOBS:-nproc}"
echo ""

# B970: Irrlicht/libpng archives come from the pinned vendor store on NFS, never SourceForge.
VENDOR_DIR="$("$PROJECT_DIR/scripts/ci/vendor_fetch.sh" --ensure)"

echo "--- Building Docker cross-compilation image ---"
docker build \
    --build-context "vendor=$VENDOR_DIR" \
    -f "$PROJECT_DIR/docker/Dockerfile.arm64-noble" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    --build-arg "ENABLE_GLES2=${ENABLE_GLES2}" \
    --build-arg "ENABLE_T122_TEXPROBE=${ENABLE_T122_TEXPROBE}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

OUTPUT_DIR="$PROJECT_DIR/build-arm64-noble"
BUILD_CACHE="$OUTPUT_DIR/cache"
mkdir -p "$OUTPUT_DIR/bin" "$BUILD_CACHE"

# T137 (see build-arm-noble.sh): a probe-flag toggle must force a clean relink.
TEXPROBE_MARKER="$BUILD_CACHE/.t122_texprobe_state"
if [ -f "$TEXPROBE_MARKER" ] && [ "$(cat "$TEXPROBE_MARKER")" != "$ENABLE_T122_TEXPROBE" ]; then
    echo "--- T122 texprobe flag changed ($(cat "$TEXPROBE_MARKER") -> $ENABLE_T122_TEXPROBE): wiping the incremental build cache ---"
    rm -rf "$BUILD_CACHE"
    mkdir -p "$BUILD_CACHE"
fi

echo ""
echo "--- Cross-compiling willeq ---"
# B1087: the shared host ccache, when ARM_CCACHE_DIR names one (scripts/ci/arm_ccache.sh).
source "$PROJECT_DIR/scripts/ci/arm_ccache.sh"
arm_ccache_docker_args "$IMAGE_NAME"
# B1093 (#413): a named, labelled container that dies with this shell (scripts/ci/arm_container.sh).
source "$PROJECT_DIR/scripts/ci/arm_container.sh"
arm_container_guard "willeq-arm64-build"
arm_container_run \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$OUTPUT_DIR/bin:/output" \
    -v "$BUILD_CACHE:/build" \
    "${ARM_CCACHE_ARGS[@]}" \
    -e "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    -e "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    -e "ENABLE_GLES2=${ENABLE_GLES2}" \
    -e "BUILD_JOBS=${BUILD_JOBS:-}" \
    "$IMAGE_NAME"

echo "$ENABLE_T122_TEXPROBE" > "$TEXPROBE_MARKER"

echo ""
if [ -f "$OUTPUT_DIR/bin/willeq" ]; then
    echo "=== Success ==="
    for bin in "$OUTPUT_DIR"/bin/*; do
        [ -f "$bin" ] && { file "$bin" | sed 's/, BuildID.*//'; ls -lh "$bin" | awk '{print "   ", $5}'; }
    done
    echo ""
    echo "Publish for the lanes (NFS):  scripts/pi/publish_arm64_build.sh <sha> [branch]"
    echo "Run on a lane (DRM/KMS, GLES2):"
    echo "  source scripts/pi/env.sh   # PI_LANE=2 (rock64) or 3 (rock4d)"
    echo "  pi_remote 'D=/mnt/projects/fable/ci-arm64/<sha>; WILLEQ_DATA_ROOT=\$D \$D/bin/willeq -c <config> --drm --renderer gles2 -r 1280 720'"
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
