#!/usr/bin/env bash
# B1089 (#409): the lane lease -- a lock that EXPIRES BY LACK OF ACTIVITY.
#
# This file is the CLIENT side of the lease (it is sourced by
# scripts/eqemu/env.sh, which also defines the lane table and eq_remote).
# The CORE is scripts/eqemu/lease_core.sh: it runs ON the lane's own host,
# one key=value record per lane (the `lease` file inside LEASE_STORE), one
# flock critical section per take/takeover/renew/release/reap. The client
# ships the core to the host for every operation, so the remote copy can
# never be a stale one; a local eqemu lane (EQEMU_HOST=local) runs it in
# place.
#
# The record: card / run / host / pid / acquired_at / last_touched /
# purpose. `show` adds state=HELD|EXPIRED and expires_in=<s>.
#
# EXPIRY RELEASES THE LANE ONLY; NOTHING IS EVER KILLED BY EXPIRY. The
# takeover rule is pure TTL: a record whose last_touched is >=
# LANE_LEASE_TTL_S old (ONE env var, default 900 s) is free, whatever pid
# it names -- the holder's pid lives on the dev box, so liveness cannot be
# tested on the lane host; a live holder renews before its TTL, which is
# what keeps a 30-min scenario from being stolen. A renew (and therefore
# any lane operation, which renews first) by a holder whose lease expired
# or was taken over exits with LEASE_EXPIRED (63) and prints the holder
# plus a re-lease one-liner (next to scenario.sh's 64-69 operator codes;
# 63 is the lease's).
#
# SCRIPTS ARE MANDATORY: only the lane scripts touch the lease; a
# hand-rolled ssh gets no renewal and loses the lane at TTL.
#
# Holder identity: LANE_LEASE_CARD / LANE_LEASE_RUN / LANE_LEASE_PID,
# defaulted from the kanban environment by eqemu_lock_take (card =
# HERMES_KANBAN_TASK, run = the kanban run id). Explicit --card/--run on
# the `lease` CLI are the human path. A missing identity is allowed and
# recorded as `manual`/<pid>.
#
# Red-before is N/A (a new helper). The net:
# scripts/eqemu/lease_selftest.sh (ctest LeaseCoreSelfTest, hermetic).

LEASE_EXPIRED=63

# One env var for the whole tree (issue #409: TTL from one env var,
# default 900 s = 15 min).
LANE_LEASE_TTL_S="${LANE_LEASE_TTL_S:-900}"
# Renew-on-use in eq_remote: 1 = every lane-touching operation renews the
# lease first (the default once this shell HOLDS the lane). A shell that
# never took the lease (LANE_LEASE_HELD unset) never renews, so read-only
# scripts (status.sh, the selftests' probes) keep working untouched.
: "${LANE_LEASE_AUTO_RENEW:=1}"
export LANE_LEASE_TTL_S LANE_LEASE_AUTO_RENEW

# Where the lease record lives, per lane (override: EQEMU_LEASE_STORE).
# Lane 5's store is on the ThinkPad's LOCAL disk, never under
# /mnt/projects (that is the NAS NFS export shared with dev-u24's local
# eqemu lane -- one shared file is exactly the defect the lease fixes,
# issue #409).
case "${EQEMU_LANE:-}" in
    5) EQEMU_LEASE_STORE="${EQEMU_LEASE_STORE:-/opt/akk-stack/.eqemu-lease/lane5}" ;;
    *) EQEMU_LEASE_STORE="${EQEMU_LEASE_STORE:-/mnt/projects/.eqemu-lease/lane${EQEMU_LANE:-1}}" ;;
esac
export EQEMU_LEASE_STORE

