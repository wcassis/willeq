#!/usr/bin/env python3
"""B973 (GitLab #291): fold a post-load crowd run's client.log into numbers.

Reads the client's own lines (no other source):
  Script: executing '...'      -- the tour's phases (first/last #spawn, /move, /q)
  EntityBuild: <id> (<name>) A -> B in <us>us [(upload=..us/..tex create=..us tex_loads=..)]
  [entity-build] ...           -- the 1 s window (governor frames, units, backlog)
  [entity-build-frame] ...     -- per frame (WILLEQ_ENTITY_BUILD_TRACE=1)

usage: build_table.py client.log [client.log ...]   (one row per log, markdown)
       build_table.py --units client.log            (per-unit cost table)
"""
import re
import statistics
import sys
from datetime import datetime

TS = re.compile(r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]")
EXEC = re.compile(r"Script: executing '([^']*)'")
BUILD = re.compile(r"EntityBuild: (\d+) \(([^)]*)\) (\w+) -> (\w+) in (\d+)us"
                   r"(?: \(upload=(\d+)us/(\d+)tex create=(\d+)us tex_loads=(\d+) did=(\d)\))?")
WIN = re.compile(r"\[entity-build\] (.*)")
FRAME = re.compile(r"\[entity-build-frame\] (.*)")
KV = re.compile(r"(\w+)=([^\s]+)")
COUNTS = re.compile(r"\(upload=(\d+) create=(\d+) finalize=(\d+) equip=(\d+)")
KSUMS = re.compile(r"kind_ms\(sum/max\) upload=([\d.]+)/([\d.]+) create=([\d.]+)/([\d.]+) "
                   r"finalize=([\d.]+)/([\d.]+) equip=([\d.]+)/([\d.]+)")
IMPWIN = re.compile(r"\[impostor-frames\] frame=\d+ n=(\d+) frame_p50_ms=([\d.]+) frame_p95_ms=([\d.]+)")


def ts(line):
    m = TS.search(line)
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp() if m else None


def pct(v, p):
    if not v:
        return float("nan")
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p / 100.0 * (len(v) - 1))))]


def parse(path):
    r = {"spawn0": None, "spawn1": None, "move": None, "quit": None, "builds": [],
         "windows": [], "frames": [], "impwin": [], "crowd_seen": set(), "crowd_built": {}}
    for line in open(path, errors="replace"):
        t = ts(line)
        m = EXEC.search(line)
        if m:
            cmd = m.group(1)
            if "#spawn" in cmd:
                r["spawn0"] = r["spawn0"] or t
                r["spawn1"] = t
            elif cmd.startswith("/move") and r["move"] is None:
                r["move"] = t
            elif cmd == "/q":
                r["quit"] = t
            continue
        m = BUILD.search(line)
        if m:
            sid, name, a, b, us = m.group(1), m.group(2), m.group(3), m.group(4), int(m.group(5))
            rec = {"t": t, "id": sid, "name": name, "from": a, "to": b, "ms": us / 1000.0}
            if m.group(6) is not None:
                rec.update(upload_ms=int(m.group(6)) / 1000.0, tex=int(m.group(7)),
                           create_ms=int(m.group(8)) / 1000.0, tex_loads=int(m.group(9)),
                           did=m.group(10) == "1")
            r["builds"].append(rec)
            if name.startswith("Crowd"):
                r["crowd_seen"].add(sid)
                if b == "Built":
                    r["crowd_built"].setdefault(sid, t)
            continue
        m = FRAME.search(line)
        if m:
            d = dict(KV.findall(m.group(1)))
            d["t"] = t
            r["frames"].append(d)
            continue
        m = IMPWIN.search(line)
        if m:
            r["impwin"].append({"t": t, "n": int(m.group(1)), "p50": float(m.group(2)),
                                "p95": float(m.group(3))})
            continue
        m = WIN.search(line)
        if m:
            body = m.group(1)
            d = dict(KV.findall(body))
            for k in ("upload", "create", "finalize", "equip"):
                d.pop(k, None)
            c = COUNTS.search(body)
            if c:
                d.update(upload=c.group(1), create=c.group(2), finalize=c.group(3), equip=c.group(4))
            s = KSUMS.search(body)
            if s:
                for i, k in enumerate(("upload", "create", "finalize", "equip")):
                    d["k_" + k] = "%s/%s" % (s.group(1 + 2 * i), s.group(2 + 2 * i))
            d["t"] = t
            r["windows"].append(d)
    return r


