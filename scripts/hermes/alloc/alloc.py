#!/usr/bin/env python3
"""B1204 (#523): alloc.py -- the deterministic resource allocator for the hermes kanban board.

  python3 -I scripts/hermes/alloc/alloc.py              # DRY RUN (default): print the decisions
  python3 -I scripts/hermes/alloc/alloc.py --apply      # perform them (kanban CLI)
  python3 -I scripts/hermes/alloc/alloc.py --no-census  # skip the ssh/live probes (offline view)

Runs on hermes-ext; python3 stdlib + sqlite3 only; no inference anywhere in the loop.
Spec: docs/hermes/allocator.md. Inputs:
  - capacity.yaml (this directory): static capacities, guards, seats, routes, codex gate;
  - the board (kanban.db read-only, `mode=ro`): `ready` cards, cards the allocator itself
    holds (`scheduled` with a reason starting "awaiting "), `running` cards, each card's
    `## Manifest` block (scripts/hermes/manifest/manifest.py), parents' latest summaries
    (B1203 `JOB:` lines) and the running runs' session models (owner_admitted -> state.db);
  - live census over ssh: ThinkPad load1 / runq / nproc / `pgrep -c model_viewer` /
    `pgrep -c eqgame`; lane leases (scripts/eqemu/leases); `pgrep -fc harness_measure` here;
    the codex usage gate (~/kb-bodies/codex_usage.sh).
Decision per candidate card, in priority order (greedy, out of order allowed):
  admit  -- every need + a seat fit and a board slot is free: route, and `unblock` if held
  queue  -- fits, but the board is at max_in_progress: stays `ready` (resources reserved)
  hold   -- something does not fit: `schedule <id> "awaiting <resource>"` (only when not
            already held for that same reason)
Running, blocked (human), todo, triage and done cards are never touched. Every decision is
one TSV row in paths.decisions. HOLD VERB: `schedule`, not `block` -- hermes routes a card
that is blocked twice for the same kind to `triage` (BLOCK_RECURRENCE_LIMIT = 2), which a
5-minute allocator would do to every waiting card on its second pass.
"""
import argparse
import os
import re
import shlex
import sqlite3
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "manifest"))
import manifest as M  # noqa: E402

HERMES_HOME = os.path.expanduser(os.environ.get("HERMES_HOME", "~/.hermes"))
KANBAN = [os.path.join(HERMES_HOME, "hermes-agent/venv/bin/python"),
          os.path.join(HERMES_HOME, "hermes-agent/hermes"), "kanban"]
JOB_RE = re.compile(r"(?m)^\s*JOB:\s*(gate_after|evidence_copy|gate_baseline)\s+(\S+)")
DECISION_COLS = ["ts_utc", "mode", "task_id", "status", "priority", "decision", "route", "reason",
                 "tier", "model_class", "needs", "flag", "action", "applied", "title"]


# ------------------------------------------------------------------ config
def _scalar(v):
    v = v.strip()
    if v.startswith("[") and v.endswith("]"):
        return [_scalar(x) for x in v[1:-1].split(",") if x.strip()]
    if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
        return v[1:-1]
    if v in ("true", "false"):
        return v == "true"
    for cast in (int, float):
        try:
            return cast(v)
        except ValueError:
            pass
    return v


def load_yaml_subset(text):
    """The capacity.yaml subset: 2-space nested maps, scalars, inline lists, # comments."""
    root = {}
    stack = [(-1, root)]
    for raw in text.splitlines():
        line = raw.split(" #")[0].rstrip() if not raw.lstrip().startswith("#") else ""
        if '"' in raw.split(" #")[0] or "'" in raw.split(" #")[0]:
            m = re.match(r'^(\s*)([\w.-]+):\s*(["\'].*?["\'])', raw)
            if m:
                line = raw[:m.end()]
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip())
        key, _, val = line.strip().partition(":")
        while stack and stack[-1][0] >= indent:
            stack.pop()
        parent = stack[-1][1]
        if val.strip() == "":
            parent[key] = {}
            stack.append((indent, parent[key]))
        else:
            parent[key] = _scalar(val)
    return root


def load_config(path=None):
    with open(path or os.path.join(HERE, "capacity.yaml")) as f:
        return load_yaml_subset(f.read())


# ------------------------------------------------------------------ cards
def card_manifest(body, cfg):
    """(manifest, flag) -- an invalid/missing block becomes cfg.unmanifested + flag."""
    m, errs = M.parse(body or "")
    if m is not None and not errs:
        return m, ""
    u = cfg["unmanifested"]
    e = []
    needs = M.parse_needs(str(u.get("needs") or ""), e)
    return ({"leg": "", "tier": u["tier"], "model": u["model"], "needs": needs, "phases": [], "est": {}},
            "NO_MANIFEST" if m is None else "BAD_MANIFEST")