# _lease_client_cmd <store> <cmd> <args...> -- run one core command against
# the store, returning the core's exit code (63 = LEASE_EXPIRED).
_lease_client_cmd() {
    local store="$1" cmd="$2"; shift 2
    # Lane 5's store is on the ThinkPad's LOCAL disk. When the client is
    # already on the lane's host, run the core in this process instead of
    # sshing to oneself (ssh to a host from itself is a needless round trip,
    # and a keyless host would be 65). The eqemu lanes are never driven from
    # their own host, so this only ever fires for lane 5 on the ThinkPad --
    # but the check is the HOST, not the lane, so it stays true for any
    # future lane whose store sits beside its driver.
    local on_lane_host=0
    if [ "${EQEMU_HOST:-local}" != local ] && \
       [ "$(hostname)" = "${EQEMU_HOST#*@}" ]; then
        on_lane_host=1
    fi
    if [ "${store}" = "${XU4_LEASE_STORE:-}" ]; then
        # The XU4's lease lives ON the XU4; it is never local to this box.
        local LANE_LEASE_LANENAME=xu4
        _lease_client_ssh "user@${XU4_SBC_HOST:-10.0.30.127}" "${store}" "${cmd}" "$@"
        return $?
    fi
    if [ "${EQEMU_HOST:-local}" = local ] || [ "${on_lane_host}" = 1 ]; then
        mkdir -p "${store}"
        # Mirror the ssh branch: the client passes the dead-runs list by
        # its store name (`deadruns`); place it there so the core's
        # store-relative resolution finds it in local mode too.
        [ -n "${LANE_LEASE_DEADRUNS_FILE:-}" ] && cp -f "${LANE_LEASE_DEADRUNS_FILE}" "${store}/deadruns"
        LANE_LEASE_HOST="${LANE_LEASE_HOST:-$(hostname)}" \
        LANE_LEASE_CARD="${LANE_LEASE_CARD:-manual}" LANE_LEASE_RUN="${LANE_LEASE_RUN:-manual}" \
        LANE_LEASE_PID="${LANE_LEASE_PID:-$$}" LANE_LEASE_LANENAME="${LANE_LEASE_LANENAME:-${EQEMU_LANE:-local}}" \
        LEASE_STORE="${store}" LEASE_TTL_S="${LANE_LEASE_TTL_S}" \
            bash "$(dirname "${BASH_SOURCE[0]}")/lease_core.sh" "${cmd}" "$@"
        return $?
    fi
    _lease_client_ssh "${EQEMU_SSH}" "${store}" "${cmd}" "$@"
    return $?
}

