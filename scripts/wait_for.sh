#!/bin/bash
# wait_for.sh — bounded, re-invokable polling helper (T129).
#
# Purpose: EXECUTOR_PROTOCOL.md forbids ending a turn waiting on a background
# task or monitor, and requires polling in bounded foreground loops instead.
# Agents kept violating that rule anyway because it asked each of them to
# invent a bounded poll loop from scratch, under time pressure, with nothing
# in scripts/ to just call. This is that tool.
#
# Hard constraint: the Bash tool that invokes this caps a single call at
# 600000ms (600s). This script must NEVER block anywhere near that long. It
# polls for at most --deadline seconds (default 240, comfortably under the
# 600s cap even with slop for process overhead), then reports state and
# exits. If the condition isn't met yet, the caller re-invokes this same
# command — the deadline path is a normal, expected outcome, not a failure.
#
# Two shapes:
#   wait_for.sh --exit-file <path> [--interval 10] [--deadline 240] [--label NAME]
#       Waits for <path> to exist (the Pi batch ".exit" pattern — a batch
#       script writes a file containing its exit code when done).
#   wait_for.sh --file <path> --contains <text> [--interval 10] [--deadline 240] [--label NAME]
#       Waits for a line containing <text> to appear anywhere in <path> (the
#       build/ctest "marker line in a growing log" pattern).
#
# Exit codes (distinct so the caller can act without a second command):
#   0 = condition met
#   2 = deadline reached, condition NOT met (probably still running — normal;
#       just call this again)
#   1 = usage error
#   3 = target file/path unusable in a way that isn't "still running"
#       (e.g. --file's directory doesn't exist at all)
#
# On every exit this prints: elapsed time, mode, target path, current file
# size (or "absent"), a tail of the file's content if present, and for
# --exit-file the exit code found inside it (when condition met). No
# dependencies beyond coreutils (stat, tail, sleep, date). Works fine over
# NFS (plain stat/read calls, no inotify).
#
# Example (Pi batch):
#   scripts/wait_for.sh --exit-file /mnt/projects/fable/review/T129/run.exit \
#       --interval 10 --deadline 240 --label pi-batch
#
# Example (build/ctest marker):
#   scripts/wait_for.sh --file /tmp/build.log --contains 'BUILD_DONE' \
#       --interval 5 --deadline 240 --label x86-build

set -u

MODE=""
TARGET=""
CONTAINS=""
INTERVAL=10
DEADLINE=240
LABEL="wait_for"

usage() {
    echo "Usage:" >&2
    echo "  $0 --exit-file <path> [--interval N] [--deadline N] [--label NAME]" >&2
    echo "  $0 --file <path> --contains <text> [--interval N] [--deadline N] [--label NAME]" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --exit-file)
            MODE="exit-file"
            TARGET="$2"
            shift 2
            ;;
        --file)
            MODE="file"
            TARGET="$2"
            shift 2
            ;;
        --contains)
            CONTAINS="$2"
            shift 2
            ;;
        --interval)
            INTERVAL="$2"
            shift 2
            ;;
        --deadline)
            DEADLINE="$2"
            shift 2
            ;;
        --label)
            LABEL="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "wait_for.sh: unknown argument: $1" >&2
            usage
            ;;
    esac
done

if [ "$MODE" = "" ] || [ "$TARGET" = "" ]; then
    echo "wait_for.sh: must pass --exit-file <path> or --file <path>" >&2
    usage
fi
if [ "$MODE" = "file" ] && [ "$CONTAINS" = "" ]; then
    echo "wait_for.sh: --file requires --contains <text>" >&2
    usage
fi
if [ "$DEADLINE" -ge 590 ] 2>/dev/null; then
    echo "wait_for.sh: --deadline $DEADLINE is too close to the 600s Bash-call cap; use <=540" >&2
    exit 1
fi

# Refuse a target whose directory doesn't exist at all — that's a caller
# mistake (typo'd path, batch never started), not "still running", and
# retrying it in a loop would just burn deadlines silently.
target_dir=$(dirname -- "$TARGET")
if [ ! -d "$target_dir" ]; then
    echo "wait_for.sh: FAIL directory does not exist: $target_dir" >&2
    echo "  mode=$MODE target=$TARGET label=$LABEL" >&2
    exit 3
fi

start_ts=$(date +%s)
deadline_ts=$((start_ts + DEADLINE))

describe_state() {
    # $1 = elapsed seconds, $2 = outcome word
    local elapsed="$1"
    local outcome="$2"
    echo "wait_for[$LABEL]: $outcome after ${elapsed}s (interval=${INTERVAL}s deadline=${DEADLINE}s)"
    echo "  mode=$MODE target=$TARGET"
    if [ -e "$TARGET" ]; then
        local sz
        sz=$(stat -c%s -- "$TARGET" 2>/dev/null || echo "?")
        echo "  file: present, size=${sz} bytes"
        echo "  tail:"
        tail -n 10 -- "$TARGET" 2>/dev/null | sed 's/^/    /'
    else
        echo "  file: absent"
    fi
}

while :; do
    now_ts=$(date +%s)
    elapsed=$((now_ts - start_ts))

    if [ "$MODE" = "exit-file" ]; then
        if [ -e "$TARGET" ]; then
            code=$(tr -d '[:space:]' < "$TARGET" 2>/dev/null)
            describe_state "$elapsed" "MET (exit-file appeared)"
            echo "  exit code found inside: ${code:-<empty>}"
            exit 0
        fi
    else
        if [ -e "$TARGET" ] && grep -q -- "$CONTAINS" "$TARGET" 2>/dev/null; then
            describe_state "$elapsed" "MET (marker '$CONTAINS' found)"
            exit 0
        fi
    fi

    if [ "$now_ts" -ge "$deadline_ts" ]; then
        describe_state "$elapsed" "DEADLINE (condition not met — probably still running; re-invoke this same command)"
        exit 2
    fi

    remaining=$((deadline_ts - now_ts))
    sleep_for=$INTERVAL
    if [ "$remaining" -lt "$sleep_for" ]; then
        sleep_for=$remaining
    fi
    sleep "$sleep_for"
done
