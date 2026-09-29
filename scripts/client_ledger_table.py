#!/usr/bin/env python3
"""B909 / GitLab #229: the client-vs-benchmark memory ledger tables.

Two subcommands, both reading only files a real run wrote:

  bench  -- run gles2_zone_benchmark for each zone with the gate's OWN two
            invocations (scripts/ci/visual_gate.py run_benchmark() with the
            manifest's production args and poses, and
            run_memory_coverage_benchmark()), imported from visual_gate.py so
            the arguments cannot drift. A zone the manifest does not carry
            (dreadlands) runs with no camera poses and one dumped frame. The
            memory JSONs land in --out; nothing under tests/goldens is touched.

  check  -- the assertion a `live` ctest runs (Part 3): every budgeted
            category of every run in a capture dir, at `ready` and at
            `steady_60s`, against its orangepi budget. Exit 0 all inside,
            1 a category over budget, 3 a run without a steady_60s point.

  table  -- parse a scripts/client_ledger_capture.sh evidence dir (the
            client's own [memledger] / [memledger-startup] / [memledger-trace]
            lines and the 2 s RssAnon samples) plus a `bench` dir, and write:
              points.tsv      every [memledger] line of every run, one row per
                              (zone, run, point, category)
              owners.tsv      the owner rows of each run's owner category
              trace.tsv       zone_parse call sites, symbolised with --bin
              rss.tsv         RssAnon per run: sampled peak, per point
              summary.md      the client-vs-benchmark table per budgeted
                              category, RssAnon vs 64 MB / 256 MB, owners
            Budgets come from config/constrained_presets.json (orangepi
            memoryLedgerBudgets), never from this file.
"""

import argparse
import bisect
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
MIB = 1048576.0
TARGET = 64 * 1024 * 1024
CEILING = 256 * 1024 * 1024
POINTS = ("zone_load_complete", "ready", "steady_60s")

LEDGER_RE = re.compile(r"\[memledger\] point=(\S+) zone=(\S+) .*?rss_anon=(\d+) ")
CAT_RE = re.compile(r" ([a-z_]+)=(\d+)/(\d+)")
OWNER_RE = re.compile(r"\[memledger-startup\] point=(\S+) owner=(\S+) live=(\d+) blocks=(\d+)")
TRACE_RE = re.compile(r"\[memledger-trace\] point=(\S+) site=(\d+) live=(\d+) peak=(\d+) "
                      r"blocks=(\d+) allocs=(\d+) stack=(.*)$")


def budgets():
    presets = json.load(open(REPO / "config" / "constrained_presets.json"))
    # the orangepi preset is the one the client runs under (--constrained orangepi)
    def find(o):
        if isinstance(o, dict):
            if "orangepi" in o and isinstance(o["orangepi"], dict) and "memoryLedgerBudgets" in o["orangepi"]:
                return o["orangepi"]["memoryLedgerBudgets"]
            for v in o.values():
                r = find(v)
                if r:
                    return r
        return None
    b = find(presets)
    if not b:
        sys.exit("no orangepi memoryLedgerBudgets in config/constrained_presets.json")
    return b


# ---------------------------------------------------------------- bench ----
def cmd_bench(a):
    sys.path.insert(0, str(REPO / "scripts" / "ci"))
    import visual_gate as vg  # the gate's own invocations
    vg.BENCH_BIN = Path(a.bench_bin).resolve()
    vg.EQ_PATH = Path(a.eq_path)
    vg.EQE1_PATH = Path(a.eqe1_path)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    manifest = vg.load_manifest()
    by_zone = {z["zone"]: z for z in manifest["zones"]}
    prod = manifest.get("productionConfig", {}).get("args", [])
    for zone in a.zones.split(","):
        zcfg = by_zone.get(zone, {"zone": zone, "designatedFrames": [0]})
        print(f"[bench] {zone}: golden invocation ({'manifest' if zone in by_zone else 'no manifest entry'})",
              flush=True)
        _, _, mem, stdout = vg.run_benchmark(zcfg, manifest["benchmark"], out, prod)
        (out / f"{zone}_golden_stdout.txt").write_text(stdout)
        print(f"[bench] {zone}: coverage invocation", flush=True)
        cov = vg.run_memory_coverage_benchmark(zone, out)
        if cov is None:
            sys.exit(f"coverage run skipped: no EQE1 tree at {a.eqe1_path}")
    return 0


