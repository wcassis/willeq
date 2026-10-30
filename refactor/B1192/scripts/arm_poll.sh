#!/usr/bin/env bash
# B1192 (#511) -- arm b1183_poll.sh on the ThinkPad so the LOOT row's oracle
# +0x251 measurement channel actually exists.
#
# WHY THIS SCRIPT EXISTS -- two distinct arm defects, both measured on lane 5:
#
# DEFECT 1 (the one that actually left the LOOT row unmeasured, run 7 and
# run 8): the arm command carried BOTH a stale-poller kill loop AND the
# poller launch, in one remote command:
#   ssh thinkpad 'for p in $(pgrep -f "b1183_poll[.]sh"); do kill "$p"; done;
#                 ...; setsid nohup bash refactor/B1183/scripts/b1183_poll.sh
#                 <dir> <tmax> > ... 2>&1 < /dev/null &'
# The remote shell runs as `bash -c '<the whole command>'`, so its OWN
# command line contains the real path `refactor/B1183/scripts/b1183_poll.sh`
# (plain dot) from the nohup line. The kill loop's pattern `b1183_poll[.]sh`
# is a regex that matches the text `b1183_poll.sh` -- which is sitting in the
# shell's own command line. pgrep therefore returns the shell's own PID and
# `kill` SIGHUPs the shell mid-script: it dies BEFORE mkdir, so the poll dir
# is never created and the poller never arms. The ssh client then reports
# exit-signal = rc=255. Proven by the X2 probe (rc=255, only the marker
# written BEFORE the kill loop survives) and by run 7 / run 8 (rc=255, no
# poll dir). The bracket trick in the pattern only stops matching the PATTERN
# TEXT (`b1183_poll[.]sh`); it does NOT stop matching the real path that
# appears elsewhere in the same command line.
#
# DEFECT 2 (secondary; why a foreground ssh arm hangs, probes 2/3):
# backgrounding a `cd X && VAR=... setsid nohup bash ...` LIST runs that list
# in a SUBSHELL whose stdout stays on the ssh channel pipe for the list's
# whole lifetime (the poller waits ~20 min for the gdb_arm line), so the ssh
# client never sees EOF and hangs. A SIMPLE backgrounded command
# (nohup setsid bash -c '...' > log 2>&1 < /dev/null &) is fork+exec'd
# directly with all three fds redirected, so nothing holds the pipe and ssh
# returns in ~1 s (probe 4: rc=0 in 1 s, poller alive, poll/ created).
#
# THE FIX -- two separate ssh calls:
#   1. KILL call: `ssh thinkpad '<kill loop>'` whose command line contains
#      ONLY the bracketed pattern `b1183_poll[.]sh` (no real path), so its
#      own regex cannot match its own command line (no self-kill). It does
#      kill any real stale poller (`bash .../b1183_poll.sh DIR TMAX`).
#   2. ARM call: `ssh thinkpad '<rm; mkdir; nohup setsid bash -c "...real
#      path..." > log 2>&1 < /dev/null & echo ARMED>'` -- a SIMPLE
#      backgrounded command (Defect 2 fixed), with NO kill loop (Defect 1
#      fixed: nothing in this command can kill its own shell).
#   3. VERIFY call: confirm the poller is actually running before returning;
#      a dead poller is a hard fail here, not a silent absent measurement at
#      rsync time (the run-7 failure mode).
#
# Usage (from hermes-ext, the card host):
#   arm_poll.sh <owner_prefix> <thinkpad_evidence_dir> <tmax_seconds>
# The poller watches /home/user/eq-oracle/sessions/<owner_prefix>_* for a
# session dir with a gdb_arm END line (b1183_poll.sh:29-39) and writes
# <thinkpad_evidence_dir>/poll/{poll.log,poll.out,poll.started}.
#
# Test (device-free, ctest B1192ArmPoll):
#   arm_poll.sh --selftest
# asserts, with no ThinkPad: (a) the KILL command's own text contains no
# `b1183_poll.sh` (plain dot) -- so it cannot self-match -- while the old
# combined form does contain it (the Defect-1 reproduction); (b) the ARM
# command's channel closes promptly over a pipe (the Defect-2 property, the
# local analog of the ssh channel: EOF only when no process holds the pipe);
# (c) the arm actually starts a stub poller and it survives the session
# ending, leaving poll/poll.started.
set -u
OWNER="${1-}"
DIR="${2-}"
TMAX="${3-}"

# The KILL command: command line contains ONLY the bracketed pattern, so its
# own regex (b1183_poll[.]sh, matching the text b1183_poll.sh) cannot match
# its own command line. It runs on a FRESH ssh call, separate from the arm.
kill_remote_cmd() {
    cat <<'REMOTE'
for p in $(pgrep -f "b1183_poll[.]sh"); do kill "$p" 2>/dev/null; done
sleep 1
REMOTE
}

# The ARM command: a SIMPLE backgrounded command (all fds redirected, so no
# channel hold) with NO kill loop (so no self-match). Substitutions are
# @@...@@-delimited so they are order-independent (a bare 'POLL' would also
# hit the B1192_POLL_OWNER= word and corrupt it).
arm_remote_cmd() {
    cat <<'REMOTE'
rm -rf '@@DIR@@'
mkdir -p '@@DIR@@'
nohup setsid bash -c 'cd @@WT@@ && B1192_POLL_OWNER=@@OWNER@@ exec bash @@POLL@@ @@DIR@@ @@TMAX@@' \
  > '@@DIR@@/poll.arm.log' 2>&1 < /dev/null &
echo ARMED
REMOTE
}

