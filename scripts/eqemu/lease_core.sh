#!/usr/bin/env bash
# B1089 (#409): the lane-lease CORE -- runs ON the lane's own host.
#
# One key=value record per lane (the `lease` file inside LEASE_STORE),
# fields card / run / host / pid / acquired_at / last_touched / purpose.
# Every mutating operation (take, takeover-of-expired, renew, release,
# reap) is ONE flock critical section on this host; the record is written
# through an atomic rename.
#
# Usage (the client in scripts/eqemu/lease_lib.sh does this for you):
#   LEASE_STORE=<dir> bash lease_core.sh <cmd> [args]
# Commands:
#   take   <purpose>                          -- holder identity from the
#                                                environment LANE_LEASE_CARD,
#                                                LANE_LEASE_RUN, LANE_LEASE_PID
#   renew  <purpose>
#   release
#   show
#   reap   <dead-runs-file> [1|0]             -- file: one run id per line
#   track  <run> <pid> <purpose> [host]
#
# Process tracking is per-host: a run's pids are recorded in runs/<run>.pids
# (this host -- where the core runs) and, per named host, in
# runs/<run>.<host>.pids. `reap` kills from both: this host by `kill`, a
# named host by raw ssh (an unreachable host's pids are reported, not killed,
# and are never fatal).
#
# EXPIRY RELEASES THE LANE ONLY; NOTHING IS EVER KILLED BY EXPIRY. The
# takeover rule is pure TTL: a record whose last_touched is >= LEASE_TTL_S
# old is free, whatever pid it names (the holder's pid lives on the dev
# box, so liveness cannot be tested on the lane host; a live holder
# renews before its TTL). A renew by a holder whose lease expired or was
# taken over exits with 63 and prints the holder plus a re-lease one-liner.
#
# Exit codes (shared with the client):
#   0   ok (or: the lane was free, on take)
#   1   lane HELD by another card (take; holder named on stderr)
#   2   record corrupt (take) -- refused, manual cleanup
#   63  LEASE_EXPIRED: not ours any more (renew/release)
#   64  usage error
#
# Red-before is N/A (new helper). Net: scripts/eqemu/lease_selftest.sh
# (ctest LeaseCoreSelfTest, hermetic, no ssh/network).

set -u
LEASE_EXPIRED=63
CMD="${1:-}"
[ -n "${CMD}" ] || { echo "usage: lease_core.sh <take|renew|release|show|reap|track> [args]" >&2; exit 64; }
shift
STORE="${LEASE_STORE:?lease_core: LEASE_STORE not set}"
TTL="${LEASE_TTL_S:-900}"
[ -n "${LANE_LEASE_CARD:-}" ] || LANE_LEASE_CARD=manual
[ -n "${LANE_LEASE_RUN:-}" ] || LANE_LEASE_RUN=manual
[ -n "${LANE_LEASE_HOST:-}" ] || LANE_LEASE_HOST=$(hostname 2>/dev/null || echo unknown)
[ -n "${LANE_LEASE_PID:-}" ] || LANE_LEASE_PID=0
FILE="${STORE}/lease"
LOCK="${STORE}/.lock"
mkdir -p "${STORE}/runs"

now() { date +%s; }
readk() { [ -e "$2" ] || return 0; sed -n "s/^$1=//p" "$2" | head -n1; }

write_rec() { # <purpose> <acquired0>
    local n="$1" t0="$2" tmp
    tmp="${FILE}.$$.tmp"
    {
        echo "card=${LANE_LEASE_CARD}"
        echo "run=${LANE_LEASE_RUN}"
        echo "host=${LANE_LEASE_HOST}"
        echo "pid=${LANE_LEASE_PID}"
        echo "acquired_at=${t0}"
        echo "last_touched=$(now)"
        echo "purpose=${n}"
    } > "$tmp" && mv -f "$tmp" "$FILE"
}

expired_msg() { # <holder> <host>
    echo "lease ${LANE_LEASE_CARD}: EXPIRED (held by $1 on $2)" >&2
    echo "re-lease: bash scripts/eqemu/lease take --lane ${LANE_LEASE_LANENAME:-?} --card '${LANE_LEASE_CARD}' --purpose '<why>'" >&2
}

