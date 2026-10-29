#!/usr/bin/env python3
"""B1203 (#522): dispatch_jobs.py [--apply] [--hours 48] -- run the chained no-agent jobs.

The chaining convention (docs/hermes/noagent_jobs.md): a worker leg that ends where a
pure-script phase begins completes with a summary line

    JOB: gate_after <worktree-slug>
    JOB: evidence_copy <worktree-slug> <B####>
    JOB: gate_baseline <ref>

This script (run on hermes-ext as a no-agent cron) reads the board's kanban.db READ-ONLY
(`mode=ro`: `tasks`, the latest `task_runs.summary`, `task_links`) for the `done` cards
completed in the last --hours, and for every JOB line in the latest summary whose
done-file does not exist and that is not running: prints `START <job> <args> (card <id>)`
and, with --apply, starts the job detached (nohup setsid). It also prints, per JOB line,
the chained children (`task_links` parent -> child) and whether they may run:
`CHILD <id> WAITS <job> <slug>` (no done-file yet) or `CHILD <id> READY-AFTER <job>`.
Unblocking children is the allocator's job (B1204): it keeps a child blocked
`awaiting job:<job>:<slug>` until the done-file exists. Default = dry run; read-only on the
board in both modes. python3 stdlib only.
"""
import argparse
import os
import re
import sqlite3
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
JOBS_ROOT = os.environ.get("JOBS_ROOT", "/mnt/scratch/jobs")
HERMES_HOME = os.path.expanduser(os.environ.get("HERMES_HOME", "~/.hermes"))
JOB_RE = re.compile(r"(?m)^\s*JOB:\s*(gate_after|evidence_copy|gate_baseline)\s+(.+?)\s*$")
SCRIPTS = {"gate_after": "gate_after.sh", "evidence_copy": "evidence_copy.sh",
           "gate_baseline": "gate_baseline.sh"}


def board_db(board):
    path = os.path.join(HERMES_HOME, "kanban", "boards", board, "kanban.db")
    con = sqlite3.connect("file:%s?mode=ro" % path, uri=True, timeout=30)
    con.row_factory = sqlite3.Row
    return con


def done_cards(con, since):
    """[(id, completed_at, latest summary, [children])] for done cards completed since `since`."""
    out = []
    for t in con.execute("select id, completed_at from tasks where status='done' and completed_at >= ? "
                         "order by completed_at", (since,)):
        r = con.execute("select summary from task_runs where task_id=? and ended_at is not null "
                        "order by ended_at desc, id desc limit 1", (t["id"],)).fetchone()
        kids = [k["child_id"] for k in con.execute("select child_id from task_links where parent_id=?",
                                                   (t["id"],))]
        out.append((t["id"], t["completed_at"], r["summary"] if r else "", kids))
    return out


def state_paths(job, args):
    """(done-file, running-file) of one job invocation (jobs_lib.sh layout)."""
    if job == "gate_baseline":
        ref = args[0] if args else "origin/fable"
        sha = ref if re.match(r"^[0-9a-f]{8,40}$", ref) else None
        if sha is None:
            return None, None          # a symbolic ref resolves only when the job runs
        d = os.path.join(JOBS_ROOT, "gate-baseline")
        return os.path.join(d, sha[:8] + ".done"), os.path.join(d, sha[:8] + ".running")
    d = os.path.join(JOBS_ROOT, args[0])
    return os.path.join(d, job + ".done"), os.path.join(d, job + ".running")


def parse_jobs(summary):
    jobs = []
    for m in JOB_RE.finditer(summary or ""):
        args = m.group(2).split()
        if m.group(1) == "evidence_copy" and len(args) < 2:
            continue
        jobs.append((m.group(1), args))
    return jobs


def self_test():
    s = "landed at abc: MR !1 pipeline 2 started\nJOB: gate_after kb-b1234-x\n JOB: evidence_copy kb-b1234-x B1234\nJOB: evidence_copy kb-b1234-x\nJOB: gate_baseline 1234abcd5678\nnot a JOB: gate_after y"
    got = parse_jobs(s)
    want = [("gate_after", ["kb-b1234-x"]), ("evidence_copy", ["kb-b1234-x", "B1234"]),
            ("gate_baseline", ["1234abcd5678"])]
    ok = got == want
    d, r = state_paths("gate_after", ["kb-b1234-x"])
    ok &= d == os.path.join(JOBS_ROOT, "kb-b1234-x", "gate_after.done")
    d, r = state_paths("gate_baseline", ["1234abcd5678"])
    ok &= d == os.path.join(JOBS_ROOT, "gate-baseline", "1234abcd.done")
    ok &= state_paths("gate_baseline", ["origin/fable"]) == (None, None)
    print("SELF-TEST %s" % ("PASS" if ok else "FAIL %r" % (got,)))
    return 0 if ok else 1


def main(argv=None):
    if (argv or sys.argv[1:])[:1] == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="start the jobs (default: print only)")
    ap.add_argument("--hours", type=float, default=48.0)
    ap.add_argument("--board", default="willeq")
    a = ap.parse_args(argv)
    now = time.time()
    cards = done_cards(board_db(a.board), now - a.hours * 3600)
    started = 0
    for cid, _, summary, kids in cards:
        for job, args in parse_jobs(summary):
            done, running = state_paths(job, args)
            if done and os.path.exists(done):
                state = "DONE " + open(done).read().strip()
            elif running and os.path.exists(running):
                state = "RUNNING"
            else:
                state = "START"
            print("%s %s %s (card %s)" % (state if state != "START" else "START", job, " ".join(args), cid))
            if state == "START" and a.apply:
                cmd = ["nohup", "setsid", "bash", os.path.join(HERE, SCRIPTS[job])] + args
                subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
                started += 1
            for child in kids:
                tag = "READY-AFTER" if state.startswith("DONE") else "WAITS"
                print("  CHILD %s %s %s %s" % (child, tag, job, " ".join(args)))
    print("DISPATCH cards=%d started=%d mode=%s" % (len(cards), started, "apply" if a.apply else "dry-run"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