def route_class(m):
    if m["model"] in ("strong", "reasoning"):
        return "strong"
    if m["tier"] == "light" or m["leg"] == "review":
        return "light"
    return "medium"


def need_keys(needs, cfg, taken):
    """Expand a manifest's needs into concrete resource units [(resource, n)];
    lane:any picks the first lane of cfg.lane_any that is not in `taken` (full)."""
    out = []
    for name, v in sorted(needs.items()):
        if name == "lane":
            if v == "any":
                lane = next((l for l in cfg["lane_any"] if ("lane-%s" % l) not in taken), None)
                out.append(("lane-%s" % (lane if lane is not None else cfg["lane_any"][0]), 1))
            else:
                out.append(("lane-%s" % v, 1))
        else:
            out.append((name, int(v)))
    return out


# ------------------------------------------------------------------ the decision core (pure)
def decide(cards, running, census, cfg):
    """cards: candidate dicts {id, status, priority, created_at, title, manifest, flag, assignee,
    job_wait, hold_reason}; running: dicts {id, manifest, endpoint}; census: dict (see
    gather_census). Returns [decision dicts] in evaluation order. No I/O."""
    res = dict(cfg["resources"])
    used = {k: 0 for k in res}
    # --- usage by running cards (declared)
    for r in running:
        for k, n in need_keys(r["manifest"]["needs"], cfg, set()):
            used[k] = used.get(k, 0) + n
    # --- live census (max with declared: a no-agent baseline render is no card)
    used["thinkpad-render"] = max(used.get("thinkpad-render", 0), census.get("model_viewer", 0))
    used["hermes-measure"] = max(used.get("hermes-measure", 0), census.get("measure", 0))
    closed = {}
    g = cfg["guards"]
    if census.get("load1") is None:
        closed["thinkpad-render"] = closed["thinkpad-quiet"] = "thinkpad census unavailable"
    else:
        if census["load1"] > g["render_load1_max"] or census.get("runq_per_core", 0) > g["render_runq_per_core_max"]:
            why = "thinkpad load1 %.1f runq/core %.2f" % (census["load1"], census.get("runq_per_core", 0))
            closed["thinkpad-render"] = closed["thinkpad-quiet"] = why
        if census["load1"] > g["quiet_load1_max"] or census.get("model_viewer", 0) > 0:
            closed.setdefault("thinkpad-quiet", "thinkpad not quiet (load1 %.1f, %d model_viewer)"
                              % (census["load1"], census.get("model_viewer", 0)))
        if census.get("eqgame", 0) > 0:
            for k in g["eqgame_closes"]:
                closed.setdefault(k, "eqgame running on the thinkpad")
    for lane, state in (census.get("leases") or {}).items():
        if state in g["lease_states_closed"]:
            closed.setdefault("lane-%s" % lane, "lease %s" % state)
    # --- seats
    seats = dict(cfg["seats"])
    seat_used = {k: 0 for k in seats}
    for r in running:
        seat_used[r["endpoint"]] = seat_used.get(r["endpoint"], 0) + 1
    board_free = cfg["board"]["max_in_progress"] - len(running)
    codex = census.get("codex") or {}
    cx = cfg["codex"]
    codex_ok, codex_why = codex_gate(codex, cx)
    window_end = (codex_ok and codex.get("resets_in_h") is not None and codex["resets_in_h"] < cx["window_end_h"]
                  and codex.get("used") is not None and codex["used"] < cx["window_end_used_max"])
    out = []
    for c in sorted(cards, key=lambda c: (-int(c["priority"] or 0), c["created_at"], c["id"])):
        m = c["manifest"]
        rc = route_class(m)
        d = {"task_id": c["id"], "status": c["status"], "priority": c["priority"], "tier": m["tier"],
             "model_class": rc, "needs": ",".join("%s:%s" % kv for kv in sorted(m["needs"].items())) or "none",
             "flag": c.get("flag", ""), "title": c["title"], "route": "", "reason": ""}
        if c.get("job_wait"):
            d.update(decision="hold", reason="job:" + c["job_wait"])
            out.append(d)
            continue
        units = need_keys(m["needs"], cfg, {k for k in res if k.startswith("lane-") and
                                             (used.get(k, 0) >= res[k] or k in closed)})
        if "thinkpad-quiet" in m["needs"] and used.get("thinkpad-render", 0) > 0:
            closed.setdefault("thinkpad-quiet", "renders running")
        miss = None
        for k, n in units:
            if k not in res:
                miss = "%s (no such resource)" % k
            elif k in closed:
                miss = "%s (%s)" % (k, closed[k])
            elif used.get(k, 0) + n > res[k]:
                miss = "%s (%d/%d used)" % (k, used.get(k, 0), res[k])
            if miss:
                break
        if miss:
            d.update(decision="hold", reason=miss)
            out.append(d)
            continue
        # --- model route
        prefs = list(cfg["routes"][rc])
        if rc == "medium" and window_end and "codex" not in prefs:
            prefs = ["codex"] + prefs
        route, why_not = None, []
        for ep in prefs:
            if ep == "codex" and not codex_ok:
                why_not.append("codex " + codex_why)
                continue
            if seat_used.get(ep, 0) >= seats.get(ep, 0):
                why_not.append("seat %s %d/%d" % (ep, seat_used.get(ep, 0), seats.get(ep, 0)))
                continue
            route = ep
            break
        if route is None:
            d.update(decision="hold", reason="seat: " + "; ".join(why_not))
            out.append(d)
            continue
        # --- reserve
        for k, n in units:
            used[k] = used.get(k, 0) + n
        if "thinkpad-quiet" in m["needs"]:
            used["thinkpad-render"] = res["thinkpad-render"]      # exclusive
        seat_used[route] = seat_used.get(route, 0) + 1
        d["route"] = route
        d["reason"] = "; ".join(why_not) if why_not else ""
        if route == "codex":
            d["reason"] = (d["reason"] + "; " if d["reason"] else "") + "codex budget %s calls/%s pts%s" % (
                cx["run_budget_calls"], cx["run_budget_points"], " (window-end rule)" if rc == "medium" else "")
        if board_free > 0:
            d["decision"] = "admit"
            board_free -= 1
        else:
            d["decision"] = "queue"
            d["reason"] = (d["reason"] + "; " if d["reason"] else "") + "board at max_in_progress"
        out.append(d)
    return out


