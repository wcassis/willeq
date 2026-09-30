#!/usr/bin/env python3
"""B932: tables for scripts/pi/mesa_ab.sh -- stock Mesa (side A) vs the
LLVM-less Mesa (side B), rendered client on the Orange Pi.

Every number comes from a file the run wrote:
  * details.json (scripts/pi/rendered_zone_summarize.py): the ledger's
    rss_anon + categories at each [memledger] point, the verdict, fps;
  * snap_<point>.json (rendered_zone_run.py --snap-points): /proc status
    (RssAnon/RssFile/VmHWM) and per-mapped-file Rss at startup/ready/steady_60s
    plus the mapped .so list -- the PROOF of which Mesa was loaded;
  * frames/*.png: the dumped frames, compared across runs of the same zone.

willeq-owned memory (Will, 2026-09-29: "64 MB excludes driver memory") is
  RssAnon - gpu_driver
taken from the SAME ledger line (both are printed together), at every
ledger point; owned_max is the maximum over the points.

usage: mesa_ab_table.py --ab-dir DIR [--json OUT.json]
       mesa_ab_table.py --frame-dirs A1=DIR B1=DIR ...   (scripts/pi/mesa_frame_ab.sh
           output: every pair of dirs, every PNG name they share)
"""
import argparse
import glob
import itertools
import json
import os
import re
import statistics

MB = 1048576.0
TARGET = 64 * 1024 * 1024
POINTS = ("startup", "ready", "steady_60s")
DRIVER = re.compile(r"libLLVM|libgallium|libEGL_mesa|libgbm|dri_gbm|libGLdispatch|libGLESv2|libEGL\.so|libdrm")


def load(p):
    try:
        return json.load(open(p))
    except (OSError, ValueError):
        return None


def runs(ab):
    for leg in sorted(glob.glob(os.path.join(ab, "r*_[AB]"))):
        m = re.match(r"r(\d+)_([AB])$", os.path.basename(leg))
        if not m:
            continue
        for zd in sorted(glob.glob(os.path.join(leg, "zones", "*"))):
            if zd.endswith("_retry") or not os.path.exists(os.path.join(zd, "details.json")):
                continue
            yield int(m.group(1)), m.group(2), os.path.basename(zd), zd


def lib_rss(snap):
    out = {}
    for path, v in (snap or {}).get("files", {}).items():
        if ".so" in path:
            out[os.path.basename(path)] = v
    return out


def one(rnd, side, zone, zd):
    det = load(os.path.join(zd, "details.json")) or {}
    row = det.get("row", {})
    pts = det.get("points", {})
    snaps = {p: load(os.path.join(zd, "snap_%s.json" % p)) for p in POINTS}
    r = {"round": rnd, "side": side, "zone": zone, "dir": zd, "verdict": row.get("verdict"),
         "signature": row.get("signature"), "fps_median": row.get("fps_median"),
         "fps_min": row.get("fps_min"), "peak_rss_anon": row.get("peak_rss_anon"),
         "vm_hwm": row.get("vm_hwm"), "nonblank": "%s/%s" % (row.get("nonblank_frames"), row.get("dumped_frames"))}
    owned = []
    for name, p in pts.items():
        ra = int(p.get("rss_anon") or 0)
        gd = int(p.get("categories", {}).get("gpu_driver", 0))
        if ra:
            owned.append((ra - gd, name))
    r["owned_max"], r["owned_max_point"] = max(owned) if owned else (0, "")
    for p in POINTS:
        lp = pts.get(p, {})
        ra = int(lp.get("rss_anon") or 0)
        gd = int(lp.get("categories", {}).get("gpu_driver", 0))
        r["ledger_rss_anon_" + p] = ra
        r["gpu_driver_" + p] = gd
        r["owned_" + p] = ra - gd if ra else 0
        st = (snaps[p] or {}).get("status", {})
        r["rss_anon_" + p] = st.get("RssAnon", 0)
        r["rss_file_" + p] = st.get("RssFile", 0)
        r["vm_rss_" + p] = st.get("VmRSS", 0)
    s = snaps.get("steady_60s") or snaps.get("ready") or {}
    r["llvm_mapped"] = s.get("llvm_mapped")
    r["gallium"] = s.get("gallium")
    r["egl_mesa"] = s.get("egl_mesa")
    r["gbm"] = s.get("gbm")
    r["proof_points"] = {p: {"llvm_mapped": (snaps[p] or {}).get("llvm_mapped"),
                             "gallium": (snaps[p] or {}).get("gallium")} for p in POINTS if snaps[p]}
    libs = lib_rss(s)
    r["libs"] = libs
    r["driver_so_rss"] = sum(v["rss"] for k, v in libs.items() if DRIVER.search(k))
    r["driver_so_anon"] = sum(v["anon"] for k, v in libs.items() if DRIVER.search(k))
    r["all_so_rss"] = sum(v["rss"] for v in libs.values())
    r["frames"] = sorted(glob.glob(os.path.join(zd, "frames", "*.png")))
    return r