def bench_live(bench_dir, zone):
    """{category: (golden_live, coverage_live)} from the two memory JSONs."""
    res = {}
    for kind, name in (("golden", f"{zone}_mem.json"), ("coverage", f"{zone}_coverage_mem.json")):
        p = Path(bench_dir) / name
        if not p.exists():
            continue
        cats = json.load(open(p)).get("ledger", {}).get("categories", {})
        for c, v in cats.items():
            res.setdefault(c, {})[kind] = v.get("live", 0)
    return res


# ---------------------------------------------------------------- table ----
class Symbols:
    """nm-based address -> function, for module-relative willeq+0x frames."""

    def __init__(self, binary):
        self.addrs, self.names = [], []
        if not binary:
            return
        out = subprocess.run(["nm", "-C", "--defined-only", "-n", binary],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split(" ", 2)
            if len(parts) == 3 and parts[1] in "tTwW":
                try:
                    self.addrs.append(int(parts[0], 16))
                    self.names.append(parts[2])
                except ValueError:
                    pass

    def name(self, off):
        i = bisect.bisect_right(self.addrs, off) - 1
        return self.names[i] if i >= 0 else "?"


BORING = re.compile(r"^(operator new|malloc|calloc|realloc|std::|__gnu_cxx|void std::|"
                    r"eqt::memory|irr::core::array|irr::core::irrAllocator|__libc)")


def site_owner(stack, syms):
    frames = []
    for tok in stack.split():
        m = re.match(r"(.+)\+0x([0-9a-f]+)$", tok)
        if not m:
            continue
        mod, off = m.group(1), int(m.group(2), 16)
        frames.append(syms.name(off) if mod == "willeq" else mod)
    first = next((f for f in frames if not BORING.match(f)), frames[0] if frames else "?")
    return first, " < ".join(frames[:6])


def parse_run(log):
    points, owners, traces = [], [], []
    for line in open(log, errors="replace"):
        m = LEDGER_RE.search(line)
        if m:
            point, zone, rss = m.group(1), m.group(2), int(m.group(3))
            tail = line.split(" allocs=", 1)[1] if " allocs=" in line else ""
            cats = {c: (int(l), int(p)) for c, l, p in CAT_RE.findall(" " + tail)}
            points.append((point, zone, rss, cats))
            continue
        m = OWNER_RE.search(line)
        if m:
            owners.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4))))
            continue
        m = TRACE_RE.search(line)
        if m:
            traces.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(5)), m.group(7)))
    return points, owners, traces


def last_point(points, name, zone):
    rows = [p for p in points if p[0] == name and p[1] == zone]
    return rows[-1] if rows else None


