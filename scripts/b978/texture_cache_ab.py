#!/usr/bin/env python3
"""B978: per-zone texture-cache + cpu p95 before/after the 51 -> 21 MiB ceiling.

Runs build-gles2/bin/gles2_zone_benchmark with the gate's own invocation
(same as visual_gate.py's run_benchmark) for each of the five gate zones,
with --texture-memory-bytes N (0 = preset default).

Writes a TSV row per (side, zone):
  side  zone  textureCacheBytesUsed  textureCacheBytesLimit  textureCount
  entries  resident  evictions  restores  restore_failures  pinned
  cpu_p95_ms  exit
and keeps the benchmark stdout per run (for the [texture-cache] line).
"""
import csv
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BENCH = REPO / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = REPO / "data" / "eq_client"
OUT = Path(sys.argv[1])
SIDE = sys.argv[2]
TMB = int(sys.argv[3]) if len(sys.argv) > 3 else 0  # 0 = preset

OUT.mkdir(parents=True, exist_ok=True)
man = json.load(open(REPO / "tests" / "goldens" / "manifest.json"))
bench_cfg = man["benchmark"]
prod_args = man.get("productionConfig", {}).get("args", [])

rows = []
for zc in man["zones"]:
    zone = zc["zone"]
    frames = zc.get("designatedFrames", [0])
    out_dir = OUT / f"{SIDE}_{zone}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(BENCH), "--zone", zone, "--eq-path", str(EQ_PATH),
        "--frames", str(bench_cfg["frames"]), "--warmup", str(bench_cfg["warmup"]),
        "--res", str(bench_cfg["resolution"]["width"]), str(bench_cfg["resolution"]["height"]),
        "--dump-frames", ",".join(str(f) for f in frames),
        "--dump-png-prefix", str(out_dir / zone),
        "--csv", str(out_dir / f"{zone}.csv"),
        "--memory-json", str(out_dir / f"{zone}_mem.json"),
        "--occluder-budget", "16",
    ]
    if TMB:
        cmd += ["--texture-memory-bytes", str(TMB)]
    cmd += list(prod_args) + list(zc.get("extraArgs", []))
    print("RUN", SIDE, zone, "tmb=", TMB or "preset", flush=True)
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    (out_dir / "bench.stdout").write_text(proc.stdout)
    (out_dir / "bench.stderr").write_text(proc.stderr)
    if proc.returncode != 0:
        rows.append([SIDE, zone, "FAIL", proc.returncode])
        print("  BENCH FAILED rc=%d\n%s" % (proc.returncode, proc.stderr[-2000:]))
        continue
    mem = json.load(open(out_dir / f"{zone}_mem.json"))
    csv_rows = list(csv.DictReader(open(out_dir / f"{zone}.csv")))
    cpus = [float(r["cpu_proc_ms"]) for r in csv_rows if "cpu_proc_ms" in r]
    cpus.sort()
    p95 = cpus[int(len(cpus) * 0.95)] if cpus else float("nan")
    m = re.search(r"\[texture-cache\] entries=(\d+) resident=(\d+) evictions=(\d+) "
                  r"restores=(\d+) restore_failures=(\d+) pinned=(\d+) used=(\d+) limit=(\d+)",
                  proc.stdout)
    tc = m.groups() if m else ["?"] * 8
    row = [SIDE, zone, mem.get("textureCacheBytesUsed"), mem.get("textureCacheBytesLimit"),
           mem.get("textureCount"), tc[0], tc[1], tc[2], tc[3], tc[4], tc[5],
           "%.3f" % p95, proc.returncode]
    rows.append(row)
    print("  used=%s count=%s entries=%s resident=%s evic=%s rest=%s restfail=%s pinned=%s p95=%.3f"
          % (row[2], row[4], *tc[:6], p95))

hdr = ["side", "zone", "textureCacheBytesUsed", "textureCacheBytesLimit", "textureCount",
       "entries", "resident", "evictions", "restores", "restore_failures", "pinned",
       "cpu_p95_ms", "exit"]
tsv = OUT / f"{SIDE}_texture_cache.tsv"
with open(tsv, "w") as f:
    f.write("\t".join(hdr) + "\n")
    for r in rows:
        f.write("\t".join(str(x) for x in r) + "\n")
print("WROTE", tsv)
