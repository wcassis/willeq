#!/usr/bin/env python3
"""B909: tables for scripts/pi/collision_ab.sh output. Reads only the files
the legs wrote.

Per leg (r<N>_<A|B>/zones/<zone>/): client.log (each line prefixed by the
epoch the runner read it at), keys.tsv (uinput_drive.py's key events),
samples.tsv (the runner's /proc samples).

Per zone and side it reports, pooled over that side's legs:
  * frame time while the keys drive the player (between the sequence's start
    and end) and while idle (after the end): the median of the client's
    `/frametiming` blocks, "Total Frame" (us, a 60-frame mean each) and the
    block's "fps estimate"; "Player Movement" (us) is the phase that holds
    the collision and ground rays;
  * `spatial` live bytes at ready and steady_60s, and the sp.* owner rows;
  * peak RssAnon (samples) and RssAnon at steady_60s;
  * whether the player moved (the /loc before and after the keys differ is
    not available -- the evidence is Player Movement us > 0 in the window and
    the key events).
The verdict line per zone: B vs A median moving frame time, and whether
either side's median moving fps is under 30.

B936: --category CAT reports that ledger category (and its owner rows)
instead of `spatial` (the rig's --ledger-owners), plus gpu_driver at
steady_60s, owned = RssAnon - gpu_driver against 64 MiB, and the
"SimWorker Apply" phase (where the renderer applies the worker's results,
the keyframe vertex rewrite included) next to Player Movement.

B955: gpu_buffers at steady_60s (the client's [memgpu] line: GPU buffer
memory outside RssAnon -- Lima GEM on the Pi) and owned + gpu_buffers, the
figure Will rules on beside owned.

usage: collision_ab_table.py AB_DIR [--out FILE.md] [--category CAT]
"""
import argparse
import re
import statistics
import sys
from pathlib import Path

BLOCK_RE = re.compile(r"^(\d+\.\d+) .*=== FRAME TIMING BREAKDOWN \((\d+) frames, ([\d.]+) fps estimate\)")
TOTAL_RE = re.compile(r"Total Frame:\s+([\d.]+) us")
MOVE_RE = re.compile(r"Player Movement:\s+([\d.]+) us")
CATEGORY = "spatial"
LEDGER_RE = None  # built in main() for CATEGORY
GPU_RE = re.compile(r" gpu_driver=(\d+)/")
APPLY_RE = re.compile(r"SimWorker Apply:\s+([\d.]+) us")
MEMGPU_RE = re.compile(r"\[memgpu\] point=steady_60s zone=(\S+) gpu_buffers=(\d+) source=(\S+) .*"
                       r" owned_plus_gpu_buffers=(\d+)")
OWNER_RE = re.compile(r"\[memledger-startup\] point=(ready|steady_60s) owner=(\S+) live=(\d+) blocks=(\d+)")