# _lease_client_ssh <ssh-target> <store> <cmd> <args...>
_lease_client_ssh() {
    local ssh_target="$1" store="$2" cmd="$3"; shift 3
    # NOTE: raw ssh/scp, NOT eq_remote -- the client must never renew
    # inside its own renew (recursion); the renew-on-use hook lives on the
    # caller's side only.
    if ! ssh -n -o BatchMode=yes -o ConnectTimeout=10 "${ssh_target}" "mkdir -p '${store}'" 2>/dev/null; then
        echo "lease: cannot reach ${ssh_target}:${store}" >&2
        return 65
    fi
    scp -q -o BatchMode=yes -o ConnectTimeout=10 \
        "$(dirname "${BASH_SOURCE[0]}")/lease_core.sh" \
        "${ssh_target}:${store}/lease_core.sh" 2>/dev/null \
        || { echo "lease: cannot ship helper to ${ssh_target}:${store}" >&2; return 65; }
    if [ -n "${LANE_LEASE_DEADRUNS_FILE:-}" ]; then
        scp -q -o BatchMode=yes -o ConnectTimeout=10 "${LANE_LEASE_DEADRUNS_FILE}" \
            "${ssh_target}:${store}/deadruns" 2>/dev/null \
            || { echo "lease: cannot ship dead-runs file" >&2; return 65; }
    fi
    # The remote command line is one bash -c argv (each field printf-%q'd),
    # so no remote word-splitting of the identity fields; the WHOLE remote
    # command is %q'd again for the outer shell ssh runs, so a purpose
    # containing " or $ cannot break out of the inner string.
    local remote_cmd
    # Layout: <identity env> bash <store>/lease_core.sh <cmd> <args...>
    remote_cmd=$(printf 'LANE_LEASE_HOST=%s LANE_LEASE_CARD=%s LANE_LEASE_RUN=%s LANE_LEASE_PID=%s LANE_LEASE_LANENAME=%s LEASE_STORE=%s LEASE_TTL_S=%s bash %s/lease_core.sh %s' \
        "$(printf '%q' "${LANE_LEASE_HOST:-$(hostname)}")" \
        "$(printf '%q' "${LANE_LEASE_CARD:-manual}")" \
        "$(printf '%q' "${LANE_LEASE_RUN:-manual}")" \
        "$(printf '%q' "${LANE_LEASE_PID:-$$}")" \
        "$(printf '%q' "${LANE_LEASE_LANENAME:-${EQEMU_LANE:-local}}")" \
        "$(printf '%q' "${store}")" \
        "$(printf '%q' "${LANE_LEASE_TTL_S}")" \
        "$(printf '%q' "${store}")" \
        "$(printf '%q' "${cmd}")")
    local a
    for a in "$@"; do remote_cmd+=" $(printf '%q' "$a")"; done
    # The remote command travels base64-encoded: ssh joins all its argv
    # with spaces into ONE remote shell invocation, so any form of nested
    # shell quoting (bash -c "..." inside bash -c "...") is at the mercy of
    # the far shell's dialect -- and the far shell here is NOT guaranteed
    # to be bash. base64 is shell-free end to end: the command string is
    # parsed exactly ONCE, by the `bash` we name explicitly, fed to it on
    # stdin. A purpose containing quotes, $ or unicode arrives byte-for-byte
    # (verified live on lane 4).
    local b64
    b64="$(printf '%s' "${remote_cmd}" | base64 -w0)"
    ssh -n -o BatchMode=yes -o ConnectTimeout=10 "${ssh_target}" \
        "printf %s ${b64} | base64 -d | bash"
    return $?
}

# ---------------------------------------------------------------------------
# The six lane operations.
# ---------------------------------------------------------------------------

# eqemu_lease_take <purpose> -- 0 = you hold the lane now; 1 = someone
# else does (named); 2 = corrupt record (refused).
eqemu_lease_take() {
    _lease_client_cmd "${EQEMU_LEASE_STORE}" take "${1:-manual take}"
}

# eqemu_lease_renew <purpose> -- 0 = still yours; 63 = lost it (the holder
# and the re-lease one-liner are on stderr).
eqemu_lease_renew() {
    _lease_client_cmd "${EQEMU_LEASE_STORE}" renew "${1:-renew}"
}

# eqemu_lease_release -- owner-checked (this closes B922 #241 for the
# eqemu half). 0 = released (or already free); 63 = not ours. VERIFIED:
# after a successful release the lane is re-shown; if the record still
# names this exact identity, the release silently no-op'd (an ssh flake)
# and this returns 65 instead of a false 0 -- a release must never claim
# "free" while the lane is still held by us.
eqemu_lease_release() {
    local rc out card run
    eqemu_lease_release_impl
    rc=$?
    [ ${rc} -eq 0 ] || return ${rc}
    out="$(eqemu_lease_show 2>/dev/null)"; rc=$?
    card="$(printf '%s\n' "${out}" | sed -n 's/^card=//p' | head -n1)"
    run="$(printf '%s\n' "${out}" | sed -n 's/^run=//p' | head -n1)"
    if [ -n "${card}" ] && [ "${card}" = "${LANE_LEASE_CARD:-}" ] && [ "${run}" = "${LANE_LEASE_RUN:-}" ]; then
        echo "lease ${LANE_LEASE_CARD}: WARNING: release returned 0 but the record is still present (card=${card} run=${run}) -- the lane is NOT free; retry the release" >&2
        return 65
    fi
    return 0
}

