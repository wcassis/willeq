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
# P32 (Phase 4, docs/decisions.md 2026-07-28 addendum): the --no-gles2
# desktop-GL-via-Lima build was retired — GLES2 (COpenGLES2Driver) is the
# only supported ARM renderer now, same as x86.
#
# Output: build-arm-noble/bin/willeq

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ENABLE_GRAPHICS=ON
ENABLE_AUDIO=ON
ENABLE_GLES2=ON
ENABLE_T122_TEXPROBE=OFF
IMAGE_NAME="willeq-arm-noble"

for arg in "$@"; do
    case "$arg" in
        --headless)
            ENABLE_GRAPHICS=OFF
            ;;
        --no-audio)
            ENABLE_AUDIO=OFF
            ;;
        --t122-texprobe)
            # T137: mirrors scripts/build-gles2-x86.sh's --t122-texprobe --
            # defines WILLEQ_T122_TEXPROBE (docker/irrlicht-drm/t122_texprobe.h)
            # in the ARM cross-compiled Irrlicht static lib and keeps -g +
            # -fno-omit-frame-pointer so a resulting Pi core dump can be
            # symbolized / call-chain resolved. OFF unless explicitly
            # requested -- a plain ./build-arm-noble.sh run never defines
            # this macro, exactly like the x86 default build.
            ENABLE_T122_TEXPROBE=ON
            ;;
        --help|-h)
            echo "Usage: $0 [--headless] [--no-audio] [--t122-texprobe]"
            echo ""
            echo "Cross-compile willeq for ARM (armhf) targeting Orange Pi One / Armbian Noble."
            echo "Uses the native GLES2 driver (COpenGLES2Driver) via Lima/Mesa - no gl4es needed."
            echo ""
            echo "Options:"
            echo "  --headless        Build without graphics (Irrlicht)"
            echo "  --no-audio        Build without audio (OpenAL/FluidSynth)"
            echo "  --t122-texprobe   Build with the T122 texture-lifecycle ring probe"
            echo "                    (WILLEQ_T122_TEXPROBE) compiled in. OFF by default."
            echo ""
            echo "Output: build-arm-noble/bin/willeq"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--headless] [--no-audio] [--t122-texprobe]"
            exit 1
            ;;
    esac
done

echo "=== WillEQ ARM Cross-Compilation (Armbian Noble) ==="
echo "Graphics:      ${ENABLE_GRAPHICS}"
echo "Audio:         ${ENABLE_AUDIO}"
echo "T122 texprobe: ${ENABLE_T122_TEXPROBE}"
echo ""

# Build the Docker image (caches layers between runs)
echo "--- Building Docker cross-compilation image ---"
docker build \
    -f "$PROJECT_DIR/docker/Dockerfile.arm-noble" \
    --build-arg "ENABLE_GRAPHICS=${ENABLE_GRAPHICS}" \
    --build-arg "ENABLE_AUDIO=${ENABLE_AUDIO}" \
    --build-arg "ENABLE_GLES2=${ENABLE_GLES2}" \
    --build-arg "ENABLE_T122_TEXPROBE=${ENABLE_T122_TEXPROBE}" \
    -t "$IMAGE_NAME" \
    "$PROJECT_DIR"

# Create output directory and persistent build cache
OUTPUT_DIR="$PROJECT_DIR/build-arm-noble"
BUILD_CACHE="$OUTPUT_DIR/cache"
mkdir -p "$OUTPUT_DIR/bin" "$BUILD_CACHE"