def cmd_table(a):
    run = Path(a.run)
    out = Path(a.out or run)
    bud = budgets()
    syms = Symbols(a.bin)
    zones = [d.name for d in sorted(run.iterdir()) if d.is_dir() and d.name != "server"]
    if a.zones:
        zones = [z for z in a.zones.split(",") if (run / z).is_dir()]
    pt_rows, ow_rows, tr_rows, rss_rows = [], [], [], []
    # client[zone][category][point] -> list of live values (one per run)
    client, peaks, runs_ok = {}, {}, {}
    owner_tab = {}   # (zone, category) -> {point: [(owner, live, blocks)]}
    for zone in zones:
        for mdir in sorted((run / zone).iterdir()):
            log = mdir / "client.log"
            if not log.exists():
                continue
            mode = mdir.name
            points, owners, traces = parse_run(log)
            result = (mdir / "result.txt").read_text().strip() if (mdir / "result.txt").exists() else ""
            ok = "steady_60s=1" in result and "landed=y" in result
            lost = next((l[1:24] for l in open(log, errors="replace") if "Zone connection lost" in l), None)
            runs_ok.setdefault(zone, []).append((mode, ok, lost))
            for point, z, rss, cats in points:
                for c, (live, peak) in cats.items():
                    pt_rows.append((zone, mode, point, z, rss, c, live, peak))
            for name in POINTS:
                p = last_point(points, name, zone)
                if p:
                    for c, (live, _) in p[3].items():
                        client.setdefault(zone, {}).setdefault(c, {}).setdefault(name, []).append(live)
            samples = []
            sp = mdir / "rss_samples.tsv"
            if sp.exists():
                for line in open(sp).read().splitlines()[1:]:
                    f = line.split("\t")
                    if len(f) >= 2 and f[1].isdigit():
                        samples.append(int(f[1]) * 1024)
            at = {n: (last_point(points, n, zone) or (None, None, None))[2] for n in POINTS}
            pt_max = max([p[2] for p in points if p[1] == zone] or [0])
            peak = max(samples + [pt_max]) if (samples or pt_max) else 0
            gpu = {n: ((last_point(points, n, zone) or (None, None, None, {}))[3].get("gpu_driver", (0, 0))[0])
                   for n in ("ready", "steady_60s")}
            rss_rows.append((zone, mode, peak, max(samples or [0]), pt_max,
                             at["zone_load_complete"], at["ready"], at["steady_60s"], len(samples),
                             gpu["ready"], gpu["steady_60s"]))
            peaks.setdefault(zone, []).append(rss_rows[-1])
            for point, owner, live, blocks in owners:
                if point in POINTS:
                    ow_rows.append((zone, mode, point, owner, live, blocks))
                    owner_tab.setdefault((zone, mode), {}).setdefault(point, []).append((owner, live, blocks))
            for point, site, live, blocks, stack in traces:
                if point not in POINTS:
                    continue
                first, chain = site_owner(stack, syms)
                tr_rows.append((zone, mode, point, site, live, blocks, first, chain))

    def w(name, header, rows):
        with open(out / name, "w") as f:
            f.write("\t".join(header) + "\n")
            for r in rows:
                f.write("\t".join(str(x) for x in r) + "\n")
    w("points.tsv", ["zone", "run", "point", "ledger_zone", "rss_anon", "category", "live", "peak"], pt_rows)
    w("owners.tsv", ["zone", "run", "point", "owner", "live", "blocks"], ow_rows)
    w("trace.tsv", ["zone", "run", "point", "site", "live", "blocks", "first_frame", "stack"], tr_rows)
    w("rss.tsv", ["zone", "run", "peak_rss_anon", "peak_sampled", "peak_at_points",
                  "rss_anon_zone_load_complete", "rss_anon_ready", "rss_anon_steady_60s", "samples",
                  "gpu_driver_ready", "gpu_driver_steady_60s"], rss_rows)

    md = []
    md.append(f"Source: `{run}` (client), `{a.bench}` (benchmark). Client value = the LARGEST of "
              "the runs for that zone (one run per owner mode), with the spread across runs; "
              "benchmark value = the larger of the gate's golden and coverage invocations "
              "(`ledger` = `benchmark_end`). % = of the orangepi budget.\n")
    for zone in zones:
        md.append(f"\n### {zone}\n")
        ok = runs_ok.get(zone, [])
        md.append("Runs: " + "; ".join(
            f"{m} {'OK' if o else 'INCOMPLETE'}" + (f" (zone link lost {lo})" if lo else "")
            for m, o, lo in ok) + "\n")
        md.append("| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | "
                  "run spread (steady) | benchmark | % | client steady − bench |")
        md.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
        bl = bench_live(a.bench, zone) if a.bench else {}
        for c, b in bud.items():
            vals = client.get(zone, {}).get(c, {})
            def mx(n):
                return max(vals[n]) if vals.get(n) else None
            zc, rd, st = mx("zone_load_complete"), mx("ready"), mx("steady_60s")
            spread = (max(vals["steady_60s"]) - min(vals["steady_60s"])) if vals.get("steady_60s") else None
            bv = bl.get(c, {})
            bench = max(bv.values()) if bv else None
            def fmt(v):
                return f"{v:,}" if v is not None else "n/a"
            def pct(v):
                return f"{100.0 * v / b:.1f}%" if (v is not None and b) else "n/a"
            flag = lambda v: " **OVER**" if (v is not None and b and v > b) else ""
            md.append(f"| `{c}` | {b:,} | {fmt(zc)} | {fmt(rd)}{flag(rd)} | {pct(rd)} | {fmt(st)}{flag(st)} | "
                      f"{pct(st)} | {fmt(spread)} | {fmt(bench)} | {pct(bench)} | "
                      f"{fmt(st - bench) if (st is not None and bench is not None) else 'n/a'} |")
        md.append("\n| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | "
                  "peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |")
        md.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
        for r in peaks.get(zone, []):
            md.append(f"| {r[1]} | {r[2]:,} ({r[2] / MIB:.1f} MiB) | {r[5] or 0:,} | {r[6] or 0:,} | "
                      f"{r[7] or 0:,} | {'+' if r[2] > TARGET else ''}{(r[2] - TARGET) / MIB:.1f} MiB | "
                      f"{'OVER' if r[2] > CEILING else 'under'} by {abs(r[2] - CEILING) / MIB:.1f} MiB | "
                      f"{r[9]:,} | {r[10]:,} |")
        for (z, mode), by_point in sorted(owner_tab.items()):
            if z != zone:
                continue
            md.append(f"\nOwner rows, `WILLEQ_LEDGER_OWNERS={mode}`:\n")
            md.append("| owner | ready live | blocks | steady_60s live | blocks |")
            md.append("|---|---:|---:|---:|---:|")
            rd = {o: (l, bk) for o, l, bk in by_point.get("ready", [])}
            st = {o: (l, bk) for o, l, bk in by_point.get("steady_60s", [])}
            for o in sorted(set(rd) | set(st), key=lambda o: -(st.get(o) or rd.get(o))[0]):
                r1, r2 = rd.get(o, (0, 0)), st.get(o, (0, 0))
                md.append(f"| {o} | {r1[0]:,} | {r1[1]:,} | {r2[0]:,} | {r2[1]:,} |")
        zt = [r for r in tr_rows if r[0] == zone and r[2] == "steady_60s"]
        if zt:
            agg = {}
            for r in zt:
                agg[r[6]] = (agg.get(r[6], (0, 0))[0] + r[4], agg.get(r[6], (0, 0))[1] + r[5])
            md.append("\n`zone_parse` call sites at steady_60s (`WILLEQ_LEDGER_TRACE=zone_parse:startup`, "
                      "first non-allocator frame; traced bytes only):\n")
            md.append("| first frame | live | blocks |")
            md.append("|---|---:|---:|")
            for k, (l, bk) in sorted(agg.items(), key=lambda kv: -kv[1][0])[:12]:
                md.append(f"| `{k[:110]}` | {l:,} | {bk:,} |")
    (out / "summary.md").write_text("\n".join(md) + "\n")
    print((out / "summary.md").read_text())
    return 0


