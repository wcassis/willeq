#!/usr/bin/env bash
# B940 (#258): the lane self-check for the process-watcher zone exemption.
#
# The one repeatable net for "the lane drifted back to the default
# watcher": fails if the lane's process-watcher.pl lacks the B940
# env-read, if the upstream allowlist regex is no longer the DEFAULT
# (someone replaced it instead of env-driving it), or if the compose
# file lacks the PROCESS_WATCHER_EXCLUDE line with the exact value.
# Read-only: takes the lease only if not already held by this shell,
# and never patches, restarts or otherwise changes the lane.
#
# usage: scripts/eqemu/watcher_selftest.sh --lane N   (repeatable)
#        scripts/eqemu/watcher_selftest.sh --lanes 1-5
# Exit: 0 = every named lane is provisioned; 1 = a lane is back at the
# default (FAIL lines name the missing piece); 64 = usage.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

# The value the provision script installs (one definition, mirrored here
# on purpose: a drift between the two is exactly what this net exists to
# catch, and it fails loudly because the grep uses the SELFTEST value).
EXCLUDE_VALUE='(bin|/bin)/zone'

LANES=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --lane)  LANES="${LANES:+${LANES} }$2"; shift 2 ;;
        --lanes) LANES="$2"; shift 2 ;;
        -h|--help) awk 'NR > 1 && !/^#/ { exit } NR > 1' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
[ -n "${LANES}" ] || { echo "usage: --lane N (repeatable) or --lanes 1-5" >&2; exit 64; }
case "${LANES}" in
    *-*|*,*) LANES="$(seq "${LANES%-*}" "${LANES#*-}" 2>/dev/null || true)";;
esac
[ -n "${LANES}" ] || { echo "unusable lane list: ${LANES}" >&2; exit 64; }

selftest_lane() {  # selftest_lane <lane>
    local lane="$1" fail=0 out rc
    # Same sticky-env trap as the provision script: env.sh keeps an
    # already-set EQEMU_HOST over the lane table, so a --lanes loop in ONE
    # shell would check every lane on lane 1's host. The host follows the
    # lane, every time.
    unset EQEMU_HOST EQEMU_SSH
    EQEMU_LANE="${lane}"
    source "${SCRIPT_DIR}/env.sh"
    echo "== lane ${lane} (${EQEMU_HOST}) =="
    # Read-only: no lease. (A provision holds one only while patching; a
    # drift check reads two files and a grep -- the renew-on-use in
    # eq_remote keeps this safe even if someone mid-provisions, because
    # nothing it reads is being mutated by this script.)

    local watcher="${AKK_DIR}/assets/scripts/process-watcher.pl"
    local composef="${AKK_DIR}/docker-compose.yml"

    out="$(eq_remote bash -c "grep -c B940 ${watcher} 2>/dev/null" 2>/dev/null)" || out=0
    [ "${out:-0}" -ge 1 ] || { echo "FAIL: ${watcher} is back at the default (no B940 env-read)"; fail=1; }

    rc=0; eq_remote bash -c "perl -c ${watcher} >/dev/null 2>&1" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: perl -c ${watcher} (a broken watcher would kill the fleet on the next scan)"; fail=1; }

    rc=0; eq_remote bash -c "grep -qF 'gcc|mysql|wget|unzip|node|go-build|tar|world|JetBrains|vscode|remote-dev-server|gzip/i' ${watcher}" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: the upstream allowlist regex is gone from ${watcher} (it must stay the DEFAULT)"; fail=1; }

    rc=0; eq_remote bash -c "grep -qF 'PROCESS_WATCHER_EXCLUDE=${EXCLUDE_VALUE}' ${composef}" || rc=1
    [ "${rc}" -eq 0 ] || { echo "FAIL: ${composef} lacks PROCESS_WATCHER_EXCLUDE=${EXCLUDE_VALUE} (the zone exemption)"; fail=1; }

    if [ "${fail}" -ne 0 ]; then
        echo "lane ${lane}: NOT provisioned (re-run scripts/eqemu/watcher_provision.sh --lane ${lane})"
        return 1
    fi
    echo "lane ${lane}: provisioned"
}

overall=0
for lane in ${LANES}; do
    selftest_lane "${lane}" || overall=1
done
[ "${overall}" -eq 0 ] && echo "all lanes provisioned (${LANES})" || echo "one or more lanes are back at the default watcher (${LANES})"
exit "${overall}"