def frame_metrics(a, b):
    import numpy as np
    from PIL import Image
    ia = np.asarray(Image.open(a).convert("RGB"), dtype=np.float64)
    ib = np.asarray(Image.open(b).convert("RGB"), dtype=np.float64)
    if ia.shape != ib.shape:
        return {"error": "shape %s vs %s" % (ia.shape, ib.shape)}
    d = np.abs(ia - ib)
    ga = ia @ [0.299, 0.587, 0.114]
    gb = ib @ [0.299, 0.587, 0.114]
    try:
        from skimage.metrics import structural_similarity
        ssim = float(structural_similarity(ga, gb, data_range=255.0))
    except ImportError:
        ssim = None
    return {"mean_abs": round(float(d.mean()), 3), "max_abs": int(d.max()),
            "pct_px_gt8": round(float((d.max(axis=2) > 8).mean() * 100), 3),
            "identical": bool(d.max() == 0), "ssim": None if ssim is None else round(ssim, 5)}


def mib(v):
    return "%.1f" % (v / MB) if v else "-"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ab-dir", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--frame-dirs", nargs="*", default=[])
    a = ap.parse_args()
    if a.frame_dirs:
        dirs = [d.split("=", 1) for d in a.frame_dirs]
        print("| image | pair | kind | identical | mean abs | max abs | % px > 8 | SSIM |")
        print("|---|---|---|---|---|---|---|---|")
        res = []
        for (la, da), (lb, db) in itertools.combinations(dirs, 2):
            kind = "within-" + la[0] if la[0] == lb[0] else "cross"
            for fa in sorted(glob.glob(os.path.join(da, "*.png"))):
                fb = os.path.join(db, os.path.basename(fa))
                if os.path.exists(fb):
                    m = frame_metrics(fa, fb)
                    res.append(dict(m, image=os.path.basename(fa), pair="%s-%s" % (la, lb), kind=kind))
                    print("| %s | %s vs %s | %s | %s | %s | %s | %s | %s |" % (
                        os.path.basename(fa), la, lb, kind, m.get("identical"), m.get("mean_abs"),
                        m.get("max_abs"), m.get("pct_px_gt8"), m.get("ssim")))
        if a.json:
            json.dump(res, open(a.json, "w"), indent=1)
        return
    rows = [one(*x) for x in runs(a.ab_dir)]
    zones = []
    for r in rows:
        if r["zone"] not in zones:
            zones.append(r["zone"])

    print("## Per run\n")
    print("| zone | side | rnd | verdict | RssAnon st/rdy/stdy MiB | RssFile st/rdy/stdy MiB | gpu_driver st/rdy/stdy MiB "
          "| owned (RssAnon-gpu_driver) rdy/stdy/max MiB | peak RssAnon | VmHWM | driver .so Rss | fps med/min | libLLVM mapped | libgallium |")
    print("|" + "---|" * 14)
    for r in sorted(rows, key=lambda r: (zones.index(r["zone"]), r["side"], r["round"])):
        print("| %s | %s | %d | %s | %s | %s | %s | %s | %s | %s | %s | %s/%s | %s | %s |" % (
            r["zone"], r["side"], r["round"], r["verdict"] + ("" if r["verdict"] == "pass" else " (%s)" % r["signature"]),
            "/".join(mib(r["rss_anon_" + p]) for p in POINTS),
            "/".join(mib(r["rss_file_" + p]) for p in POINTS),
            "/".join(mib(r["gpu_driver_" + p]) for p in POINTS),
            "/".join(mib(r[k]) for k in ("owned_ready", "owned_steady_60s", "owned_max")),
            mib(r["peak_rss_anon"] or 0), mib(r["vm_hwm"] or 0), mib(r["driver_so_rss"]),
            r["fps_median"], r["fps_min"], r["llvm_mapped"],
            ",".join(os.path.basename(g) for g in (r["gallium"] or [])) or "-"))

    print("\n## Per zone, medians over passing runs (B - A)\n")
    keys = [("rss_file_steady_60s", "RssFile steady"), ("rss_anon_steady_60s", "RssAnon steady"),
            ("gpu_driver_steady_60s", "gpu_driver steady"), ("owned_steady_60s", "owned steady"),
            ("owned_max", "owned max"), ("peak_rss_anon", "peak RssAnon"), ("vm_hwm", "VmHWM"),
            ("driver_so_rss", "driver .so Rss")]
    print("| zone | metric | A stock MiB | B no-LLVM MiB | B-A MiB |")
    print("|---|---|---|---|---|")
    summary = {}
    for z in zones:
        for k, label in keys:
            va = [r[k] for r in rows if r["zone"] == z and r["side"] == "A" and r["verdict"] == "pass" and r[k]]
            vb = [r[k] for r in rows if r["zone"] == z and r["side"] == "B" and r["verdict"] == "pass" and r[k]]
            ma = statistics.median(va) if va else 0
            mb = statistics.median(vb) if vb else 0
            summary.setdefault(z, {})[k] = {"A": va, "B": vb}
            print("| %s | %s | %s | %s | %s |" % (z, label, mib(ma), mib(mb),
                                                  ("%+.1f" % ((mb - ma) / MB)) if va and vb else "-"))
        fa = [r["fps_median"] for r in rows if r["zone"] == z and r["side"] == "A" and r["fps_median"] not in ("", None)]
        fb = [r["fps_median"] for r in rows if r["zone"] == z and r["side"] == "B" and r["fps_median"] not in ("", None)]
        print("| %s | fps median (runs) | %s | %s | |" % (z, fa, fb))
        owned_b = [r["owned_max"] for r in rows if r["zone"] == z and r["side"] == "B" and r["verdict"] == "pass"]
        if owned_b:
            owned_a = [r["owned_max"] for r in rows if r["zone"] == z and r["side"] == "A" and r["verdict"] == "pass"]
            print("| %s | owned max (worst run) minus 64 MiB | %s | %s | |" % (
                z, "%+.1f" % ((max(owned_a) - TARGET) / MB) if owned_a else "-",
                "%+.1f" % ((max(owned_b) - TARGET) / MB)))

    print("\n## Driver shared objects, resident at steady_60s (median over passing runs)\n")
    print("| zone | library | A Rss MiB | A anon MiB | B Rss MiB | B anon MiB |")
    print("|---|---|---|---|---|---|")
    for z in zones:
        libs = {}
        for side in "AB":
            for r in rows:
                if r["zone"] == z and r["side"] == side and r["verdict"] == "pass":
                    for k, v in r["libs"].items():
                        libs.setdefault(k, {}).setdefault(side, []).append(v)
        onlya = sorted(k for k, v in libs.items() if "A" in v and "B" not in v)
        onlyb = sorted(k for k, v in libs.items() if "B" in v and "A" not in v)
        for k in sorted(libs, key=lambda k: -max([x["rss"] for x in libs[k].get("A", [])] + [0])):
            if not DRIVER.search(k) and k not in onlya and k not in onlyb:
                continue
            cells = []
            for side in "AB":
                xs = libs[k].get(side, [])
                cells += [mib(statistics.median(x["rss"] for x in xs)) if xs else "absent",
                          mib(statistics.median(x["anon"] for x in xs)) if xs else "-"]
            print("| %s | %s | %s |" % (z, k, " | ".join(cells)))
        summary.setdefault(z, {})["so_only_A"] = onlya
        summary.setdefault(z, {})["so_only_B"] = onlyb

    print("\n## Frames: pairwise diff of the dumped frames, same zone, same frame index\n")
    print("| zone | frame | pair | kind | mean abs | max abs | % px > 8 | SSIM |")
    print("|---|---|---|---|---|---|---|---|")
    fsum = {}
    for z in zones:
        zr = [r for r in rows if r["zone"] == z and r["frames"]]
        for ra, rb in itertools.combinations(zr, 2):
            kind = "within-%s" % ra["side"] if ra["side"] == rb["side"] else "cross"
            for fa in ra["frames"]:
                name = os.path.basename(fa)
                fb = os.path.join(os.path.dirname(rb["frames"][0]), name)
                if not os.path.exists(fb):
                    continue
                m = frame_metrics(fa, fb)
                fsum.setdefault(z, []).append(dict(m, frame=name, kind=kind,
                                                   pair="%s%d-%s%d" % (ra["side"], ra["round"], rb["side"], rb["round"])))
                print("| %s | %s | %s%d vs %s%d | %s | %s | %s | %s | %s |" % (
                    z, name, ra["side"], ra["round"], rb["side"], rb["round"], kind,
                    m.get("mean_abs"), m.get("max_abs"), m.get("pct_px_gt8"), m.get("ssim")))
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"runs": [{k: v for k, v in r.items() if k != "libs"} for r in rows],
                       "libs": {"%s/%s%d" % (r["zone"], r["side"], r["round"]): r["libs"] for r in rows},
                       "zone_summary": summary, "frames": fsum}, f, indent=1)


if __name__ == "__main__":
    main()