eqemu_lease_release_impl() {
    _lease_client_cmd "${EQEMU_LEASE_STORE}" release
}

# eqemu_lease_show -- the record plus state/expires_in; exit 1 = FREE.
eqemu_lease_show() {
    _lease_client_cmd "${EQEMU_LEASE_STORE}" show
}

# eqemu_lease_reap <dead-runs-file> [--dry] -- ship the dead-run list
# (one run id per line) and reap: release their leases and kill exactly
# the pids tracked for those runs. A run NOT on the list is never touched.
eqemu_lease_reap() {
    local dry=0 rf="${1:-}"
    [ -n "${rf}" ] || { echo "eqemu_lease_reap: no dead-runs file" >&2; return 64; }
    [ ! -f "${rf}" ] && { echo "eqemu_lease_reap: no such file: ${rf}" >&2; return 64; }
    [ "$#" -ge 2 ] && [ "${2}" = "--dry" ] && dry=1
    # The file is shipped to <store>/deadruns, so the remote arg is that
    # fixed name, never the local basename.
    LANE_LEASE_DEADRUNS_FILE="${rf}" \
        _lease_client_cmd "${EQEMU_LEASE_STORE}" reap deadruns "${dry}"
}

# eqemu_lease_track <run> <pid> <purpose> [host] -- record a pid this run
# owns, so `reap` kills exactly those of a dead run. With a host: the pid
# lives on that host (its runs/<run>.<host>.pids, killed over ssh); without:
# this host.
eqemu_lease_track() {
    _lease_client_cmd "${EQEMU_LEASE_STORE}" track "${1:?run}" "${2:?pid}" "${3:-}" "${4:-}"
}

# ---------------------------------------------------------------------------
# Renew-on-use: eq_remote renews FIRST when this shell holds the lane
# (LANE_LEASE_HELD, set by eqemu_lock_take / eqemu_lease_take). If the
# renewal fails (63), the operation is refused -- a stolen lane is never
# used by its old holder.
# ---------------------------------------------------------------------------
eq_remote() {
    # eq_remote <argv...> — run one command on the eqemu host, argv preserved.
    # ssh -n: never read this shell's stdin (callers loop over `read` input).
    if [ "${LANE_LEASE_HELD:-0}" = 1 ] && [ "${LANE_LEASE_AUTO_RENEW:-1}" = 1 ]; then
        eqemu_lease_renew "use: $1 $2 $3" || return "${LEASE_EXPIRED}"
    fi
    if eqemu_is_local; then
        "$@"
    else
        ssh -n -o BatchMode=yes -o ConnectTimeout=10 "${EQEMU_SSH}" -- "$(printf '%q ' "$@")"
    fi
}

