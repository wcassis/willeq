#!/usr/bin/env bash
# Check C++ file sizes against the ceilings in docs/decisions.md.
#
# M00 (2026-09-04): 1500 is now the ENFORCED ceiling, not a soft warning.
# Batches J, K, L and G brought every *.cpp/*.h/*.hpp under src/, include/ and
# tests/ below it, so under --all a file over 1500 that is not allowlisted is a
# FAIL.
#
# T385 (2026-09-05, board B167): --all now also scans tools/ and benchmarks/.
# It did not, and that made M00's "tree-wide" claim false for tools/ -- six
# files there were over the ceiling, two of them over the 3000 hard cap, with
# ctest reporting a clean tree the whole time. tools/retired/ is scanned too
# (its largest file is 474 lines, so nothing there is excused by omission).
#
# The 3000-line hard cap stays as a second line of defence (it also
# applies to allowlisted files). The allowlist
# (scripts/refactor/file_size_allowlist.txt) is now EMPTY; an entry is how a
# future single-class file that must exceed 1500 is permitted, with a reason
# and a no-grow max.
#
# In the diff/--files modes the allowlist is not consulted, so a file over 1500
# is reported as WARN there — the tree-wide --all run wired into ctest
# (FileSizeCeiling) is what turns it into a FAIL.
#
# Usage:
#   check_file_sizes.sh [BASE_REV]              # check files changed since BASE_REV
#   check_file_sizes.sh -C <repo> [BASE_REV]    # anchor to <repo> instead of the
#                                                # script's own checkout / ambient cwd
#   check_file_sizes.sh --files f1 f2 ...       # check specific files
#   check_file_sizes.sh --all                   # check every *.cpp/*.h/*.hpp under
#                                                # src/, include/, tests/, tools/ and
#                                                # benchmarks/ (excludes third_party/,
#                                                # build*/), tree-wide,
#                                                # not just what changed vs a base rev
#                                                # (K00). Over 1500 is a FAIL unless the
#                                                # file is listed in
#                                                # scripts/refactor/file_size_allowlist.txt
#                                                # with a per-file no-grow ceiling (M00).
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
all_mode=0
if [ "${1:-}" = "--all" ]; then
    all_mode=1
    # T385 (2026-09-05, board B167): tools/ and benchmarks/ are in scope too.
    # They were left out when --all was written, so M00's "the ceiling holds
    # tree-wide" was FALSE for the one directory ctest never looked at --
    # tools/ held SIX files over 1,500 (two of them over the 3,000 hard cap)
    # while the checker reported a clean tree. tools/ holds production-API
    # callers (board B166), so the same structural rule applies. A scanned
    # directory that does not exist is skipped silently; benchmarks/ holds no
    # C++ today, and is listed so it is covered the day it does. tools/retired/
    # is NOT pruned -- it is dead code kept for reference, but every file in it
    # is already well under the ceiling, so excluding it would only hide a
    # future regression.
    scan_dirs=()
    for d in src include tests tools benchmarks; do
        [ -d "$repo_root/$d" ] && scan_dirs+=("$repo_root/$d")
    done
    if [ "${#scan_dirs[@]}" -eq 0 ]; then
        echo "ERROR none of src/ include/ tests/ tools/ benchmarks/ exist under $repo_root — nothing was checked"
        exit 2
    fi
    mapfile -t files < <(find "${scan_dirs[@]}" \
        -type d \( -name third_party -o -name 'build*' -o -name '@eaDir' \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print 2>/dev/null | sort)
    source_desc="every *.cpp/*.h/*.hpp under src/, include/, tests/, tools/, benchmarks/ in $repo_root (excludes third_party/, build*/)"
elif [ "${1:-}" = "--files" ]; then
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

# K00: --all consults the no-grow allowlist. Format: path<TAB>max_lines<TAB>reason,
# path relative to repo_root. A listed file is permitted over the 1500 ceiling but
# FAILs if it exceeds its own recorded max_lines (ratchet: a split unit lowers the
# number, it never rises) — this is on top of, not instead of, the universal
# FAIL-over-3000 rule. M00 emptied the list; an unlisted file over 1500 now FAILs.
allowlist_file="$script_dir/file_size_allowlist.txt"
declare -A allow_max
declare -A allow_reason
if [ "$all_mode" -eq 1 ] && [ -f "$allowlist_file" ]; then
    while IFS=$'\t' read -r a_path a_max a_reason; do
        [ -z "$a_path" ] && continue
        case "$a_path" in \#*) continue ;; esac
        allow_max["$a_path"]="$a_max"
        allow_reason["$a_path"]="$a_reason"
    done < "$allowlist_file"
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
    # display/allowlist-match path: relative to repo_root when possible, so
    # --all (which finds absolute paths) and --files/diff mode (already
    # relative) key the allowlist the same way.
    rel="$f"
    case "$f" in
        "$repo_root"/*) rel="${f#"$repo_root"/}" ;;
    esac
    lines=$(wc -l < "$path")
    if [ "$lines" -gt "$FAIL" ]; then
        echo "FAIL  $rel: $lines lines (> $FAIL)"
        status=1
        n_fail=$((n_fail + 1))
    elif [ "$all_mode" -eq 1 ] && [ -n "${allow_max[$rel]+x}" ]; then
        max="${allow_max[$rel]}"
        if [ "$lines" -gt "$max" ]; then
            echo "FAIL  $rel: $lines lines (> allowlisted max $max — no-grow ratchet exceeded; ${allow_reason[$rel]})"
            status=1
            n_fail=$((n_fail + 1))
        else
            echo "ok    $rel: $lines lines (allowlisted, max $max; ${allow_reason[$rel]})"
            n_ok=$((n_ok + 1))
        fi
    elif [ "$all_mode" -eq 1 ] && [ "$lines" -gt "$WARN" ]; then
        # M00: tree-wide, 1500 is the enforced ceiling. Split the file, or add an
        # allowlist entry with a reason and a no-grow max if it genuinely cannot be.
        echo "FAIL  $rel: $lines lines (> $WARN — the enforced ceiling; split it, or add an entry to scripts/refactor/file_size_allowlist.txt with a reason and a no-grow max)"
        status=1
        n_fail=$((n_fail + 1))
    elif [ "$lines" -gt "$WARN" ]; then
        echo "WARN  $rel: $lines lines (> $WARN — a FAIL under --all, which is what ctest runs)"
        n_warn=$((n_warn + 1))
    else
        echo "ok    $rel: $lines lines"
        n_ok=$((n_ok + 1))
    fi
done

# T37: always close with a countable summary, so a caller pasting this into a
# Review shows what was actually examined rather than an absence of output.
echo "checked ${#files[@]} file(s): $n_ok ok, $n_warn warn, $n_fail fail"
exit $status
