#!/usr/bin/env bash
# B1089 (#409): the hermetic net for the lane-lease CORE.
#
# Runs scripts/eqemu/lease_core.sh against a temp dir -- no ssh, no
# network, no lane host (the core is host-side and self-contained, so a
# local run of it IS the unit test; the client wrapper is exercised by the
# integration proof, refactor/B1089/README.md). Pattern:
# scripts/pi/lock_selftest.sh, but hermetic, so ctest runs it in CI
# (LeaseCoreSelfTest, label hygiene). The core's per-host reap kills reach
# `ssh` over argv, so the per-host case runs the core with an ssh SHIM on
# PATH (the log-and-check stand-in: it records each call and decides
# reachability) -- still no network, and the core's real command lines are
# what run.
#
# Red-before is N/A: the helper is new (there is no pre-B1089 core to be
# red against); the cases below are the contract.
#
# Exit 0 = PASS; 1 = a case failed.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE="${SCRIPT_DIR}/lease_core.sh"
TTL=3
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
FAIL=0

pass() { echo "ok: $*"; }
fail() { echo "FAIL: $*" >&2; FAIL=1; }

# run <store> <card> <run> <pid> <cmd> [args...] -- the remote core's exact
# invocation shape, locally. Prints output, returns the core's exit code.
run() {
    local store="$1" card="$2" runid="$3" pid="$4" cmd="$5"; shift 5
    LANE_LEASE_HOST=corebox LANE_LEASE_CARD="${card}" LANE_LEASE_RUN="${runid}" \
    LANE_LEASE_PID="${pid}" LANE_LEASE_LANENAME=t LANE_LEASE_PURPOSE= \
    LEASE_STORE="${store}" LEASE_TTL_S="${TTL}" \
        bash "${CORE}" "${cmd}" "$@" 2>&1
}

S="${WORK}/s1"
mkdir -p "${S}"

# 1. take on a free lane: ok.
out="$(run "${S}" card-a run-a 1234 take "take-a")" || fail "take on a free lane (rc=$?)"
echo "${out}" | grep -q "taken by card=card-a run=run-a" && pass "take names card and run" || fail "take output: ${out}"

# 2. second taker refused (lane HELD).
out="$(run "${S}" card-b run-b 5678 take "take-b")"; rc=$?
[ ${rc} -eq 1 ] && pass "second taker refused (rc=1)" || fail "second taker rc=${rc}: ${out}"
echo "${out}" | grep -q "HELD by card-a/run-a" && pass "refusal names the holder" || fail "holder not named: ${out}"

# 3. show: HELD, with expires_in > 0.
out="$(run "${S}" card-a run-a 1234 show)"
echo "${out}" | grep -q "state=HELD" && pass "show: state=HELD" || fail "show state: ${out}"
e="$(echo "${out}" | sed -n 's/^expires_in=//p')"
[ -n "${e}" ] && [ "${e}" -gt 0 ] && pass "show: expires_in=${e}" || fail "show expires_in: ${out}"

# 4. release by a non-owner: refused, owner's lease intact.
out="$(run "${S}" card-b run-b 5678 release)"; rc=$?
[ ${rc} -eq 63 ] && pass "non-owner release refused (rc=63)" || fail "non-owner release rc=${rc}: ${out}"
echo "${out}" | grep -q "refusing to release" && pass "refusal message" || fail "refusal message: ${out}"
run "${S}" card-a run-a 1234 show | grep -q "state=HELD" && pass "lease intact after refused release" || fail "lease gone after refused release"

# 5. owner renew: ok, last_touched moves.
lt1="$(run "${S}" card-a run-a 1234 show | sed -n 's/^last_touched=//p')"
sleep 1
out="$(run "${S}" card-a run-a 1234 renew "still working")"; rc=$?
[ ${rc} -eq 0 ] && pass "owner renew rc=0" || fail "owner renew rc=${rc}: ${out}"
lt2="$(run "${S}" card-a run-a 1234 show | sed -n 's/^last_touched=//p')"
[ -n "${lt2}" ] && [ "${lt2}" -gt "${lt1}" ] && pass "renew extends last_touched (${lt1} -> ${lt2})" || fail "renew did not touch: ${lt1} ${lt2}"

