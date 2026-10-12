#!/usr/bin/env bash
# B852: the offline zone-content sweep.
#
# Will, 2026-09-20 (docs/decisions.md): "velious and kunark zones are part of
# the willeq target, so we need to verify that content works." The CI gate
# loads three classic zones and nothing else; this sweep loads EVERY era zone
# (the 118-zone T490 census) offline, ONE gles2_zone_benchmark process per
# zone, and records one TSV row per zone so a content defect in any Kunark or
# Velious zone shows up instead of sitting unverified.
#
# What "clean" means (a row is clean iff ALL hold):
#   exit code 0,
#   zero FATAL and zero ERROR log lines,
#   "Zone loaded in" present (the loader's own success line,
#   tools/hwcaps/zone_bench_loader.cpp),
#   "Zone placeholder: 0 triangles" absent (the zone drew nothing),
#   "No entity meshes loaded" absent (no model could be built for ANY test
#   race -- placeholder-cube territory; the benchmark prints per-race
#   "getRaceModelData returned NULL" and this aggregate line),
#   peak RssAnon over the [memledger] lines under the 256 MB ceiling
#   (docs/decisions.md, T293 envelope).
# A zone that dies on the 4 GiB worker cap (SIGKILL) or hits the per-zone
# timeout is a RESULT: it gets a row (status=died_on_cap / status=timeout)
# and the sweep moves on -- it is exactly the kind of thing B852 exists to
# surface, not a reason to stop.
#
# Per-zone invocation: the gate's memory-coverage form
# (scripts/ci/visual_gate.py run_memory_coverage_benchmark(): --eqe1-path /
# --objects / --entities / --test-races) with a fixed tiny frame count, so
# the run loads the whole zone plus its objects and a handful of character
# models and renders a few frames -- content, not a performance benchmark.
# B927: each zone's memory JSON (the same file visual_gate.py asserts the
# budgets against) is kept beside its log as logs/<zone>.mem.json, so the
# per-category ledger live bytes at the asserted point and s3dZoneSourceBytes
# can be tabulated for every zone (refactor/B927/zone_parse_census.py).
#
# Watchdog: a child that stops writing to its log for WATCHDOG_S seconds is
# presumed dead/hung (the process-level timeout alone cannot catch a zone
# whose loader hangs without logging), so it is SIGKILLed and recorded.
#
# Exit codes:
#   0  every swept row is clean
#   1  at least one non-clean row (details in the TSV + stdout table)
#   2  the benchmark binary or an asset root is absent (nothing was swept)
#   3  bad usage
#
# Row status values: ok (clean row), failed (nonzero exit or a content check
# failed), timeout / watchdog (this script SIGKILLed the child),
# died_on_cap (rc 137 from OUTSIDE the script -- the 4 GiB cgroup cap),
# died_by_signal_N (any other signal).

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
# B852: the ctest smoke runs in the tree ctest was configured in; the CI gate
# stage builds the benchmark into build-gles2/. Check both, so one script
# serves both callers (the card's own build tree is build/).
BENCH_BIN="$REPO/build/bin/gles2_zone_benchmark"
if [ ! -x "$BENCH_BIN" ] && [ -x "$REPO/build-gles2/bin/gles2_zone_benchmark" ]; then
    BENCH_BIN="$REPO/build-gles2/bin/gles2_zone_benchmark"
fi
EQ_PATH="$REPO/data/eq_client"
EQE1_PATH="$REPO/data/eqe1_client"
LIST="$SCRIPT_DIR/zone_content_sweep_zones.tsv"
OUT_DIR="${B852_OUT_DIR:-$REPO/build/b852_sweep}"
TIMEOUT_S="${B852_TIMEOUT_S:-300}"
WATCHDOG_S="${B852_WATCHDOG_S:-120}"
FRAMES="${B852_FRAMES:-5}"
WARMUP="${B852_WARMUP:-1}"
RES_W="${B852_RES_W:-800}"
RES_H="${B852_RES_H:-600}"
ENTITIES="${B852_ENTITIES:-5}"
RACES="${B852_RACES:-1,2,3,4,5}"
OCCLUDER_BUDGET="${B852_OCCLUDER_BUDGET:-16}"
# docs/decisions.md (T293 envelope): 256 MB.
RSS_CEILING_BYTES=268435456