def summarize(path):
    r = parse(path)
    s0, s1, mv = r["spawn0"], r["spawn1"], r["move"]
    out = {"log": path}
    mode = r["windows"][0].get("mode", "?") if r["windows"] else "?"
    out["mode"] = mode
    built_times = sorted(r["crowd_built"].values())
    out["crowd_built_total"] = len(built_times)
    out["crowd_built_by_move"] = sum(1 for t in built_times if mv is None or t <= mv)
    # visible backlog drained: the first window after the last spawn with
    # nothing visible left to build and nothing in progress
    drained = None
    for w in r["windows"]:
        if s1 and w["t"] >= s1 and w.get("unbuilt_visible") == "0" and w.get("in_progress") == "0":
            drained = w["t"]
            break
    out["drain_after_last_spawn_s"] = (drained - s1) if (drained and s1) else None
    out["last_crowd_built_after_last_spawn_s"] = (
        (max(t for t in built_times if mv is None or t <= mv) - s1)
        if (s1 and out["crowd_built_by_move"]) else None)
    out["first_crowd_built_after_first_spawn_s"] = (built_times[0] - s0) if (built_times and s0) else None
    # frames from first spawn to /move (spawn + build phases)
    fr = [f for f in r["frames"] if s0 and f["t"] >= s0 and (mv is None or f["t"] <= mv)]
    work = [float(f["work_ms"]) for f in fr if int(f["units"]) > 0]
    fms_w = [float(f["frame_ms"]) for f in fr if int(f["units"]) > 0]
    fms_n = [float(f["frame_ms"]) for f in fr if int(f["units"]) == 0]
    states = [f["gov"] for f in fr]
    out["trace_frames_spawn_to_move"] = len(fr)
    out["trace_frames_R_Y_G"] = "%d/%d/%d" % (states.count("R"), states.count("Y"), states.count("G"))
    out["frames_with_units"] = len(work)
    out["work_ms_p50_p95_max"] = "%.2f / %.2f / %.2f" % (pct(work, 50), pct(work, 95), max(work) if work else float("nan"))
    out["frame_ms_with_units_p50_p95"] = "%.1f / %.1f" % (pct(fms_w, 50), pct(fms_w, 95))
    out["frame_ms_without_units_p50_p95"] = "%.1f / %.1f" % (pct(fms_n, 50), pct(fms_n, 95))
    # after the walk: hold phase frame times (everything built)
    hold = [float(f["frame_ms"]) for f in r["frames"] if mv and f["t"] > mv]
    out["frame_ms_after_move_p50_p95"] = "%.1f / %.1f" % (pct(hold, 50), pct(hold, 95))
    # Every frame, pending work or not: the impostor pass's 5 s windows
    # ([impostor-frames], B964), median of the window p50s per tour phase.
    phases = [("spawn", s0, s1), ("build", s1, mv), ("walk", mv, mv + 30 if mv else None),
              ("hold", mv + 30 if mv else None, r["quit"])]
    for name, a, b in phases:
        p50s = [w["p50"] for w in r["impwin"] if a and b and a < w["t"] <= b and w["n"] > 0]
        p95s = [w["p95"] for w in r["impwin"] if a and b and a < w["t"] <= b and w["n"] > 0]
        out["frame_ms_%s_p50(max_p95)" % name] = (
            "%.1f (%.1f)" % (statistics.median(p50s), max(p95s)) if p50s else "-")
    # unit kinds over the run, from the window lines
    for kind in ("upload", "create", "finalize", "equip"):
        n = sum(int(w.get(kind, 0)) for w in r["windows"] if w.get(kind, "").isdigit())
        sums = [float(w["k_" + kind].split("/")[0]) for w in r["windows"] if "k_" + kind in w]
        maxs = [float(w["k_" + kind].split("/")[1]) for w in r["windows"] if "k_" + kind in w]
        out["%s_units(avg/max ms)" % kind] = (
            "%d (%.2f/%.2f)" % (n, sum(sums) / n if n else 0.0, max(maxs) if maxs else 0.0))
    return out, r


def unit_table(r):
    kinds = {}
    for b in r["builds"]:
        key = "%s -> %s" % (b["from"], b["to"])
        kinds.setdefault(key, []).append(b)
    print("| unit (phase -> phase) | n | ms p50 | p90 | max | upload ms p50 / max | create ms p50 / max | tex_loads p50 / max |")
    print("|---|---|---|---|---|---|---|---|")
    for key, v in sorted(kinds.items(), key=lambda kv: -len(kv[1])):
        ms = [b["ms"] for b in v]
        up = [b["upload_ms"] for b in v if "upload_ms" in b]
        cr = [b["create_ms"] for b in v if "create_ms" in b]
        tl = [b["tex_loads"] for b in v if "tex_loads" in b]
        fmt2 = lambda a: "%.2f / %.2f" % (pct(a, 50), max(a)) if a else "-"
        print("| %s | %d | %.2f | %.2f | %.2f | %s | %s | %s |" % (
            key, len(v), pct(ms, 50), pct(ms, 90), max(ms), fmt2(up), fmt2(cr),
            ("%d / %d" % (pct(tl, 50), max(tl))) if tl else "-"))


def main():
    args = sys.argv[1:]
    if args and args[0] == "--units":
        for p in args[1:]:
            print("###", p)
            unit_table(parse(p))
        return
    rows = [summarize(p)[0] for p in args]
    if not rows:
        print(__doc__)
        return
    # one column per log, one row per metric
    keys = list(rows[0].keys())
    print("| metric | " + " | ".join("run %d" % (i + 1) for i in range(len(rows))) + " |")
    print("|---|" + "---|" * len(rows))
    for k in keys:
        cells = []
        for row in rows:
            v = row.get(k, "-")
            cells.append("%.1f" % v if isinstance(v, float) else str(v))
        print("| %s | %s |" % (k, " | ".join(cells)))


if __name__ == "__main__":
    main()