# 6. TTL expiry: after TTL the lane is free to a second taker (takeover).
sleep "$TTL"
run "${S}" card-a run-a 1234 show | grep -q "state=EXPIRED" && pass "show: EXPIRED after TTL" || fail "not EXPIRED after TTL: $(run "${S}" card-a run-a 1234 show)"
out="$(run "${S}" card-b run-b 5678 take "takeover")"; rc=$?
[ ${rc} -eq 0 ] && pass "takeover after expiry rc=0" || fail "takeover rc=${rc}: ${out}"
echo "${out}" | grep -q "taking over from card-a/run-a" && pass "takeover names the former holder" || fail "takeover message: ${out}"

# 7. the former holder's renew now fails with the documented code + message.
out="$(run "${S}" card-a run-a 1234 renew "too late")"; rc=$?
[ ${rc} -eq 63 ] && pass "former holder renew rc=63 (LEASE_EXPIRED)" || fail "former holder renew rc=${rc}: ${out}"
echo "${out}" | grep -q "EXPIRED" && echo "${out}" | grep -q "re-lease: bash scripts/eqemu/lease take" && \
    pass "expiry message has EXPIRED + re-lease one-liner" || fail "expiry message: ${out}"

# 8. re-acquire by the former holder works (the lane is now card-b's... it
# must be refused; then card-b releases and card-a takes cleanly).
out="$(run "${S}" card-a run-a 1234 take "again")"; rc=$?
[ ${rc} -eq 1 ] && pass "re-take refused while a live other card holds" || fail "re-take rc=${rc}: ${out}"
run "${S}" card-b run-b 5678 release >/dev/null
out="$(run "${S}" card-a run-a 1234 take "again")"; rc=$?
[ ${rc} -eq 0 ] && pass "re-acquire after release rc=0" || fail "re-acquire rc=${rc}: ${out}"

# 9. reap: a dead run's lease is released and its tracked pid killed; a
#    live run's tracked process is untouched; a lease held by ANOTHER run
#    is kept.
S2="${WORK}/s2"; mkdir -p "${S2}"
sleep 20 & LIVEPID=$!
sleep 20 & DEADPID=$!
# A live holder takes, tracks its pid, and releases; a DEAD run then takes
# the same lane (dead pid 99999 recorded) and tracks a planted sleep.
run "${S2}" card-l live-run "${LIVEPID}" take "live" >/dev/null
run "${S2}" card-l live-run "${LIVEPID}" track live-run "${LIVEPID}" live-sleep >/dev/null
run "${S2}" card-l live-run "${LIVEPID}" release >/dev/null
out="$(run "${S2}" card-d dead-run 99999 take "stale")"; rc=$?
[ ${rc} -eq 0 ] && pass "take with a dead pid recorded" || fail "dead-pid take rc=${rc}: ${out}"
run "${S2}" card-d dead-run 99999 track dead-run "${DEADPID}" planted-sleep >/dev/null
# card-e cannot take while card-d's lease is within TTL (no liveness check:
# a live holder renews, a quiet one expires).
out="$(run "${S2}" card-e e-run 4321 take "e")"; rc=$?
[ ${rc} -eq 1 ] || fail "take refused while lease held (rc=${rc}: ${out})"
printf 'dead-run\n' > "${WORK}/dead.txt"
out="$(run "${S2}" card-e e-run 4321 reap "${WORK}/dead.txt" 0)" || fail "reap rc=$? (${out})"
echo "${out}" | grep -q "run=dead-run: 1 tracked pids killed" && pass "reap killed the dead run's planted pid" || fail "reap killed output: ${out}"
echo "${out}" | grep -q "run=dead-run: lease released" && pass "reap released the dead run's lease" || fail "reap release output: ${out}"
kill -0 "${DEADPID}" 2>/dev/null && fail "planted pid survived reap" || pass "planted pid is gone"
kill -0 "${LIVEPID}" 2>/dev/null && pass "live run's process untouched" || fail "live run's process was killed"
# The dead run no longer holds the lease: a fresh taker succeeds.
out="$(run "${S2}" card-e e-run 4321 take "e")"; rc=$?
[ ${rc} -eq 0 ] && pass "lane free after reap (no takeover needed)" || fail "take after reap rc=${rc}: ${out}"
run "${S2}" card-e e-run 4321 release >/dev/null
kill "${LIVEPID}" 2>/dev/null; wait "${LIVEPID}" 2>/dev/null; wait "${DEADPID}" 2>/dev/null

