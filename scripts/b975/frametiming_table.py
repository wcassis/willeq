#!/usr/bin/env python3
"""B975 (GitLab #293): the name-tag columns of a crowd run.

Reads <run>/zones/<zone>/client.log (the Pi sweep, scripts/b964/pi_crowd.sh
with --frametiming) or <run>/<zone>/<variant>/client.log and prints one
markdown row per run from the client's own lines:

  - the `/frametiming` breakdown (`=== FRAME TIMING BREAKDOWN (60 frames, X fps
    estimate) ===` blocks): median over the blocks of Total Frame, Scene Draw
    All, Solid Pass, Transparent Pass and the fps estimate -- only the blocks
    in the tour's idle phase (between the `/face` and the first `/move`
    script lines), after rendered readiness in the FINAL visit. Missing
    boundaries/readiness produce no samples, never a whole-run fallback;
  - the `[nametags]` windows (the batch's own 5 s line; absent on the per-node
    path): median tags drawn, CPU us per frame and per tag, draws, quads,
    buffer bytes;
  - the `ent.nametag_batch` owner row at steady_60s.

usage: frametiming_table.py RUN_DIR [RUN_DIR...]
"""
import argparse
import json
import os
import re
import statistics
import sys

KV = re.compile(r"([a-z_0-9]+)=([^ ]+)")
FT_HEAD = re.compile(r"FRAME TIMING BREAKDOWN \(\d+ frames, ([\d.]+) fps estimate\)")
FT_ROW = re.compile(r"\]\s+(Total Frame|Scene Draw All|Solid Pass|Transparent Pass):\s+(\d+) us")
TS = re.compile(r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?)\]")


def logs_of(run):
    for root, _dirs, files in os.walk(run):
        if "client.log" in files:
            yield os.path.join(root, "client.log")


def label_of(path):
    parts = path.split(os.sep)
    if "zones" in parts:
        i = parts.index("zones")
        return f"{parts[i + 1]} / {parts[i - 1]}"
    return " / ".join(parts[-3:-1])


def med(xs):
    return statistics.median(xs) if xs else None


def fmt(x, nd=1):
    if x is None:
        return "--"
    return f"{x:.{nd}f}" if isinstance(x, float) else str(x)


