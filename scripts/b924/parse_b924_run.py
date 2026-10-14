#!/usr/bin/env python3
"""B924 / GitLab #243: parse one B924 capture run directory.

Inputs (all written by b924_hold.sh under the run dir):
  client.log        the client's stdout+stderr (its own [memledger]/[memmap] lines)
  samples.tsv       t_s \t rss_anon_kb (2 s samples from /proc/<pid>/status)
  (the client itself prints the [memmap] smaps census at every idle point when
   WILLEQ_IDLE_MEMMAP=1 is set -- no separate /proc snapshots needed; an
   optional smaps_*.smaps file in the dir is diffed if present)

Outputs:
  ledger.tsv        point  zone  frames  rss_anon  gpu_driver  owned  unaccounted
                    (one row per [memledger] line, in log order)
  memmap_ready.tsv  owner  anon  rss  size  (the [memmap] block at point=ready)
  memmap_last.tsv   same, for the LAST [memmap] block in the log (idle_N)
  smaps_diff.tsv    the /proc smaps diff ready -> last snapshot, aggregated by
                    mapping NAME: name  anon_ready_kb  anon_last_kb  delta_kb
  summary.txt       peak RssAnon, gpu_driver at ready vs last, growth, frames
                    at ready vs last, growth per 1000 frames.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r"\[memledger\] (.*)")
KV = re.compile(r"(\w+)=([^\s]+)")
STAMP = re.compile(r"^\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+\] \[[A-Z]+ \] \[\w+\] ")
MAP = re.compile(r"\[memmap\] point=(\S+) owner=(\S+) anon=(\S+) rss=(\S+) size=(\S+) maps=(\S+)")


def log_lines(path: Path):
    for raw in path.read_text(errors="replace").splitlines():
        yield STAMP.sub("", raw)


def parse_log(path: Path):
    lines = {}
    memmap_blocks = []
    for raw in log_lines(path):
        m = LINE.match(raw.strip())
        if m:
            kv = dict(KV.findall(m.group(1)))
            lines.setdefault("ledger", []).append(kv)
        m = MAP.match(raw.strip())
        if m:
            memmap_blocks.append(m.groups())
    return lines, memmap_blocks


def memmap_block(rows):
    """group rows into (point, [rows]) tuples preserving order"""
    out = []
    for r in rows:
        if out and out[-1][0] == r[0]:
            out[-1][1].append(r)
        else:
            out.append([r[0], [r]])
    return out


def write_tsv(path: Path, header, rows):
    with path.open("w") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")


def parse_smaps(path: Path):
    """aggregate /proc smaps Anonymous by mapping name (pathname or [anon] kind)"""
    agg = {}
    cur = None
    for raw in path.read_text(errors="replace").splitlines():
        if not raw.startswith(" "):
            # "7f...-7f... rwxp ... 0 0 ... /usr/lib/..." or "[heap]" etc.
            parts = raw.split(None, 5)
            name = parts[5] if len(parts) >= 6 else ""
            if name.startswith("/"):
                cur = "so:" + name.split("/")[-1]
            elif name == "[heap]":
                cur = "[heap]"
            elif name:
                cur = name              # [stack] [vvar] ...
            else:
                cur = "[anon]"
            agg.setdefault(cur, 0)
        elif raw.startswith("Anonymous:") and cur is not None:
            agg[cur] += int(raw.split()[1])
    return agg


def live(val):
    """'live/peak' category value -> the live half (int)."""
    return int(val.split("/")[0]) if val else 0


def memmap_timeseries(blocks):
    """(point, [rows]) blocks -> rows of (point, owner, anon, rss) for every
    point that has a census, in point order -- the growth-per-owner table."""
    out = []
    for point, rows in blocks:
        for r in rows:
            out.append((point, r[1], r[2], r[3]))
    return out


def main(run: Path):
    run = Path(run)
    ledger, memmap = parse_log(run / "client.log")
    rows = [r for r in ledger.get("ledger", [])]
    write_tsv(run / "ledger.tsv",
              ["point", "zone", "frames", "rss_anon", "rss_file", "gpu_driver", "owned", "unaccounted", "allocs"],
              [[r.get("point"), r.get("zone"), r.get("frames"), r.get("rss_anon"),
                r.get("rss_file"), r.get("gpu_driver"), r.get("owned"), r.get("unaccounted"), r.get("allocs")]
               for r in rows])
    blocks = memmap_block(memmap)
    for tag, b in (("ready", blocks[0]) if blocks else None,
                   ("last", blocks[-1]) if blocks else None):
        if b:
            write_tsv(run / f"memmap_{tag}.tsv",
                      ["owner", "anon", "rss", "size", "maps"],
                      [[r[1], r[2], r[3], r[4], r[5]] for r in b[1]])
    if blocks:
        write_tsv(run / "memmap_timeseries.tsv",
                  ["point", "owner", "anon", "rss"],
                  memmap_timeseries(blocks))
    # /proc smaps diff
    snaps = sorted(run.glob("smaps_*.smaps"), key=lambda p: int(p.stem.split("_")[1].split(".")[0]))
    if len(snaps) >= 2:
        first, last = parse_smaps(snaps[0]), parse_smaps(snaps[-1])
        names = sorted(set(first) | set(last), key=lambda n: -(first.get(n, 0) + last.get(n, 0)))
        write_tsv(run / "smaps_diff.tsv", ["name", "anon_ready_kb", "anon_last_kb", "delta_kb"],
                  [[n, first.get(n, 0), last.get(n, 0), last.get(n, 0) - first.get(n, 0)] for n in names])
    # summary
    ready = next((r for r in rows if r.get("point") == "ready"), {})
    idle = [r for r in rows if r.get("point", "").startswith("idle_")]
    lastpt = idle[-1] if idle else (rows[-1] if rows else {})
    peak = 0
    try:
        for line in (run / "samples.tsv").read_text().splitlines()[1:]:
            p = line.split("\t")
            if len(p) >= 2 and p[1].isdigit():
                peak = max(peak, int(p[1]))
    except FileNotFoundError:
        pass
    g0, g1 = live(ready.get("gpu_driver")), live(lastpt.get("gpu_driver"))
    f0, f1 = int(ready.get("frames", 0)), int(lastpt.get("frames", 0))
    s = [
        f"zone={ready.get('zone', lastpt.get('zone', '?'))}",
        f"peak_rss_anon_kb={peak}",
        f"gpu_driver_ready={g0}",
        f"gpu_driver_last={g1} (point={lastpt.get('point')})",
        f"gpu_driver_growth={g1 - g0}",
        f"frames_ready={f0}",
        f"frames_last={f1}",
        f"frames_delta={f1 - f0}",
    ]
    if f1 > f0:
        s.append(f"gpu_driver_growth_per_1000_frames={1000 * (g1 - g0) // (f1 - f0)}")
    (run / "summary.txt").write_text("\n".join(s) + "\n")
    print("\n".join(s))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
