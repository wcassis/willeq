#!/bin/bash
set -euo pipefail

# B929 (#247): build a local EQE1 pack tree for the zone budget census sweep,
# because the runner's NFS pack tree is READ-ONLY for the runner.
#
# The census job sweeps every zone in scripts/ci/zone_content_sweep_zones.tsv
# on a runner whose EQE1 tree is the shared NFS mount (/mnt/projects/
# EverQuest_ETC1, the data/eqe1_client symlink). A pack that is STALE --
# B930/B1017's signature, v1/v2 holding a 31-char name (a pre-B910
# truncation such as soldunga.eqe1, refactor/B930/README.md) -- FATALs the
# benchmark's all-or-nothing texture load, so that zone could never be
# asserted against the budgets. The runner CANNOT repair the NFS tree in
# place: it is read-only for the runner (measured, MR !182 pipeline 1174,
# job 6273: eqe1_builder "Cannot open output: data/eqe1_client/soldunga.eqe1"
# after 90 s of a successful .s3d read + ETC1 encode).
#
# So this step builds a local pack tree the sweep reads. The sweep's
# invocation is the gate's memory-coverage form WITHOUT --composite-entities,
# so the benchmark opens packs ONLY by the loader's own fixed names -- the
# eqe1Names array in tools/hwcaps/zone_bench_loader.cpp, where the zone's
# own entries are "zoneName", "zoneName + _obj" and "zoneName + _chr". (The
# composite texture index's on-demand opens are that flag's path and never
# runs in the sweep.) So the local tree holds exactly the MANDATORY set:
#   - every swept zone's three packs (zone, zone_obj, zone_chr);
#   - the fixed pack list (the eqe1Names array's non-zone entries: the
#     global_chr globals, the gequip/gatesequip/... equipment packs,
#     willeq), re-read from the loader's source on every run so no
#     hand-kept copy can drift.
# A mandatory pack is verified with verify_eqe1 --require-v3 (B930/B1017:
# flags exactly the truncation-signature packs, so a clean v1/v2 whose
# names all fit passes) and hard-LINKED into the tree when it is clean (a
# second name for the same inode -- nothing is copied, nothing is written
# in the source tree). A STALE one is REBUILT with eqe1_builder --archive
# <name>.s3d (the client FATAL's own named remedy) into the tree, where the
# write succeeds; the builder emits v3 only when a name is >31 chars, so a
# rebuild of a clean pack would be byte-identical and this path is taken
# only for the ones that are not. A mandatory pack that is MISSING, or
# stale with no .s3d to rebuild from, fails the job (exit 1) -- the sweep
# would die on it. Packs OUTSIDE the mandatory set (the other ~1,100 in
# the NFS tree) are not linked: the runner's out dir is another filesystem,
# so linking all of them would mean copying gigabytes for packs the sweep
# never opens. The NFS tree is left exactly as it was -- B930's open item
# for the runner/Pi/dev-u24 copies stays open (it needs the tree's own
# writer), while this job no longer dies on the asset.
#
# Usage:
#   zone_census_pack_selfheal.sh --verify <verify_eqe1> --builder <eqe1_builder> \
#       --eq-path <EQ client root> --pack-dir <EQE1 NFS root> \
#       --out-dir <local pack tree to build> \
#       [--list <zone list tsv>] [--fixed-list <newline-separated names>]
#
#   --fixed-list defaults to the eqe1Names array of zone_bench_loader.cpp
#     MINUS its zone-derived entries (_obj/_chr suffixes, zone names),
#     re-read from the source on every run; the option exists for the
#     unit test, which drives the script with stubs and its own names.
#
# Exit codes:
#   0  every mandatory pack has a clean v3 pack in the local tree (linked
#      or rebuilt)
#   1  a mandatory pack is missing from the source tree, or its stale
#      rebuild failed, still fails verify, or has no .s3d to rebuild from
#      (the job must not sweep on a tree that FATALs the benchmark)
#   3  bad usage

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIST="$SCRIPT_DIR/zone_content_sweep_zones.tsv"
FIXED_LIST=""
VERIFY=""
BUILDER=""
EQ_PATH=""
PACK_DIR=""
OUT_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST="$2"; shift 2 ;;
        --fixed-list) FIXED_LIST="$2"; shift 2 ;;
        --verify) VERIFY="$2"; shift 2 ;;
        --builder) BUILDER="$2"; shift 2 ;;
        --eq-path) EQ_PATH="$2"; shift 2 ;;
        --pack-dir) PACK_DIR="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done