def parse(path, zone=None):
    with open(path, errors="replace") as log:
        lines = log.read().splitlines()
    # Never search backwards for a successful earlier visit (even same zone).
    visit = max((i for i, ln in enumerate(lines)
                 if "[memledger] point=game_zone_begin " in ln), default=0)
    ready = steady = None
    ready_zone = None
    for i in range(visit, len(lines)):
        ln = lines[i]
        if "[memledger] point=ready " in ln and ready is None:
            ready, ready_zone = i, dict(KV.findall(ln)).get("zone")
        if ("[memledger] point=steady_60s " in ln and ready is not None
                and i > ready and dict(KV.findall(ln)).get("zone") == ready_zone
                and steady is None):
            steady = i
    if zone is not None and ready_zone != zone:
        ready = steady = None
    # Hop tours face repeatedly: idle starts at the LAST face before movement.
    idle_start = idle_end = None
    for i in range(visit, len(lines)):
        ln = lines[i]
        if "Script: executing" in ln or "executing script" in ln.lower():
            if "/face" in ln and idle_end is None:
                idle_start = i
            elif "/move" in ln and idle_start is not None and idle_end is None:
                idle_end = i
    boundary = max(idle_start, ready) if idle_start is not None and ready is not None else None
    def in_idle(start, end):
        return (boundary is not None and idle_end is not None
                and boundary < start <= end < idle_end)

    blocks, cur, previous = [], None, -1
    for i, ln in enumerate(lines):
        m = FT_HEAD.search(ln)
        if m:
            cur = {"line": i, "start": previous, "end": i, "fps": float(m.group(1))}
            previous = i
            blocks.append(cur)
            continue
        if cur is not None:
            r = FT_ROW.search(ln)
            if r and r.group(1) not in cur:
                cur[r.group(1)] = int(r.group(2)) / 1000.0
                cur["end"] = i
    # A timing header reports the PRECEDING frames, not an instantaneous sample.
    # Discard the first boundary-crossing block; the previous header proves
    # that every frame in subsequent blocks starts after ready AND final face.
    blocks = [b for b in blocks if in_idle(b["start"], b["end"])
              and all(k in b for k in ("Total Frame", "Scene Draw All", "Solid Pass", "Transparent Pass"))]
    nt, previous = [], -1
    for i, ln in enumerate(lines):
        if "[nametags]" in ln:
            if in_idle(previous, i):
                nt.append(dict(KV.findall(ln.split("[nametags]", 1)[1])))
            previous = i
    row_bytes = None
    for i, ln in enumerate(lines):
        if "[memledger-owner]" in ln and "point=steady_60s" in ln and "owner=ent.nametag_batch" in ln:
            if steady is not None and i > steady:
                row_bytes = int(dict(KV.findall(ln))["live"])
    def col(name):
        return med([b[name] for b in blocks if name in b])
    def ntcol(name, conv=float):
        return med([conv(d[name]) for d in nt if name in d])
    return {
        "blocks": len(blocks),
        "fps": med([b["fps"] for b in blocks]),
        "total": col("Total Frame"),
        "draw_all": col("Scene Draw All"),
        "solid": col("Solid Pass"),
        "transparent": col("Transparent Pass"),
        "tags": ntcol("tags_avg"),
        "cpu_us": ntcol("cpu_us_p50"),
        "us_per_tag": ntcol("us_per_tag_p50"),
        "draws": ntcol("draws", int),
        "quads": ntcol("quads", int),
        "buffer": ntcol("buffer_bytes", int),
        "row": row_bytes,
        "idle": idle_start is not None and idle_end is not None,
        "ready": ready is not None,
        "steady": steady is not None,
        "ready_zone": ready_zone,
        "visit_line": visit + 1,
        "ready_line": ready + 1 if ready is not None else None,
        "steady_line": steady + 1 if steady is not None else None,
        "idle_start_line": idle_start + 1 if idle_start is not None else None,
        "idle_end_line": idle_end + 1 if idle_end is not None else None,
        "block_lines": [b["line"] + 1 for b in blocks],
    }


def main(argv):
    if "--check" in argv:
        ap = argparse.ArgumentParser(description="Fail closed on missing final-visit crowd observation")
        ap.add_argument("--check", action="store_true")
        ap.add_argument("--zone", required=True)
        ap.add_argument("--require-timing", action="store_true")
        ap.add_argument("log")
        a = ap.parse_args(argv[1:])
        try:
            row = parse(a.log, a.zone)
        except OSError as exc:
            print(json.dumps({"pass": False, "error": str(exc)}))
            return 1
        row["pass"] = row["ready"] and row["steady"] and (not a.require_timing or row["blocks"] > 0)
        print(json.dumps(row, indent=2))
        return 0 if row["pass"] else 1
    if len(argv) < 2:
        print(__doc__)
        return 2
    print("| run | blocks (idle?) | fps est. | total ms | scene draw ms | solid ms | **transparent ms** "
          "| tags/frame | batch cpu us | us/tag | draws | quads | buffer B | row @steady_60s B |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for run in argv[1:]:
        for log in sorted(logs_of(run)):
            r = parse(log)
            print(f"| {label_of(log)} | {r['blocks']} ({'idle' if r['idle'] else 'all'}) | {fmt(r['fps'])} "
                  f"| {fmt(r['total'])} | {fmt(r['draw_all'])} | {fmt(r['solid'])} | **{fmt(r['transparent'])}** "
                  f"| {fmt(r['tags'])} | {fmt(r['cpu_us'])} | {fmt(r['us_per_tag'], 2)} | {fmt(r['draws'])} "
                  f"| {fmt(r['quads'])} | {fmt(r['buffer'])} | {fmt(r['row'])} |")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