# T137: the persistent build cache above is a host-mounted volume reused
# incrementally across `docker run` invocations (that's its whole point --
# "only recompiles changed files"). WILLEQ_T122_TEXPROBE is baked into the
# STATIC Irrlicht library (docker/irrlicht-drm/t122_texprobe.h), which is
# rebuilt fresh inside the Docker IMAGE on every run -- but Make's
# timestamp-based incremental relink of the `willeq` target against that
# library did NOT reliably detect the change when only this flag toggled:
# observed directly while building this unit's evidence -- a probe-ON build
# followed immediately by a plain rebuild reused the probe-ON link output
# byte-for-byte (`nm` still showed t122 symbols in the supposedly "plain"
# binary; see refactor/T137_arm_probe_infra.md's Review for the full
# repro). Since "off by default, zero probe symbols in a normal build" is a
# load-bearing T122 constraint, silently serving a stale probe-tainted link
# here would be a real regression, not a build-speed nit -- so the cache is
# wiped whenever the flag's value differs from the last recorded run,
# forcing one full rebuild in that case only (a same-flag rerun still gets
# normal incremental caching).
TEXPROBE_MARKER="$BUILD_CACHE/.t122_texprobe_state"
if [ -f "$TEXPROBE_MARKER" ] && [ "$(cat "$TEXPROBE_MARKER")" != "$ENABLE_T122_TEXPROBE" ]; then
    echo "--- T122 texprobe flag changed ($(cat "$TEXPROBE_MARKER") -> $ENABLE_T122_TEXPROBE): wiping stale incremental build cache to force a clean relink ---"
    rm -rf "$BUILD_CACHE"
    mkdir -p "$BUILD_CACHE"
fi

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

# Record the flag state this build used, so a future run can detect a toggle.
echo "$ENABLE_T122_TEXPROBE" > "$TEXPROBE_MARKER"

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
    if [ -f "$OUTPUT_DIR/bin/gles2_etc1_alpha_readback_test" ]; then
        file "$OUTPUT_DIR/bin/gles2_etc1_alpha_readback_test"
        ls -lh "$OUTPUT_DIR/bin/gles2_etc1_alpha_readback_test"
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
    for bench in gles2_shader_perpixel_benchmark gles2_program_switch_benchmark gles2_icosphere_anim_benchmark gles2_texture_read_benchmark; do
        if [ -f "$OUTPUT_DIR/bin/$bench" ]; then
            file "$OUTPUT_DIR/bin/$bench"
            ls -lh "$OUTPUT_DIR/bin/$bench"
        fi
    done
    echo ""
    echo "Binaries: $OUTPUT_DIR/bin/willeq"
    echo "          $OUTPUT_DIR/bin/model_viewer"
    echo "          $OUTPUT_DIR/bin/gpu_texture_formats"
    echo "          $OUTPUT_DIR/bin/gles2_etc1_benchmark"
    echo "          $OUTPUT_DIR/bin/egl_image_sharing_test"
    echo "          $OUTPUT_DIR/bin/test_gl_points"
    echo "          $OUTPUT_DIR/bin/gles2_derivatives_test"
    echo "          $OUTPUT_DIR/bin/gles2_npot_test"
    echo "          $OUTPUT_DIR/bin/gles2_shader_perpixel_benchmark"
    echo "          $OUTPUT_DIR/bin/gles2_program_switch_benchmark"
    echo "          $OUTPUT_DIR/bin/gles2_fog_volume_benchmark"
    echo "          $OUTPUT_DIR/bin/gles2_icosphere_anim_benchmark"
    echo "          $OUTPUT_DIR/bin/gles2_texture_read_benchmark"
    echo "          $OUTPUT_DIR/bin/gles2_zone_benchmark"
    echo ""
    echo "Deploy to Orange Pi (Armbian Noble):"
    echo "  scp $OUTPUT_DIR/bin/{willeq,model_viewer,gpu_texture_formats,gles2_etc1_benchmark,egl_image_sharing_test,test_gl_points,gles2_derivatives_test,gles2_npot_test,gles2_shader_perpixel_benchmark,gles2_program_switch_benchmark,gles2_fog_volume_benchmark,gles2_icosphere_anim_benchmark,gles2_texture_read_benchmark,gles2_zone_benchmark} orangepi:~/willeq/"
    echo ""
    echo "Run on Orange Pi (DRM/KMS, native GLES2 via Mali 400 - no X11 needed):"
    echo "  ./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600"
    echo ""
    echo "Run model viewer spell test (GLES2, DRM):"
    echo "  ./model_viewer --spell-test --renderer gles2 --client /path/to/EQ/"
    echo ""
else
    echo "=== Build failed: output binary not found ==="
    exit 1
fi
