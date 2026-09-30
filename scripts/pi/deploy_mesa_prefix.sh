#!/usr/bin/env bash
# B935 (#253): install the shipped LLVM-less Mesa prefix on the Orange Pi.
#
# Copies <src>/ (default: the store's extracted prefix, $MESA_NOLLVM_STORE/prefix,
# an NFS path the Pi mounts at the same place) to $MESA_NOLLVM_PI_PREFIX on
# the Pi's ext4 home -- the one path the prefix was built for
# (scripts/pi/mesa_nollvm.env). rsync --checksum --delete, so a re-deploy is a
# no-op when nothing changed and a stale file never survives. The source's
# and the result's .willeq-mesa-pin must both equal the pin in
# scripts/pi/mesa_nollvm.sha256, or it FAILS (exit 1).
#
# Run it from the dev host or a worker, WITH THE PI LOCK HELD
# (source scripts/pi/env.sh; pi_lock_take <owner>).
# scripts/pi_rendered_zone_sweep.sh calls it itself unless WILLEQ_MESA=system.
#
# usage: scripts/pi/deploy_mesa_prefix.sh [src-dir-as-seen-on-the-Pi]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "$HERE/env.sh"
# shellcheck source=mesa_nollvm.env
source "$HERE/mesa_nollvm.env"
SRC="${1:-$MESA_NOLLVM_STORE/prefix}"
PIN="$(mesa_nollvm_pin)"
DST="$MESA_NOLLVM_PI_PREFIX"
[ -n "$PIN" ] || { echo "deploy_mesa_prefix: no pin in $MESA_NOLLVM_PIN_FILE" >&2; exit 1; }
pi_remote "set -e
src_pin=\$(cat '$SRC/.willeq-mesa-pin' 2>/dev/null || true)
[ \"\$src_pin\" = '$PIN' ] || { echo \"deploy_mesa_prefix: $SRC/.willeq-mesa-pin is '\$src_pin', the pin is $PIN\" >&2; exit 1; }
mkdir -p '$DST'
rsync -a --checksum --delete '$SRC/' '$DST/'
[ \"\$(cat '$DST/.willeq-mesa-pin')\" = '$PIN' ]
echo \"deploy_mesa_prefix: $DST = pin $PIN (\$(du -sh '$DST' | cut -f1)); libgallium: \$(ls '$DST'/lib/libgallium-*.so)\"" </dev/null