def parse_zone(d, zone):
    log = d / "client.log"
    out = {"moving": [], "idle": [], "move_us": [], "idle_move_us": [], "spatial": {}, "owners": {},
           "apply_mov": [], "apply_idle": [], "gpu_steady": None,
           "gpubuf_steady": None, "gpubuf_src": "", "owned_plus_steady": None,
           "rss_steady": None, "rss_peak": None, "keys": 0, "ready": None, "ok": False}
    if not log.exists():
        return out
    seq_start = seq_end = None
    kt = d / "keys.tsv"
    if kt.exists():
        for line in kt.read_text().splitlines():
            f = line.split("\t")
            if len(f) == 3 and f[1] == "sequence":
                if f[2] == "start":
                    seq_start = float(f[0])
                elif f[2] == "end":
                    seq_end = float(f[0])
            elif len(f) == 3 and f[2] == "down":
                out["keys"] += 1
    lines = log.read_text(errors="replace").splitlines()
    for i, line in enumerate(lines):
        m = LEDGER_RE.search(line)
        if m and m.group(3) == zone:
            pt = m.group(2)
            if pt in ("ready", "steady_60s"):
                out["spatial"][pt] = int(m.group(5))
                if pt == "ready":
                    out["ready"] = float(m.group(1))
                if pt == "steady_60s":
                    out["rss_steady"] = int(m.group(4))
                    g = GPU_RE.search(line)
                    out["gpu_steady"] = int(g.group(1)) if g else None
                    out["ok"] = True
            continue
        m = MEMGPU_RE.search(line)
        if m and m.group(1) == zone:
            out["gpubuf_steady"], out["gpubuf_src"] = int(m.group(2)), m.group(3)
            out["owned_plus_steady"] = int(m.group(4))
            continue
        m = OWNER_RE.search(line)
        if m:
            out["owners"].setdefault(m.group(1), {})[m.group(2)] = int(m.group(3))
            continue
        m = BLOCK_RE.search(line)
        if m:
            t, fps = float(m.group(1)), float(m.group(3))
            total = move = apply_us = None
            for nxt in lines[i + 1:i + 60]:
                if "FRAME TIMING BREAKDOWN" in nxt:
                    break
                mt, mm, ma = TOTAL_RE.search(nxt), MOVE_RE.search(nxt), APPLY_RE.search(nxt)
                if mt and total is None:
                    total = float(mt.group(1))
                if mm and move is None:
                    move = float(mm.group(1))
                if ma and apply_us is None:
                    apply_us = float(ma.group(1))
            if total is None or out["ready"] is None or t < out["ready"]:
                continue
            # A block covers the 60 frames BEFORE its line: count it for the
            # window only when it lies wholly inside it.
            span = 60.0 / fps if fps > 0 else 5.0
            if seq_start and seq_end and t - span >= seq_start and t <= seq_end:
                out["moving"].append((total, fps))
                if move is not None:
                    out["move_us"].append(move)
                if apply_us is not None:
                    out["apply_mov"].append(apply_us)
            elif seq_end and t - span >= seq_end + 1.0:
                out["idle"].append((total, fps))
                if move is not None:
                    out["idle_move_us"].append(move)
                if apply_us is not None:
                    out["apply_idle"].append(apply_us)
    sp = d / "samples.tsv"
    if sp.exists():
        vals = []
        for line in sp.read_text().splitlines()[1:]:
            f = line.split("\t")
            if len(f) > 1 and f[1].isdigit():
                vals.append(int(f[1]) * 1024)
        out["rss_peak"] = max(vals) if vals else None
    return out