for need in "$VERIFY" "$BUILDER" "$EQ_PATH" "$PACK_DIR" "$OUT_DIR"; do
    [ -n "$need" ] || { echo "FATAL: missing --verify/--builder/--eq-path/--pack-dir/--out-dir" >&2; exit 3; }
done
[ -x "$VERIFY" ] || { echo "FATAL: verifier not executable: $VERIFY" >&2; exit 3; }
[ -x "$BUILDER" ] || { echo "FATAL: builder not executable: $BUILDER" >&2; exit 3; }
[ -d "$EQ_PATH" ] || { echo "FATAL: EQ client root missing: $EQ_PATH" >&2; exit 3; }
[ -d "$PACK_DIR" ] || { echo "FATAL: pack dir missing: $PACK_DIR (the data/eqe1_client symlink)" >&2; exit 3; }
[ -f "$LIST" ] || { echo "FATAL: zone list missing: $LIST" >&2; exit 3; }

# The zone list's data rows (the swept zones; the not_in_t490_census rows
# are documentation and are never swept).
ZONES=""
while IFS=$'\t' read -r zone _exp _bytes notes; do
    [ -n "$zone" ] || continue
    case "${notes:-}" in *not_in_t490_census*) continue ;; esac
    ZONES="${ZONES}${zone}"$'\n'
done < <(tail -n +2 "$LIST")

# The fixed list: the loader's eqe1Names array, re-read from the source
# (unless overridden). The array's zone-derived entries (_obj/_chr suffix
# forms and bare zone names) are NOT fixed -- they are per-zone and come
# from the zone list, so strip them out. A missing source file is a FATAL
# -- the mandatory set must be known, not guessed.
FIXED_LIST_FILE="$SCRIPT_DIR/../../tools/hwcaps/zone_bench_loader.cpp"
if [ -z "$FIXED_LIST" ]; then
    [ -f "$FIXED_LIST_FILE" ] || { echo "FATAL: fixed list source missing: $FIXED_LIST_FILE" >&2; exit 3; }
    # The array's string literals, one per line, minus the zone-derived
    # ones. The zone entries are NOT literals -- they are built from
    # config.zoneName at runtime -- so the only zone-derived literals in
    # the array are the bare "_obj" and "_chr" strings of the
    # config.zoneName + "_obj" lines. A suffix filter would eat
    # global_chr and friends, so match exactly.
    FIXED_LIST="$(awk '/eqe1Names = {/,/};/' "$FIXED_LIST_FILE" \
        | grep -o '"[a-zA-Z0-9_]*"' | tr -d '"' \
        | grep -v -x -e '_obj' -e '_chr' \
        | sort -u || true)"
fi
# A reformat of that array would leave the extraction empty and silently
# un-mandate every fixed pack: the extraction must name the one entry the
# loader's own fallback is guaranteed to keep (global_chr).
case "$FIXED_LIST" in
    *global_chr*) ;;
    *) echo "FATAL: fixed list from $FIXED_LIST_FILE does not name global_chr (extraction broke): '$FIXED_LIST'" >&2; exit 3 ;;
esac

# MANDATORY = every swept zone's three packs + the fixed list.
MUST=""
while IFS= read -r z; do
    [ -n "$z" ] || continue
    MUST="${MUST}${z}"$'\n'
    MUST="${MUST}${z}_obj"$'\n'
    MUST="${MUST}${z}_chr"$'\n'
