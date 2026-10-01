#!/usr/bin/env python3
"""B964: fold crowd runs into tables.

Reads every <run>/<zone>/<variant>/client.log (x86 rig: scripts/b964/crowd_run.sh)
or <run>/zones/<zone>/client.log (Pi sweep: scripts/pi_rendered_zone_sweep.sh
with a B964 tour; the variant is the run directory's name) and prints one
markdown row per run:

  - phases: the [impostor-frames] windows whose timestamps fall in the tour's
    idle phase and moving phase (between the `Script: executing` lines of the
    `/face` and the first `/move`, and between the first `/move` and the
    `# phase: hold` wait) -- frame p50/p95/max, the median of the windows'
    values;
  - the last [impostor] line: tier counts, pages, cells, creations, refreshes,
    unit times, draws;
  - the max `frame_unit_ms_max` (the worst capture work in one frame) and the
    max single frame time seen anywhere after the crowd was spawned;
  - the ledger at steady_60s and at the last point: RssAnon, gpu_driver, owned,
    gpu_buffers, owned+gpu_buffers, and the tex.impostor owner row.

usage: crowd_table.py RUN_DIR [RUN_DIR...]
"""
import os
import re
import statistics
import sys

TS = re.compile(r"^\[?(\d{4}-\d\d-\d\d[ T]\d\d:\d\d:\d\d(?:\.\d+)?)")
KV = re.compile(r"([a-z_0-9]+)=([^ ]+)")


def ts_of(line):
    # The client's log lines carry `[YYYY-MM-DD HH:MM:SS.mmm]`; the Pi runner
    # prefixes the read epoch. Use whichever parses, as seconds.
    m = re.match(r"^(\d+\.\d+) ", line)
    if m:
        return float(m.group(1))
    m = re.search(r"\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)(?:\.(\d+))?\]", line)
    if not m:
        return None
    import datetime
    y, mo, d, h, mi, s = (int(m.group(i)) for i in range(1, 7))
    frac = float("0." + m.group(7)) if m.group(7) else 0.0
    return datetime.datetime(y, mo, d, h, mi, s).timestamp() + frac


def kv(line):
    return dict(KV.findall(line))


def mib(b):
    try:
        return "%.2f" % (int(b) / 1048576.0)
    except (TypeError, ValueError):
        return "-"


def fold(log):
    lines = open(log, errors="replace").read().splitlines()
    t_face = t_move = t_hold = t_spawn_done = None
    frames = []      # (t, p50, p95, max, n)
    last_imp = {}
    imp_lines = []   # (t, dict)
    unit_max = 0.0
    ledger = {}
    gpu = {}
    owner = {}
    for ln in lines:
        t = ts_of(ln)
        if "Script: executing '/face" in ln and t_face is None:
            t_face = t
        elif "Script: executing '/move" in ln and t_move is None:
            t_move = t
        elif "Script: wait" in ln and t_move is not None and t_hold is None and t is not None and t > t_move + 20:
            t_hold = t
        if "Script: executing '/warp" in ln:
            t_spawn_done = t
        if "[impostor-frames]" in ln:
            d = kv(ln)
            frames.append((t, float(d.get("frame_p50_ms", 0)), float(d.get("frame_p95_ms", 0)),
                           float(d.get("frame_max_ms", 0)), int(d.get("n", 0))))
        elif "[impostor] " in ln:
            last_imp = kv(ln)
            imp_lines.append((t, last_imp))
            unit_max = max(unit_max, float(last_imp.get("frame_unit_ms_max", 0)))
        elif "[memledger] point=" in ln:
            d = kv(ln)
            ledger[d.get("point")] = d
        elif "[memgpu] point=" in ln:
            d = kv(ln)
            gpu[d.get("point")] = d
        elif "[memledger-owner]" in ln and "owner=tex.impostor" in ln:
            d = kv(ln)
            owner[d.get("point")] = d

    def window(a, b):
        sel = [f for f in frames if f[0] is not None and a is not None and f[0] > a and (b is None or f[0] <= b)]
        if not sel:
            return None
        return (statistics.median(f[1] for f in sel), statistics.median(f[2] for f in sel),
                max(f[3] for f in sel), sum(f[4] for f in sel))

    idle = window(t_face, t_move)
    moving = window(t_move, t_hold)
    after = [f for f in frames if t_spawn_done and f[0] and f[0] > t_spawn_done]
    worst = max((f[3] for f in after), default=0.0)
    # Tier counts at the end of the idle phase (the last line before /move);
    # the cumulative counters from the run's last line.
    at_idle = {}
    for t, d in imp_lines:
        if t is not None and t_move is not None and t <= t_move:
            at_idle = d
    tiers = dict(last_imp)
    for k in ("candidates", "full", "impostor", "want", "limit", "gov", "pages", "cells", "unique", "shared", "draws"):
        if k in at_idle:
            tiers[k] = at_idle[k]
    row_max = max((int(d.get("row", 0)) for _t, d in imp_lines), default=0)
    return dict(idle=idle, moving=moving, imp=tiers, unit_max=unit_max, worst=worst, row_max=row_max,
                ledger=ledger, gpu=gpu, owner=owner)