def codex_gate(codex, cx):
    """(ok, why) from the parsed codex_usage.sh line."""
    if not cx.get("enabled"):
        return False, "disabled in capacity.yaml"
    if not codex:
        return False, "usage unknown"
    if codex.get("record_age_h") is None or codex["record_age_h"] > cx["record_max_age_h"]:
        return False, "CODEX_USAGE_STALE (record %sh old)" % codex.get("record_age_h")
    if codex.get("verdict") != "GO":
        return False, "verdict %s (used %s%% line %s%%)" % (codex.get("verdict"), codex.get("used"), codex.get("line"))
    return True, "GO"


# ------------------------------------------------------------------ actions
def actions_for(d, card, cfg):
    """The kanban CLI calls one decision needs (list of argv tails)."""
    acts = []
    hold_prefix = cfg["board"]["hold_prefix"]
    if d["decision"] in ("admit", "queue"):
        if d["route"] == "codex" and not str(card.get("assignee") or "").startswith("codex"):
            acts.append(["assign", card["id"], cfg["codex"]["profile"]])
        elif d["route"] != "codex" and card.get("model_override") not in (None, "", d["route"]):
            acts.append(["set-model", card["id"], "none"])
        if card["status"] == "scheduled":
            acts.append(["unblock", card["id"]])
    elif d["decision"] == "hold":
        r = d["reason"]
        key = "seat" if r.startswith("seat:") else r.split(" (")[0]
        want = hold_prefix + key
        if card["status"] == "ready":
            acts.append(["schedule", card["id"], want])
        # an already-held (scheduled) card stays held: hermes refuses schedule on a scheduled
        # card, so a changed reason is visible in the decisions TSV, not on the card
    return acts


# ------------------------------------------------------------------ gather (I/O)
def ro(path):
    con = sqlite3.connect("file:%s?mode=ro" % path, uri=True, timeout=30)
    con.row_factory = sqlite3.Row
    return con


