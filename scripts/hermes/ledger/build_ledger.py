#!/usr/bin/env python3
"""B1201 (#520): the INFERENCE LEDGER -- one row per hermes kanban RUN.

Runs ON hermes-ext (it reads the live hermes stores; python3 stdlib + sqlite3 only):

  python3 -I scripts/hermes/ledger/build_ledger.py --since 2026-10-23
      -> /mnt/scratch/ledger/runs.tsv  (one row per kanban run)
         /mnt/scratch/ledger/cards.tsv (one row per card: the roll-up of its runs)

Read-only on every store: each SQLite file is opened with `mode=ro`
(URI), nothing is written except the two output TSVs, which are rewritten
whole (temp file + rename) on every invocation, so the script is idempotent.

Sources (column documentation: refactor/B1201/README.md):
  board   ~/.hermes/kanban/boards/<board>/kanban.db
          tasks (title, status), task_runs (one row per run: profile, start/end,
          status, outcome, error), task_events (owner_admitted -> the run's
          session id + state.db path; timed_out; heartbeat notes),
          task_comments (worker-posted `phase: <name>` signals)
  session ~/.hermes/profiles/<profile>/state.db
          sessions (model, provider, token totals), session_model_usage
          (main vs auxiliary compression/vision calls), messages (every
          assistant turn and tool result with its timestamp: the command log)
  codex   ~/.hermes/profiles/codex*/codex-runtime/sessions/**/rollout-*.jsonl and
          ~/.codex/sessions/**/rollout-*.jsonl
          (codex-* profiles: turns, tool calls, tokens and the command log)

Join keys: task_runs.id == task_events.run_id (kind owner_admitted, payload
{db, session_id}); runs older than the owner_admitted event (2026-10-24 23:52Z)
fall back to the kanban-source session of the run's profile whose started_at is
within [run start - 60 s, run start + 600 s] and whose title starts with the
card title's first 30 characters. codex rollouts are matched by the
"work kanban task <id>" user message and an overlapping time window.
"""
import argparse
import datetime as dt
import glob
import json
import os
import re
import sqlite3
import sys
import time

HERMES = os.path.expanduser(os.environ.get("HERMES_HOME", "~/.hermes"))
CODEX_HOME = os.path.expanduser(os.environ.get("CODEX_HOME", "~/.codex"))
LONG_S = 60.0  # a command longer than this is non-inference wait (issue #520 ask 1)
PHASES = ("trace", "code", "build", "render", "measure", "leg", "doc", "review")
PHASE_RE = re.compile(r"(?im)^\s*phase:\s*(%s)\b" % "|".join(PHASES))

RUN_COLS = [
    "run_id", "task_id", "b_number", "issue", "leg_type", "profile", "model", "provider",
    "base_url", "session_id", "session_source", "start_utc", "end_utc", "wall_s", "status",
    "outcome", "stop_reason", "turns", "tool_calls", "api_calls", "aux_calls",
    "prompt_tokens", "completion_tokens", "cached_tokens", "reasoning_tokens",
    "model_s", "tool_s", "noninference_s", "inference_frac", "n_long_cmds",
    "longest_cmd_s", "longest_cmd", "phase_comments", "reported_phases",
    "inferred_phases", "title",
]
CARD_COLS = [
    "task_id", "b_number", "issue", "leg_type", "status", "n_runs", "n_worker_runs",
    "n_timeouts", "n_iter_budget", "n_crashed", "first_start_utc", "last_end_utc",
    "wall_s", "turns", "tool_calls", "prompt_tokens", "completion_tokens",
    "noninference_s", "inference_frac", "longest_cmd_s", "models", "codex_record_age_h", "title",
]


# ------------------------------------------------------------------ helpers
def ro(path):
    """Open a SQLite file read-only (never creates, never writes)."""
    con = sqlite3.connect("file:%s?mode=ro" % path, uri=True, timeout=30)
    con.row_factory = sqlite3.Row
    return con


