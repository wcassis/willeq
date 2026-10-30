#!/usr/bin/env bash
# B1194 (#513) FIX, review note 8626 finding 5: tee the `rbuild --host auto
# --status` probe run to the B1194 evidence dir so the README's wall-time
# figure points at committed output (the VM files are teed the same way).
#
#   bash refactor/B1194/scripts/auto_probe_tee.sh   (from any lab200 host that
#   has the rbuild script and mounts /mnt/projects; run on hermes-ext 2026-10-29)
set -u
OUT=/mnt/projects/fable/review/B1194/hermes-ext/auto_probe_$(date -u +%Y%m%dT%H%M%SZ).txt
{
  echo "# rbuild --host auto --status, run on $(hostname) at $(date -u)"
  s=$(date +%s.%N)
  rbuild --host auto --status 2>&1
  rc=$?
  e=$(date +%s.%N)
  echo "[exit $rc in $(awk -v s="$s" -v e="$e" 'BEGIN{printf "%.1f", e-s}') s wall]"
} | tee "$OUT"
