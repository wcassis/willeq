#!/usr/bin/env python3
"""B923 / GitLab #242 -- the repeatable budget check for a client-ledger capture.

`scripts/client_ledger_table.py check` is the assertion every budgeted
memoryLedgerBudgets category of a `scripts/client_ledger_capture.sh` evidence
dir is asserted against at `ready` and at `steady_60s`. This script pins that
contract with a REPEATABLE ctest (label `hygiene`, no server, no display, no
binary): it builds a capture-shaped fixture in a temp dir from the client's
own log-line format (memory_ledger.cpp toLogLine, the B909 table values) and
drives `check` through it.

Three cases, one per contract the live `ClientLedgerLive` ctest and the CI
`client-ledger-live` job rely on:

  1. every budgeted category inside its orangepi budget  -> exit 0;
  2. ONE client-only owner's category over its budget
     (the client allocates it; the benchmark the `gate` job measures never
     does -- B923's whole point)                            -> exit 1;
  3. a run without a `steady_60s` point (incomplete)       -> exit 3.

The budgets are read by `check` itself from config/constrained_presets.json
(the orangepi preset), never from this file: this test is red if the preset's
budget table is renamed or a budgeted category drops out of it, and it stays
green while the check and the preset agree. A client-only category is
client-only BY CONSTRUCTION here: the fixture names the owner row, and the
benchmark's memory JSON (the `gate` job's input) carries no such owner at all.
"""

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TABLE = REPO / "scripts" / "client_ledger_table.py"

ZONE = "iceclad"  # the ClientLedgerLive zone (Velious)
MODE = "spatial"
BUDGETS = json.load(open(REPO / "config" / "constrained_presets.json"))


def budget_table():
    def find(o):
        if isinstance(o, dict):
            if "orangepi" in o and isinstance(o["orangepi"], dict) \
                    and "memoryLedgerBudgets" in o["orangepi"]:
                return o["orangepi"]["memoryLedgerBudgets"]
            for v in o.values():
                r = find(v)
                if r:
                    return r
        return None
    b = find(BUDGETS)
    assert b, "orangepi memoryLedgerBudgets missing from constrained_presets.json"
    return b


# The B909 measured values at `ready` (client, after the collision fix):
# spatial 2,152,000 -- inside its 4,194,304 budget in iceclad. The fixture
# cites them as its one non-zero zone category; every other budgeted category
# is zero, which is a live ledger line no different (a category the client
# never charged prints 0).
CATEGORY_LIVE_AT_READY = {"spatial": 2_152_000}
# steady_60s is the point the assertion must survive to: one more point of
# the same shape, a few kB higher, as B909's own runs show.
CATEGORY_LIVE_AT_STEADY = {"spatial": 2_152_032}
RSS = 150_000_000  # a plausible x86 RssAnon (the check does not read it)


def ledger_line(point, zone, cats):
    # toLogLine()'s contract: the categories ride as "<name>=<live>/<peak>"
    # after "allocs=<n>". client_ledger_table.py parse_run() reads exactly
    # this shape (LEDGER_RE + CAT_RE).
    tail = " ".join(f"{c}={v}/{v}" for c, v in cats.items())
    return (f"[2026-10-02 00:00:00.000] [INFO] [MOD_MAIN] "
            f"[memledger] point={point} zone={zone} seq=1 enabled=1 "
            f"rss_anon={RSS} rss_file=0 rss_shmem=0 vm_rss={RSS} "
            f"ledger=0 ledger_peak=0 unattributed=0 unattributed_pct=0.0 "
            f"malloc_arena=0 malloc_inuse=0 malloc_slack=0 heap_uncharged=0 "
            f"unaccounted=0 target=67108864 gpu_driver=0 owned={RSS} "
            f"over_target=0 over_target_rss_anon=0 gpu_buffers=0 "
            f"gpu_buffers_src=none owned_plus_gpu_buffers={RSS} "
            f"ceiling=268435456 over_ceiling=0 allocs=0 {tail}\n")


def write_run(root, zone, cats_by_point, owner_row=None):
    d = root / zone / MODE
    d.mkdir(parents=True, exist_ok=True)
    lines = []
    for point in ("zone_load_complete", "ready", "steady_60s"):
        if point in cats_by_point:
            lines.append(ledger_line(point, zone, cats_by_point[point]))
    if owner_row:
        lines.append(f"[2026-10-02 00:00:00.000] [INFO] [MOD_MAIN] "
                     f"[memledger-startup] point={owner_row[0]} "
                     f"owner={owner_row[1]} live={owner_row[2]} blocks=3\n")
    (d / "client.log").write_text("".join(lines))
    (d / "result.txt").write_text(
        f"zone={zone} mode={MODE} rc=0 wall_s=200 ready=1 steady_60s=1 "
        f"landed=y watchdog=none zone_link_lost=no\n")


def run_check(root):
    return subprocess.run(
        [sys.executable, str(TABLE), "check", "--run", str(root)],
        capture_output=True, text=True)


def main():
    budgets = budget_table()
    with tempfile.TemporaryDirectory(prefix="b923-client-ledger-") as tmp:
        root = Path(tmp)

        # Case 1: every budgeted category inside its budget -> 0.
        write_run(root, ZONE,
                  {"zone_load_complete": CATEGORY_LIVE_AT_READY,
                   "ready": CATEGORY_LIVE_AT_READY,
                   "steady_60s": CATEGORY_LIVE_AT_STEADY},
                  owner_row=("ready", "sp.region_bounds", 1_717_184))
        p = run_check(root)
        assert p.returncode == 0, (
            f"in-budget capture must pass (exit 0), got {p.returncode}:\n"
            f"{p.stdout}{p.stderr}")
        checked = p.stdout.count(f" {ZONE}/{MODE} ")
        assert checked == len(budgets) * 2, (
            f"check must assert every budgeted category at both points "
            f"({len(budgets)} x 2), saw {checked} lines:\n{p.stdout}")

        # Case 2: a CLIENT-ONLY owner's category over its budget -> 1.
        # The client allocates `spatial`'s collision/occluder structures
        # (sp.* rows); the benchmark the gate job measures never builds them
        # and its memory JSON carries no such owner. Bumping one client
        # category past its budget must turn the check red.
        over = dict(CATEGORY_LIVE_AT_READY)
        over["spatial"] = budgets["spatial"] + 1
        root2 = Path(tmp) / "over"
        write_run(root2, ZONE,
                  {"ready": over, "steady_60s": over},
                  owner_row=("ready", "sp.region_bounds", 1_717_184))
        p = run_check(root2)
        assert p.returncode == 1, (
            f"an over-budget client-only category must fail (exit 1), "
            f"got {p.returncode}:\n{p.stdout}{p.stderr}")
        assert f"{ZONE}/{MODE} ready spatial" in p.stdout and "OVER" in p.stdout

        # Case 3: a run without a steady_60s point -> 3 (incomplete).
        root3 = Path(tmp) / "incomplete"
        write_run(root3, ZONE, {"ready": CATEGORY_LIVE_AT_READY})
        p = run_check(root3)
        assert p.returncode == 3, (
            f"a run without steady_60s must fail (exit 3), got "
            f"{p.returncode}:\n{p.stdout}{p.stderr}")
        assert "no steady_60s point" in p.stdout

    print(f"B923 client-ledger check contract: 0 in-budget / 1 over-budget "
          f"(client-only owner) / 3 incomplete -- all three hold "
          f"({len(budgets)} budgeted categories asserted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