def utc(ts):
    if ts is None:
        return ""
    return dt.datetime.fromtimestamp(ts, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def clean(s, n=160):
    s = re.sub(r"\s+", " ", str(s or "")).strip()
    return s[:n]


def parse_since(s):
    return dt.datetime.strptime(s, "%Y-%m-%d").replace(tzinfo=dt.timezone.utc).timestamp()


def leg_type(title):
    """Leg type from the card title (first rule that matches wins)."""
    t = title or ""
    if re.match(r"\s*Re-review", t, re.I):
        return "re-review"
    if re.match(r"\s*Review", t, re.I):
        return "review"
    if re.search(r"\bCONTINUATION\b", t):
        return "continuation"
    if re.search(r"\bFIX\b|MERGE-FIX", t):
        return "fix"
    if re.search(r"TRACE|report[- ]only|\bdiagnose\b", t, re.I):
        return "diagnose"
    return "dev"


def b_and_issue(title):
    b = re.search(r"\bB(\d{3,4})\b", title or "")
    i = re.search(r"\(#(\d+)", title or "") or re.search(r"#(\d+)", title or "")
    return ("B" + b.group(1)) if b else "", i.group(1) if i else ""


# ------------------------------------------------------------ phase inference
CMD_RULES = [
    ("render", r"render_gate|model_viewer|ffmpeg|--spell-test|kb-frames"),
    ("build", r"\brbuild\b|\bcmake\b|\bninja\b|\bctest\b|build-.*\.sh"),
    ("leg", r"run_leg|oracle_class_leg|EQEMU_LANE|eqemu_lock|scenario\.sh|oracle_"),
    ("measure", r"harness_measure|build_tables|measure|cv2|numpy"),
    ("ci-wait", r"pipelines|/jobs/|glab (mr|api)"),
    ("copy", r"\brsync\b|\bscp\b"),
    ("wait", r"\bsleep\b|\bwait\b|until |while .*pgrep|tail -f"),
    ("trace", r"ghidra|eqgame\.(dis|exe\.c)|EQGraphicsDX9|\.dis\b"),
    ("code", r"git (commit|diff|add|push)|\bpatch\b"),
]


def cmd_phase(tool, args):
    """Inferred phase of one tool call from its name and arguments (documented
    in refactor/B1201/README.md; 'other' when nothing matches)."""
    tool = tool or ""
    if tool in ("terminal", "process_manage", "execute_code", "Bash", "exec", "shell",
                "local_shell_call", "exec_command", "tool_call"):
        text = args if isinstance(args, str) else json.dumps(args)
        for name, rx in CMD_RULES:
            if re.search(rx, text):
                return name
        return "shell"
    path = ""
    if isinstance(args, dict):
        path = str(args.get("path") or args.get("file_path") or args.get("pattern") or "")
    if tool in ("patch", "write_file", "write", "apply_patch"):
        return "doc" if path.lower().endswith((".md", ".txt", ".tsv")) else "code"
    if tool in ("read_file", "search_files"):
        return "trace" if re.search(r"ghidra|\.dis$|eqgame|EQGraphics", path) else "read"
    if tool.startswith("kanban"):
        return "board"
    if tool.startswith("vision"):
        return "vision"
    return "other"


def cmd_text(tool, args):
    if isinstance(args, dict):
        for k in ("command", "cmd", "input", "code", "action", "path"):
            if args.get(k):
                text = re.sub(r"^\s*cd \S+\s*(&&|;)\s*", "", str(args[k]))
                return "%s: %s" % (tool, text)
    return "%s: %s" % (tool, args if isinstance(args, str) else json.dumps(args))


def loads(s):
    try:
        return json.loads(s)
    except Exception:
        return s or ""


# -------------------------------------------------------- hermes session log
def hermes_session(db_path, sid):
    """Command log + totals of one hermes session (profile state.db)."""
    con = ro(db_path)
    s = con.execute("select * from sessions where id=?", (sid,)).fetchone()
    if s is None:
        con.close()
        return None
    out = {
        "model": s["model"] or "", "provider": s["billing_provider"] or "",
        "base_url": s["billing_base_url"] or "",
        "prompt_tokens": s["input_tokens"] or 0, "completion_tokens": s["output_tokens"] or 0,
        "cached_tokens": s["cache_read_tokens"] or 0, "reasoning_tokens": s["reasoning_tokens"] or 0,
        "api_calls": s["api_call_count"] or 0, "aux_calls": 0,
    }
    for u in con.execute("select task, billing_base_url, api_call_count from session_model_usage "
                         "where session_id=?", (sid,)):
        if u["task"]:
            out["aux_calls"] += u["api_call_count"] or 0
        elif not out["base_url"]:
            out["base_url"] = u["billing_base_url"] or ""
    msgs = con.execute(
        "select id, role, tool_name, tool_calls, tool_call_id, timestamp, _compressed_summary "
        "from messages where session_id=? order by id", (sid,)).fetchall()
    con.close()
    # events: (ts, kind, payload) -- kind 'asst' with its calls, or 'tool'
    events = []
    seen = set()
    # Context compression re-inserts copies of kept messages with their original
    # timestamps (a 1213-row session had 988 `compacted` rows): one event per
    # (role, timestamp, tool_call_id), ordered by timestamp, not by row id.
    for m in sorted(msgs, key=lambda m: (m["timestamp"], m["id"])):
        if m["_compressed_summary"]:
            continue
        key = (m["role"], round(m["timestamp"], 3), m["tool_call_id"] or "")
        if key in seen:
            continue
        seen.add(key)
        if m["role"] == "assistant":
            calls = []
            tc = loads(m["tool_calls"]) if m["tool_calls"] else []
            for c in tc if isinstance(tc, list) else []:
                fn = (c or {}).get("function") or {}
                calls.append((c.get("call_id") or c.get("id"), fn.get("name") or "",
                              loads(fn.get("arguments") or "")))
            events.append((m["timestamp"], "asst", calls))
        elif m["role"] == "tool":
            events.append((m["timestamp"], "tool", (m["tool_call_id"], m["tool_name"])))
        else:
            events.append((m["timestamp"], m["role"], None))
    out["events"] = events
    return out


def timeline_from_events(events):
    """Model seconds, per-call durations and inferred phases from the ordered
    message log. A call's duration = its result's timestamp minus the later of
    the issuing assistant message and the previous result of the same batch;
    model seconds = the gap before each assistant message."""
    calls = []          # (dur_s, phase, text)
    model_s = 0.0
    turns = 0
    phase_model = {}
    prev_ts = None
    pending = {}        # call_id -> (name, args, phase), in issue order
    batch_prev = None
    for ts, kind, payload in events:
        if kind == "asst":
            turns += 1
            gap = max((ts - prev_ts) if prev_ts is not None else 0.0, 0.0)
            model_s += gap
            phs = [cmd_phase(n, a) for (_, n, a) in payload] or ["answer"]
            for p in phs:
                phase_model[p] = phase_model.get(p, 0.0) + gap / len(phs)
            pending = {}
            for i, ((cid, n, a), p) in enumerate(zip(payload, phs)):
                pending[cid or "#%d" % i] = (n, a, p)
            batch_prev = ts
        elif kind == "tool":
            cid, tname = payload
            if cid in pending:
                info = pending.pop(cid)
            elif pending:
                info = pending.pop(next(iter(pending)))
            else:
                info = (tname or "", "", cmd_phase(tname, ""))
            name, args, ph = info
            start = batch_prev if batch_prev is not None else (prev_ts if prev_ts is not None else ts)
            calls.append((max(ts - start, 0.0), ph, cmd_text(name, args)))
            batch_prev = ts
        prev_ts = ts
    return turns, model_s, calls, phase_model


# -------------------------------------------------------------- codex rollouts
def rollout_files():
    """Every codex rollout: kanban codex workers write under the profiles'
    codex-runtime; one-shots and non-profile runs under ~/.codex/sessions."""
    pats = [os.path.join(HERMES, "profiles", "codex*", "codex-runtime", "sessions", "*", "*", "*",
                         "rollout-*.jsonl"),
            os.path.join(CODEX_HOME, "sessions", "*", "*", "*", "rollout-*.jsonl")]
    return [f for p in pats for f in glob.glob(p)]


def codex_rollouts(task_id, t0, t1):
    """codex rollouts that worked this card inside [t0, t1]."""
    hits = []
    day0 = dt.datetime.fromtimestamp(t0 - 86400, dt.timezone.utc).strftime("%Y-%m-%d")
    day1 = dt.datetime.fromtimestamp(t1 + 86400, dt.timezone.utc).strftime("%Y-%m-%d")
    for f in rollout_files():
        m = re.search(r"rollout-(\d{4}-\d{2}-\d{2})T(\d{2})-(\d{2})-(\d{2})", f)
        if not m or not (day0 <= m.group(1) <= day1):
            continue
        fstart = dt.datetime.strptime("%s %s:%s:%s" % m.groups(), "%Y-%m-%d %H:%M:%S").replace(
            tzinfo=dt.timezone.utc).timestamp()
        if not (t0 - 300 <= fstart <= t1):
            continue
        with open(f, errors="ignore") as fh:
            head = fh.read(200000)
        if ("work kanban task %s" % task_id) in head:
            hits.append(f)
    return sorted(hits)


def iso_ts(s):
    return dt.datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


def codex_session(path, t0, t1):
    out = {"model": "", "provider": "openai-codex", "base_url": "", "prompt_tokens": 0,
           "completion_tokens": 0, "cached_tokens": 0, "reasoning_tokens": 0,
           "api_calls": 0, "aux_calls": 0}
    events = []
    started = {}
    for line in open(path, errors="ignore"):
        try:
            d = json.loads(line)
        except Exception:
            continue
        ts = iso_ts(d.get("timestamp")) if d.get("timestamp") else None
        p = d.get("payload") or {}
        typ = d.get("type")
        pt = p.get("type") if isinstance(p, dict) else None
        if typ == "world_state":
            mode = ((p.get("state") or {}).get("collaboration_mode") or {})
            out["model"] = mode.get("model") or out["model"]
        if ts is None or ts < t0 - 300 or ts > t1 + 5:
            continue
        if typ == "token_usage_record":
            out["api_calls"] += 1
            events.append((ts, "asst", []))
        elif typ == "event_msg" and pt == "token_count":
            tot = ((p.get("info") or {}).get("total_token_usage") or {})
            if tot:
                out["prompt_tokens"] = tot.get("input_tokens", 0)
                out["cached_tokens"] = tot.get("cached_input_tokens", 0)
                out["completion_tokens"] = tot.get("output_tokens", 0)
                out["reasoning_tokens"] = tot.get("reasoning_output_tokens", 0)
        elif typ == "response_item" and pt in ("function_call", "custom_tool_call", "local_shell_call"):
            args = p.get("arguments") or p.get("input") or p.get("action") or ""
            started[p.get("call_id")] = (ts, p.get("name") or pt, loads(args) if isinstance(args, str) else args)
        elif typ == "response_item" and pt in ("function_call_output", "custom_tool_call_output",
                                              "local_shell_call_output"):
            st = started.pop(p.get("call_id"), None)
            if st:
                events.append((st[0], "call", (ts - st[0], st[1], st[2])))
    out["events"] = sorted(events, key=lambda e: e[0])
    return out


def codex_timeline(events):
    turns = 0
    calls = []
    phase_model = {}
    for ts, kind, payload in events:
        if kind == "asst":
            turns += 1
        else:
            dur, name, args = payload
            calls.append((max(dur, 0.0), cmd_phase(name, args), cmd_text(name, args)))
    # codex records no per-turn model latency; model_s = wall - tool time (set by caller)
    return turns, None, calls, phase_model


# ------------------------------------------------------------------- the run
def find_session(kdb, run, title):
    """(state.db path, session id, how) for one run."""
    ev = kdb.execute("select payload from task_events where run_id=? and kind='owner_admitted' "
                     "order by id desc limit 1", (run["id"],)).fetchone()
    if ev:
        p = loads(ev["payload"])
        if isinstance(p, dict) and p.get("session_id") and p.get("db"):
            return p["db"], p["session_id"], "admitted"
    prof = run["profile"] or ""
    db = os.path.join(HERMES, "profiles", prof, "state.db")
    if not prof or not os.path.exists(db):
        return None, None, "none"
    con = ro(db)
    rows = con.execute(
        "select id, title, started_at from sessions where source='kanban' and started_at between ? and ? "
        "order by abs(started_at - ?)", (run["started_at"] - 60, run["started_at"] + 600,
                                         run["started_at"])).fetchall()
    con.close()
    key = (title or "")[:30]
    for r in rows:
        if (r["title"] or "").startswith(key):
            return db, r["id"], "matched"
    return None, None, "none"


def stop_reason(run, kdb):
    oc = run["outcome"] or ("running" if run["status"] == "running" else run["status"])
    err = run["error"] or ""
    if oc == "timed_out":
        if err.startswith("Iteration budget"):
            return "iter_budget"
        ev = kdb.execute("select payload from task_events where run_id=? and kind='timed_out'",
                         (run["id"],)).fetchone()
        return "wall_timeout" if ev else "timed_out"
    return oc


def phase_signals(kdb, task_id, t0, t1):
    """Worker-posted `phase: <name>` lines (comments + heartbeat notes) in [t0, t1]."""
    sig = []
    for c in kdb.execute("select body, created_at from task_comments where task_id=? and created_at "
                         "between ? and ?", (task_id, t0, t1)):
        for m in PHASE_RE.finditer(c["body"] or ""):
            sig.append((c["created_at"], m.group(1).lower()))
    for e in kdb.execute("select payload, created_at from task_events where task_id=? and kind='heartbeat' "
                         "and created_at between ? and ?", (task_id, t0, t1)):
        p = loads(e["payload"])
        note = p.get("note") if isinstance(p, dict) else ""
        for m in PHASE_RE.finditer(note or ""):
            sig.append((e["created_at"], m.group(1).lower()))
    sig.sort()
    return sig


def fmt_phases(model, cmds):
    keys = sorted(set(model) | set(cmds), key=lambda k: -(model.get(k, 0) + cmds.get(k, 0)))
    return ";".join("%s=%d+%d" % (k, round(model.get(k, 0)), round(cmds.get(k, 0))) for k in keys)


def run_row(kdb, run, task, now=None):
    now = now or time.time()
    title = task["title"]
    b, issue = b_and_issue(title)
    t0 = run["started_at"]
    t1 = run["ended_at"] or now
    wall = max(t1 - t0, 0)
    row = {k: "" for k in RUN_COLS}
    row.update(run_id=run["id"], task_id=run["task_id"], b_number=b, issue=issue,
               leg_type=leg_type(title), profile=run["profile"] or "", start_utc=utc(t0),
               end_utc=utc(run["ended_at"]), wall_s=round(wall), status=run["status"],
               outcome=run["outcome"] or "", stop_reason=stop_reason(run, kdb), title=clean(title, 120))
    sess = None
    how = "none"
    if (run["profile"] or "").startswith("codex"):
        rolls = codex_rollouts(run["task_id"], t0, t1)
        if rolls:
            sess = codex_session(rolls[-1], t0, t1)
            row["session_id"] = os.path.basename(rolls[-1])
            how = "rollout"
            turns, model_s, calls, phase_model = codex_timeline(sess["events"])
    if sess is None:
        db, sid, how = find_session(kdb, run, title)
        if sid:
            sess = hermes_session(db, sid)
            row["session_id"] = sid
            if sess:
                # only what happened inside this run's window counts toward it
                ev = [e for e in sess["events"] if t0 - 5 <= e[0] <= t1 + 5]
                turns, model_s, calls, phase_model = timeline_from_events(ev)
    row["session_source"] = how if sess else "none"
    sig = phase_signals(kdb, run["task_id"], t0, t1)
    row["phase_comments"] = len(sig)
    row["reported_phases"] = ">".join("%s@%s" % (p, utc(ts)[11:16]) for ts, p in sig)
    if not sess:
        for k in ("turns", "tool_calls", "model_s", "tool_s", "noninference_s", "n_long_cmds",
                  "longest_cmd_s"):
            row[k] = 0
        row["inference_frac"] = ""
        return row
    tool_s = sum(c[0] for c in calls)
    if model_s is None:
        model_s = max(wall - tool_s, 0)
    long_calls = [c for c in calls if c[0] > LONG_S]
    nonin = sum(c[0] for c in long_calls)
    longest = max(calls, key=lambda c: c[0]) if calls else (0, "", "")
    cmds_by_phase = {}
    for d, ph, _ in calls:
        cmds_by_phase[ph] = cmds_by_phase.get(ph, 0) + d
    row.update(
        model=sess["model"], provider=sess["provider"], base_url=sess["base_url"], turns=turns,
        tool_calls=len(calls), api_calls=sess["api_calls"], aux_calls=sess["aux_calls"],
        prompt_tokens=sess["prompt_tokens"], completion_tokens=sess["completion_tokens"],
        cached_tokens=sess["cached_tokens"], reasoning_tokens=sess["reasoning_tokens"],
        model_s=round(model_s), tool_s=round(tool_s), noninference_s=round(nonin),
        inference_frac=("%.3f" % (1 - nonin / wall)) if wall > 0 else "",
        n_long_cmds=len(long_calls), longest_cmd_s=round(longest[0]), longest_cmd=clean(longest[2]),
        inferred_phases=fmt_phases(phase_model, cmds_by_phase))
    row["_calls"] = calls
    return row


def load_runs(kdb, since=None, task_ids=None):
    q = ("select r.*, t.title, t.status task_status from task_runs r join tasks t on t.id=r.task_id")
    cond, args = [], []
    if since is not None:
        cond.append("r.started_at >= ?")
        args.append(since)
    if task_ids:
        cond.append("r.task_id in (%s)" % ",".join("?" * len(task_ids)))
        args.extend(task_ids)
    if cond:
        q += " where " + " and ".join(cond)
    return kdb.execute(q + " order by r.id", args).fetchall()


def codex_record_age_h(now=None):
    """Age in hours of the codex rate-limit record codex_usage.sh reads (the newest
    rollout's mtime); '' when there is no rollout."""
    files = rollout_files()
    if not files:
        return ""
    return "%.1f" % (((now or time.time()) - max(os.path.getmtime(f) for f in files)) / 3600)


def card_rows(rows, kdb, now=None):
    cards = {}
    age = None
    for r in rows:
        c = cards.setdefault(r["task_id"], [])
        c.append(r)
    out = []
    for tid, rs in cards.items():
        t = kdb.execute("select title, status from tasks where id=?", (tid,)).fetchone()
        worker = [r for r in rs if r["session_source"] != "none"]
        wall = sum(r["wall_s"] for r in worker)
        nonin = sum(r["noninference_s"] or 0 for r in worker)
        b, issue = b_and_issue(t["title"])
        is_codex = any((r["profile"] or "").startswith("codex") for r in rs)
        if is_codex and age is None:
            age = codex_record_age_h(now)
        out.append({
            "codex_record_age_h": age if is_codex else "",
            "task_id": tid, "b_number": b, "issue": issue, "leg_type": leg_type(t["title"]),
            "status": t["status"], "n_runs": len(rs), "n_worker_runs": len(worker),
            "n_timeouts": sum(r["stop_reason"] in ("wall_timeout", "timed_out", "iter_budget") for r in rs),
            "n_iter_budget": sum(r["stop_reason"] == "iter_budget" for r in rs),
            "n_crashed": sum(r["stop_reason"] == "crashed" for r in rs),
            "first_start_utc": min(r["start_utc"] for r in rs),
            "last_end_utc": max(r["end_utc"] or "" for r in rs),
            "wall_s": wall, "turns": sum(r["turns"] or 0 for r in rs),
            "tool_calls": sum(r["tool_calls"] or 0 for r in rs),
            "prompt_tokens": sum(int(r["prompt_tokens"] or 0) for r in rs),
            "completion_tokens": sum(int(r["completion_tokens"] or 0) for r in rs),
            "noninference_s": nonin,
            "inference_frac": ("%.3f" % (1 - nonin / wall)) if wall else "",
            "longest_cmd_s": max([r["longest_cmd_s"] or 0 for r in rs] or [0]),
            "models": ",".join(sorted({r["model"] for r in worker if r["model"]})),
            "title": clean(t["title"], 120),
        })
    out.sort(key=lambda c: c["first_start_utc"])
    return out


def write_tsv(path, cols, rows):
    tmp = path + ".tmp.%d" % os.getpid()
    with open(tmp, "w") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(clean(r.get(c, ""), 400) for c in cols) + "\n")
    os.replace(tmp, path)


def board_db(board):
    return os.path.join(HERMES, "kanban", "boards", board, "kanban.db")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--since", default="2026-10-23", help="UTC date; runs started on/after it")
    ap.add_argument("--board", default="willeq")
    ap.add_argument("--out", default="/mnt/scratch/ledger")
    a = ap.parse_args(argv)
    kdb = ro(board_db(a.board))
    now = time.time()
    rows = []
    for run in load_runs(kdb, since=parse_since(a.since)):
        task = {"title": run["title"]}
        rows.append(run_row(kdb, run, task, now))
    cards = card_rows(rows, kdb, now)
    os.makedirs(a.out, exist_ok=True)
    write_tsv(os.path.join(a.out, "runs.tsv"), RUN_COLS, rows)
    write_tsv(os.path.join(a.out, "cards.tsv"), CARD_COLS, cards)
    print("LEDGER board=%s since=%s runs=%d cards=%d with_session=%d -> %s/runs.tsv %s/cards.tsv"
          % (a.board, a.since, len(rows), len(cards),
             sum(r["session_source"] != "none" for r in rows), a.out, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