sub_cmd() { # $1=cmd $2=dir $3=wt $4=owner $5=poll $6=tmax
    local c="$1"
    c="${c//@@DIR@@/$2}"; c="${c//@@WT@@/$3}"
    c="${c//@@OWNER@@/$4}"; c="${c//@@POLL@@/$5}"; c="${c//@@TMAX@@/$6}"
    printf '%s' "$c"
}

if [[ ${1:-} == --selftest ]]; then
    T="$(mktemp -d)"
    trap 'rm -rf "$T"' EXIT
    mkdir -p "$T/wt" "$T/evidence"
    # stub "poller": creates poll/poll.started (like b1183_poll.sh:27) then
    # sleeps (like the real one waiting for the gdb_arm line).
    cat >"$T/wt/b1183_poll.sh" <<'STUB'
#!/usr/bin/env bash
set -u
EV="$1"; TMAX="$2"
mkdir -p "$EV/poll"
touch "$EV/poll/poll.started"
sleep "$TMAX"
STUB
    chmod +x "$T/wt/b1183_poll.sh"
    DIRS="$T/evidence/arm"
    POLL="$T/wt/b1183_poll.sh"
    fail=0

    # (a) Defect-1 property: the KILL command's own text must NOT contain the
    # real-path form `b1183_poll.sh` (plain dot) that its regex matches; the
    # old combined form (kill loop + real path in one command) DOES contain
    # it -- the self-kill the kill loop performs on run 7's shell.
    if [[ "$(kill_remote_cmd)" == *b1183_poll\.sh* ]]; then
        echo "selftest FAIL: kill cmd's text contains 'b1183_poll.sh' (would self-kill)"
        fail=1
    else
        echo "selftest OK: kill cmd cannot self-match"
    fi
    old_combined="$(kill_remote_cmd)nohup setsid bash -c 'exec bash $POLL $DIRS 300' &"
    if [[ "$old_combined" == *b1183_poll\.sh* ]]; then
        echo "selftest OK: old combined form carries the real path (Defect 1 reproducible)"
    else
        echo "selftest NOTE: old combined form unexpectedly clean"
    fi

    # (b)+(c) Defect-2 + arming: run the ARM command over a pipe (the local
    # analog of the ssh channel: EOF only when nothing holds the pipe). The
    # stub is a SIMPLE backgrounded command with all fds redirected, so the
    # pipe closes promptly and the stub survives.
    cmd="$(sub_cmd "$(arm_remote_cmd)" "$DIRS" "$T/wt" stubowner "$POLL" 300)"
    t0=$(date +%s)
    { timeout 10 bash -c "$cmd" </dev/null | timeout 12 cat >/dev/null; }
    rc=$?
    t1=$(( $(date +%s) - t0 ))
    if [[ $rc -eq 124 ]]; then
        echo "selftest FAIL: arm channel held open (timeout) -- Defect 2"
        fail=1
    elif [[ $rc -ne 0 ]]; then
        echo "selftest FAIL: arm command rc=$rc"
        fail=1
    else
        echo "selftest OK: arm channel closed in ${t1}s (rc=0)"
    fi
    sleep 1  # settle: the stub starts as the wrapper's last action
    [[ -f $DIRS/poll/poll.started ]] || { echo "selftest FAIL: no poll/poll.started"; fail=1; }
    if pgrep -f "b1183_poll.sh $DIRS" | grep -qv 'bash -c'; then
        echo "selftest OK: stub alive after arm returned"
    else
        echo "selftest FAIL: stub not running after arm returned"
        fail=1
    fi
    # cleanup the stub
    for p in $(pgrep -f "b1183_poll.sh $DIRS" 2>/dev/null | while read -r l; do [[ $l == *"bash -c"* ]] || echo "$l"; done); do
        kill "$p" 2>/dev/null || true
    done
    [[ $fail -eq 0 ]] || exit 1
    echo "selftest PASS"
    exit 0
fi

[[ -n $OWNER && -n $DIR && -n $TMAX ]] || { echo "usage: arm_poll.sh <owner> <dir> <tmax> | --selftest" >&2; exit 2; }

WT=/home/user/src/willeq-wt/kb-b1192-loot-row
POLL=refactor/B1183/scripts/b1183_poll.sh

# 1. kill stale pollers (own call: command line = pattern only, no self-kill)
ssh -T -o BatchMode=yes -o ConnectTimeout=15 thinkpad "$(kill_remote_cmd)" </dev/null \
    || { echo "arm_poll: kill call rc=$?" >&2; exit 1; }

# 2. arm (simple backgrounded command; no kill loop; channel closes promptly)
ssh -T -o BatchMode=yes -o ConnectTimeout=15 thinkpad \
    "$(sub_cmd "$(arm_remote_cmd)" "$DIR" "$WT" "$OWNER" "$POLL" "$TMAX")" </dev/null
rc=$?
[[ $rc -eq 0 ]] || { echo "arm_poll: ssh arm rc=$rc" >&2; exit "$rc"; }

# 3. verify the poller is actually running before the leg starts.
sleep 3
ssh -T -o BatchMode=yes thinkpad \
    "pgrep -af 'b1183_poll[.]sh' | grep -v 'bash -c' || { echo 'arm_poll: NO POLLER RUNNING' >&2; exit 1; }" \
    || exit 1
echo "arm_poll: armed owner=$OWNER dir=$DIR tmax=$TMAX"
