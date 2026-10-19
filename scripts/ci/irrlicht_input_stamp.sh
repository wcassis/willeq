#!/bin/bash
# B654 (issue #56): stamp the CONTENT of the inputs that scripts/build-gles2-x86.sh
# turns into third_party/irrlicht-gles2-x86/lib/libIrrlicht.a, so the runner can
# rebuild its warm Irrlicht when a committed change actually changes the library.
#
# WHY: .gitlab-ci.yml keeps the locally-built Irrlicht warm across pipelines
# (GIT_CLEAN_FLAGS: -ffdx -e third_party/irrlicht-gles2-x86) and the build job
# only re-ran build-gles2-x86.sh --irrlicht-only when libIrrlicht.a was ABSENT.
# A committed fix to the device sources (docker/irrlicht-drm-x86/CIrrDeviceFB*.cpp,
# the new x11_keysym_map.h) therefore left the warm lib -- and the whole pipeline
# -- running the old code. The stamp makes the build job compare "what inputs
# produced this .a" against "what the inputs are now" and rebuild on mismatch.
#
# The digest is over the file CONTENTS (relative path + sha256 each, sorted), so
# it is identical on every checkout of the same commit -- a stable, mtime- and
# git-state-independent "does the warm lib match these inputs" signal.
#
# IN-SCOPE inputs (the things that drift with ordinary work): the two driver
# source dirs the build script copies into the build area, the shared
# DRM/GLES2 compile-config patch, and the build script itself (its sed edits and
# make flags are part of what the .a encodes). OUT OF SCOPE, deliberately: the
# pristine Irrlicht reference at /mnt/projects/irrlicht-1.8.5 -- a fixed
# read-only source that never changes with ordinary work. Updating THAT is a
# manual cold-rebuild event (docs/ci.md, "cold pipeline"), not something a
# stamp should chase.
#
# Usage:
#   irrlicht_input_stamp.sh verify <stamp-file>   # exit 0 if stored == computed, else 1
#   irrlicht_input_stamp.sh write  <stamp-file>   # write the computed digest
#
# scripts/build-gles2-x86.sh calls `write` immediately after producing the lib
# (both the --irrlicht-only and full paths), so the stamp is always authoritative
# for the lib that just landed. The CI build job calls `verify` to decide
# whether to skip the warm lib.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

compute() {
    {
        # The two driver source dirs copied into the build area. Hash the whole
        # dirs (content + relative path, sorted) so adding a copied file also
        # changes the digest; a stray non-copied file in the dir causes at most
        # one harmless extra rebuild.
        find docker/irrlicht-drm docker/irrlicht-drm-x86 -type f -print0 \
            | LC_ALL=C sort -z \
            | while IFS= read -r -d '' f; do
                printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$f"
            done
        # The shared patch and the build script itself.
        sha256sum docker/irrlicht-drm-patch.py scripts/build-gles2-x86.sh \
            scripts/build-gles2-darwin.sh scripts/ci/irrlicht_gles2_files.sh
    } | sha256sum | cut -d' ' -f1
}

cmd="${1:-}"
stamp="${2:-}"
if [ -z "$cmd" ] || [ -z "$stamp" ]; then
    echo "usage: $0 {verify|write} <stamp-file>" >&2
    exit 2
fi

computed="$(compute)"
case "$cmd" in
    write)
        mkdir -p "$(dirname "$stamp")"
        printf '%s\n' "$computed" > "$stamp"
        echo "irrlicht input stamp written: $computed"
        ;;
    verify)
        if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$computed" ]; then
            echo "irrlicht input stamp matches (warm lib is current): $computed"
            exit 0
        fi
        stored="none"
        [ -f "$stamp" ] && stored="$(cat "$stamp")"
        echo "irrlicht input stamp MISMATCH (stored=$stored computed=$computed)" >&2
        exit 1
        ;;
    *)
        echo "usage: $0 {verify|write} <stamp-file>" >&2
        exit 2
        ;;
esac
