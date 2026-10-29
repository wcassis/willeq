#!/usr/bin/env bash
# B1192 (#511) -- arm b1183_poll.sh on the ThinkPad WITHOUT the ssh channel
# staying open.
#
# Why this script exists: run 7 (2026-10-29) armed the poller with
#   ssh thinkpad '... setsid nohup bash b1183_poll.sh ... < /dev/null &' &
#   sleep 4; kill $A
# The remote command's backgrounded part was a `cd ... && VAR=... setsid
# nohup bash ...` LIST, and bash runs a backgrounded list in a SUBSHELL
# whose stdout stays on the ssh channel pipe for the whole list's lifetime
# (the poller waits ~20 min for the gdb_arm line before it polls). The ssh
# client therefore never returned; run 6 survived only because its ThinkPad
# was idle and the 4 s kill $A race won, run 7 lost the race (the ThinkPad
# was running CI jobs) and kill $A aborted the remote command before mkdir
# -- the poll dir was never created and the LOOT row was unmeasured for a
# third time, with the leg itself completing rc=0.
#
# The fixed pattern: the backgrounded job is a SIMPLE command
# (nohup setsid bash -c '...'), so bash forks+execs it directly with all
# three fds redirected (stdout/err -> poll.arm.log, stdin <- /dev/null);
# no process keeps the channel open and ssh returns in ~1 s, verified on
# the ThinkPad by probe4 (arm rc=0 in 1 s, poller alive, poll/ created).
#
# Usage (from hermes-ext, the card host):
#   arm_poll.sh <owner_prefix> <thinkpad_evidence_dir> <tmax_seconds>
# The poller watches /home/user/eq-oracle/sessions/<owner_prefix>_* for a
# session dir with a gdb_arm END line (b1183_poll.sh:29-39) and writes
# <thinkpad_evidence_dir>/poll/{poll.log,poll.out,poll.started}.
#
# Dry-run / test mode (device-free, run as ctest B1192ArmPoll):
#   arm_poll.sh --selftest
# runs the same remote command string against a LOCAL stub poller instead of
# the ThinkPad, and asserts the three properties run 7 violated:
#   1. the arm command returns promptly (< 10 s), i.e. the channel closes;
#   2. the poll dir + poll.started appear (the measurement channel exists);
#   3. the stub is still running after the arm returns (it survived the
#      session ending, exactly as the real poller must during the ~30-min
#      leg).
# A backgrounded `&&` LIST (the run-7 form) fails property 1: its subshell
# holds the channel pipe for the stub's whole lifetime.
set -u
OWNER="${1-}"
DIR="${2-}"
TMAX="${3-}"

# the remote command body; $1=dir $2=tmax $3=worktree $4=owner $5=poll_sh
# ONLY the nohup job (a SIMPLE command) is backgrounded: bash forks+execs
# it directly with all fds redirected, so no process keeps the ssh channel
# open. The kill/rm/mkdir setup runs in the foreground and is done BEFORE
# `echo ARMED`, so a successful return means the poll dir exists.
arm_remote_cmd() {
    cat <<'REMOTE'
for p in $(pgrep -f "b1183_poll[.]sh"); do kill "$p" 2>/dev/null; done
sleep 1
rm -rf '@@DIR@@'
mkdir -p '@@DIR@@'
nohup setsid bash -c 'cd @@WT@@ && B1192_POLL_OWNER=@@OWNER@@ exec bash @@POLL@@ @@DIR@@ @@TMAX@@' \
  > '@@DIR@@/poll.arm.log' 2>&1 < /dev/null &
echo ARMED
REMOTE
}

if [[ ${1:-} == --selftest ]]; then
    # local stand-in for the ThinkPad: a stub "poller" that creates
    # poll/poll.started (like b1183_poll.sh:27 does) then sleeps a long time
    # (like the real one waiting for the gdb_arm line).
    #
    # The ssh channel-close property is tested over a PIPE: ssh waits for
    # EOF on the channel, and EOF arrives only when no process in the
    # session still holds the channel pipe. `(cmd) | cat` has the identical
    # semantics, device-free: the old (run-7) form's backgrounded `&&` LIST
    # runs in a subshell whose stdout is the pipe -> cat never sees EOF;
    # the fixed form backgrounds a SIMPLE command with stdout redirected to
    # a file -> nothing holds the pipe -> EOF after the wrapper exits.
    T="$(mktemp -d)"
    trap 'rm -rf "$T"' EXIT
    mkdir -p "$T/wt" "$T/evidence"
    cat >"$T/wt/stub_poll.sh" <<'STUB'
