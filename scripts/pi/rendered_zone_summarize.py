#!/usr/bin/env python3
"""#102: turn ONE rendered Pi zone run (scripts/pi/rendered_zone_run.py's
output dir) into ONE TSV row, plus a details JSON beside the run.

Runs on the sweep host (scripts/pi_rendered_zone_sweep.sh). Every number in
the row comes from a file the run wrote: the client's own log ([memledger] /
[memmap] / [LOADING] / RENDERER STATS / Frame dump lines), the sampler's
samples.tsv + summary.json, the dumped PNG frames, and the Pi's dmesg diff.

usage: rendered_zone_summarize.py --run DIR --zone Z --expansion E
           --s3d-bytes N --attempt first|retry --dmesg FILE --tsv OUT.tsv

PASS means ALL of: the ledger's `ready` point names this zone; `steady_60s`
was reached; rendering is proven (zone_mesh, textures_decoded+textures_driver
and irrlicht_scene all non-zero at ready, and at least one dumped frame is a
real non-blank image); no OOM line; no FATAL; the client exited 0 on its
scripted /q without a watchdog kill. ERROR lines are counted and their
signatures recorded, but are not by themselves a failure.
"""
import argparse
import glob
import json
import os
import re
import statistics

RSS_TARGET = 64 * 1024 * 1024       # the design yardstick (printed, not a gate)
RSS_CEILING = 256 * 1024 * 1024     # the failing bound
DRIVER_SO = re.compile(r"owner=(so:(libLLVM|libgallium|libEGL|libGLES|libGLdispatch|libdrm|libgbm|libglapi)[^ ]*"
                       r"|[^ ]*(/dev/dri|renderD|card[0-9])[^ ]*)")

HEADER = ("zone expansion s3d_bytes attempt verdict signature exit_code kill_reason wall_s "
          "phase_reached ready_zone reached_ready reached_steady time_to_ready_s "
          "stats_blocks frames_rendered_min fps_median fps_min dumped_frames nonblank_frames "
          "zone_mesh_ready textures_ready irrlicht_scene_ready gpu_driver_ready "
          "rss_anon_ready rss_anon_steady peak_rss_anon peak_rss_anon_point vm_hwm "
          "over_64MB_peak over_256MB_peak driver_so_anon_ready driver_so_rss_ready "
          "sys_mem_used_delta_steady top5_ready top5_steady oom oom_line fatal_count "
          "error_count first_fatal error_signatures rcvbuf").split()


def parse_kv(s):
    return dict(re.findall(r"([a-z_0-9]+)=([^ ]+)", s))


def ledger_points(lines):
    """point -> (epoch, scalars dict, categories dict live bytes); FIRST
    occurrence of each point name, plus the ordered list of every point."""
    out = {}
    seq = []
    for ln in lines:
        if "[memledger] point=" not in ln:
            continue
        epoch = float(ln.split(" ", 1)[0]) if re.match(r"^\d+\.\d+ ", ln) else 0.0
        body = ln[ln.index("[memledger] point="):]
        head, _, cats = body.partition(" allocs=")
        scal = parse_kv(head)
        cat = {}
        for name, live, _peak in re.findall(r"([a-z_]+)=(\d+)/(\d+)", cats):
            cat[name] = int(live)
        out.setdefault(scal.get("point", "?"), (epoch, scal, cat))
        seq.append((epoch, scal.get("point", "?"), scal.get("zone", "")))
    return out, seq


def top5(cat):
    items = sorted(((v, k) for k, v in cat.items() if k != "unattributed"), reverse=True)[:5]
    return ";".join("%s(%.1fM)" % (k, v / 1048576.0) for v, k in items)


def driver_maps(lines, point, until=float("inf")):
    anon = rss = 0
    for ln in lines:
        m = re.match(r"^(\d+\.\d+) ", ln)
        if m and float(m.group(1)) >= until:
            break
        if "[memmap] point=%s " % point in ln and DRIVER_SO.search(ln):
            kv = parse_kv(ln[ln.index("[memmap]"):])
            anon += int(kv.get("anon", 0))
            rss += int(kv.get("rss", 0))
    return anon, rss


def frame_check(paths):
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return len(paths), -1, []
    good, detail = 0, []
    for p in sorted(paths):
        try:
            im = Image.open(p).convert("RGB")
            w, h = im.size
            small = im.resize((160, 90))
            colors = len(set(small.getdata()))
            sd = sum(ImageStat.Stat(im.convert("L")).stddev)
            ok = w >= 320 and h >= 200 and colors > 32 and sd > 4.0
            good += ok
            detail.append({"file": os.path.basename(p), "w": w, "h": h, "colors_160x90": colors,
                           "luma_stddev": round(sd, 2), "nonblank": ok})
        except Exception as e:  # a truncated/garbage PNG is a finding, not a crash
            detail.append({"file": os.path.basename(p), "error": str(e)})
    return len(paths), good, detail


