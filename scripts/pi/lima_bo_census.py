#!/usr/bin/env python3
"""B955 (GitLab #273): fold a scripts/pi/lima_bo_census.c log into the set of
Lima GEM buffer objects LIVE at an instant, so the gpu_buffers row's residual
(kernel GEM bytes minus the renderer's logical tally) is named per BO.

usage: lima_bo_census.py LOG [--at MONO_SECONDS] [--json]
  --at   cut the log at this CLOCK_MONOTONIC time (the shim's timestamps);
         default: the whole log (the live set at the end)

Output: live BOs by kind (regular / heap -- a heap BO's size is a MAXIMUM, its
pages grow on GPU demand) and by size bucket, the
page-rounded bytes (what the kernel pins: size rounded up to 4 KiB), and the
PRIME imports (buffers another device allocated: the display's scanout, CMA).
"""
import argparse
import json
import sys

PAGE = 4096
BUCKETS = [(4096, "<=4K"), (16384, "4K-16K"), (65536, "16K-64K"), (262144, "64K-256K"),
           (1048576, "256K-1M"), (4194304, "1M-4M"), (float("inf"), ">4M")]


def fold(path, at=None):
    live = {}
    imports = set()
    created = closed = 0
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 3:
                continue
            t = float(p[0])
            if at is not None and t > at:
                break
            if p[1] == "C" and len(p) >= 6:
                live[(p[5], p[2])] = (int(p[3]), int(p[4]))
                created += 1
            elif p[1] == "X" and len(p) >= 4:
                key = (p[3], p[2])
                if live.pop(key, None) is not None:
                    closed += 1
                imports.discard(key)
            elif p[1] == "I" and len(p) >= 4:
                imports.add((p[3], p[2]))
    return live, imports, created, closed


def summarize(live, imports, created, closed):
    out = {"created": created, "closed": closed, "live": len(live), "prime_imports_live": len(imports)}
    for kind, want in (("regular", 0), ("heap", 1)):
        sel = [s for s, fl in live.values() if (fl & 1) == want]
        out[kind] = {"count": len(sel), "bytes": sum(sel),
                     "page_rounded_bytes": sum((s + PAGE - 1) // PAGE * PAGE for s in sel)}
    hist = {name: [0, 0] for _, name in BUCKETS}
    for s, fl in live.values():
        if fl & 1:
            continue
        for lim, name in BUCKETS:
            if s <= lim:
                hist[name][0] += 1
                hist[name][1] += s
                break
    out["regular_by_size"] = {k: {"count": v[0], "bytes": v[1]} for k, v in hist.items()}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--at", type=float, default=None)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    s = summarize(*fold(a.log, a.at))
    if a.json:
        json.dump(s, sys.stdout, indent=1)
        print()
        return 0
    print("created %d closed %d live %d (prime imports live %d)"
          % (s["created"], s["closed"], s["live"], s["prime_imports_live"]))
    for kind in ("regular", "heap"):
        k = s[kind]
        print("  %-8s %6d BOs %12d B (page-rounded %12d B)" % (kind, k["count"], k["bytes"], k["page_rounded_bytes"]))
    print("  regular BOs by size:")
    for name, v in s["regular_by_size"].items():
        print("    %-9s %6d BOs %12d B" % (name, v["count"], v["bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
