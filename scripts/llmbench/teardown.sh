#!/usr/bin/env bash
# T492: the teardown half of record.sh, standalone.
#
# It exists because a recorder that dies without running its trap -- killed,
# crashed, or (T492's own mistake) edited on disk while bash was still reading
# it -- leaves three clients up, the characters moved and the eqemu lock held.
# The teardown must be runnable from OUTSIDE the thing that failed.
#
#   teardown.sh <corpus-dir> [--keep-server] [--release-lock]
set -u
C="${1:?usage: teardown.sh <corpus-dir> [--keep-server] [--release-lock]}"; shift
KEEP=0; RELEASE=0
while [ $# -gt 0 ]; do case "$1" in --keep-server) KEEP=1; shift;; --release-lock) RELEASE=1; shift;; *) shift;; esac; done
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
# T493 (board B784): the snapshot/restore recipe lives in ONE place now and
# scenario.sh uses the same one. Do not re-inline it here.
source "$REPO/scripts/eqemu/restore_lib.sh"
HERMES="${LLMB_HERMES:-user@172.16.200.100}"
say() { printf '[teardown] %s %s\n' "$(date '+%H:%M:%S')" "$*" >&2; }
CHARS="65,67,68,74"
B="$C/shared/db_before/snapshot.tsv"; A="$C/shared/db_after/snapshot.tsv"
IB="$C/shared/db_before/inventory.tsv"; IA="$C/shared/db_after/inventory.tsv"
mkdir -p "$C/shared/db_after"

say "killing clients"
pkill -f 'wine.*eqgame' >/dev/null 2>&1
pkill -f 'llmbench-T492/willeq' >/dev/null 2>&1
pkill -x willeq >/dev/null 2>&1
ssh -o BatchMode=yes "$HERMES" "pkill -f build-gles2/bin/willeq; true" >/dev/null 2>&1
pkill -f 'Xvfb :99' >/dev/null 2>&1
sleep 3

say "stopping the eqemu-server CONTAINER (mariadb stays up)"
eqemu_world_stop

RC=0
eqemu_restore_snapshot "$B" "$A" say || RC=$?
if [ -s "$IB" ]; then
    eqemu_inventory_restore "$IB" "${CHARS}"
    eqemu_inventory_snapshot "$IA" "${CHARS}"
    if diff -q "$IB" "$IA" >/dev/null 2>&1; then say "inventory restored BYTE-IDENTICALLY"
    else say "INVENTORY MISMATCH"; RC=66; fi
fi
if [ "$KEEP" -eq 0 ]; then
    bash "$REPO/scripts/eqemu/down.sh" >/dev/null 2>&1; say "server down (whole compose project)"
else
    eqemu_world_start; say "eqemu-server container restarted (--keep-server)"
fi
[ "$RELEASE" -eq 1 ] && { eqemu_lock_release; say "lease released"; }
exit $RC
