#!/usr/bin/env python3
"""T491 -- unit test for visual_gate.py's memory-ledger assertions.

Drives assert_memory_ledger() on synthetic memory dicts (no benchmark run,
no build artefact): a JSON without the ledger object is a failure, hooks OFF
is a failure, an unattributed share over the bound is a failure, a category
over its configured budget is a failure, a zero budget is skipped, and a
clean dict produces no failure. Red before T491 (the function does not
exist), green after.
"""
import importlib.util
import sys
from pathlib import Path

GATE = Path(__file__).resolve().parent / "visual_gate.py"


def load_gate():
    spec = importlib.util.spec_from_file_location("visual_gate", GATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def ledger_dict(rss_anon, ledger_total, cats):
    return {
        "ledgerEnabled": True,
        "rssAnonBytes": rss_anon,
        "ledgerTotalBytes": ledger_total,
        "unattributedBytes": max(0, rss_anon - ledger_total),
        "unattributedPct": 100.0 * max(0, rss_anon - ledger_total) / rss_anon,
        "mallocSlackBytes": 0,
        "categories": {k: {"live": v, "peak": v, "allocs": 1} for k, v in cats.items()},
    }


def main():
    gate = load_gate()
    assert hasattr(gate, "assert_memory_ledger"), "visual_gate.py has no assert_memory_ledger (pre-T491)"
    assert isinstance(gate.LEDGER_UNATTRIBUTED_MAX_PCT, float)

    # 1. no ledger object -> failure
    f = []
    gate.assert_memory_ledger("t1", {"totalEstimate": 1}, f)
    assert len(f) == 1 and "no \"ledger\"" in f[0], f

    # 2. hooks off -> failure
    f = []
    gate.assert_memory_ledger("t2", {"ledger": {"ledgerEnabled": False}}, f)
    assert len(f) == 1 and "OFF" in f[0], f

    # 3. clean -> no failure, budgets honoured
    mem = {
        "ledger": ledger_dict(100_000_000, 95_000_000, {"zone_parse": 10_000_000, "ui": 1_000_000}),
        "ledgerBudgets": {"zone_parse": 16_000_000, "ui": 0},
    }
    f = []
    gate.assert_memory_ledger("t3", mem, f, max_unattributed_pct=10.0)
    assert f == [], f

    # 4. unattributed over the bound -> failure naming the percentage
    mem["ledger"] = ledger_dict(100_000_000, 80_000_000, {"zone_parse": 10_000_000})
    f = []
    gate.assert_memory_ledger("t4", mem, f, max_unattributed_pct=10.0)
    assert len(f) == 1 and "20.0%" in f[0] and "over the 10.0% bound" in f[0], f

    # 5. category over budget -> failure naming the category
    mem["ledger"] = ledger_dict(100_000_000, 99_000_000, {"zone_parse": 17_000_000})
    f = []
    gate.assert_memory_ledger("t5", mem, f, max_unattributed_pct=10.0)
    assert len(f) == 1 and "zone_parse" in f[0] and "exceeds budget" in f[0], f

    # 6. default bound is the module constant
    mem["ledger"] = ledger_dict(100_000_000, 100_000_000 - int(gate.LEDGER_UNATTRIBUTED_MAX_PCT * 1e6) - 1,
                                {"zone_parse": 1})
    f = []
    gate.assert_memory_ledger("t6", mem, f)
    assert len(f) == 1, f

    print("test_visual_gate_ledger: 6 checks OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
