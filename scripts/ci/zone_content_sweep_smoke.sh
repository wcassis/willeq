#!/usr/bin/env bash
# B852: the 3-zone smoke leg of the offline zone-content sweep, driven by ctest.
#
# Runs scripts/ci/zone_content_sweep.sh over ONE classic, ONE Kunark and ONE
# Velious zone (qeynos2 / sebilis / iceclad -- one per expansion, tagged in
# scripts/ci/zone_content_sweep_zones.tsv) and fails on any non-clean row.
# The 3 zones are the sweep's permanent smoke list; a wider list can be
# injected only through B852_SWEEP_ZONES (used by the test's own red check).
#
# Exit codes (consumed as ctest SKIP_RETURN_CODE 125):
#   0  all three zones clean
#   1  a zone did not load clean (the sweep's TSV row says why)
#   2  the benchmark binary or an asset root is absent -> ctest SKIP, loud
#   3  bad zone list (a B852_SWEEP_ZONES name is not in the committed list)

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"

ZONES="${B852_SWEEP_ZONES:-qeynos2,sebilis,iceclad}"

# Loud skip, ctest convention (see visual_gate above in tests/cmake/eqe1.cmake):
# 125 -> SKIP, with a line on stderr saying exactly what is missing.
bench_bin="$REPO/build/bin/gles2_zone_benchmark"
[ -x "$bench_bin" ] || bench_bin="$REPO/build-gles2/bin/gles2_zone_benchmark"
if [ ! -x "$bench_bin" ]; then
    echo "SKIP (B852 smoke): no gles2_zone_benchmark binary (build: cmake --build build --target gles2_zone_benchmark, or scripts/build-gles2-x86.sh for build-gles2/)" >&2
    exit 125
fi
for p in "$REPO/data/eq_client" "$REPO/data/eqe1_client"; do
    if [ ! -d "$p" ]; then
        echo "SKIP (B852 smoke): EQ asset root missing: $p (gitignored symlink -- .gitlab-ci.yml before_script re-makes it; CI sets WILLEQ_REQUIRE_ASSETS=1 where this is a hard fail)" >&2
        exit 125
    fi
done
# A named zone that is not in the committed list is a usage error (exit 3),
# not a quiet zero-row sweep; zone_content_sweep.sh validates --zones names
# against the list and says which name is unknown.

exec "$SCRIPT_DIR/zone_content_sweep.sh" \
    --zones "$ZONES" \
    --out-dir "${B852_OUT_DIR:-$REPO/build/b852_sweep_smoke}"