# 10. reap --dry + reap, including per-host pids.
# The core reaches a NAMED host via `ssh user@<host> ...`; hermetic means
# that ssh is a LOGGING SHIM on PATH (the calls are still the core's real
# command lines). fakehost is "reachable" (probe ok, kills applied);
# 192.0.2.1 (TEST-NET) is "unreachable" (probe fails): its pids must be
# reported as NOT killed and its pidfile kept, never fatal.
SSHD="${WORK}/sshbin"
mkdir -p "${SSHD}"
cat > "${SSHD}/ssh" <<'SH'
#!/usr/bin/env bash
# shim: usage ssh [-n] [-o ...] user@<host> <cmd...>
H=""
C=""
SEEN=""
for a in "$@"; do
    case "$a" in
        -*) ;;
        user@*) H="${a#user@}"
                SEEN=1 ;;
        *) if [ -n "${SEEN}" ]; then
               if [ -n "${C}" ]; then C="${C} ${a}"; else C="${a}"; fi
           fi ;;
    esac
done
echo "ssh ${H} ${C}" >> "${SSH_SHIM_LOG:-/dev/null}"
case "${H}|${C}" in
    "192.0.2.1|true") exit 1 ;;
    *) exit 0 ;;
esac
SH
bash -n "${SSHD}/ssh" || { echo "ssh shim syntax error" >&2; exit 1; }
chmod +x "${SSHD}/ssh"

S3="${WORK}/s3"; mkdir -p "${S3}"
sleep 30 & D2=$!
run "${S3}" card-x x-run 77777 take "x" >/dev/null   # dead pid, holds (within TTL)
run "${S3}" card-x x-run 77777 track x-run "${D2}" planted >/dev/null
# Per-host tracking: pids on NAMED hosts go to runs/<run>.<host>.pids.
run "${S3}" card-x x-run 77777 track x-run 424242 "planted-fake" fakehost >/dev/null
run "${S3}" card-x x-run 77777 track x-run 424243 "planted-far" 192.0.2.1 >/dev/null
[ -e "${S3}/runs/x-run.fakehost.pids" ] && pass "track --host wrote runs/<run>.<host>.pids" || fail "per-host pidfile missing"
printf 'x-run\n' > "${WORK}/dead2.txt"
# The env prefix must be EXPORTED: an `X=v out="$(cmd)"` form binds X to the
# variable assignment, not into the substitution's environment.
export PATH="${SSHD}:${PATH}" SSH_SHIM_LOG="${WORK}/ssh.log"
out="$(run "${S3}" card-x x-run 77777 reap "${WORK}/dead2.txt" 1)" || fail "dry reap rc=$? (${out})"
echo "${out}" | grep -q "DRY run=x-run: would release lease" && pass "dry: would-release reported" || fail "dry output: ${out}"
echo "${out}" | grep -q "DRY run=x-run: would kill 1 tracked pids on fakehost" && pass "dry: per-host pids reported" || fail "dry per-host output: ${out}"
echo "${out}" | grep -q "DRY run=x-run: would kill 1 tracked pids on 192.0.2.1" && pass "dry: unreachable host's pids reported too" || fail "dry per-host output: ${out}"
run "${S3}" card-x x-run 77777 show | grep -q "state=HELD" && pass "dry: lease still HELD" || fail "dry released the lease"
kill -0 "${D2}" 2>/dev/null && pass "dry: planted pid alive" || fail "dry killed the planted pid"
[ -s "${WORK}/ssh.log" ] && fail "dry reap must not reach ssh" || pass "dry reap: no ssh at all"
: > "${WORK}/ssh.log"
# The real reap, through the shim: the dead run's lease + local pids are
# gone; the reachable host's pid is killed (the shim records the kill
# command); the unreachable host is reported, not fatal, file kept.
out="$(run "${S3}" card-x x-run 77777 reap "${WORK}/dead2.txt" 0 2>&1)" || fail "reap rc=$? (${out})"
echo "${out}" | grep -q "run=x-run: 1 tracked pids killed on fakehost" && pass "reap: reachable host's pid kill issued" || fail "reap per-host output: ${out}"
grep -q "kill 424242" "${WORK}/ssh.log" && pass "reap: the kill command names exactly the tracked pid" || fail "ssh log: $(cat "${WORK}/ssh.log")"
echo "${out}" | grep -q "192.0.2.1 unreachable -- its tracked pids were NOT killed" && pass "reap: unreachable host reported, not fatal" || fail "reap unreachable output: ${out}"
[ -e "${S3}/runs/x-run.192.0.2.1.pids" ] && pass "reap: unreachable host's pidfile kept" || fail "unreachable host's pidfile deleted"
[ ! -e "${S3}/runs/x-run.pids" ] && pass "reap: local pidfile consumed" || fail "local pidfile survived"
[ ! -e "${S3}/runs/x-run.fakehost.pids" ] && pass "reap: reachable host's pidfile consumed" || fail "reachable host's pidfile survived"
run "${S3}" card-x x-run 77777 release >/dev/null
kill "${D2}" 2>/dev/null; wait "${D2}" 2>/dev/null
# Restore the real PATH for the remaining (ssh-free) cases.
export PATH="${PATH/#"${SSHD}:"}"
unset SSH_SHIM_LOG