def runs(root):
    out = []
    for dirpath, _dirs, files in os.walk(root):
        if "client.log" in files:
            rel = os.path.relpath(dirpath, root)
            out.append((rel, os.path.join(dirpath, "client.log")))
    return sorted(out)


def fmt_phase(p):
    return "%.1f / %.1f / %.0f" % (p[0], p[1], p[2]) if p else "-"


def main():
    print("(full/impostor/want/limit/gov/pages/cells: at the end of the idle phase; counters: whole run)")
    print("| run | idle p50 / p95 / max ms | moving p50 / p95 / max ms | full | impostor | want | limit | gov | "
          "pages | cells (u/s) | created | refresh app/angle/flip | demotions | unit ms avg / max | worst frame-unit ms | "
          "worst frame ms after spawn | draws | tex.impostor MiB (steady / max) | RssAnon | gpu_driver | owned | gpu_buffers | owned+gpu |")
    print("|" + "---|" * 25)
    for root in sys.argv[1:]:
        for rel, log in runs(root):
            f = fold(log)
            i = f["imp"]
            pt = "steady_60s" if "steady_60s" in f["ledger"] else (list(f["ledger"])[-1] if f["ledger"] else None)
            L = f["ledger"].get(pt, {}) if pt else {}
            G = f["gpu"].get(pt, {}) if pt else {}
            O = f["owner"].get(pt, {}) if pt else {}
            print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s (%s/%s) | %s | %s/%s/%s | %s | %s / %s | %.1f | %.0f | %s | %s | %s | %s | %s | %s | %s |" % (
                os.path.join(os.path.basename(os.path.normpath(root)), rel), fmt_phase(f["idle"]), fmt_phase(f["moving"]),
                i.get("full", "-"), i.get("impostor", "-"), i.get("want", "-"), i.get("limit", "-"), i.get("gov", "-"),
                i.get("pages", "-"), i.get("cells", "-"), i.get("unique", "-"), i.get("shared", "-"),
                i.get("created", "-"), i.get("refresh_appearance", "-"), i.get("refresh_angle", "-"),
                i.get("refresh_flip", "-"), i.get("demotions", "-"), i.get("unit_ms_avg", "-"), i.get("unit_ms_max", "-"),
                f["unit_max"], f["worst"], i.get("draws", "-"),
                (mib(O.get("live")) if O else "0") + " / " + mib(f["row_max"]),
                mib(L.get("rss_anon")), mib(L.get("gpu_driver", "").split("/")[0] if L.get("gpu_driver") else None),
                mib(G.get("owned")), mib(G.get("gpu_buffers")), mib(G.get("owned_plus_gpu_buffers"))))


if __name__ == "__main__":
    main()
