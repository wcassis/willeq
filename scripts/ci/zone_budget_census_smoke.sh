#!/usr/bin/env bash
# B929 (#247): the ctest leg of the zone budget census.
#
# The full 118-zone census is the gate-stage CI job (zone-budget-census,
# .gitlab-ci.yml -- one gles2_zone_benchmark per zone measured at ~3 s each on
# this host's llvmpipe, ~10 min total: refactor/B929/README.md). Too slow for
# the ctest suite; this smoke is its net:
#
#   1. the RED half: a synthetic sweep dir whose growthplane row carries
#      B927's measured over-budget figure (placeableObjectsBytes 28,687,788
#      against the 25,165,824 budget) MUST be failed by
#      scripts/ci/zone_budget_census.py -- a checker that lets the very
#      defect that filed this issue pass is itself broken;
#   2. the GREEN half: the smoke zones (qeynos2 / sebilis / iceclad, the
#      sweep's permanent smoke list, one per expansion) are swept fresh and
#      must all be inside every budget.
#
# Both halves run the same --check-budgets path the CI job runs.
#
# Exit codes (consumed as ctest SKIP_RETURN_CODE 125, like the B852 smoke):
#   0  both halves held
#   1  a smoke zone is over a budget, or the synthetic case did not fail
#   2  the benchmark binary or an asset root is absent -> ctest SKIP, loud
#   3  bad zone list

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"

bench_bin="$REPO/build/bin/gles2_zone_benchmark"
[ -x "$bench_bin" ] || bench_bin="$REPO/build-gles2/bin/gles2_zone_benchmark"
if [ ! -x "$bench_bin" ]; then
    echo "SKIP (B929 census smoke): no gles2_zone_benchmark binary (build: cmake --build build --target gles2_zone_benchmark, or scripts/build-gles2-x86.sh for build-gles2/)" >&2
    exit 125
fi
for p in "$REPO/data/eq_client" "$REPO/data/eqe1_client"; do
    if [ ! -d "$p" ]; then
        echo "SKIP (B929 census smoke): EQ asset root missing: $p (gitignored symlink -- .gitlab-ci.yml before_script re-makes it; CI sets WILLEQ_REQUIRE_ASSETS=1 where this is a hard fail)" >&2
        exit 125
    fi
done

WORK="${B929_OUT_DIR:-$REPO/build/b929_census_smoke}"
rm -rf "$WORK"
mkdir -p "$WORK/real/logs" "$WORK/synthetic/logs"

# --- half 1: the synthetic RED case. -------------------------------------
# The over-budget JSON is hand-written from B927's MEASURED census row
# (refactor/B927/category_census.tsv: growthplane estimate:placeableObjectsBytes
# 28,687,788) -- this is test input for the checker, not evidence for the
# tree (the evidence is the GREEN half below and the CI job). The budget
# 25,165,824 is the orangepi preset's placeableObjectsMemoryBytes
# (config/constrained_presets.json), unchanged.
cat > "$WORK/synthetic/results.tsv" <<'TSV'
zone	expansion	status	exit_code	wall_s	fatal_count	error_count	first_fatal	missing_texture_count	missing_eqe1_count	placeholder_model_count	zone_load_complete	peak_rss_anon	clean
growthplane	velious	ok	0	4	0	0		0	0	0	y	100000000	1
TSV
cat > "$WORK/synthetic/logs/growthplane.mem.json" <<'JSON'
{
  "zone": "growthplane",
  "placeableObjectsBytes": 28687788,
  "placeableObjectsBudgetBytes": 25165824
}
JSON
if python3 "$SCRIPT_DIR/zone_budget_census.py" --check-budgets \
        --sweep-dir "$WORK/synthetic" --zones growthplane > "$WORK/synthetic.out" 2> "$WORK/synthetic.err"; then
    echo "FAIL (B929 census smoke): the synthetic over-budget growthplane (28,687,788 > 25,165,824) was NOT failed by zone_budget_census.py" >&2
    cat "$WORK/synthetic.out" >&2
    exit 1
fi
grep -q "placeableObjectsBytes 28687788 > 25165824" "$WORK/synthetic.out" || {
    echo "FAIL (B929 census smoke): the checker failed the synthetic case but did not name the over-budget figure:" >&2
    cat "$WORK/synthetic.out" >&2
    exit 1
}
echo "  synthetic over-budget case: failed as required (RED half)"

# --- half 2: the real smoke zones, freshly swept. --------------------------
ZONES="${B929_CENSUS_ZONES:-qeynos2,sebilis,iceclad}"
"$SCRIPT_DIR/zone_content_sweep.sh" \
    --zones "$ZONES" \
    --out-dir "$WORK/real" > "$WORK/sweep.log" 2>&1 || {
        echo "FAIL (B929 census smoke): the sweep itself did not finish (its row says why):" >&2
        tail -n 20 "$WORK/sweep.log" >&2
        exit 1
    }
python3 "$SCRIPT_DIR/zone_budget_census.py" --check-budgets \
        --sweep-dir "$WORK/real" --zones "$ZONES" \
        --out "$WORK/budget_rows.tsv" || {
            echo "FAIL (B929 census smoke): a smoke zone is over a budget (see above)" >&2
            exit 1
        }
echo "  smoke zones $ZONES: inside every budget (GREEN half)"
echo "rows: $WORK/budget_rows.tsv"
exit 0
