#!/usr/bin/env bash
# T503 -- publish one ARM cross-build where the Orange Pi can run it.
#   publish_arm_build.sh <sha> [branch]
# hermes-ext workers have no NFS, the Pi does: CI's build-arm job (runner
# dev-u24-arm-nfs) calls this, and a worker then runs
#   ssh <pi> 'WILLEQ_DATA_ROOT=$D $D/bin/willeq ...'   with D=$ARM_PUBLISH_ROOT/<sha>
# Publishes the STRIPPED client plus the tracked config/ + data/ of that commit
# (so a branch's data changes travel with its binary). Keeps the newest
# ARM_PUBLISH_KEEP builds; `fable` and `by-branch/*` symlinks are never pruned targets.
set -euo pipefail
SHA="$1"; BRANCH="${2:-}"
ROOT="${ARM_PUBLISH_ROOT:-/mnt/projects/fable/ci-arm}"
KEEP="${ARM_PUBLISH_KEEP:-12}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$SRC/build-arm-noble/bin/willeq"
[ -x "$BIN" ] || { echo "publish_arm_build: no $BIN"; exit 1; }

DEST="$ROOT/$SHA"; TMP="$ROOT/.tmp.$SHA.$$"
rm -rf "$TMP"; mkdir -p "$TMP/bin" "$ROOT/by-branch"
cp "$BIN" "$TMP/bin/willeq"
arm-linux-gnueabihf-strip "$TMP/bin/willeq"
git -C "$SRC" archive "$SHA" config data | tar -x -C "$TMP"
{ echo "sha=$SHA"; echo "branch=$BRANCH"; echo "built=$(date -Iseconds) on $(hostname)"; file -b "$TMP/bin/willeq"; } > "$TMP/BUILD_INFO"
rm -rf "$DEST"; mv "$TMP" "$DEST"
[ -n "$BRANCH" ] && ln -sfn "../$SHA" "$ROOT/by-branch/$(echo "$BRANCH" | tr '/' '_')"

# Prune: newest KEEP sha dirs stay; anything a by-branch link points at stays.
keepers=$(for l in "$ROOT"/by-branch/*; do [ -L "$l" ] && basename "$(readlink "$l")"; done | sort -u)
ls -1dt "$ROOT"/[0-9a-f]*/ 2>/dev/null | tail -n +"$((KEEP + 1))" | while read -r d; do
    b=$(basename "$d"); echo "$keepers" | grep -qx "$b" || rm -rf "$d"
done
echo "published $DEST ($(du -sh "$DEST" | cut -f1))"