def gather_board(cfg):
    kdb = ro(os.path.join(HERMES_HOME, "kanban", "boards", cfg["board"]["name"], "kanban.db"))
    hold_prefix = cfg["board"]["hold_prefix"]
    jobs_root = cfg["paths"]["jobs_root"]
    cards, running = [], []
    for t in kdb.execute("select * from tasks where status in ('ready','scheduled','running')"):
        m, flag = card_manifest(t["body"], cfg)
        c = {"id": t["id"], "status": t["status"], "priority": t["priority"] or 0,
             "created_at": t["created_at"], "title": (t["title"] or "")[:90], "assignee": t["assignee"],
             "model_override": t["model_override"], "manifest": m, "flag": flag, "hold_reason": ""}
        if t["status"] == "running":
            c["endpoint"] = running_endpoint(kdb, t, cfg)
            running.append(c)
            continue
        if t["status"] == "scheduled":
            ev = kdb.execute("select payload from task_events where task_id=? and kind='scheduled' "
                             "order by id desc limit 1", (t["id"],)).fetchone()
            reason = ""
            if ev and ev["payload"]:
                mm = re.search(r'"reason":\s*"([^"]*)"', ev["payload"])
                reason = mm.group(1) if mm else ""
            if not reason.startswith(hold_prefix):
                continue                 # someone else's schedule: never touched
            c["hold_reason"] = reason
        c["job_wait"] = job_wait(kdb, t["id"], jobs_root)
        cards.append(c)
    return cards, running


def job_wait(kdb, task_id, jobs_root):
    """'<job>:<slug>' when a parent's latest summary names a B1203 job whose done-file is missing."""
    for p in kdb.execute("select parent_id from task_links where child_id=?", (task_id,)):
        r = kdb.execute("select summary from task_runs where task_id=? and ended_at is not null "
                        "order by ended_at desc, id desc limit 1", (p["parent_id"],)).fetchone()
        for job, arg in JOB_RE.findall((r["summary"] if r else "") or ""):
            if job == "gate_baseline":
                done = os.path.join(jobs_root, "gate-baseline", arg[:8] + ".done")
            else:
                done = os.path.join(jobs_root, arg, job + ".done")
            if not os.path.exists(done):
                return "%s:%s" % (job, arg)
    return ""


def running_endpoint(kdb, t, cfg):
    """The endpoint a running card occupies: its live session's model when known, else the
    task's model override, else its profile's default."""
    ev = kdb.execute("select payload from task_events where task_id=? and kind='owner_admitted' "
                     "order by id desc limit 1", (t["id"],)).fetchone()
    model = None
    if ev:
        db = re.search(r'"db":\s*"([^"]+)"', ev["payload"] or "")
        sid = re.search(r'"session_id":\s*"([^"]+)"', ev["payload"] or "")
        if db and sid and os.path.exists(db.group(1)):
            try:
                row = ro(db.group(1)).execute("select model from sessions where id=?", (sid.group(1),)).fetchone()
                model = row["model"] if row else None
            except sqlite3.Error:
                model = None
    model = model or t["model_override"]
    if model in cfg["seats"]:
        return model
    prof = str(t["assignee"] or "")
    return cfg["profiles"].get(prof, "codex" if prof.startswith("codex") or (model or "").startswith("gpt") else "hermes-a")


def sh(cmd, timeout=30):
    try:
        p = subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout
    except (subprocess.TimeoutExpired, OSError) as e:
        return 99, str(e)


def parse_codex_usage(line):
    out = {}
    for k, rx in (("used", r"used=([\d.]+)%"), ("line", r"line=([\d.]+)%"), ("resets_in_h", r"resets_in=([\d.]+)h"),
                  ("record_age_h", r"record_age=([\d.]+)h")):
        mm = re.search(rx, line or "")
        out[k] = float(mm.group(1)) if mm else None
    mm = re.search(r"verdict=(\w+)", line or "")
    out["verdict"] = mm.group(1) if mm else None
    return out


def parse_leases(text):
    out = {}
    for line in (text or "").splitlines():
        mm = re.match(r"^lane(\d)\s+(\w+)", line.strip())
        if mm:
            out[mm.group(1)] = mm.group(2)
    return out


def gather_census(cfg):
    c = {}
    tp = cfg["guards"]["thinkpad_host"]
    rc, out = sh(["ssh", "-n", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", tp,
                  "nproc; cat /proc/loadavg; pgrep -c model_viewer; pgrep -c eqgame"], 20)
    lines = out.split("\n")
    try:
        nproc = int(lines[0])
        la = lines[1].split()
        c["load1"] = float(la[0])
        c["runq_per_core"] = int(la[3].split("/")[0]) / max(nproc, 1)
        c["model_viewer"] = int(lines[2] or 0)
        c["eqgame"] = int(lines[3] or 0)
    except (ValueError, IndexError):
        c["load1"] = None
    rc, out = sh("pgrep -fc '[h]arness_measure'", 10)
    c["measure"] = int(out.strip() or 0) if out.strip().isdigit() else 0
    leases = os.path.join(os.path.dirname(os.path.dirname(HERE)), "eqemu", "leases")
    rc, out = sh(["bash", leases], 60)
    c["leases"] = parse_leases(out)
    rc, out = sh(os.path.expanduser(cfg["codex"]["usage_cmd"]), 30)
    c["codex_line"] = out.strip().splitlines()[-1] if out.strip() else ""
    c["codex"] = parse_codex_usage(c["codex_line"])
    return c