def med(xs):
    return statistics.median(xs) if xs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ab_dir")
    ap.add_argument("--out")
    ap.add_argument("--category", default="spatial")
    a = ap.parse_args()
    global CATEGORY, LEDGER_RE
    CATEGORY = a.category
    LEDGER_RE = re.compile(r"^(\d+\.\d+) .*\[memledger\] point=(\S+) zone=(\S+) .*?rss_anon=(\d+) .* "
                           + re.escape(CATEGORY) + r"=(\d+)/")
    root = Path(a.ab_dir)
    legs = sorted(p for p in root.iterdir() if p.is_dir() and re.match(r"r\d+_[AB]$", p.name))
    zones = []
    for leg in legs:
        for z in sorted((leg / "zones").iterdir()) if (leg / "zones").is_dir() else []:
            if z.is_dir() and not z.name.endswith("_retry") and z.name not in zones:
                zones.append(z.name)
    data = {}
    for leg in legs:
        side = leg.name.split("_")[1]
        for z in zones:
            data.setdefault((z, side), []).append((leg.name, parse_zone(leg / "zones" / z, z)))
    md = [f"Source: `{root}`. Frame time = median of the client's `/frametiming` 60-frame blocks "
          "(\"Total Frame\", us); moving = wholly inside the key sequence, idle = after it. "
          "fps = median of the blocks' fps estimates.\n"]
    md.append(f"| zone | side | runs ok | blocks mov/idle | moving frame us | moving fps | Player Movement us (mov / idle) | SimWorker Apply us (mov / idle) | idle frame us | idle fps | {CATEGORY} @ready | {CATEGORY} @steady | peak RssAnon | RssAnon @steady | gpu_driver @steady | owned = RssAnon - gpu_driver @steady | owned - 64 MiB | gpu_buffers @steady (source) | owned + gpu_buffers @steady |")
    md.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    verdicts = []
    for z in zones:
        meds = {}
        for side in ("A", "B"):
            runs = [r for _, r in data.get((z, side), [])]
            mv = [t for r in runs for t, _ in r["moving"]]
            mf = [f for r in runs for _, f in r["moving"]]
            iv = [t for r in runs for t, _ in r["idle"]]
            ifp = [f for r in runs for _, f in r["idle"]]
            pm = [x for r in runs for x in r["move_us"]]
            pim = [x for r in runs for x in r["idle_move_us"]]
            spr = sorted({r["spatial"].get("ready") for r in runs if r["spatial"].get("ready") is not None})
            sps = sorted({r["spatial"].get("steady_60s") for r in runs if r["spatial"].get("steady_60s") is not None})
            pk = [r["rss_peak"] for r in runs if r["rss_peak"]]
            st = [r["rss_steady"] for r in runs if r["rss_steady"]]
            ok = sum(1 for r in runs if r["ok"])
            am = [x for r in runs for x in r["apply_mov"]]
            ai = [x for r in runs for x in r["apply_idle"]]
            gp = [r["gpu_steady"] for r in runs if r["gpu_steady"]]
            ow = [r["rss_steady"] - r["gpu_steady"] for r in runs if r["rss_steady"] and r["gpu_steady"]]
            over = [o - 64 * 1024 * 1024 for o in ow]
            gb = [r["gpubuf_steady"] for r in runs if r["gpubuf_steady"] is not None]
            gsrc = ",".join(sorted({r["gpubuf_src"] for r in runs if r["gpubuf_src"]}))
            op = [r["owned_plus_steady"] for r in runs if r["owned_plus_steady"] is not None]
            meds[side] = (med(mv), med(mf), med(pm))
            rng = lambda xs: (f"{min(xs):,}" if len(xs) and min(xs) == max(xs) else
                              (f"{min(xs):,}–{max(xs):,}" if xs else "n/a"))
            fmt = lambda v, p=0: (f"{v:,.{p}f}" if v is not None else "n/a")
            md.append(f"| {z} | {side} | {ok}/{len(runs)} | {len(mv)}/{len(iv)} | {fmt(med(mv))} | {fmt(med(mf), 1)} | "
                      f"{fmt(med(pm))} / {fmt(med(pim))} | {fmt(med(am))} / {fmt(med(ai))} | {fmt(med(iv))} | {fmt(med(ifp), 1)} | {rng(spr)} | {rng(sps)} | "
                      f"{rng(pk)} | {rng(st)} | {rng(gp)} | {rng(ow)} | {rng(over)} | "
                      f"{rng(gb)}{' (' + gsrc + ')' if gsrc else ''} | {rng(op)} |")
        (a_t, a_f, a_p), (b_t, b_f, b_p) = meds["A"], meds["B"]
        if a_t and b_t:
            d = 100.0 * (b_t - a_t) / a_t
            flag = []
            if d > 3.0:
                flag.append("B SLOWER THAN A BY >3 %")
            if a_f and b_f and a_f >= 30 > b_f:
                flag.append("B DROPS UNDER 30 fps WHERE A HOLDS")
            # Total Frame includes the frame limiter's wait (the orangepi
            # preset targets 30 fps), so a small cost can hide under it. The
            # Player Movement phase is where the collision rays run: its
            # B-minus-A delta, as a share of A's frame, is the uncapped cost.
            pm_txt = ""
            if a_p is not None and b_p is not None:
                pm_share = 100.0 * (b_p - a_p) / a_t
                pm_txt = f"; Player Movement A {a_p:,.0f} us, B {b_p:,.0f} us ({pm_share:+.2f} % of A's frame)"
                if pm_share > 3.0:
                    flag.append("B's collision phase costs >3 % of a frame more than A's")
            verdicts.append(f"- {z}: B vs A median moving frame time {d:+.2f} % "
                            f"(A {a_t:,.0f} us / {a_f:.1f} fps, B {b_t:,.0f} us / {b_f:.1f} fps){pm_txt}"
                            f"{' -- ' + '; '.join(flag) if flag else ''}")
        else:
            verdicts.append(f"- {z}: no moving-window blocks on one side -- no verdict")
    md.append("\n" + "\n".join(verdicts))
    md.append(f"\n{CATEGORY} owner rows at steady_60s (per leg):\n")
    md.append("| zone | leg | owner rows |")
    md.append("|---|---|---|")
    for z in zones:
        for side in ("A", "B"):
            for leg, r in data.get((z, side), []):
                rows = r["owners"].get("steady_60s", {})
                txt = ", ".join(f"{k.replace('startup.', '')} {v:,}" for k, v in sorted(rows.items(), key=lambda kv: -kv[1]) if k != "TOTAL")
                md.append(f"| {z} | {leg} | {txt or 'n/a'} |")
    text = "\n".join(md) + "\n"
    if a.out:
        Path(a.out).write_text(text)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
