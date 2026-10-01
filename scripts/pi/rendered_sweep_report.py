#!/usr/bin/env python3
"""#102: markdown tables for refactor/B854/RENDERED_SWEEP.md, straight from
the rendered sweep's TSVs (scripts/pi_rendered_zone_sweep.sh), so no number
in the doc is typed by hand.

usage: rendered_sweep_report.py <sweep dir>   (reads results.tsv and, if
present, results_retry.tsv; prints markdown to stdout)
"""
import csv
import os
import statistics
import sys

MB = 1048576.0


def load(p):
    if not os.path.exists(p):
        return []
    with open(p) as f:
        return list(csv.DictReader(f, delimiter="\t"))


def mb(v):
    try:
        return "%.1f" % (int(v) / MB)
    except (TypeError, ValueError):
        return "-"


def table(rows, title):
    out = ["### " + title, "",
           "| zone | exp | verdict | phase | t→ready s | fps med/min | frames (dumped/non-blank) | "
           "zone_mesh / textures / irrlicht_scene @ready MB | RssAnon peak MB | @ready | @steady | "
           "VmHWM MB | gpu_driver @ready MB | driver .so RSS MB | sys MemAvailable drop @steady MB | OOM | signature |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        out.append("| %s | %s | **%s** | %s | %s | %s/%s | ≥%s (%s/%s) | %s / %s / %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["zone"], r["expansion"][:1], r["verdict"], r["phase_reached"], r["time_to_ready_s"] or "-",
            r["fps_median"] or "-", r["fps_min"] or "-", r["frames_rendered_min"], r["dumped_frames"],
            r["nonblank_frames"], mb(r["zone_mesh_ready"]), mb(r["textures_ready"]),
            mb(r["irrlicht_scene_ready"]), mb(r["peak_rss_anon"]), mb(r["rss_anon_ready"]),
            mb(r["rss_anon_steady"]), mb(r["vm_hwm"]), mb(r["gpu_driver_ready"]),
            mb(r["driver_so_rss_ready"]), mb(r["sys_mem_used_delta_steady"]), r["oom"], r["signature"].replace("|", "/") or "-"))
    return "\n".join(out)


OWNED_KEYS = ("owned_peak",)   # B933/B978: the only keys the 64 MB target is counted on
OWNED_LABEL = "owned=RssAnon-gpu_driver+gpu_buffers"   # B978: scripts/ci/owned_memory.py


def dist(rows, key, label):
    v = sorted(int(r[key]) for r in rows if r.get(key) not in (None, "", "0"))
    if not v:
        return "- %s: no data" % label
    over256 = sum(x > 256 * MB for x in v)
    # B933 (Will, 2026-09-29: "64 MB excludes driver memory") + B978 (Will,
    # 2026-10-01: GPU buffers count): "over 64 MB" is counted only on OWNED
    # memory (RssAnon - gpu_driver + gpu_buffers); a whole-RssAnon figure is
    # counted against the 256 MB failing bound only.
    over64 = ("over 64 MB (%s): %d/%d; " % (OWNED_LABEL, sum(x > 64 * MB for x in v), len(v))
              if key in OWNED_KEYS else "")
    return ("- %s (n=%d): min %.1f, median %.1f, max %.1f MB; %sover 256 MB (whole RssAnon): %d/%d"
            % (label, len(v), v[0] / MB, statistics.median(v) / MB, v[-1] / MB, over64, over256, len(v)))