def main():
    ap = argparse.ArgumentParser()
    for k in ("--run", "--zone", "--attempt", "--tsv"):
        ap.add_argument(k, required=True)
    ap.add_argument("--expansion", default="")
    ap.add_argument("--s3d-bytes", default="")
    ap.add_argument("--dmesg", default="")
    ap.add_argument("--preset-signature", default="",
                    help="the zone never reached a client run (zone_boot_failed / seed_failed): "
                         "write a fail row with this signature")
    a = ap.parse_args()
    os.makedirs(a.run, exist_ok=True)

    summ = {}
    sp = os.path.join(a.run, "summary.json")
    if os.path.exists(sp):
        summ = json.load(open(sp))
    logp = os.path.join(a.run, "client.log")
    lines = open(logp, errors="replace").read().splitlines() if os.path.exists(logp) else []

    pts, seq = ledger_points(lines)
    ready = pts.get("ready")
    # MOVED: the server moved the character to another zone AFTER this
    # zone's ready (measured: chardok, 34 s after ready, to qeynos). Every
    # later figure belongs to the other zone, so the row's memory is cut at
    # the first ledger point naming a different zone.
    move_epoch, moved_to = float("inf"), ""
    if ready and ready[1].get("zone") == a.zone:
        for ep, _pt, zn in seq:
            if ep > ready[0] and zn not in ("", "-", a.zone):
                move_epoch, moved_to = ep, zn
                break
    steady = pts.get("steady_60s")
    if steady and (steady[0] >= move_epoch or steady[1].get("zone") != a.zone):
        steady = None
    phases = [int(m) for m in re.findall(r"\[LOADING\]\[Phase (\d+)/15\]", "\n".join(lines))]
    phase = "%d/15" % max(phases) if phases else "none"
    ready_zone = ready[1].get("zone", "") if ready else ""
    t_ready = round(ready[0] - summ.get("start_epoch", ready[0]), 1) if ready and ready[0] else ""

    stats_blocks = sum("=== RENDERER STATS" in ln for ln in lines)
    dumps = [int(m) for m in re.findall(r"Frame dump: \S+ \(frame counter (\d+)\)", "\n".join(lines))]
    frames_min = max([stats_blocks * 150] + dumps)
    fps = []
    ready_epoch = ready[0] if ready else float("inf")
    for ln in lines:
        m = re.match(r"^(\d+\.\d+) .*\] +FPS: (\d+)", ln)
        if m and ready_epoch <= float(m.group(1)) < move_epoch:
            fps.append(int(m.group(2)))
    pngs = glob.glob(os.path.join(a.run, "frames", "*.png"))
    n_png, n_good, fdetail = frame_check(pngs)

    def cat_at(p, *names):
        return sum(p[2].get(n, 0) for n in names) if p else 0

    zm = cat_at(ready, "zone_mesh")
    tx = cat_at(ready, "textures_decoded", "textures_driver")
    irr = cat_at(ready, "irrlicht_scene")
    gpu = cat_at(ready, "gpu_driver")
    rss_ready = int(ready[1].get("rss_anon", 0)) if ready else 0
    rss_steady = int(steady[1].get("rss_anon", 0)) if steady else 0
    ledger_max = max([int(p[1].get("rss_anon", 0)) for p in pts.values() if p[0] < move_epoch] + [0])
    sampled = int(summ.get("peak_rss_anon_kb_sampled", 0)) * 1024
    if move_epoch != float("inf"):  # re-derive the sampled peak before the move
        sampled = 0
        sp_tsv = os.path.join(a.run, "samples.tsv")
        if os.path.exists(sp_tsv):
            with open(sp_tsv) as f:
                next(f, None)
                for ln in f:
                    c = ln.split("\t")
                    if float(c[0]) >= move_epoch:
                        break
                    sampled = max(sampled, int(c[1]) * 1024)
    peak = max(ledger_max, sampled)
    peak_point = ""
    if summ.get("peak_rss_anon_epoch"):
        pe = summ["peak_rss_anon_epoch"]
        before = [(p[0], k) for k, p in pts.items() if p[0] and p[0] <= pe]
        peak_point = ("after_" + max(before)[1]) if before else "before_startup"
    vm_hwm = int(summ.get("vm_hwm_kb", 0)) * 1024
    # the client prints its [memmap] census at startup and ready only
    d_anon, d_rss = driver_maps(lines, "ready", move_epoch)
    sys_delta = ""
    before_avail = summ.get("mem_available_kb_before", -1)
    for p in summ.get("points", []):
        if p["point"] == ("steady_60s" if steady else "ready") and before_avail > 0 \
                and p["mem_available_kb"] > 0 and p["epoch"] < move_epoch and sys_delta == "":
            sys_delta = (before_avail - p["mem_available_kb"]) * 1024

    oom_lines = []
    if a.dmesg and os.path.exists(a.dmesg):
        oom_lines = [l.strip() for l in open(a.dmesg, errors="replace") if l.strip()]
    oom = "y" if oom_lines else "n"
    fatal = [ln for ln in lines if "[FATAL]" in ln]
    errors = [ln for ln in lines if "[ERROR]" in ln]
    sigs = {}
    for ln in errors:
        s = re.sub(r"^\S+ \[[^\]]*\] \[ERROR\] ", "", ln)
        s = re.sub(r"\d+", "N", s)[:90]
        sigs[s] = sigs.get(s, 0) + 1
    sig_txt = " | ".join("%dx %s" % (n, s) for s, n in sorted(sigs.items(), key=lambda kv: -kv[1])[:3])
    rcv = next((re.sub(r"^.*PosixUdpTransport: ", "", ln) for ln in lines if "rcvbuf=" in ln), "")

    rc = summ.get("returncode", "none")
    kill = summ.get("kill_reason", "") if summ else "no summary.json (runner never finished)"
    rendered = zm > 0 and tx > 0 and irr > 0 and n_good >= 1
    checks = [
        (oom == "y", "oom_kill"),
        (not ready, "no_ready (phase %s; %s)" % (phase, kill or ("exit %s" % rc))),
        (bool(ready) and ready_zone != a.zone, "wrong_zone (%s)" % ready_zone),
        (moved_to != "", "moved_after_ready (server sent the character to %s %.0f s after ready)"
         % (moved_to, move_epoch - ready[0] if ready else 0)),
        (bool(ready) and not steady, "no_steady_60s (%s)" % (kill or "exit %s" % rc)),
        (bool(ready) and not rendered, "render_unproven (zone_mesh=%d textures=%d irrlicht_scene=%d nonblank=%s/%d)"
         % (zm, tx, irr, n_good, n_png)),
        (bool(fatal), "fatal"),
        (bool(ready) and steady is not None and (rc != 0 or kill), "unclean_exit (rc=%s %s)" % (rc, kill)),
    ]
    sig = a.preset_signature or next((s for bad, s in checks if bad), "")
    if a.preset_signature:
        kill = "no client run"
    verdict = "pass" if not sig else "fail"
    wall = round(summ["end_epoch"] - summ["start_epoch"], 1) if "end_epoch" in summ else ""

    row = {
        "zone": a.zone, "expansion": a.expansion, "s3d_bytes": a.s3d_bytes, "attempt": a.attempt,
        "verdict": verdict, "signature": sig, "exit_code": rc, "kill_reason": kill, "wall_s": wall,
        "phase_reached": phase, "ready_zone": ready_zone, "reached_ready": "y" if ready else "n",
        "reached_steady": "y" if steady else "n", "time_to_ready_s": t_ready,
        "stats_blocks": stats_blocks, "frames_rendered_min": frames_min,
        "fps_median": statistics.median(fps) if fps else "", "fps_min": min(fps) if fps else "",
        "dumped_frames": n_png, "nonblank_frames": n_good,
        "zone_mesh_ready": zm, "textures_ready": tx, "irrlicht_scene_ready": irr, "gpu_driver_ready": gpu,
        "rss_anon_ready": rss_ready, "rss_anon_steady": rss_steady, "peak_rss_anon": peak,
        "peak_rss_anon_point": peak_point, "vm_hwm": vm_hwm,
        "over_64MB_peak": max(0, peak - RSS_TARGET), "over_256MB_peak": max(0, peak - RSS_CEILING),
        "driver_so_anon_ready": d_anon, "driver_so_rss_ready": d_rss,
        "sys_mem_used_delta_steady": sys_delta,
        "top5_ready": top5(ready[2]) if ready else "", "top5_steady": top5(steady[2]) if steady else "",
        "oom": oom, "oom_line": (oom_lines[0][:200] if oom_lines else ""),
        "fatal_count": len(fatal), "error_count": len(errors),
        "first_fatal": (re.sub(r"^\S+ ", "", fatal[0])[:200] if fatal else ""),
        "error_signatures": sig_txt, "rcvbuf": rcv[:160],
    }
    clean = {k: str(v).replace("\t", " ").replace("\n", " ") for k, v in row.items()}
    new = not os.path.exists(a.tsv) or os.path.getsize(a.tsv) == 0
    with open(a.tsv, "a") as f:
        if new:
            f.write("\t".join(HEADER) + "\n")
        f.write("\t".join(clean[h] for h in HEADER) + "\n")
    with open(os.path.join(a.run, "details.json"), "w") as f:
        json.dump({"row": row, "frames": fdetail, "fps_after_ready": fps,
                   "points": {k: {"epoch": v[0], "rss_anon": v[1].get("rss_anon"), "categories": v[2]}
                              for k, v in pts.items()}}, f, indent=1)
    print("%s %s %s peak_rss_anon=%.1fMB steady=%.1fMB fps_med=%s frames=%d/%d %s"
          % (a.zone, a.attempt, verdict, peak / 1048576.0, rss_steady / 1048576.0,
             row["fps_median"], n_good, n_png, sig))


if __name__ == "__main__":
    main()
