#!/bin/bash
set -euo pipefail

# B929 (#247): heal stale EQE1 zone packs before the zone budget census sweep.
#
# The census job sweeps every zone in scripts/ci/zone_content_sweep_zones.tsv
# on a runner whose EQE1 tree is the shared NFS mount (/mnt/projects/
# EverQuest_ETC1, the data/eqe1_client symlink). A pack that is STALE --format
# B9 (v1/v2 holding a 31-char name, i.e. a pre-B910 truncation such as
# soldunga.eqe1, refactor/B930/README.md) FATALs the benchmark's all-or-
# nothing texture load, so that zone could never be asserted against the
# budgets. The client's own FATAL names the remedy: rebuild with
# tools/convert_all_to_eqe1.sh. This step applies it for the swept zones:
# verify_eqe1 --require-v3 flags exactly the truncation-signature packs
# (a clean v1/v2 whose names all fit passes), and eqe1_builder --archive
# rebuilds one zone from its .s3d (the builder emits v3 only when a name is
# >31 chars, so a rebuild of a clean v1 pack is byte-identical and this step
# is a no-op for the 117 zones whose packs are fine).
#
# The rebuild overwrites the runner's NFS pack in place -- the same bytes the
# hermes-ext tree already carries for soldunga (B910 rebuilt it there on
# 2026-09-21; B930 left the runner/dev-u24/Pi copies open).
#
# Usage:
#   zone_census_pack_selfheal.sh --verify <verify_eqe1> --builder <eqe1_builder> \
#       --eq-path <EQ client root> --pack-dir <EQE1 root> \
#       [--list <zone list tsv>]
#
# Exit codes:
#   0  every swept zone's pack is clean v3 or was rebuilt to a clean v3
#   1  a stale pack's rebuild failed or still fails verify (the job must not
#      sweep on it)
#   3  bad usage

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIST="$SCRIPT_DIR/zone_content_sweep_zones.tsv"
VERIFY=""
BUILDER=""
EQ_PATH=""
PACK_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST="$2"; shift 2 ;;
        --verify) VERIFY="$2"; shift 2 ;;
        --builder) BUILDER="$2"; shift 2 ;;
        --eq-path) EQ_PATH="$2"; shift 2 ;;
        --pack-dir) PACK_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 3 ;;
    esac
done

for need in "$VERIFY" "$BUILDER" "$EQ_PATH" "$PACK_DIR"; do
    [ -n "$need" ] || { echo "FATAL: missing --verify/--builder/--eq-path/--pack-dir" >&2; exit 3; }
done
[ -x "$VERIFY" ] || { echo "FATAL: verifier not executable: $VERIFY" >&2; exit 3; }
[ -x "$BUILDER" ] || { echo "FATAL: builder not executable: $BUILDER" >&2; exit 3; }
[ -d "$EQ_PATH" ] || { echo "FATAL: EQ client root missing: $EQ_PATH" >&2; exit 3; }
[ -d "$PACK_DIR" ] || { echo "FATAL: pack dir missing: $PACK_DIR (the data/eqe1_client symlink)" >&2; exit 3; }
[ -f "$LIST" ] || { echo "FATAL: zone list missing: $LIST" >&2; exit 3; }

rebuilt=0
failed=0
line=0
while IFS=$'\t' read -r zone _rest; do
    line=$((line + 1))
    [ "$line" -eq 1 ] && continue  # header row
    [ -n "$zone" ] || continue
    pack="$PACK_DIR/$zone.eqe1"
    # A missing pack (an unconverted zone) is not STALE; the sweep's own row
    # reports it, so the self-heal only heals what verify can flag.
    [ -f "$pack" ] || { echo "  $zone: pack absent, skipping verify (sweep row will name it)"; continue; }
    if out=$("$VERIFY" --require-v3 "$pack" 2>&1); then
        continue
    fi
    if ! printf '%s\n' "$out" | grep -q "STALE"; then
        echo "FATAL: verify of $pack failed without a STALE line (not a format issue):" >&2
        printf '%s\n' "$out" >&2
        failed=1
        continue
    fi
    s3d="$EQ_PATH/$zone.s3d"
    if [ ! -f "$s3d" ]; then
        echo "FATAL: stale pack $pack but no source $s3d to rebuild from" >&2
        failed=1
        continue
    fi
    echo "REBUILD: $pack (stale, B930 signature) from $s3d"
    if ! "$BUILDER" --eq-path "$EQ_PATH" --archive "$zone.s3d" --output "$PACK_DIR"; then
        echo "FATAL: rebuild of $pack failed" >&2
        failed=1
        continue
    fi
    if "$VERIFY" --require-v3 "$pack" >/dev/null 2>&1; then
        echo "  $zone: rebuilt, verify clean"
        rebuilt=$((rebuilt + 1))
    else
        echo "FATAL: rebuilt $pack still fails verify_eqe1 --require-v3" >&2
        failed=1
    fi
done < "$LIST"

echo "=== B929 pack self-heal: $rebuilt rebuilt, $failed failed ==="
exit "$failed"
