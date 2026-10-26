#!/usr/bin/env bash
# B1158: the visual FX ledger -- one side-by-side clip per measured record
# (oracle LEFT / willeq RIGHT, aligned at BeginCast by the stated frame rule),
# 8-cell strips, index.tsv + index.html. One command, from a gate-run dir.
#
# usage:
#   make_ledger.sh <gate-run-dir> <out-dir> [band-table.tsv]
#
#   <gate-run-dir>   the harness gate sweep dir with measure/<kind>_r<rec>/
#                    video.mkv + runs/<kind>_r<rec>/timing.tsv (the B1155
#                    layout; the B1157 re-gate is expected to keep it).
#   <out-dir>        the ledger output (clips/, specs/, index.tsv, index.html,
#                    alignments.jsonl).
#   [band-table.tsv] the gate's own banded verdict table (e.g.
#                    refactor/B1155/banded_table.tsv); omit for a gate that
#                    has no banded table (verdict columns left empty).
#
# env:
#   LEDGER_EXTRA_MEASURE   colon-separated extra measure dirs consulted for
#                          the missile legs' willeq side (e.g. a standalone
#                          B1158 harness render dir with m1_rig38/ m2_rig54/).
#   LEDGER_JOBS            parallel clip encodes (default 3).
#
# First run (the B1155 pre-animation baseline):
#   bash refactor/B1158/scripts/make_ledger.sh \
#     /home/user/kb-evidence/kb-b1155-b1130-camera/sweep \
#     /home/user/kb-evidence/kb-b1158-visual-ledger/VISUAL-FX/b1155-baseline \
#     refactor/B1155/banded_table.tsv
set -euo pipefail
if [ $# -lt 2 ]; then grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 1; fi
GATE="$1"; OUT="$2"
BAND="${3:-}"
JOBS="${LEDGER_JOBS:-3}"
PY=python3
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$OUT/clips" "$OUT/specs"
rm -f "$OUT/alignments.jsonl"

echo "[1/3] inventory (gate: $GATE)" >&2
"$PY" "$SCRIPTS/ledger_inventory.py" "$GATE" "$OUT" ${BAND:+"$BAND"}

echo "[2/3] clips" >&2
rm -f "$OUT/clip.log"
run_one() {  # $1 = spec id
    "$PY" "$SCRIPTS/make_clip.py" "$OUT/specs/$1.json" "$OUT/clips" \
        >> "$OUT/alignments.jsonl" 2>>"$OUT/clip.log" \
        || { echo "CLIP FAILED: $1 (see $OUT/clip.log)" >> "$OUT/clip.log"; return 1; }
}
export PY OUT SCRIPTS
export -f run_one 2>/dev/null || true
# ids: the specs/ dir is the source of truth
specs=()
for f in "$OUT"/specs/*.json; do specs+=("$(basename "$f" .json)"); done
n_specs=${#specs[@]}
failed=0
printf '%s\n' "${specs[@]}" | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {} || failed=1
n_ok=$(grep -c . "$OUT/alignments.jsonl" || true)
if [ "$failed" != "0" ] || [ "$n_ok" != "$n_specs" ]; then
    echo "[FAILED] $n_ok/$n_specs clips ok; failures at the end of $OUT/clip.log" >&2
    exit 1
fi

echo "[3/3] index" >&2
"$PY" "$SCRIPTS/make_index.py" "$OUT/alignments.jsonl" "$OUT"
echo "[done] $OUT (clips/, index.tsv, index.html)" >&2