case "${CMD}" in
take)
    exec 9>"${LOCK}"; flock 9
    P="${1:-}"
    TOOKOVER=0
    if [ -e "${FILE}" ]; then
        CR="$(readk run "${FILE}")"; CP="$(readk pid "${FILE}")"; CT="$(readk last_touched "${FILE}")"
        if [ -z "${CR}" ] || [ -z "${CP}" ] || ! [ "${CP}" -gt 0 ] 2>/dev/null \
           || [ -z "${CT}" ] || ! [ "${CT}" -gt 0 ] 2>/dev/null; then
            echo "lease ${LANE_LEASE_CARD}: record at ${FILE} corrupt -- refusing; manual cleanup" >&2
            exit 2
        fi
        if [ $(( $(now) - CT )) -lt "${TTL}" ]; then
            echo "lease ${LANE_LEASE_CARD}: HELD by $(readk card "${FILE}")/${CR} on $(readk host "${FILE}") (purpose: $(readk purpose "${FILE}"))" >&2
            exit 1
        fi
        echo "lease ${LANE_LEASE_CARD}: taking over from $(readk card "${FILE}")/${CR} (expired $(( $(now) - CT ))s ago, ttl ${TTL}s)"
        TOOKOVER=1
    fi
    write_rec "${P}" "$(now)"
    echo "lease ${LANE_LEASE_CARD}: taken by card=${LANE_LEASE_CARD} run=${LANE_LEASE_RUN} host=${LANE_LEASE_HOST} pid=${LANE_LEASE_PID} ttl=${TTL}s"
    exit 0
    ;;
renew)
    # <purpose> -- optional: without one, the current purpose is kept.
    exec 9>"${LOCK}"; flock 9
    P="${1:-}"
    [ -e "${FILE}" ] || { expired_msg "<none>" "<free>"; exit "${LEASE_EXPIRED}"; }
    CR="$(readk run "${FILE}")"; CP="$(readk pid "${FILE}")"; CC="$(readk card "${FILE}")"
    if [ "${CR}" != "${LANE_LEASE_RUN}" ] || [ "${CP}" != "${LANE_LEASE_PID}" ] || [ "${CC}" != "${LANE_LEASE_CARD}" ]; then
        expired_msg "${CC}/${CR}" "$(readk host "${FILE}")"
        exit "${LEASE_EXPIRED}"
    fi
    write_rec "${P:-$(readk purpose "${FILE}")}" "$(readk acquired_at "${FILE}")"
    exit 0
    ;;
release)
    # Owner-checked (closes B922 #241 for the eqemu half): a release of
    # someone else's lease is refused, never a blind rm.
    exec 9>"${LOCK}"; flock 9
    [ -e "${FILE}" ] || exit 0
    CR="$(readk run "${FILE}")"; CP="$(readk pid "${FILE}")"; CC="$(readk card "${FILE}")"
    if [ "${CR}" != "${LANE_LEASE_RUN}" ] || [ "${CP}" != "${LANE_LEASE_PID}" ] || [ "${CC}" != "${LANE_LEASE_CARD}" ]; then
        echo "lease ${LANE_LEASE_CARD}: refusing to release: held by ${CC}/${CR} on $(readk host "${FILE}")" >&2
        exit "${LEASE_EXPIRED}"
    fi
    rm -f "${FILE}" "${STORE}/runs/${LANE_LEASE_RUN}.pids"
    exit 0
    ;;
show)
    [ -e "${FILE}" ] || { echo "state=FREE"; exit 1; }
    N="$(now)"; LT="$(readk last_touched "${FILE}")"
    if [ $((N - LT)) -lt "${TTL}" ]; then S=HELD; E=$((TTL - N + LT)); else S=EXPIRED; E=0; fi
    cat "${FILE}"
    echo "state=${S}"
    echo "expires_in=${E}"
    exit 0
    ;;