#!/usr/bin/env bash
set -u
EV="$1"; TMAX="$2"
mkdir -p "$EV/poll"
touch "$EV/poll/poll.started"
sleep "$TMAX"
STUB
    chmod +x "$T/wt/stub_poll.sh"
    DIRS="$T/evidence/arm"

    cmd="$(arm_remote_cmd)"; cmd="${cmd//@@DIR@@/$DIRS}"
    cmd="${cmd//@@WT@@/$T/wt}"; cmd="${cmd//@@OWNER@@/stubowner}"
    cmd="${cmd//@@POLL@@/stub_poll.sh}"; cmd="${cmd//@@TMAX@@/300}"
    # the @@...@@ delimiters keep the substitutions order-independent (a
    # bare 'POLL' would also hit the B1192_POLL_OWNER= word and corrupt it).
    fail=0
    t0=$(date +%s)
    # property 1: the arm command's channel (pipe) closes promptly. Both
    # sides carry their own timeout: if the pipe never closes, cat's
    # timeout is the one that fires (124).
    { timeout 10 bash -c "$cmd" </dev/null | timeout 12 cat >/dev/null; }
    rc=$?
    t1=$(( $(date +%s) - t0 ))
    if [[ $rc -eq 124 ]]; then
        echo "selftest FAIL: channel held open (timeout) -- the run-7 defect"
        fail=1
    elif [[ $rc -ne 0 ]]; then
        echo "selftest FAIL: arm command rc=$rc"
        fail=1
    else
        echo "selftest OK: channel closed in ${t1}s (rc=0)"
    fi
    # the backgrounded stub only starts as the wrapper's last action; give
    # it a beat to create poll/poll.started before checking (this is a
    # settle delay, not a property).
    sleep 1
    # property 2: the measurement channel exists (poll dir + poll.started)
    [[ -f $DIRS/poll/poll.started ]] || { echo "selftest FAIL: no poll/poll.started"; fail=1; }
    # property 3: the stub survived the session ending (pgrep excludes the
    # 'bash -c' wrappers that contain the pattern in their own cmdline)
    if pgrep -f "stub_poll.sh $DIRS" | grep -qv 'bash -c'; then
        echo "selftest OK: stub alive after arm returned"
    else
        echo "selftest FAIL: stub not running after arm returned"
        fail=1
    fi
    # the old (run-7) form demonstrably holds the channel: its backgrounded
    # `&&` LIST runs in a subshell with stdout on the pipe.
    oldcmd="rm -rf '$DIRS'; mkdir -p '$DIRS'; cd $T/wt && B1192_POLL_OWNER=stubowner setsid nohup bash stub_poll.sh '$DIRS' 300 > '$DIRS/poll.arm.log' 2>&1 < /dev/null & echo ARMED"
    { timeout 8 bash -c "$oldcmd" </dev/null | timeout 10 cat >/dev/null; }
    olt=$?
    if [[ $olt -eq 124 ]]; then
        echo "selftest OK: old (run-7) form held the channel (timeout, as in run 7)"
    else
        echo "selftest NOTE: old form returned rc=$olt (host too fast to reproduce)"
    fi
    # cleanup the stubs
    for p in $(pgrep -f "stub_poll.sh $DIRS" 2>/dev/null | while read -r l; do [[ $l == *"bash -c"* ]] || echo "$l"; done); do
        kill "$p" 2>/dev/null || true
    done
    [[ $fail -eq 0 ]] || exit 1
    echo "selftest PASS"
    exit 0
fi

[[ -n $OWNER && -n $DIR && -n $TMAX ]] || { echo "usage: arm_poll.sh <owner> <dir> <tmax> | --selftest" >&2; exit 2; }

WT=/home/user/src/willeq-wt/kb-b1192-loot-row
POLL=refactor/B1183/scripts/b1183_poll.sh
cmd="$(arm_remote_cmd)"; cmd="${cmd//@@DIR@@/$DIR}"
cmd="${cmd//@@WT@@/$WT}"; cmd="${cmd//@@OWNER@@/$OWNER}"
cmd="${cmd//@@POLL@@/$POLL}"; cmd="${cmd//@@TMAX@@/$TMAX}"

# kill any stale poller first (the bracket pattern avoids self-matching the
# ssh wrapper's own command line), then arm on a fresh dir.
ssh -T -o BatchMode=yes -o ConnectTimeout=15 thinkpad "$cmd" </dev/null
rc=$?
[[ $rc -eq 0 ]] || { echo "arm_poll: ssh arm rc=$rc" >&2; exit "$rc"; }

# verify the poller is actually running before the leg starts (a dead poller
# is exactly what run 7 missed: nothing checks, the leg completes, and the
# measurement is absent at rsync time).
sleep 3
ssh -T -o BatchMode=yes thinkpad \
  "pgrep -af 'b1183_poll[.]sh' | grep -v 'bash -c' || { echo 'arm_poll: NO POLLER RUNNING' >&2; exit 1; }" \
  || exit 1
echo "arm_poll: armed owner=$OWNER dir=$DIR tmax=$TMAX"
