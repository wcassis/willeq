#!/usr/bin/env bash
# B1203 (#522): evidence_copy.sh <slug> <B####> [--force] -- the hand-back evidence copy as a
# no-agent job (solo prompt pass step 2): small evidence from ~/kb-evidence/<slug>/ and bulk
# evidence from /mnt/scratch/kb-evidence/<slug>/ to /mnt/projects/fable/review/<B>/worker/,
# frames and PNGs excluded; the ledger clips ~/kb-evidence/VISUAL-FX/<slug>/ (when present)
# to /mnt/projects/fable/review/VISUAL-FX/<B>/. Idempotent (rsync); done-file
# $JOBS_ROOT/<slug>/evidence_copy.done:  JOB evidence_copy <slug> status=ok B=<B> files=N bytes=M
set -u
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/jobs_lib.sh"
SLUG=${1:?usage: evidence_copy.sh <slug> <B####> [--force]}; B=${2:?B number}; FORCE=${3:-}
case "$B" in B[0-9]*) ;; *) echo "B number must look like B1234, got $B" >&2; exit 2 ;; esac
REVIEW_ROOT=${REVIEW_ROOT:-/mnt/projects/fable/review}
DST="$REVIEW_ROOT/$B/worker"
JOB_NAME=evidence_copy; JOB_SLUG=$SLUG
job_begin "$SLUG" evidence_copy $FORCE; rc=$?
[ $rc -eq 10 ] && exit 0; [ $rc -eq 11 ] && exit 0
mkdir -p "$DST"
st=ok; n=0
for src in "$HOME/kb-evidence/$SLUG" "/mnt/scratch/kb-evidence/$SLUG"; do
  [ -d "$src" ] || continue
  n=$((n+1))
  rsync -a --exclude frames --exclude '*.png' "$src/" "$DST/" >> "$JOB_LOG" 2>&1 || st=rsync_failed
done
if [ -d "$HOME/kb-evidence/VISUAL-FX/$SLUG" ]; then
  mkdir -p "$REVIEW_ROOT/VISUAL-FX/$B"
  rsync -a "$HOME/kb-evidence/VISUAL-FX/$SLUG/" "$REVIEW_ROOT/VISUAL-FX/$B/" >> "$JOB_LOG" 2>&1 || st=rsync_failed
fi
[ $n -gt 0 ] || st=no_evidence
files=$(find "$DST" -type f | wc -l); bytes=$(du -sb "$DST" | cut -f1)
job_end "$st" "B=$B sources=$n files=$files bytes=$bytes dst=$DST"
