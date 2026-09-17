#!/usr/bin/env bash
# T491: the memory cap hunt, as a script.
#
# Runs one command repeatedly under a shrinking cgroup memory cap and reports,
# per cap, whether it survived and the LAST memory-ledger lifecycle line it
# printed before the OOM killer took it. That line names the lifecycle point
# ("zone_parse", "mesh_build_textures", "entities", "ready", ...) and the
# ledger's per-category bytes at that moment, so a kill is attributed to the
# phase and the category that pushed the process over, not just noticed.
#
# The command must print [memledger] lines (include/client/core/memory_ledger.h:
# every lifecycle point in willeq and gles2_zone_benchmark does, to the log /
# stdout). Anything that logs them works -- x86 or the Orange Pi.
#
# Mechanism: systemd-run --scope -p MemoryMax=<cap> -p MemorySwapMax=0. The
# scope puts THIS shell's child in a fresh cgroup with a hard anonymous+file
# limit and no swap, so the kernel OOM-kills it (exit 137) at the cap instead
# of thrashing. --user needs a user manager (any login session has one); root
# can use --system. Both x86 (ubuntu) and the Pi (armbian, systemd) have it.
#
# Usage:
#   scripts/ci/memory_cap_hunt.sh [--caps 256M,128M,64M,32M] [--out DIR]
#                                 [--system] [--timeout SECONDS] -- <cmd> [args...]
#
# Output: one table row per cap on stdout, the full per-cap logs under --out
# (default: a mktemp dir, printed). Exit 0 if every cap ran (killed or not --
# a kill IS the measurement); non-zero only on usage/systemd errors.
#
# Example (T490 Leg B, Iceclad on the Pi; T491 validated it on nro):
#   scripts/ci/memory_cap_hunt.sh --caps 256M,128M,64M,32M -- \
#       build-gles2/bin/gles2_zone_benchmark --zone nro --eq-path data/eq_client \
#       --frames 5 --warmup 1 --res 800 600 --eqe1-path data/eqe1_client --objects --entities 5
set -uo pipefail

CAPS="256M,128M,64M,32M"
OUT=""
SCOPE_USER="--user"
TIMEOUT_S=600
while [ $# -gt 0 ]; do
    case "$1" in
        --caps) CAPS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --system) SCOPE_USER=""; shift ;;
        --timeout) TIMEOUT_S="$2"; shift 2 ;;
        --) shift; break ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "memory_cap_hunt: unknown option $1 (use -- before the command)" >&2; exit 2 ;;
    esac
done
if [ $# -eq 0 ]; then
    echo "memory_cap_hunt: no command given (usage: ... -- <cmd> [args])" >&2
    exit 2
fi
if ! command -v systemd-run >/dev/null 2>&1; then
    echo "memory_cap_hunt: systemd-run not found; cannot cap memory" >&2
    exit 3
fi
# Probe the scope path once so a missing user manager is a clear error, not
# four identical failures.
if ! systemd-run $SCOPE_USER --scope -q -p MemoryMax=1G -p MemorySwapMax=0 -- true >/dev/null 2>&1; then
    echo "memory_cap_hunt: systemd-run $SCOPE_USER --scope is not usable here" \
         "(no user manager? try --system as root)" >&2
    exit 3
fi
if [ -z "$OUT" ]; then
    OUT="$(mktemp -d "${TMPDIR:-/tmp}/memory_cap_hunt.XXXXXX")"
fi
mkdir -p "$OUT"

# T491 addendum (2026-09-17): the row also carries how far past the 64 MB
# target the last point was, so a hunt answers the target question directly.
printf '%-8s %-8s %-26s %12s %12s %12s %12s  %s\n' \
    cap exit last_point rss_anon ledger unaccount over_tgt top_category
IFS=',' read -r -a CAP_LIST <<< "$CAPS"
for cap in "${CAP_LIST[@]}"; do
    log="$OUT/cap_${cap}.log"
    timeout --signal=KILL "$TIMEOUT_S" \
        systemd-run $SCOPE_USER --scope -q -p MemoryMax="$cap" -p MemorySwapMax=0 -- "$@" \
        > "$log" 2>&1
    rc=$?
    last="$(grep -a '\[memledger\] point=' "$log" | tail -n 1)"
    if [ -z "$last" ]; then
        point="(no memledger line)"; rss="-"; ledger="-"; unac="-"; ovt="-"; top="-"
    else
        # SPLIT FIRST, then parse. The scalar fields and the per-category
        # tokens share names ("unattributed=" is both a byte count and a
        # category), and sed's greedy .* takes the LAST match -- so parsing the
        # whole line reported the CATEGORY's bytes for every colliding name.
        # (T491, 2026-09-17: the inherited script did exactly that and read
        # unattributed=0 for a line that said 500.) Everything up to " allocs="
        # is scalars; everything after it is name=live/peak.
        head_="${last%% allocs=*}"
        tail_="$(sed -n 's/.* allocs=[0-9]* //p' <<< "$last")"
        point="$(sed -n 's/.*point=\([^ ]*\).*/\1/p' <<< "$head_")"
        rss="$(sed -n 's/.* rss_anon=\([0-9]*\).*/\1/p' <<< "$head_")"
        ledger="$(sed -n 's/.* ledger=\([0-9]*\).*/\1/p' <<< "$head_")"
        # unaccounted = RssAnon - ledger - allocator slack: what attribution
        # work can still remove. Older logs have only "unattributed".
        unac="$(sed -n 's/.* unaccounted=\([0-9]*\).*/\1/p' <<< "$head_")"
        [ -n "$unac" ] || unac="$(sed -n 's/.* unattributed=\([0-9]*\).*/\1/p' <<< "$head_")"
        ovt="$(sed -n 's/.* over_target=\([0-9]*\).*/\1/p' <<< "$head_")"
        [ -n "$ovt" ] || ovt="-"
        # largest live bucket on that line: tokens after allocs= are name=live/peak
        top="$(tr ' ' '\n' <<< "$tail_" \
               | awk -F'[=/]' '{ if ($2+0 > best) { best = $2+0; name = $1 } } END { printf "%s=%d", name, best }')"
    fi
    case "$rc" in
        0)   verdict="ok" ;;
        137) verdict="OOM-killed" ;;
        124) verdict="timeout" ;;
        *)   verdict="exit=$rc" ;;
    esac
    printf '%-8s %-8s %-26s %12s %12s %12s %12s  %s\n' \
        "$cap" "$verdict" "$point" "$rss" "$ledger" "$unac" "$ovt" "$top"
done
echo "logs: $OUT"
exit 0