# 10b. reap with the RELATIVE dead-runs name, core CWD != store (the exact
#     red-before: the client ships <store>/deadruns and passes `deadruns`;
#     the ssh login cwd is not the store, so cwd-relative resolution 64'd
#     on a file that WAS present -- the B1089 run-4 reclaim failure).
S3B="${WORK}/s3b"; mkdir -p "${S3B}/runs" "${WORK}/elsewhere"
run "${S3B}" card-r relrun 5555 take "relative reap" >/dev/null
printf 'relrun\n' > "${S3B}/deadruns"
# The core runs with its CWD in ${WORK}/elsewhere, NOT the store: a bare
# `deadruns` arg must still resolve against LEASE_STORE.
out="$( cd "${WORK}/elsewhere" && LANE_LEASE_HOST=corebox LANE_LEASE_CARD=card-r LANE_LEASE_RUN=relrun \
       LANE_LEASE_PID=5555 LANE_LEASE_LANENAME=t LANE_LEASE_PURPOSE= \
       LEASE_STORE="${S3B}" LEASE_TTL_S="${TTL}" bash "${CORE}" reap deadruns 0 2>&1 )"
rc=$?
[ ${rc} -eq 0 ] && pass "reap: relative dead-runs name resolves against the store" || fail "reap relative rc=${rc}: ${out}"
echo "${out}" | grep -q "run=relrun: lease released" && pass "reap: the lease was released (file was found)" || fail "reap relative output: ${out}"
[ ! -e "${S3B}/lease" ] && pass "reap: relative-run lane is free" || fail "reap relative: lease survived"

# 11. concurrent takers: exactly one wins (a real race: all five take in
#     the same instant; the core's flock critical section serializes them).
S4="${WORK}/s4"; mkdir -p "${S4}"
for i in 1 2 3 4 5; do
    ( run "${S4}" card-r "${i}" "$$" take "race-${i}" > "${WORK}/race-${i}.out" 2>&1; echo $? > "${WORK}/race-${i}.rc" ) &
done
wait
winners=0
for i in 1 2 3 4 5; do
    rc=$(cat "${WORK}/race-${i}.rc")
    [ "${rc}" -eq 0 ] && winners=$((winners + 1))
done
[ "${winners}" -eq 1 ] && pass "concurrent race: exactly one winner (${winners})" || fail "concurrent race: ${winners} winners"

# 12. corrupt record: take refused, nothing deleted.
S5="${WORK}/s5"; mkdir -p "${S5}"; echo "garbage" > "${S5}/lease"
out="$(run "${S5}" card-c run-c 1111 take "c")"; rc=$?
[ ${rc} -eq 2 ] && pass "corrupt record refused (rc=2)" || fail "corrupt rc=${rc}: ${out}"
[ -f "${S5}/lease" ] && pass "corrupt record not deleted" || fail "corrupt record deleted"
[ -s "${S5}/lease" ] && echo "garbage" | grep -q "$(cat "${S5}/lease")" && pass "corrupt bytes untouched" || fail "corrupt bytes changed"

if [ "${FAIL}" -eq 0 ]; then
    echo "PASS: lease core hermetic self-test (take/refuse/show/owner-release/renew/TTL-expiry/takeover/63+re-lease/re-acquire/reap-live-vs-dead/dry-race/corrupt)"
else
    echo "FAIL: see above" >&2
fi
exit "${FAIL}"
