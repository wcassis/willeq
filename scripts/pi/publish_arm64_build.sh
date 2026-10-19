#!/usr/bin/env bash
# B1030 (#350) -- publish one aarch64 cross-build where the aarch64 SBC lanes can run it.
#   publish_arm64_build.sh <sha> [branch]
# The lanes (scripts/pi/env.sh: 2 = rock64, 3 = rock4d) mount the same NFS share as
# dev-u24; hermes-ext workers do not. Publishes, under $ARM64_PUBLISH_ROOT/<sha>/
# (default /mnt/projects/fable/ci-arm64/<sha>/):
#   bin/<tool>      every binary scripts/build-arm64-noble.sh produced, STRIPPED
#                   (the strip runs inside the willeq-arm64-noble image: dev-u24 has
#                   no aarch64 binutils)
#   config/ data/   the tracked trees of that commit (a branch's data travels with it)
#   BUILD_INFO      sha, branch, when/where, `file` of the client
#   by-branch/<branch> symlink at the root; the newest ARM64_PUBLISH_KEEP sha dirs stay.
# Same shape as publish_arm_build.sh (the armhf/lane-1 publisher), MINUS the LLVM-less
# Mesa prefix step: the aarch64 lanes run the system Mesa (lane 2 Lima 25.2.8, lane 3
# Panfrost 26.0.8); a prefix decision for either is a measured ruling request, not a
# default. This script never touches ci-arm/ (build-arm is not widened; hand-off 2026-10-12).
set -euo pipefail
# 2026-10-18 (B1093 follow-up): three runner uids publish into this one NFS tree (ThinkPad
# gitlab-runner 996, the VM runners 999, dev-u24 user 1000). A sha dir written with the
# default umask is only writable by its owner, so the NEXT host's prune (or a re-publish
# of the same sha) died on 'rm: Permission denied' (fable pipeline 1781, job 9360).
# Everything this script writes is world-writable, and a prune it cannot do is a warning.
umask 000
SHA="$1"; BRANCH="${2:-}"
ROOT="${ARM64_PUBLISH_ROOT:-/mnt/projects/fable/ci-arm64}"
KEEP="${ARM64_PUBLISH_KEEP:-12}"
IMAGE="${ARM64_IMAGE:-willeq-arm64-noble}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$SRC/build-arm64-noble/bin"
[ -x "$OUT/willeq" ] || { echo "publish_arm64_build: no $OUT/willeq (run scripts/build-arm64-noble.sh)"; exit 1; }

DEST="$ROOT/$SHA"; TMP="$ROOT/.tmp.$SHA.$$"
rm -rf "$TMP"; mkdir -p "$TMP/bin" "$ROOT/by-branch"
for b in "$OUT"/*; do [ -f "$b" ] && cp "$b" "$TMP/bin/"; done
# Strip in the cross image (aarch64-linux-gnu-strip lives there, not on the host).
docker run --rm --entrypoint /bin/bash -v "$TMP/bin:/strip" "$IMAGE" \
    -c 'for f in /strip/*; do aarch64-linux-gnu-strip "$f"; done'
git -C "$SRC" archive "$SHA" config data | tar -x -C "$TMP"
{
    echo "sha=$SHA"; echo "branch=$BRANCH"; echo "built=$(date -Iseconds) on $(hostname)"
    echo "lanes=2 (rock64, lima) 3 (rock4d, panfrost) -- scripts/pi/env.sh"
    echo "mesa=system (no LLVM-less prefix on the aarch64 lanes)"
    file -b "$TMP/bin/willeq"
    echo "tools=$(ls "$TMP/bin" | tr '\n' ' ')"
} > "$TMP/BUILD_INFO"
if [ -e "$DEST" ] && ! rm -rf "$DEST" 2>/dev/null; then
    echo "publish: cannot replace $DEST (owned by another publisher uid; fix its mode with chmod -R a+rwX from that host)" >&2
    exit 1
fi
mv "$TMP" "$DEST"
chmod -R a+rwX "$DEST" 2>/dev/null || true
[ -n "$BRANCH" ] && ln -sfn "../$SHA" "$ROOT/by-branch/$(echo "$BRANCH" | tr '/' '_')"

# Prune: newest KEEP sha dirs stay; anything a by-branch link points at stays.
keepers=$(for l in "$ROOT"/by-branch/*; do [ -L "$l" ] && basename "$(readlink "$l")"; done | sort -u)
ls -1dt "$ROOT"/[0-9a-f]*/ 2>/dev/null | tail -n +"$((KEEP + 1))" | while read -r d; do
    b=$(basename "$d"); echo "$keepers" | grep -qx "$b" && continue
    rm -rf "$d" 2>/dev/null || echo "prune: could not remove $d (another publisher uid owns it); leaving it" >&2
done
echo "published $DEST ($(du -sh "$DEST" | cut -f1))"
