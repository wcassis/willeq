#!/usr/bin/env bash
# T64: encode a --dump-frames-dir capture (frame_%06d.png) into an mp4,
# using an EFFECTIVE FRAME RATE COMPUTED FROM THE FRAMES' OWN FILE MTIMES —
# never assumed as 30/60. This is P12's hard-won convention
# (videos/p12_anim_check/README.txt), and it's the whole reason those videos
# play back at true elapsed wall-clock time instead of some arbitrary speed.
#
# THE NUMBER THIS SCRIPT PRINTS IS THE MOST IMPORTANT PART OF ITS OUTPUT.
# Per-frame glReadPixels + PNG encode is expensive: P12 measured ~4-7.6
# effective fps on an x86 dev VM under llvmpipe software rendering — far
# below real-time. That number is an artifact of the CAPTURE MECHANISM, not
# a performance measurement of the client or renderer. Anyone who watches a
# capture video without this context will wrongly conclude the game runs at
# ~5fps. Do not strip this warning out of the output when reusing this
# script.
#
# Usage:
#   encode_capture.sh <frame_dir> <unit_id> [output_dir]
#
#   <frame_dir>   directory of frame_NNNNNN.png files (as written by
#                 --dump-frames-dir/--dump-every)
#   <unit_id>     short identifier used in the output filename, e.g. T64
#   [output_dir]  defaults to /mnt/projects/fable/review/<unit_id>
#
# Output (written to output_dir):
#   <unit_id>_capture.mp4     the encoded video
#   <unit_id>_capture.txt     measured fps / frame count / real-time span,
#                             plus the honesty note above, so the number
#                             behind the video is never lost or separated
#                             from it.
#
# Requires ffmpeg (present at /usr/bin/ffmpeg on this host, 6.1.1). Fails
# loudly, before doing any work, if it's missing.

set -u

die() {
    echo "ERROR: $*" >&2
    exit 1
}

if ! command -v ffmpeg >/dev/null 2>&1; then
    die "ffmpeg not found on PATH. This script cannot encode without it (expected at /usr/bin/ffmpeg)."
fi

frame_dir="${1:-}"
unit_id="${2:-}"
output_dir="${3:-}"

if [ -z "$frame_dir" ] || [ -z "$unit_id" ]; then
    die "usage: encode_capture.sh <frame_dir> <unit_id> [output_dir]"
fi
if [ ! -d "$frame_dir" ]; then
    die "frame directory does not exist: $frame_dir"
fi
if [ -z "$output_dir" ]; then
    output_dir="/mnt/projects/fable/review/${unit_id}"
fi
mkdir -p "$output_dir" || die "failed to create output dir: $output_dir"

# Collect frames in numeric order. frame_NNNNNN.png is the format written by
# Application::render()'s dump hook.
mapfile -t frames < <(find "$frame_dir" -maxdepth 1 -name 'frame_*.png' | sort)
frame_count="${#frames[@]}"
if [ "$frame_count" -lt 2 ]; then
    die "need at least 2 frames to compute an effective fps (found $frame_count in $frame_dir)"
fi

first_frame="${frames[0]}"
last_frame="${frames[$((frame_count - 1))]}"

# Effective fps from FILE MTIMES, not assumed. stat -c %Y gives whole
# seconds, which is too coarse for a handful-of-seconds capture, so use
# %.Y (nanosecond-resolution mtime) where available and fall back to %Y.
first_mtime="$(stat -c %.Y "$first_frame" 2>/dev/null || stat -c %Y "$first_frame")"
last_mtime="$(stat -c %.Y "$last_frame" 2>/dev/null || stat -c %Y "$last_frame")"

real_span="$(awk -v a="$first_mtime" -v b="$last_mtime" 'BEGIN { d = b - a; if (d < 0) d = 0; printf "%.3f", d }')"

if awk -v s="$real_span" 'BEGIN { exit !(s <= 0) }'; then
    die "measured real-time span is zero or negative ($real_span s across $frame_count frames) — refusing to encode with an undefined fps. Check the frames' mtimes are meaningful (e.g. not all copied/touched at once)."
fi

# (frame_count - 1) intervals span real_span seconds.
effective_fps="$(awk -v n="$frame_count" -v s="$real_span" 'BEGIN { printf "%.4f", (n - 1) / s }')"

echo "=== T64 encode_capture.sh ==="
echo "frame_dir:        $frame_dir"
echo "frame_count:       $frame_count"
echo "first frame:       $(basename "$first_frame")"
echo "last frame:        $(basename "$last_frame")"
echo "real-time span:    ${real_span}s (measured from frame file mtimes)"
echo "effective fps:      $effective_fps  <-- CAPTURE ARTIFACT, NOT A PERFORMANCE MEASUREMENT"
echo "=============================="

out_video="${output_dir}/${unit_id}_capture.mp4"
out_meta="${output_dir}/${unit_id}_capture.txt"

# Detect the numbering pattern (assume zero-padded frame_%06d.png, as
# written by the dump hook; frame indices need not start at 0 if a run was
# resumed, so glob via -pattern_type glob to be tolerant of that).
ffmpeg -y -hide_banner -loglevel warning \
    -framerate "$effective_fps" \
    -pattern_type glob -i "${frame_dir%/}/frame_*.png" \
    -c:v libx264 -pix_fmt yuv420p -crf 24 \
    "$out_video"
ffmpeg_status=$?

if [ $ffmpeg_status -ne 0 ] || [ ! -s "$out_video" ]; then
    die "ffmpeg failed (exit $ffmpeg_status) or produced an empty file: $out_video"
fi

cat > "$out_meta" <<EOF
T64 video capture — measured numbers (do not discard alongside the video)
===========================================================================
source frame dir:  $frame_dir
frame count:        $frame_count
first frame:        $(basename "$first_frame")
last frame:         $(basename "$last_frame")
real-time span:      ${real_span} s (measured from frame file mtimes, first to last)
effective capture fps: $effective_fps  (= (frame_count - 1) / real_time_span_seconds)

HONESTY NOTE (read before drawing any conclusion from the video):
The fps above is an artifact of the CAPTURE MECHANISM (per-frame
glReadPixels + PNG encode, done synchronously on the render thread), not a
measurement of game or renderer performance. P12's original measurements on
this x86 dev VM under llvmpipe software rendering ranged ~4-7.6 effective
fps -- far below real-time 30/60fps. The encoded video's playback speed was
set to this measured fps specifically so it plays back at true elapsed
wall-clock time (not sped up or slowed down), but the source material
itself is a low-frame-rate capture: motion in the video will look less
smooth than live 30/60fps play. NOTHING about the client's real-time frame
rate may be concluded from this number or from watching the video.

output video: $out_video
EOF

echo "wrote: $out_video"
echo "wrote: $out_meta"
