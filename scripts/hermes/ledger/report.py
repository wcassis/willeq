#!/usr/bin/env python3
"""B1201 (#520): report.py <task_id|B####> [--board willeq] [--top N]

The on-demand per-card consumption report, from the LIVE hermes stores (so it
works for a RUNNING card: the open run's wall clock runs to now). Runs ON
hermes-ext, read-only, python3 stdlib + sqlite3. Same row logic as
build_ledger.py (imported from this directory); see refactor/B1201/README.md.

  python3 -I scripts/hermes/ledger/report.py t_86c81d8e
  python3 -I scripts/hermes/ledger/report.py B1194        # every card whose title names B1194

Prints: one line per run (profile, model, wall, turns, tool calls, tokens,
noninference_s, inference_frac, stop reason), the per-phase split
(inferred phase = model seconds + command seconds, summed over the card's runs,
next to the worker-posted `phase:` signals so a mismatch is visible), and the
N longest commands with their seconds.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_ledger as L  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("card", help="t_xxxxxxxx or B####")
    ap.add_argument("--board", default="willeq")
    ap.add_argument("--top", type=int, default=10)
    a = ap.parse_args(argv)
    kdb = L.ro(L.board_db(a.board))
    if re.match(r"^t_[0-9a-f]+$", a.card):
        tasks = kdb.execute("select id, title, status from tasks where id=?", (a.card,)).fetchall()
    else:
        b = a.card.upper().lstrip("B")
        tasks = [t for t in kdb.execute("select id, title, status from tasks order by created_at")
                 if re.search(r"\bB%s\b" % re.escape(b), t["title"] or "")]
    if not tasks:
        print("REPORT no card matches %s on board %s" % (a.card, a.board))
        return 1
    now = time.time()
    for t in tasks:
        runs = L.load_runs(kdb, task_ids=[t["id"]])
        rows = [L.run_row(kdb, r, {"title": t["title"]}, now) for r in runs]
        print("=" * 100)
        print("%s [%s] %s  (%s)" % (t["id"], t["status"], L.clean(t["title"], 90), L.leg_type(t["title"])))
        if not rows:
            print("  no runs yet")
            continue
        print("  %-5s %-8s %-12s %-17s %7s %5s %5s %11s %9s %7s %6s %s" % (
            "run", "profile", "model", "start", "wall_s", "turns", "tools", "prompt_tok", "compl_tok",
            "nonin_s", "inf_fr", "stop"))
        tw = tn = 0
        phase_m, phase_c, calls = {}, {}, []
        for r in rows:
            print("  %-5s %-8s %-12s %-17s %7s %5s %5s %11s %9s %7s %6s %s" % (
                r["run_id"], r["profile"], (r["model"] or "-")[:12], r["start_utc"][:16], r["wall_s"],
                r["turns"], r["tool_calls"], r["prompt_tokens"], r["completion_tokens"],
                r["noninference_s"], r["inference_frac"] or "-", r["stop_reason"]))
            if r["session_source"] != "none":
                tw += r["wall_s"]
                tn += r["noninference_s"] or 0
            for part in (r["inferred_phases"] or "").split(";"):
                m = re.match(r"([\w-]+)=(\d+)\+(\d+)$", part)
                if m:
                    phase_m[m.group(1)] = phase_m.get(m.group(1), 0) + int(m.group(2))
                    phase_c[m.group(1)] = phase_c.get(m.group(1), 0) + int(m.group(3))
            if r["reported_phases"]:
                print("        reported phases: %s" % r["reported_phases"])
            calls += [(d, ph, txt, r["run_id"]) for d, ph, txt in r.get("_calls", [])]
        print("  TOTAL worker wall %d s (%.2f h), noninference %d s, inference_frac %s" % (
            tw, tw / 3600.0, tn, ("%.3f" % (1 - tn / tw)) if tw else "-"))
        print("  per inferred phase (model s + command s):")
        for k in sorted(set(phase_m) | set(phase_c), key=lambda k: -(phase_m.get(k, 0) + phase_c.get(k, 0))):
            print("    %-9s model %7d  commands %7d" % (k, phase_m.get(k, 0), phase_c.get(k, 0)))
        print("  longest %d commands:" % a.top)
        for d, ph, txt, rid in sorted(calls, key=lambda c: -c[0])[:a.top]:
            print("    %6.0f s  run %-5s %-8s %s" % (d, rid, ph, L.clean(txt, 110)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
