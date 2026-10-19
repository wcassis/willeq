#!/usr/bin/env bash
# T503 -- publish one ARM cross-build where the Orange Pi can run it.
#   publish_arm_build.sh <sha> [branch]
# hermes-ext workers have no NFS, the Pi does: CI's build-arm job (runner
# dev-u24-arm-nfs) calls this, and a worker then runs
#   ssh <pi> 'WILLEQ_DATA_ROOT=$D $D/bin/willeq ...'   with D=$ARM_PUBLISH_ROOT/<sha>
# Publishes the STRIPPED client plus the tracked config/ + data/ of that commit
# (so a branch's data changes travel with its binary). Keeps the newest
# ARM_PUBLISH_KEEP builds; `fable` and `by-branch/*` symlinks are never pruned targets.
#
# B935 (#253, Will 2026-09-30 "ship the prefix"): the LLVM-less Mesa ships
# with the client. Per sha this also publishes
#   bin/willeq-nollvm  the same stripped binary with DT_RPATH=<Pi prefix>/lib
#                      (scripts/pi/mesa_nollvm_select.sh) -- what the Pi rigs
#                      run by default; bin/willeq (no rpath, the system Mesa)
#                      is the WILLEQ_MESA=system opt-out and every A side
#   mesa               a symlink to the versioned store dir
#                      ($MESA_NOLLVM_STORE: the pinned tarball + prefix/),
#                      made or verified by build_mesa_nollvm.sh --ensure
#   BUILD_INFO         + the Mesa version, tarball sha256 and Pi prefix
# A Mesa failure still publishes the rest, WITHOUT bin/willeq-nollvm, and
# exits 1 so the job is red.
set -euo pipefail
# 2026-10-18 (B1093 follow-up): three runner uids publish into this one NFS tree (ThinkPad
# gitlab-runner 996, the VM runners 999, dev-u24 user 1000). A sha dir written with the
# default umask is only writable by its owner, so the NEXT host's prune (or a re-publish
# of the same sha) died on 'rm: Permission denied' (fable pipeline 1781, job 9360).
# Everything this script writes is world-writable, and a prune it cannot do is a warning.
umask 000
SHA="$1"; BRANCH="${2:-}"
ROOT="${ARM_PUBLISH_ROOT:-/mnt/projects/fable/ci-arm}"
KEEP="${ARM_PUBLISH_KEEP:-12}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$SRC/build-arm-noble/bin/willeq"
[ -x "$BIN" ] || { echo "publish_arm_build: no $BIN"; exit 1; }
# B975 (#293): the name-tag batch tool, when this build has it (the branch's
# docker/Dockerfile.arm-noble builds it into the output) -- a stripped
# bin/nametag_drawcost (system Mesa) and the same with DT_RPATH to the
# LLVM-less prefix, bin/nametag_drawcost-nollvm (what the Pi runs).
NDC_BIN="$SRC/build-arm-noble/bin/nametag_drawcost"
HAVE_NDC=0
[ -x "$NDC_BIN" ] && HAVE_NDC=1
# B971 (#289): the gate benchmark runs on the Pi for the golden poses
# (scripts/b971/run_gate_poses.sh). Same shape as nametag_drawcost: a
# stripped bin/gles2_zone_benchmark (system Mesa) and, when the Mesa step
# succeeded, bin/gles2_zone_benchmark-nollvm (the Mesa-prefix rpath copy).
BENCH_BIN="$SRC/build-arm-noble/bin/gles2_zone_benchmark"
HAVE_BENCH=0
[ -x "$BENCH_BIN" ] && HAVE_BENCH=1
: "${MESA_NOLLVM_STORE_ROOT:=$ROOT/mesa}"
# shellcheck source=mesa_nollvm.env
source "$SRC/scripts/pi/mesa_nollvm.env"

DEST="$ROOT/$SHA"; TMP="$ROOT/.tmp.$SHA.$$"
rm -rf "$TMP"; mkdir -p "$TMP/bin" "$ROOT/by-branch"
cp "$BIN" "$TMP/bin/willeq"
arm-linux-gnueabihf-strip "$TMP/bin/willeq"
git -C "$SRC" archive "$SHA" config data | tar -x -C "$TMP"
{ echo "sha=$SHA"; echo "branch=$BRANCH"; echo "built=$(date -Iseconds) on $(hostname)"; file -b "$TMP/bin/willeq"; } > "$TMP/BUILD_INFO"

