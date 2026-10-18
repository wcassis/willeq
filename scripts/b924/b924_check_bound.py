#!/usr/bin/env python3
"""B924 / GitLab #243: assert the gpu_driver growth over a hold is bounded.

Reads the client's OWN [memledger] lines (client.log in a b924_hold.sh run
dir) and compares:

    growth = gpu_driver(last idle point) - gpu_driver(ready)

against the bound. Writes bound_check.txt with every number and its source
line, and exits 0 (within bound) or 1 (over the bound, or no ready point).

The bound is passed as argv[2] (bytes). Its basis -- the measured growth of
the worst of the three 5-minute holds plus a 25 % margin -- is stated in
refactor/B924/README.md.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r"\[memledger\] (.*)")
KV = re.compile(r"(\w+)=([^\s]+)")
STAMP = re.compile(r"^\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+\] \[[A-Z]+ \] \[\w+\] ")


def live(val):
    """'live/peak' category value -> the live half (int)."""
    return int(val.split("/")[0]) if val else 0


def main():
    run = Path(sys.argv[1])
    bound = int(sys.argv[2])
    rows = []
    for raw in (run / "client.log").read_text(errors="replace").splitlines():
        m = LINE.match(STAMP.sub("", raw).strip())
        if m:
            rows.append(dict(KV.findall(m.group(1))))
    ready = next((r for r in rows if r.get("point") == "ready"), None)
    idle = [r for r in rows if r.get("point", "").startswith("idle_")]
    last = idle[-1] if idle else (rows[-1] if rows else None)
    out = [f"bound_bytes={bound}", f"ready_point={'present' if ready else 'MISSING'}"]
    if not ready or not last:
        (run / "bound_check.txt").write_text("\n".join(out) + "\n")
        print("\n".join(out))
        sys.exit(1)
    g0, g1 = live(ready.get("gpu_driver")), live(last.get("gpu_driver"))
    f0, f1 = int(ready.get("frames", 0)), int(last.get("frames", 0))
    growth = g1 - g0
    out += [
        f"gpu_driver_ready={g0}",
        f"gpu_driver_last={g1} (point={last['point']})",
        f"growth={growth}",
        f"frames_ready={f0} frames_last={f1} (delta={f1 - f0})",
        f"growth_per_1000_frames={1000 * growth // (f1 - f0) if f1 > f0 else 0}",
        f"verdict={'PASS' if growth <= bound else 'FAIL'}",
    ]
    (run / "bound_check.txt").write_text("\n".join(out) + "\n")
    print("\n".join(out))
    sys.exit(0 if growth <= bound else 1)


if __name__ == "__main__":
    main()