# ---------------------------------------------------------------------------
# Compatibility: the old lock wrappers. EVERY caller in issue #409's table
# keeps working through these; the lease is the mechanism, the names are
# the contract. release is owner-checked by the lease's identity (stronger
# than the old blind rm).
# ---------------------------------------------------------------------------
eqemu_lock_take() {
    # eqemu_lock_take <owner, e.g. T###> -- takes the lane's LEASE (the
    # record lives on the lane's host; see the header). The card is the
    # kanban task id when this is a kanban run (the dispatcher's env, or
    # the shared board DB when no env var is set), else the owner's first
    # word; the run is the kanban run id, else the owner string. A missing
    # identity is allowed and recorded as `manual`/<pid>. Exit 0 = you own
    # the lane; 1 = someone else does (named): STOP.
    local owner="${1:-manual}"
    local card run
    card="${HERMES_KANBAN_TASK:-}"
    run="${HERMES_KANBAN_RUN_ID:-}"
    if [ -z "${card}" ] && [ -n "${HERMES_KANBAN_DB:-}" ] && [ -n "${HERMES_KANBAN_WORKSPACE:-}" ] && command -v python3 >/dev/null 2>&1; then
        # The headless harness sets no task/run env vars, but the
        # dispatcher pins one workspace per task: the running task with
        # this workspace IS this run's card.
        local q
        q="$(python3 - "$HERMES_KANBAN_DB" "$HERMES_KANBAN_WORKSPACE" <<'PYEOF' 2>/dev/null
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
try:
    row = c.execute(
        "select id, current_run_id from tasks where status='running' and workspace_path=?",
        (sys.argv[2],)).fetchone()
except Exception:
    sys.exit(0)
if row and row[1]:
    print(row[0]); print(row[1], file=sys.stdout)
PYEOF
)"
        # The query prints task id and run id on two lines.
        card="$(printf '%s\n' "${q}" | head -n1)"
        run="$(printf '%s\n' "${q}" | tail -n1)"
        [ -n "${card}" ] || card=""
        [ -n "${run}" ] || run=""
    fi
    [ -n "${card}" ] || card="${owner%% *}"
    [ -n "${run}" ] || run="${owner}"
    # The identity is exported for THIS command only (prefix env); the
    # success path below re-exports it so renew/release in this shell
    # match the record. LANE_LEASE_PID is exported too, because the core
    # records whatever pid it is given and the renew/release identity must
    # match it.
    export LANE_LEASE_PID="${LANE_LEASE_PID:-$$}"
    local rc
    LANE_LEASE_CARD="${card}" LANE_LEASE_RUN="${run}" LANE_LEASE_HOST="$(hostname)" \
        _lease_client_cmd "${EQEMU_LEASE_STORE}" take "${owner}"
    rc=$?
    if [ ${rc} -eq 0 ]; then
        export LANE_LEASE_CARD="${card}" LANE_LEASE_RUN="${run}" \
               LANE_LEASE_HELD=1
    fi
    return ${rc}
}

eqemu_lock_show() {
    # Prints the lease record (plus state=HELD|EXPIRED and expires_in), or
    # "state=FREE" (exit 1) when the lane is free.
    eqemu_lease_show
}

eqemu_lock_release() {
    # Owner-checked through the lease's identity (B922's blind rm is gone):
    # a release of someone else's lease is refused (exit 63). The
    # background renewer is stopped HERE too (not only by scripts' cleanup
    # traps), so a script that releases on an early error path cannot keep
    # renewing a lease it just gave away.
    eqemu_lease_stop_renewer
    local rc
    eqemu_lease_release
    rc=$?
    [ ${rc} -eq 0 ] && export LANE_LEASE_HELD=0
    return ${rc}
}

# ---------------------------------------------------------------------------
# The background renewer (issue #409, point 1): long operations renew from
# inside their own wait loops (or via eq_remote's renew-on-use), OR a
# script starts ONE renewer bound to its own pid that renews every
# LANE_LEASE_TTL_S/3 (floor LANE_LEASE_RENEW_FLOOR_S, default 60 s = an ssh
# round trip) and stops with the script. The renewer dies with its parent
# (an INT/TERM to the script kills the subshell, so nothing outlives the
# run), which is exactly the lease's contract: a dead holder is quiet, and
# a quiet lane is free at TTL.
#   eqemu_lease_start_renewer [interval]  -- after taking the lease, before
#       a long wait (a 30-min scenario must never be stolen).
#   eqemu_lease_stop_renewer              -- in the script's cleanup trap,
#       BEFORE eqemu_lock_release (the final renew is the release's own job).
# ---------------------------------------------------------------------------
eqemu_lease_start_renewer() {
    local iv="${1:-$(( ${LANE_LEASE_TTL_S:-900} / 3 ))}"
    local floor="${LANE_LEASE_RENEW_FLOOR_S:-60}"
    [ "${iv}" -lt "${floor}" ] 2>/dev/null && iv="${floor}"
    [ "${LANE_LEASE_HELD:-0}" = 1 ] || { echo "renewer: lease not held" >&2; return 64; }
    (
        while :; do
            sleep "${iv}"
            eqemu_lease_renew "renewer iv=${iv}s" 2>/dev/null || break
        done
    ) &
    LANE_LEASE_RENEWER_PID=$!
    export LANE_LEASE_RENEWER_PID
    return 0
}

