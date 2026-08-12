#!/usr/bin/env bash
# Check C++ file sizes against the Batch F ceilings (docs/decisions.md):
# warn > 1500 lines, fail > 3000 lines. Applies to files changed vs a base
# revision (default: fable). Legacy files are exempt until their split unit
# lands — pass explicit paths to check only those.
#
# Usage:
#   check_file_sizes.sh [BASE_REV]              # check files changed since BASE_REV
#   check_file_sizes.sh -C <repo> [BASE_REV]    # anchor to <repo> instead of the
#                                                # script's own checkout / ambient cwd
#   check_file_sizes.sh --files f1 f2 ...       # check specific files
#
# T79: this script used to resolve repo_root against the ambient cwd
# (`git rev-parse --show-toplevel` with no -C), so running it exactly as the
# protocol documents — `bash <slot>/scripts/refactor/check_file_sizes.sh
# fable` from outside <slot> — silently checked the WRONG checkout (whatever
# repo the caller's cwd happened to be in, or `pwd` itself if that wasn't a
# git repo at all). T67, T69, T73 and T75 each independently worked around
# this with `cd`, `env -C`, or `--files` with absolute paths. Fixed by
# anchoring to the script's own location by default (it lives at
# <repo>/scripts/refactor/check_file_sizes.sh, so its grandparent's parent is
# the repo), with an explicit -C/--repo override for callers that want a
# different tree.

set -u
WARN=1500
FAIL=3000

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
default_repo_root="$(cd "$script_dir/../.." >/dev/null 2>&1 && pwd)"
repo_root=""
if [ "${1:-}" = "-C" ] || [ "${1:-}" = "--repo" ]; then
    repo_root="${2:-}"
    if [ -z "$repo_root" ]; then
        echo "ERROR $1 given with no path — nothing was checked"
        exit 2
    fi
    shift 2
fi
if [ -z "$repo_root" ]; then
    repo_root="$(git -C "$default_repo_root" rev-parse --show-toplevel 2>/dev/null || echo "$default_repo_root")"
fi

# T37: this script used to be able to check NOTHING and still exit 0 with no
# output — byte-identical to a clean pass from the caller's side. Every unit's
# verification bar ends with this step, so "no new FAILs" could equally mean
# "checked and clean" or "checked nothing", and the report read the same way.
# Three distinct routes produced that silent pass:
#   1. an empty diff (running from main, a slot detached at the base rev, or
#      after a merge) — legitimate, but must be VISIBLE;
#   2. `--files` with no paths after it — a caller mistake;
#   3. a BASE_REV that does not resolve (typo, or not a git repo at all) —
#      git wrote to stderr, mapfile got nothing, and the script passed.
# 1 stays exit 0 but now says so; 2 and 3 are errors, because they mean the
# caller asked for a check that never happened.
if [ "${1:-}" = "--files" ]; then
    shift
    files=("$@")
    if [ "${#files[@]}" -eq 0 ]; then
        echo "ERROR --files given with no paths — nothing was checked"
        exit 2
    fi
    source_desc="explicit file list"
else
    base="${1:-fable}"
    if ! git -C "$repo_root" rev-parse --verify --quiet "${base}^{commit}" >/dev/null 2>&1; then
        echo "ERROR base revision '$base' does not resolve in $repo_root — nothing was checked"
        exit 2
    fi
    # --diff-filter=d excludes files deleted in the diff (T67 hit this
    # removing constrained_mesh_cache.{h,cpp} — a deleted path has nothing
    # left on disk to measure and would otherwise report a false "not found").
    mapfile -t files < <(git -C "$repo_root" diff --name-only --diff-filter=d "$base" -- '*.cpp' '*.h' '*.hpp')
    source_desc="files changed vs '$base' in $repo_root"
fi

if [ "${#files[@]}" -eq 0 ]; then
    echo "no C++ files to check ($source_desc) — nothing was checked, this is not a pass"
    exit 0
fi

echo "checking ${#files[@]} file(s) ($source_desc)"

status=0
n_ok=0
n_warn=0
n_fail=0
for f in "${files[@]}"; do
    path="$repo_root/$f"
    [ -f "$path" ] || path="$f"
    if [ ! -f "$path" ]; then
        echo "ERROR $f: file not found (cwd=$PWD, repo_root=$repo_root) — pass absolute paths or run from the target worktree"
        status=2
        continue
    fi
    lines=$(wc -l < "$path")
    if [ "$lines" -gt "$FAIL" ]; then
        echo "FAIL  $f: $lines lines (> $FAIL)"
        status=1
        n_fail=$((n_fail + 1))
    elif [ "$lines" -gt "$WARN" ]; then
        echo "WARN  $f: $lines lines (> $WARN)"
        n_warn=$((n_warn + 1))
    else
        echo "ok    $f: $lines lines"
        n_ok=$((n_ok + 1))
    fi
done

# T37: always close with a countable summary, so a caller pasting this into a
# Review shows what was actually examined rather than an absence of output.
echo "checked ${#files[@]} file(s): $n_ok ok, $n_warn warn, $n_fail fail"
exit $status