MESA_OK=0
if nice -n 10 bash "$SRC/scripts/pi/build_mesa_nollvm.sh" --ensure \
   && bash "$SRC/scripts/pi/mesa_nollvm_select.sh" "$TMP/bin/willeq" "$TMP/bin/willeq-nollvm" "$MESA_NOLLVM_PI_PREFIX" \
        > "$TMP/willeq-nollvm.dynamic.txt"; then
    ln -sfn "$MESA_NOLLVM_STORE" "$TMP/mesa"
    {
        echo "mesa_nollvm_version=$MESA_NOLLVM_VERSION"
        echo "mesa_nollvm_tarball=$MESA_NOLLVM_TARBALL sha256=$(mesa_nollvm_pin)"
        echo "mesa_nollvm_pi_prefix=$MESA_NOLLVM_PI_PREFIX"
        echo "willeq_nollvm_rpath=$MESA_NOLLVM_PI_PREFIX/lib"
    } >> "$TMP/BUILD_INFO"
    MESA_OK=1
else
    rm -f "$TMP/bin/willeq-nollvm"
    echo "mesa_nollvm=FAILED (no bin/willeq-nollvm; see the build-arm job log)" >> "$TMP/BUILD_INFO"
fi
if [ "$HAVE_NDC" -eq 1 ]; then
    cp "$NDC_BIN" "$TMP/bin/nametag_drawcost"
    arm-linux-gnueabihf-strip "$TMP/bin/nametag_drawcost"
    if [ "$MESA_OK" -eq 1 ] \
        && bash "$SRC/scripts/pi/mesa_nollvm_select.sh" "$TMP/bin/nametag_drawcost" "$TMP/bin/nametag_drawcost-nollvm" "$MESA_NOLLVM_PI_PREFIX" \
            > "$TMP/nametag_drawcost-nollvm.dynamic.txt" 2>&1; then
        echo "nametag_drawcost=published (bin/nametag_drawcost, bin/nametag_drawcost-nollvm)" >> "$TMP/BUILD_INFO"
    else
        rm -f "$TMP/bin/nametag_drawcost-nollvm"
        echo "nametag_drawcost=stripped only (bin/nametag_drawcost-nollvm missing; see nametag_drawcost-nollvm.dynamic.txt)" >> "$TMP/BUILD_INFO"
    fi
fi
if [ "$HAVE_BENCH" -eq 1 ]; then
    cp "$BENCH_BIN" "$TMP/bin/gles2_zone_benchmark"
    arm-linux-gnueabihf-strip "$TMP/bin/gles2_zone_benchmark"
    if [ "$MESA_OK" -eq 1 ] \
        && bash "$SRC/scripts/pi/mesa_nollvm_select.sh" "$TMP/bin/gles2_zone_benchmark" "$TMP/bin/gles2_zone_benchmark-nollvm" "$MESA_NOLLVM_PI_PREFIX" \
            > "$TMP/gles2_zone_benchmark-nollvm.dynamic.txt" 2>&1; then
        echo "gles2_zone_benchmark=published (bin/gles2_zone_benchmark, bin/gles2_zone_benchmark-nollvm)" >> "$TMP/BUILD_INFO"
    else
        rm -f "$TMP/bin/gles2_zone_benchmark-nollvm"
        echo "gles2_zone_benchmark=stripped only (bin/gles2_zone_benchmark-nollvm missing; see gles2_zone_benchmark-nollvm.dynamic.txt)" >> "$TMP/BUILD_INFO"
    fi
fi
if [ -e "$DEST" ] && ! rm -rf "$DEST" 2>/dev/null; then
    echo "publish: cannot replace $DEST (owned by another publisher uid; fix its mode with chmod -R a+rwX from that host)" >&2
    exit 1
fi
mv "$TMP" "$DEST"
chmod -R a+rwX "$DEST" 2>/dev/null || true
[ -n "$BRANCH" ] && ln -sfn "../$SHA" "$ROOT/by-branch/$(echo "$BRANCH" | tr '/' '_')"

# Prune: newest KEEP sha dirs stay; anything a by-branch link points at stays.
# (The Mesa store, $ROOT/mesa, is not a sha dir and is never pruned.)
keepers=$(for l in "$ROOT"/by-branch/*; do [ -L "$l" ] && basename "$(readlink "$l")"; done | sort -u)
ls -1dt "$ROOT"/[0-9a-f]*/ 2>/dev/null | tail -n +"$((KEEP + 1))" | while read -r d; do
    b=$(basename "$d"); echo "$keepers" | grep -qx "$b" && continue
    if [ ! -w "$d" ]; then
        echo "publish_arm_build: cannot prune $d (not writable by this runner)" >&2
        continue
    fi
    rm -rf "$d" 2>/dev/null || echo "prune: could not remove $d (another publisher uid owns it); leaving it" >&2
done
echo "published $DEST ($(du -sh "$DEST" | cut -f1))"
if [ "$MESA_OK" -ne 1 ]; then
    echo "publish_arm_build: the LLVM-less Mesa step FAILED -- $DEST has no bin/willeq-nollvm" >&2
    exit 1
fi
