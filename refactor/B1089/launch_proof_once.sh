#!/usr/bin/env bash
# B1089: launch ONE integration proof DETACHED (setsid+nohup), and REFUSE if
# a proof is already running (two runs share card ids + lane + log, so a
# second concurrent run collides with the first -- the run4/run5 failure).
# A terminal(background=true) proc is reaped when the agent turn ends, so a
# 39-min proof must be orphaned this way. It writes <out>/exit (0=PASS,1=FAIL)
# and <out>/proof.log. Poll those; do NOT launch a second one.
set -u
OUT="/home/user/kb-evidence/kb-b1089-lane-lease/proof-<next>"
SCRIPT="/home/user/kb-evidence/kb-b1089-lane-lease/lease_integration_proof.sh"
# Guard: exactly one proof process at a time. The pattern below matches the
# proof's own argv ("bash <script> 4 <out>") but never this launcher's argv
# (which only contains the launcher path), so the hermes wrapper shell that
# runs this script cannot false-positive.
if pgrep -f "bash ${SCRIPT} 4 " | grep -qv "$$"; then
    echo "REFUSED: a proof is already running ($(pgrep -af "bash ${SCRIPT} 4 " | head -1))"
    exit 3
fi
mkdir -p "${OUT}"
rm -f "${OUT}/exit"
setsid nohup bash "${SCRIPT}" 4 "${OUT}" > "${OUT}/nohup.out" 2>&1 < /dev/null &
echo "launched detached proof pid=$! -> ${OUT}/exit when done"