reap)
    # reap <dead-runs-file> [dry 0|1] -- for each run id: kill exactly the
    # pids tracked under runs/<run>.pids, and release its lease WHEN THAT
    # RUN HOLDS IT. The release is keyed on the record NAMING the dead run,
    # which is the safe rule: a live run's record names THAT live run (not
    # on the dead list) and is never touched; a record that names a dead
    # run is, by construction, a lane whose holder is dead, so freeing it
    # -- fresh or expired -- is correct. (A takeover is already reflected:
    # a taken-over lease names its new holder, so it does not match.)
    exec 9>"${LOCK}"; flock 9
    RF="${1:?lease_core: reap needs a dead-runs file}"
    DRY="${2:-0}"
    # A relative name (the client ships the list to <store>/deadruns and
    # passes just that name) resolves against the STORE, never the cwd --
    # the ssh login cwd is not the store, so cwd-relative resolution would
    # silently miss a file that is actually present.
    case "${RF}" in
        /*) ;;
        *) [ -e "${STORE}/${RF}" ] && RF="${STORE}/${RF}" ;;
    esac
    [ -e "${RF}" ] || { echo "reap: no such dead-run file: ${RF}" >&2; exit 64; }
    while IFS= read -r run; do
        [ -n "${run}" ] || continue
        N=0; PIDS="${STORE}/runs/${run}.pids"
        if [ -e "${FILE}" ] && [ "$(readk run "${FILE}")" = "${run}" ]; then
            if [ "${DRY}" = 1 ]; then
                echo "DRY run=${run}: would release lease"
            else
                rm -f "${FILE}"
                echo "run=${run}: lease released"
            fi
        fi
        if [ "${DRY}" = 1 ]; then
            if [ -e "${PIDS}" ]; then
                N="$(wc -l < "${PIDS}" | tr -d '[:space:]')"
                [ "${N}" -gt 0 ] && echo "DRY run=${run}: would kill ${N} tracked pids"
            fi
            for FH in "${STORE}"/runs/${run}.*.pids; do
                [ -e "${FH}" ] || continue
                N="$(wc -l < "${FH}" | tr -d '[:space:]')"
                [ "${N}" -gt 0 ] && echo "DRY run=${run}: would kill ${N} tracked pids on $(basename "${FH}" | sed "s/^${run}\.//; s/\.pids$//")"
            done
        else
            if [ -e "${PIDS}" ]; then
                while read -r p _rest; do
                    [ -n "${p}" ] && { kill "${p}" 2>/dev/null; N=$((N + 1)); }
                done < "${PIDS}"
                rm -f "${PIDS}"
                [ "${N}" -gt 0 ] && echo "run=${run}: ${N} tracked pids killed"
            fi
            for FH in "${STORE}"/runs/${run}.*.pids; do
                [ -e "${FH}" ] || continue
                H="$(basename "${FH}" | sed "s/^${run}\.//; s/\.pids$//")"; N=0
                if ssh -n -o BatchMode=yes -o ConnectTimeout=10 "user@${H}" "true" 2>/dev/null; then
                    while read -r p _rest; do
                        [ -n "${p}" ] && { ssh -n -o BatchMode=yes "user@${H}" "kill ${p}" 2>/dev/null; N=$((N + 1)); }
                    done < "${FH}"
                    rm -f "${FH}"
                    [ "${N}" -gt 0 ] && echo "run=${run}: ${N} tracked pids killed on ${H}"
                else
                    echo "run=${run}: ${H} unreachable -- its tracked pids were NOT killed" >&2
                fi
            done
        fi
    done < "${RF}"
    exit 0
    ;;
track)
    # track <run> <pid> <purpose> [host] -- record a pid this run owns.
    # With a host: runs/<run>.<host>.pids (killed over ssh by reap);
    # without: runs/<run>.pids on THIS host.
    [ -n "${2:-}" ] && [ "${2}" -gt 0 ] 2>/dev/null || exit 64
    if [ -n "${4:-}" ]; then
        case "${4}" in *[!A-Za-z0-9._-]*) echo "track: bad host '${4}'" >&2; exit 64 ;; esac
        echo "${2} ${3:-}" >> "${STORE}/runs/${1}.${4}.pids"
    else
        echo "${2} ${3:-}" >> "${STORE}/runs/${1}.pids"
    fi
    exit 0
    ;;
*)
    echo "lease_core: unknown command '${CMD}'" >&2
    exit 64
    ;;
esac
