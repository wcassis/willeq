#!/usr/bin/env bash
# Check C++ file sizes against the Batch F ceilings (docs/decisions.md):
# warn > 1500 lines, fail > 3000 lines. Applies to files changed vs a base
# revision (default: fable). Legacy files are exempt until their split unit
# lands — pass explicit paths to check only those.
#
# Usage:
#   check_file_sizes.sh [BASE_REV]         # check files changed since BASE_REV
#   check_file_sizes.sh --files f1 f2 ...  # check specific files

set -u
WARN=1500
FAIL=3000
repo_root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

if [ "${1:-}" = "--files" ]; then
    shift
    files=("$@")
else
    base="${1:-fable}"
    mapfile -t files < <(git -C "$repo_root" diff --name-only "$base" -- '*.cpp' '*.h' '*.hpp')
fi

status=0
for f in "${files[@]}"; do
    path="$repo_root/$f"
    [ -f "$path" ] || path="$f"
    [ -f "$path" ] || continue
    lines=$(wc -l < "$path")
    if [ "$lines" -gt "$FAIL" ]; then
        echo "FAIL  $f: $lines lines (> $FAIL)"
        status=1
    elif [ "$lines" -gt "$WARN" ]; then
        echo "WARN  $f: $lines lines (> $WARN)"
    else
        echo "ok    $f: $lines lines"
    fi
done
exit $status
