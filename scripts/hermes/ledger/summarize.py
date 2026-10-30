#!/usr/bin/env python3
"""B1201 (#520): summarize.py <runs.tsv> <cards.tsv> <out_dir>

Turns the ledger (build_ledger.py output) into the committed summary tables
(refactor/B1201/tables/). Pure function of its two inputs; python3 stdlib only;
runs anywhere. Population = WORKER runs (a session was found) that have ENDED;
the zero-length orchestrator closures (no session) and still-running runs are
counted in headline.tsv but excluded from every median.

  python3 -I scripts/hermes/ledger/summarize.py refactor/B1201/tables/runs.tsv \
      refactor/B1201/tables/cards.tsv refactor/B1201/tables

Writes by_leg_type.tsv, by_profile_model.tsv, by_phase.tsv, by_card.tsv,
headline.tsv. Percentiles are nearest-rank (p50 = median of the sorted list,
p90 = value at ceil(0.9 n)).
"""
import csv
import math
import os
import re
import statistics
import sys

METRICS = ["turns", "tool_calls", "prompt_tokens", "completion_tokens", "wall_s", "model_s",
           "noninference_s", "inference_frac"]


def read(path):
    with open(path) as f:
        return list(csv.DictReader(f, delimiter="\t"))


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def p90(xs):
    xs = sorted(xs)
    return xs[max(math.ceil(0.9 * len(xs)) - 1, 0)] if xs else None


def fmt(v):
    if v is None:
        return ""
    if isinstance(v, float) and not v.is_integer():
        return "%.3f" % v if abs(v) < 10 else "%.0f" % v
    return "%d" % v


def group_stats(rows):
    out = {"n": len(rows)}
    for m in METRICS:
        xs = [num(r[m]) for r in rows if num(r[m]) is not None]
        out["med_" + m] = statistics.median(xs) if xs else None
        out["p90_" + m] = p90(xs)
    wall = sum(num(r["wall_s"]) or 0 for r in rows)
    wait = sum(num(r["noninference_s"]) or 0 for r in rows)
    out["seat_h"] = wall / 3600
    out["wait_seat_h"] = wait / 3600
    out["pooled_inference_frac"] = (1 - wait / wall) if wall else None
    for k in ("wall_timeout", "iter_budget", "crashed", "blocked", "completed"):
        out["n_" + k] = sum(r["stop_reason"] == k for r in rows)
    out["timeout_rate"] = out["n_wall_timeout"] / len(rows) if rows else None
    out["iter_budget_rate"] = out["n_iter_budget"] / len(rows) if rows else None
    return out


def write(path, cols, rows):
    with open(path, "w") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(fmt(r.get(c)) if not isinstance(r.get(c), str) else r.get(c)
                              for c in cols) + "\n")


def main(argv):
    runs_p, cards_p, out = argv[1:4]
    runs = read(runs_p)
    cards = read(cards_p)
    os.makedirs(out, exist_ok=True)
    worker = [r for r in runs if r["session_source"] != "none" and r["end_utc"]]
    stat_cols = (["n"] + [p + m for m in METRICS for p in ("med_", "p90_")] +
                 ["seat_h", "wait_seat_h", "pooled_inference_frac", "n_completed", "n_blocked",
                  "n_crashed", "n_wall_timeout", "n_iter_budget", "timeout_rate", "iter_budget_rate"])
    # per leg type
    rows = []
    for lt in sorted({r["leg_type"] for r in worker}) + ["ALL"]:
        g = [r for r in worker if lt == "ALL" or r["leg_type"] == lt]
        s = group_stats(g)
        s["leg_type"] = lt
        rows.append(s)
    write(os.path.join(out, "by_leg_type.tsv"), ["leg_type"] + stat_cols, rows)
    # per profile x model
    rows = []
    for key in sorted({(r["profile"], r["model"]) for r in worker}):
        s = group_stats([r for r in worker if (r["profile"], r["model"]) == key])
        s["profile"], s["model"] = key
        rows.append(s)
    write(os.path.join(out, "by_profile_model.tsv"), ["profile", "model"] + stat_cols, rows)
    # per inferred phase x leg type: model seconds + command seconds
    acc = {}
    for r in worker:
        for part in (r["inferred_phases"] or "").split(";"):
            m = re.match(r"([\w-]+)=(\d+)\+(\d+)$", part)
            if not m:
                continue
            for lt in (r["leg_type"], "ALL"):
                a = acc.setdefault((lt, m.group(1)), [0, 0])
                a[0] += int(m.group(2))
                a[1] += int(m.group(3))
    rows = [{"leg_type": lt, "phase": ph, "model_s": v[0], "command_s": v[1],
             "model_h": v[0] / 3600, "command_h": v[1] / 3600}
            for (lt, ph), v in sorted(acc.items(), key=lambda kv: (kv[0][0], -(kv[1][0] + kv[1][1])))]
    write(os.path.join(out, "by_phase.tsv"), ["leg_type", "phase", "model_s", "command_s", "model_h",
                                               "command_h"], rows)
    # per card (the cards.tsv roll-up, minus free text that is not a number)
    keep = ["task_id", "b_number", "issue", "leg_type", "status", "n_runs", "n_worker_runs",
            "n_timeouts", "n_iter_budget", "n_crashed", "wall_s", "turns", "tool_calls",
            "prompt_tokens", "completion_tokens", "noninference_s", "inference_frac", "longest_cmd_s",
            "models", "codex_record_age_h"]
    write(os.path.join(out, "by_card.tsv"), keep, cards)
    # headline
    head = [
        {"metric": "runs_in_window", "value": len(runs)},
        {"metric": "worker_runs_ended", "value": len(worker)},
        {"metric": "runs_still_running", "value": sum(1 for r in runs if not r["end_utc"])},
        {"metric": "runs_without_session", "value": sum(r["session_source"] == "none" for r in runs)},
        {"metric": "session_join_admitted", "value": sum(r["session_source"] == "admitted" for r in runs)},
        {"metric": "session_join_matched", "value": sum(r["session_source"] == "matched" for r in runs)},
        {"metric": "session_join_rollout", "value": sum(r["session_source"] == "rollout" for r in runs)},
        {"metric": "cards", "value": len(cards)},
        {"metric": "first_run_start_utc", "value": min(r["start_utc"] for r in runs)},
        {"metric": "last_run_start_utc", "value": max(r["start_utc"] for r in runs)},
        {"metric": "longest_single_command_s", "value": max(num(r["longest_cmd_s"]) or 0 for r in worker)},
        {"metric": "commands_over_60s", "value": sum(int(num(r["n_long_cmds"]) or 0) for r in worker)},
        {"metric": "runs_with_phase_comments", "value": sum((num(r["phase_comments"]) or 0) > 0 for r in runs)},
    ]
    a = group_stats(worker)
    head += [{"metric": "seat_hours_worker_runs", "value": a["seat_h"]},
             {"metric": "seat_hours_inside_commands_over_60s", "value": a["wait_seat_h"]},
             {"metric": "seat_hours_model_time", "value": sum(num(r["model_s"]) or 0 for r in worker) / 3600}]
    write(os.path.join(out, "headline.tsv"), ["metric", "value"],
          [{"metric": h["metric"], "value": h["value"] if isinstance(h["value"], str) else fmt(float(h["value"]))}
           for h in head])
    print("SUMMARY worker_runs=%d legs=%s -> %s" % (len(worker), ",".join(sorted({r['leg_type'] for r in worker})), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