def cmd_check(a):
    run = Path(a.run)
    bud = budgets()
    over, incomplete, checked = 0, 0, 0
    for log in sorted(run.glob("*/*/client.log")):
        zone, mode = log.parent.parent.name, log.parent.name
        points, _, _ = parse_run(log)
        for name in ("ready", "steady_60s"):
            p = last_point(points, name, zone)
            if not p:
                print(f"[check] {zone}/{mode}: no {name} point -- incomplete run")
                incomplete += 1
                continue
            for c, b in bud.items():
                live = p[3].get(c, (0, 0))[0]
                checked += 1
                verdict = "OVER" if live > b else "ok"
                if live > b:
                    over += 1
                print(f"[check] {zone}/{mode} {name} {c}: {live:,} / {b:,} ({100.0 * live / b:.1f}%) {verdict}")
    print(f"[check] {checked} category checks, {over} over budget, {incomplete} missing points")
    if incomplete or not checked:
        return 3
    return 1 if over else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bench")
    b.add_argument("--zones", default="gfaydark,dreadlands,iceclad,timorous")
    b.add_argument("--bench-bin", default=str(REPO / "build" / "bin" / "gles2_zone_benchmark"))
    b.add_argument("--eq-path", default="/mnt/projects/EverQuestP1999")
    b.add_argument("--eqe1-path", default="/mnt/projects/EverQuest_ETC1")
    b.add_argument("--out", required=True)
    t = sub.add_parser("table")
    t.add_argument("--run", required=True)
    t.add_argument("--bench")
    t.add_argument("--bin", help="the willeq binary that ran (symbolises trace frames)")
    t.add_argument("--zones")
    t.add_argument("--out")
    c = sub.add_parser("check")
    c.add_argument("--run", required=True)
    a = ap.parse_args()
    return {"bench": cmd_bench, "table": cmd_table, "check": cmd_check}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