usage() {
    cat <<EOF
Usage: $(basename "$0") [--list FILE] [--out-dir DIR] [--zones z1,z2]
                        [--eq-path P] [--eqe1-path P] [--bench-bin P]
                        [--timeout N] [--watchdog N]

Options (env overrides in B852_*):
  --list FILE      zone list (default: $LIST; TSV zone/expansion/s3d_bytes/notes;
                   rows tagged not_in_t490_census are documentation, not swept)
  --out-dir DIR    results TSV + per-zone logs (default: $OUT_DIR)
  --zones z1,z2    sweep only these (a 3-zone smoke list for ctest)
  --eq-path P      EQ client root (default: $EQ_PATH)
  --eqe1-path P    EQE1 root (default: $EQE1_PATH)
  --bench-bin P    benchmark binary (default: $BENCH_BIN)
  --timeout N      per-zone wall-clock seconds (default: $TIMEOUT_S)
  --watchdog N     seconds of log silence before SIGKILL (default: $WATCHDOG_S)
Exit: 0 all clean, 1 a non-clean row, 2 nothing could be swept, 3 bad usage.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --zones) SWEEP_ZONES="$2"; shift 2 ;;
        --eq-path) EQ_PATH="$2"; shift 2 ;;
        --eqe1-path) EQE1_PATH="$2"; shift 2 ;;
        --bench-bin) BENCH_BIN="$2"; shift 2 ;;
        --timeout) TIMEOUT_S="$2"; shift 2 ;;
        --watchdog) WATCHDOG_S="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 3 ;;
    esac
done

