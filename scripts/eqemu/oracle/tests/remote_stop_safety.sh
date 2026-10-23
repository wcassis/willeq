#!/usr/bin/env bash
# Offline regression (B1057, #377): oracle_remote.sh stop must never kill a
# display it did not start. Lane 5's :99 is a real Xorg (oracle-xorg.service)
# that run finds up, so it saves no xvfb.pid; a stale pid file must not reach
# a process that is not an Xvfb either. Stand-ins are copies of sleep(1)
# named Xorg/Xvfb; pkill, sleep and xdpyinfo are PATH stubs.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/oracle-stop.XXXXXX")
PIDS=()
trap 'kill "${PIDS[@]}" 2>/dev/null || :; rm -rf "$FIXTURE"' EXIT
REAL_SLEEP=$(command -v sleep)
mkdir -p "$FIXTURE/stub" "$FIXTURE/real" "$FIXTURE/home/eq-oracle/client"
: >"$FIXTURE/ui-lib.sh"
sed -e "s#/home/user/eq-oracle#$FIXTURE/home/eq-oracle#g" \
    -e "s#/mnt/projects/eq-oracle/scripts/ui-lib.sh#$FIXTURE/ui-lib.sh#" \
    "${ORACLE_REMOTE_SOURCE:-$REPO/scripts/eqemu/oracle/oracle_remote.sh}" >"$FIXTURE/oracle_remote.sh"
printf '#!/bin/sh\necho "$*" >>"%s/pkill.log"\n' "$FIXTURE" >"$FIXTURE/stub/pkill"
printf '#!/bin/sh\nexit 0\n' >"$FIXTURE/stub/sleep"
printf '#!/bin/sh\nexit 0\n' >"$FIXTURE/stub/xdpyinfo"
chmod +x "$FIXTURE/stub/"*
cp "$REAL_SLEEP" "$FIXTURE/real/Xorg"
cp "$REAL_SLEEP" "$FIXTURE/real/Xvfb"
"$FIXTURE/real/Xorg" 300 & XORG=$!; PIDS+=("$XORG")
"$FIXTURE/real/Xvfb" 300 & XVFB=$!; PIDS+=("$XVFB")
stop() {
    local dir="$FIXTURE/home/eq-oracle/sessions/$1"
    mkdir -p "$dir"
    [[ -z ${2:-} ]] || echo "$2" >"$dir/xvfb.pid"
    [[ -z ${3:-} ]] || echo "$3" >"$dir/xvfb-caster.pid"
    PATH="$FIXTURE/stub:$PATH" bash "$FIXTURE/oracle_remote.sh" stop "$dir" || { echo "stop $1 failed"; exit 1; }
    "$REAL_SLEEP" 0.2
}
# 1. Lane 5 shape: :99 was already up, no xvfb.pid -> the display survives.
stop no-pid
kill -0 "$XORG" 2>/dev/null || { echo 'stop without xvfb.pid killed the display'; exit 1; }
[[ $(sort -u "$FIXTURE/pkill.log") == '-TERM -x eqgame.exe' ]] || { echo "unexpected pkill: $(cat "$FIXTURE/pkill.log")"; exit 1; }
# 2. A stale/reused xvfb.pid naming a non-Xvfb process -> left alone.
stop stale-pid "$XORG"
kill -0 "$XORG" 2>/dev/null || { echo 'stop killed a non-Xvfb pid from xvfb.pid'; exit 1; }
# 3. The session's own Xvfb (lanes 1-4 shape) is still stopped.
stop own-xvfb "$XVFB"
if kill -0 "$XVFB" 2>/dev/null; then echo 'stop left its own Xvfb running'; exit 1; fi
# 4. B1134: an observer session's caster Xvfb (xvfb-caster.pid) is stopped,
#    the lane's :99 Xorg is not, and a stale caster pid naming the Xorg is left.
"$FIXTURE/real/Xvfb" 300 & XVFB2=$!; PIDS+=("$XVFB2")
stop observer-stale "" "$XORG"
kill -0 "$XORG" 2>/dev/null || { echo 'stop killed a non-Xvfb pid from xvfb-caster.pid'; exit 1; }
stop observer-own "" "$XVFB2"
if kill -0 "$XVFB2" 2>/dev/null; then echo 'stop left the caster Xvfb running'; exit 1; fi
kill -0 "$XORG" 2>/dev/null || { echo 'stopping the caster Xvfb killed the display'; exit 1; }
echo 'PASS: stop keeps a display it did not start (no pid, stale pid) and stops its own Xvfb, the caster Xvfb of an observer session included'