eqemu_lease_stop_renewer() {
    [ -n "${LANE_LEASE_RENEWER_PID:-}" ] || return 0
    kill "${LANE_LEASE_RENEWER_PID}" 2>/dev/null
    wait "${LANE_LEASE_RENEWER_PID}" 2>/dev/null
    LANE_LEASE_RENEWER_PID=
    export LANE_LEASE_RENEWER_PID
    return 0
}

# ---------------------------------------------------------------------------
# XU4: its own lease ON the box (issue #409: "XU4: no lock at all" today --
# the rule was human; now the machine says it too).
# ---------------------------------------------------------------------------
XU4_LEASE_STORE="${XU4_LEASE_STORE:-/home/user/.willeq-lease/xu4}"
export XU4_LEASE_STORE

xu4_lease_take() {   # xu4_lease_take <purpose> -- 0 = held; 1 = held by other
    local rc
    LANE_LEASE_CARD="${LANE_LEASE_CARD:-manual}" LANE_LEASE_RUN="${LANE_LEASE_RUN:-$$}" \
        _lease_client_cmd "${XU4_LEASE_STORE}" take "${1:-manual take}"
    rc=$?
    [ ${rc} -eq 0 ] && export LANE_LEASE_HELD_XU4=1
    return ${rc}
}
xu4_lease_renew() {  # xu4_lease_renew <purpose> -- 63 when lost
    _lease_client_cmd "${XU4_LEASE_STORE}" renew "${1:-renew}"
}
xu4_lease_release() {
    local rc out card run
    xu4_lease_release_impl
    rc=$?
    [ ${rc} -eq 0 ] || return ${rc}
    # Same verification as the eqemu side: a release that returned 0 must
    # have freed the lane.
    out="$(xu4_lease_show 2>/dev/null)"; rc=$?
    card="$(printf '%s\n' "${out}" | sed -n 's/^card=//p' | head -n1)"
    run="$(printf '%s\n' "${out}" | sed -n 's/^run=//p' | head -n1)"
    if [ -n "${card}" ] && [ "${card}" = "${LANE_LEASE_CARD:-}" ] && [ "${run}" = "${LANE_LEASE_RUN:-}" ]; then
        echo "lease xu4: WARNING: release returned 0 but the record is still present (card=${card} run=${run}) -- the lane is NOT free; retry the release" >&2
        return 65
    fi
    export LANE_LEASE_HELD_XU4=0
    return 0
}
xu4_lease_release_impl() {
    _lease_client_cmd "${XU4_LEASE_STORE}" release
}
xu4_lease_show() {
    _lease_client_cmd "${XU4_LEASE_STORE}" show
}
xu4_lease_track() {  # xu4_lease_track <run> <pid> <purpose> [host]
    _lease_client_cmd "${XU4_LEASE_STORE}" track "${1:?run}" "${2:?pid}" "${3:-}" "${4:-}"
}
xu4_lease_reap() {   # xu4_lease_reap <dead-runs-file> [--dry]
    local dry=0 rf="${1:-}"
    [ -n "${rf}" ] || { echo "xu4_lease_reap: no dead-runs file" >&2; return 64; }
    [ ! -f "${rf}" ] && { echo "xu4_lease_reap: no such file: ${rf}" >&2; return 64; }
    [ "$#" -ge 2 ] && [ "${2}" = "--dry" ] && dry=1
    LANE_LEASE_DEADRUNS_FILE="${rf}" \
        _lease_client_cmd "${XU4_LEASE_STORE}" reap deadruns "${dry}"
}