[ -x "$BENCH_BIN" ] || { echo "FATAL: benchmark binary missing: $BENCH_BIN (build it: cmake --build build --target gles2_zone_benchmark)" >&2; exit 2; }
[ -d "$EQ_PATH" ] || { echo "FATAL: EQ client root missing: $EQ_PATH (gitignored symlink -- see .gitlab-ci.yml before_script)" >&2; exit 2; }
[ -d "$EQE1_PATH" ] || { echo "FATAL: EQE1 root missing: $EQE1_PATH (gitignored symlink)" >&2; exit 2; }
[ -f "$LIST" ] || { echo "FATAL: zone list missing: $LIST" >&2; exit 2; }
# A --zones name that is not in the list would otherwise sweep zero zones and
# exit 0 (the smoke's RED case must be loud, not hollow): validate up front.
if [ -n "${SWEEP_ZONES:-}" ]; then
    for z in ${SWEEP_ZONES//,/ }; do
        if ! grep -P "^$z\t" "$LIST" >/dev/null; then
            echo "FATAL: --zones names unknown zone '$z' (not in $LIST; regenerate the list with scripts/ci/zone_content_sweep_zones.py)" >&2
            exit 3
        fi
    done
fi

mkdir -p "$OUT_DIR/logs"
TSV="$OUT_DIR/results.tsv"
printf 'zone\texpansion\tstatus\texit_code\twall_s\tfatal_count\terror_count\tfirst_fatal\tmissing_texture_count\tmissing_eqe1_count\tplaceholder_model_count\tzone_load_complete\tpeak_rss_anon\tclean\n' > "$TSV"

run_one() {
    local zone="$1" expansion="$2"
    local log="$OUT_DIR/logs/$zone.log"
    local start end wall rc

    start=$(date +%s)
    # One process per zone (the card's rule): the loader's own --objects /
    # --entities / --eqe1-path form, tiny frame count, INFO logging so the
    # FATAL/ERROR census and the [memledger] lines land in the log.
    # stdbuf -oL -eL: the redirected stdout is block-buffered, so a hang with
    # no output would freeze the log mtime and defeat the watchdog; line-
    # buffering makes "no bytes for WATCHDOG_S" mean "no progress".
    stdbuf -oL -eL "$BENCH_BIN" \
        --zone "$zone" \
        --eq-path "$EQ_PATH" \
        --eqe1-path "$EQE1_PATH" \
        --frames "$FRAMES" \
        --warmup "$WARMUP" \
        --res "$RES_W" "$RES_H" \
        --objects \
        --entities "$ENTITIES" \
        --test-races "$RACES" \
        --occluder-budget "$OCCLUDER_BUDGET" \
        --memory-json "$OUT_DIR/logs/$zone.mem.json" \
        --log-level=INFO \
        > "$log" 2>&1 &
    local child=$!
    # Watchdog loop: poll every 2 s; SIGKILL on (a) per-zone wall-clock
    # timeout or (b) WATCHDOG_S of log silence. The loader logs at every step
    # boundary, so silence means it hung inside a step. killed_by tracks WHO
    # sent the SIGKILL so the row says "timeout"/"watchdog" rather than
    # "died_on_cap" (an OOM kill is indistinguishable by exit status: 137).
    local killed_by=""
    while kill -0 "$child" 2>/dev/null; do
        sleep 2
        kill -0 "$child" 2>/dev/null || break
        local now elapsed mtime age
        now=$(date +%s)
        elapsed=$((now - start))
        if [ "$elapsed" -ge "$TIMEOUT_S" ]; then
            echo "timeout: $zone exceeded ${TIMEOUT_S}s -- SIGKILL"
            killed_by="timeout"
            kill -9 "$child" 2>/dev/null
            break
        fi
        # give a freshly started process some room before silence is judged
        [ "$elapsed" -ge 10 ] || continue
        mtime=$(stat -c %Y "$log" 2>/dev/null || echo "$now")
        age=$((now - mtime))
        if [ "$age" -ge "$WATCHDOG_S" ]; then
            echo "watchdog: $zone silent for ${age}s (log: $log) -- SIGKILL"
            killed_by="watchdog"
            kill -9 "$child" 2>/dev/null
            break
        fi
    done
    wait "$child" 2>/dev/null
    rc=$?
    end=$(date +%s)
    wall=$((end - start))

    local fatal_count error_count first_fatal missing_tex missing_eqe1 placeholder loaded peak_rss status
    # Level tags are padded by GetLevelName() ("[FATAL ]", "[ERROR ]"); match
    # the padded form primarily, tolerate a missing space (unstructured
    # stderr lines from before the logger initialised).
    fatal_count=$(grep -cE '\[FATAL ?\]| FATAL ' "$log" 2>/dev/null || true)
    error_count=$(grep -cE '\[ERROR ?\]| ERROR ' "$log" 2>/dev/null || true)
    first_fatal=$(grep -m1 -E '\[FATAL ?\]| FATAL ' "$log" 2>/dev/null | head -c 300 | tr '\t' ' ')
    # "Texture 'X' not found in any EQE1 flat pack" (constrained_texture_cache.cpp)
    # plus any FATAL naming a texture. B1017 (#338): this does NOT count the
    # B930/B1017 stale-pack FATAL ("is format v1" / "v2 ... needs v3") -- that
    # one names the pack and its version, not a missing texture; a stale pack
    # is caught by fatal_count (the run is not clean either way).
    missing_tex=$(grep -c "not found in any EQE1" "$log" 2>/dev/null || true)
    missing_eqe1=$(grep -cE 'EQT_EQE1|EQE1.*not found|not found.*EQE1' "$log" 2>/dev/null || true)
    # Placeholder models: the benchmark names each race whose model data did
    # not resolve ("getRaceModelData returned NULL") and the aggregate
    # "No entity meshes loaded".
    placeholder=$(grep -c 'getRaceModelData returned NULL' "$log" 2>/dev/null || true)
    if grep -q 'No entity meshes loaded' "$log" 2>/dev/null; then
        placeholder=$((placeholder + 1))
    fi
    if grep -q 'Zone loaded in' "$log" 2>/dev/null; then loaded=y; else loaded=n; fi
    peak_rss=$(grep '^\[memledger\]' "$log" 2>/dev/null | sed 's/.* rss_anon=\([0-9]*\).*/\1/' | sort -n | tail -1)
    [ -n "$peak_rss" ] || peak_rss=0

    # Status: who (or what) ended the run. rc 137 means "killed by SIGKILL" --
    # if WE sent it, killed_by says why; if nobody in this script sent it,
    # the most likely sender is the cgroup/OOM killer (the 4 GiB cap).
    status="ok"
    if [ -n "$killed_by" ]; then
        status="$killed_by"
    elif [ "$rc" -eq 137 ]; then
        status="died_on_cap"
    elif [ "$rc" -gt 128 ]; then
        status="died_by_signal_$((rc - 128))"
    fi
    # Clean iff the run finished 0 and every content check held. A kill by
    # anyone (including the cap) is a RESULT this sweep records, never clean
    # and never relabelled "failed" -- the status above names it.
    local clean=0
    if [ "$rc" -eq 0 ] && [ "$fatal_count" -eq 0 ] && [ "$error_count" -eq 0 ] \
        && [ "$loaded" = "y" ] \
        && [ "$placeholder" -eq 0 ] \
        && [ "$peak_rss" -lt "$RSS_CEILING_BYTES" ]; then
        clean=1
    elif [ "$rc" -le 128 ]; then
        status="failed"
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$zone" "$expansion" "$status" "$rc" "$wall" \
        "$fatal_count" "$error_count" "$first_fatal" \
        "$missing_tex" "$missing_eqe1" "$placeholder" \
        "$loaded" "$peak_rss" "$clean" >> "$TSV"
    local verdict="CLEAN"
    [ "$clean" -eq 1 ] || verdict="NOT-CLEAN"
    printf '  %-14s %-9s %-18s rc=%-4s wall=%5ss fatal=%s err=%s load=%s peak_rss_anon=%s  %s\n' \
        "$zone" "$expansion" "$status" "$rc" "$wall" "$fatal_count" "$error_count" "$loaded" "$peak_rss" "$verdict"
}

count=0
tail -n +2 "$LIST" | while IFS=$'\t' read -r zone expansion s3d_bytes notes; do
    [ -n "$zone" ] || continue
    case "$notes" in
        *not_in_t490_census*) continue ;;  # drift documentation, not sweep input
    esac
    if [ -n "${SWEEP_ZONES:-}" ]; then
        case ",$SWEEP_ZONES," in
            *",$zone,"*) ;;
            *) continue ;;
        esac
    fi
    count=$((count + 1))
    printf '[%d] %s (%s)\n' "$count" "$zone" "$expansion"
    run_one "$zone" "$expansion"
done

# The sweep loop above ran in a pipeline subshell; the summary re-reads the
# TSV, which is the source of truth anyway.
echo
echo "=== B852 sweep summary ==="
awk -F'\t' 'NR>1 {
    n[$2]++;
    if ($14 == "1") clean[$2]++; else dirty[$2]++;
    if ($14 != "1") { d++; printf("NOT-CLEAN: %-14s (%s) status=%s rc=%s first_fatal=\"%s\"\n", $1, $2, $3, $4, $8); }
}
END {
    printf("rows=%d\n", NR - 1);
    for (e in n) printf("  %-9s: %d rows, %d clean, %d not-clean\n", e, n[e], clean[e] + 0, dirty[e] + 0);
    if (d + 0 > 0) exit 1;
}' "$TSV"
awk_exit=$?

echo "results: $TSV"
echo "logs:    $OUT_DIR/logs/"
exit "$awk_exit"
