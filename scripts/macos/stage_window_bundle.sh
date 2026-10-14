#!/bin/bash
# B1044 (#364): stage the window-proof bundle that the CONSOLE user runs.
# Run as the build user on the Mac, from the tree that built build-gles2/:
#
#     scripts/macos/stage_window_bundle.sh [dest]     # default /Users/Shared/willeq-b1044
#
# The bundle holds bin/{willeq,gles2_zone_benchmark}, lib/ with ANGLE's two
# dylibs, the shipped config/ + data/ (the client's ResolveDataPath() finds
# them beside the binary's parent), and run_window_proof.sh. The binaries'
# build-tree rpath (the build user's private ANGLE prefix) is replaced with
# @executable_path/../lib and the bundle is ad-hoc re-signed (docs/macos.md
# stages the ANGLE prefix the same way), so another user can run it.
set -euo pipefail
[ "$(uname -s)" = Darwin ] || { echo "ERROR: Darwin only" >&2; exit 1; }
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:-/Users/Shared/willeq-b1044}"
BUILD="${GLES2_BUILD_DIR:-$REPO/build-gles2}"
ANGLE_OUT="${ANGLE_OUT:-$HOME/willeq-angle/prefix/lib}"

for f in "$BUILD/bin/willeq" "$BUILD/bin/gles2_zone_benchmark" "$ANGLE_OUT/libEGL.dylib" "$ANGLE_OUT/libGLESv2.dylib"; do
    [ -e "$f" ] || { echo "ERROR: missing $f" >&2; exit 1; }
done

rm -rf "$DEST.staging"; mkdir -p "$DEST.staging"/{bin,lib,out}
S="$DEST.staging"
cp "$BUILD/bin/willeq" "$BUILD/bin/gles2_zone_benchmark" "$S/bin/"
cp "$ANGLE_OUT/libEGL.dylib" "$ANGLE_OUT/libGLESv2.dylib" "$S/lib/"
cp -R "$REPO/config" "$S/config"
rsync -a --exclude eq_client --exclude eqe1_client "$REPO/data/" "$S/data/"
cp "$REPO/scripts/macos/run_window_proof.sh" "$S/"

for b in "$S/bin/willeq" "$S/bin/gles2_zone_benchmark"; do
    otool -l "$b" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}' | while read -r rp; do
        case "$rp" in /opt/homebrew/*) ;; *) install_name_tool -delete_rpath "$rp" "$b" ;; esac
    done
    install_name_tool -add_rpath @executable_path/../lib "$b"
done
for f in "$S/lib/libEGL.dylib" "$S/lib/libGLESv2.dylib" "$S/bin/willeq" "$S/bin/gles2_zone_benchmark"; do
    codesign --force -s - "$f"
done

# Proof of what was staged.
{
    echo "staged $(date -u +%Y-%m-%dT%H:%M:%SZ) by $(id -un) from $REPO"
    shasum -a 256 "$S"/bin/* "$S"/lib/*
    for b in "$S/bin/willeq" "$S/bin/gles2_zone_benchmark"; do
        echo "== $b"; otool -L "$b" | sed -n '2,40p'
        otool -l "$b" | awk '/LC_RPATH/{r=1} r&&/ path /{print "  rpath " $2; r=0}'
    done
} > "$S/STAGED.txt"

chmod -R a+rX "$S"
chmod 1777 "$S/out"
if [ -d "$DEST" ]; then
    # Re-stage IN PLACE: out/ may hold the console user's files, which this
    # (build) user cannot delete in the sticky directory; leave out/ alone.
    rsync -a --delete --exclude /out/ "$S/" "$DEST/"
    rm -rf "$S"
else
    mv "$S" "$DEST"
fi
echo "bundle staged at $DEST"