done < <(printf '%s' "$ZONES" | sort -u)
MUST="${MUST}${FIXED_LIST}"$'\n'
MUST="$(printf '%s' "$MUST" | sort -u)"
is_mandatory() {
    printf '%s\n' "$MUST" | grep -qx -- "$1"
}

mkdir -p "$OUT_DIR"

# make_usable <name>: ensure <name>.eqe1 exists in $OUT_DIR.
# MANDATORY pack: verify it with verify_eqe1 --require-v3 (the B930/B1017
# truncation-signature audit; a clean v1/v2 whose names all fit passes);
# hard-link it when it is clean (a second name for the source inode --
# nothing is copied, nothing is written in the source tree). A STALE one is
# REBUILT with eqe1_builder --archive <name>.s3d (the client FATAL's own
# named remedy) into $OUT_DIR, where the write succeeds; the builder emits
# v3 only when a name is >31 chars, so a rebuild of a clean pack would be
# byte-identical and this path is taken only for the ones that are not.
# Sets REBUILT_NOW.
rebuilt_now=0
make_usable() {
    local name="$1"
    local src="$PACK_DIR/$name.eqe1" dst="$OUT_DIR/$name.eqe1"
    if [ -f "$dst" ]; then
        return 0  # already linked or rebuilt
    fi
    if ! is_mandatory "$name"; then
        return 0  # not in the mandatory set: not linked, not needed
    fi
    if [ ! -f "$src" ]; then
        echo "FATAL: mandatory pack $name.eqe1 missing from $PACK_DIR -- the sweep would die on it" >&2
        return 1
    fi
    # Capture, then match: under set -o pipefail a "verify | grep" pipeline
    # carries verify's own exit 1 into the pipeline status, so the STALE
    # line would never be seen on a failing verify.
    local vout
    if vout=$("$VERIFY" --require-v3 "$src" 2>&1); then
        # A hard link keeps one inode; the fallback is a plain copy (cross-
        # device, e.g. the pack dir not on the same filesystem as $OUT_DIR).
        ln "$src" "$dst" 2>/dev/null || cp "$src" "$dst"
        return 0
    fi
    if ! printf '%s\n' "$vout" | grep -q "STALE"; then
        echo "FATAL: verify of $src failed without a STALE line (not a format issue):" >&2
        printf '%s\n' "$vout" >&2
        return 1
    fi
    local s3d="$EQ_PATH/$name.s3d"
    if [ ! -f "$s3d" ]; then
        # Mandatory and stale, but nothing to rebuild from: the sweep's run
        # would FATAL on the pack, so fail here, where the remedy is known.
        echo "FATAL: mandatory stale pack $src but no source $s3d to rebuild from" >&2
        return 1
    fi
    echo "REBUILD: $name.eqe1 (stale, B930 signature) from $s3d -> $OUT_DIR"
    if ! "$BUILDER" --eq-path "$EQ_PATH" --archive "$name.s3d" --output "$OUT_DIR"; then
        echo "FATAL: rebuild of $name.eqe1 failed" >&2
        return 1
    fi
    if "$VERIFY" --require-v3 "$dst" >/dev/null 2>&1; then
        echo "  $name: rebuilt, verify clean"
        rebuilt_now=1
        return 0
    fi
    echo "FATAL: rebuilt $dst still fails verify_eqe1 --require-v3" >&2
    return 1
}

# Make every mandatory pack usable.
linked=0
rebuilt=0
failed=0
while IFS= read -r name; do
    [ -n "$name" ] || continue
    rebuilt_now=0
    if make_usable "$name"; then
        [ -f "$OUT_DIR/$name.eqe1" ] || { echo "FATAL: mandatory pack $name.eqe1 missing from $PACK_DIR -- the sweep would die on it" >&2; failed=1; continue; }
        [ "$rebuilt_now" -eq 1 ] && rebuilt=$((rebuilt + 1)) || linked=$((linked + 1))
    else
        failed=1
    fi
done < <(printf '%s\n' "$MUST")

echo "=== B929 pack self-heal: $linked linked, $rebuilt rebuilt, $failed failed ($OUT_DIR) ==="
exit "$failed"