def main():
    d = sys.argv[1]
    first = load(os.path.join(d, "results.tsv"))
    retry = load(os.path.join(d, "results_retry.tsv"))
    npass = sum(r["verdict"] == "pass" for r in first)
    print("## Totals\n")
    print("- first attempt: %d/%d pass" % (npass, len(first)))
    if retry:
        print("- retry (failed zones only, once): %d/%d pass" % (sum(r["verdict"] == "pass" for r in retry), len(retry)))
    print("- OOM kills (dmesg): first %d, retry %d" % (sum(r["oom"] == "y" for r in first),
                                                     sum(r["oom"] == "y" for r in retry)))
    # Memory figures only from rows whose `ready` point names the zone swept
    # (a server kick to qeynos makes a mixed two-zone load, which is not that
    # zone's figure): the first attempt when valid, else the retry, else any
    # extra leg's row (argv[2:]: more sweep dirs, e.g. the eligibility leg).
    extra = []
    for more in sys.argv[2:]:
        extra += load(os.path.join(more, "results.tsv")) + load(os.path.join(more, "results_retry.tsv"))
    best = {}
    for r in first + retry + extra:
        if (r["ready_zone"] == r["zone"] and not r["signature"].startswith("moved_after_ready")
                and r["zone"] not in best):
            best[r["zone"]] = r
    ran = list(best.values())
    print("- zones with a verified own-zone ready row (any attempt/leg): %d/%d; of those passing: %d"
          % (len(ran), len(first), sum(r["verdict"] == "pass" for r in ran)))
    print(dist(ran, "peak_rss_anon", "peak RssAnon (verified rows)"))
    # B978: a row summarized before B978 has no owned_rss_anon_peak column and
    # its owned_peak omits gpu_buffers -- never mixed in; re-summarize the run
    # dirs (scripts/pi/rendered_sweep_rebuild.py) to count them.
    b978 = [r for r in ran if r.get("owned_rss_anon_peak") not in (None, "")]
    if len(b978) != len(ran):
        print("- %d verified row(s) were summarized before B978 (owned without gpu_buffers): left out of the "
              "owned line; re-summarize with scripts/pi/rendered_sweep_rebuild.py" % (len(ran) - len(b978)))
    print(dist(b978, "owned_peak", "peak OWNED = RssAnon - gpu_driver + gpu_buffers (verified rows)"))
    print(dist(ran, "rss_anon_ready", "RssAnon at ready (verified rows)"))
    print(dist([r for r in ran if r["reached_steady"] == "y"], "rss_anon_steady", "RssAnon at steady_60s (verified rows)"))
    print(dist(ran, "vm_hwm", "VmHWM, peak total RSS incl. file-backed (verified rows)"))
    fps = [float(r["fps_median"]) for r in ran if r["fps_median"]]
    if fps:
        print("- client-reported FPS (median of each zone's post-ready samples): min %.0f, median %.0f, max %.0f"
              % (min(fps), statistics.median(fps), max(fps)))
    for key, label, note in (
            ("gpu_driver_ready", "ledger gpu_driver at ready", "driver heap charged by the ledger, INSIDE RssAnon"),
            ("driver_so_rss_ready", "driver .so RSS (libLLVM/libgallium/EGL/GLES/drm) at ready",
             "file-backed, INSIDE VmHWM, not RssAnon"),
            ("sys_mem_used_delta_steady", "system MemAvailable drop, launch -> steady (ready if no steady)",
             "system-wide: process RSS + page cache + kernel/GEM buffers the process RSS never shows")):
        print(dist(ran, key, label).split("; over 256 MB")[0] + " [" + note + "]")
    worst = sorted(ran, key=lambda r: -int(r["peak_rss_anon"] or 0))[:5]
    print("\nWorst five by peak RssAnon: " + ", ".join(
        "%s %s MB (steady %s)" % (r["zone"], mb(r["peak_rss_anon"]), mb(r["rss_anon_steady"])) for r in worst))
    print()
    print(table(first, "First attempt (run order: heaviest four first, then s3d_bytes descending)"))
    if retry:
        print()
        print(table(retry, "Retry (each first-attempt failure, once, at the end of the sweep)"))
    for more in sys.argv[2:]:
        for name, title in (("results.tsv", "first attempt"), ("results_retry.tsv", "retry")):
            rows = load(os.path.join(more, name))
            if rows:
                print()
                print(table(rows, "%s: %s" % (os.path.basename(more.rstrip("/")), title)))
    print("\n### Top ledger categories\n")
    print("| zone | top 5 at ready | top 5 at steady_60s |\n|---|---|---|")
    for r in sorted(ran, key=lambda r: r["zone"]):
        print("| %s (%s) | %s | %s |" % (r["zone"], r["attempt"], r["top5_ready"] or "-", r["top5_steady"] or "-"))


if __name__ == "__main__":
    main()