def current_status(cfg, task_id):
    """Re-read one card's status right before acting (kanban.db, read-only)."""
    kdb = ro(os.path.join(HERMES_HOME, "kanban", "boards", cfg["board"]["name"], "kanban.db"))
    row = kdb.execute("select status from tasks where id=?", (task_id,)).fetchone()
    return row["status"] if row else None


def utc(ts=None):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts or time.time()))


def main(argv=None):
    ap = argparse.ArgumentParser(description="B1204 allocator (dry run unless --apply)")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="the default; accepted for clarity")
    ap.add_argument("--no-census", action="store_true", help="skip ssh/live probes (render/quiet closed, codex off)")
    ap.add_argument("--config")
    ap.add_argument("--decisions", help="override paths.decisions")
    a = ap.parse_args(argv)
    cfg = load_config(a.config)
    cards, running = gather_board(cfg)
    census = {"load1": None} if a.no_census else gather_census(cfg)
    decisions = decide(cards, running, census, cfg)
    mode = "apply" if a.apply else "dry-run"
    by_id = {c["id"]: c for c in cards}
    if census.get("codex_line"):
        cx = census["codex"]
        ok, why = codex_gate(cx, cfg["codex"])
        print("CODEX %s -- %s" % ("GO" if ok else "NO", why))
        if why.startswith("CODEX_USAGE_STALE"):
            print("CODEX_USAGE_STALE record_age=%sh > %sh: codex routing off, local fallback"
                  % (cx.get("record_age_h"), cfg["codex"]["record_max_age_h"]))
    print("CENSUS thinkpad load1=%s runq/core=%s model_viewer=%s eqgame=%s measure=%s leases=%s"
          % (census.get("load1"), ("%.2f" % census["runq_per_core"]) if census.get("runq_per_core") is not None else None,
             census.get("model_viewer"), census.get("eqgame"), census.get("measure"),
             ",".join("%s:%s" % kv for kv in sorted((census.get("leases") or {}).items()))))
    print("RUNNING %d: %s" % (len(running), " ".join("%s@%s" % (r["id"], r["endpoint"]) for r in running)))
    rows = []
    for d in decisions:
        acts = actions_for(d, by_id[d["task_id"]], cfg)
        applied = 0
        if a.apply and acts and current_status(cfg, d["task_id"]) != d["status"]:
            acts = []                    # the card moved (e.g. the gateway dispatched it): touch nothing
            d["reason"] = (d["reason"] + "; " if d["reason"] else "") + "status changed before apply"
        if a.apply:
            ok = True
            for act in acts:
                rc, out = sh(KANBAN + ["--board", cfg["board"]["name"]] + act, 60)
                ok = ok and rc == 0
            applied = 1 if ok and acts else 0
            if acts:
                print("APPLIED %s %s rc_ok=%d" % (d["task_id"], " && ".join(shlex.join(x) for x in acts), ok))
        d.update(ts_utc=utc(), mode=mode, action=" && ".join(shlex.join(x) for x in acts) or "-", applied=applied)
        rows.append(d)
        print("%-6s %-10s p%-2s %-9s %-6s %-8s %-7s %-22s %s" % (
            d["decision"], d["task_id"], d["priority"], d["status"], d["tier"], d["model_class"],
            d["route"] or "-", (d["flag"] or "-"), d["reason"] or "-"))
        print("         action: %s" % d["action"])
    path = a.decisions or cfg["paths"]["decisions"]
    try:
        new = not os.path.exists(path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a") as f:
            if new:
                f.write("\t".join(DECISION_COLS) + "\n")
            for d in rows:
                f.write("\t".join(re.sub(r"[\t\n]", " ", str(d.get(k, ""))) for k in DECISION_COLS) + "\n")
    except OSError as e:
        print("DECISIONS not written (%s)" % e)
    print("ALLOC mode=%s candidates=%d admit=%d queue=%d hold=%d -> %s" % (
        mode, len(rows), sum(d["decision"] == "admit" for d in rows), sum(d["decision"] == "queue" for d in rows),
        sum(d["decision"] == "hold" for d in rows), path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
