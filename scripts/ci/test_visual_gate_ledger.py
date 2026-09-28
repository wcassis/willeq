#!/usr/bin/env python3
"""T491 -- unit test for visual_gate.py's memory-ledger assertions.

Drives assert_memory_ledger() and report_rss_target_gap() on synthetic memory
dicts (no benchmark run, no build artefact): a JSON without the ledger object
is a failure, hooks OFF is a failure, an unaccounted share over the bound is a
failure, allocator slack over ITS (looser) bound is a failure, a category over
its configured budget is a failure, a zero budget is skipped, RssAnon over the
256 MB interim ceiling is a failure, and being over the 64 MB target is only
PRINTED. Red before T491 (the functions do not exist), green after.
"""
import importlib.util
import io
import sys
from contextlib import redirect_stdout
from pathlib import Path

GATE = Path(__file__).resolve().parent / "visual_gate.py"

MIB = 1024 * 1024


def load_gate():
    spec = importlib.util.spec_from_file_location("visual_gate", GATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def ledger_dict(rss_anon, ledger_total, cats, slack=0, uncharged=0):
    """Build a ledger object exactly the way memory_ledger.h's snapshot() does:
    four buckets that COVER RssAnon (they do not partition it -- glibc's arena
    can hold non-resident free pages), with `unaccounted` = RssAnon - arena,
    the non-heap anonymous remainder."""
    in_use = ledger_total + uncharged
    arena = in_use + slack
    unattributed = max(0, rss_anon - ledger_total)
    unaccounted = max(0, rss_anon - arena)
    return {
        "mallocArenaBytes": arena,
        "mallocInUseBytes": in_use,
        "heapUnchargedBytes": uncharged,
        "ledgerEnabled": True,
        "rssAnonBytes": rss_anon,
        "ledgerTotalBytes": ledger_total,
        "unattributedBytes": unattributed,
        "unattributedPct": 100.0 * unattributed / rss_anon,
        "unaccountedBytes": unaccounted,
        "unaccountedPct": 100.0 * unaccounted / rss_anon,
        "mallocSlackBytes": slack,
        "rssTargetBytes": 64 * MIB,
        "rssCeilingBytes": 256 * MIB,
        "overTargetBytes": max(0, rss_anon - 64 * MIB),
        "overCeilingBytes": max(0, rss_anon - 256 * MIB),
        "categories": {k: {"live": v, "peak": v, "allocs": 1} for k, v in cats.items()},
    }


def run(gate, mem, **kw):
    """Call the assertion with stdout captured; return (failures, printed)."""
    failures, buf = [], io.StringIO()
    with redirect_stdout(buf):
        gate.assert_memory_ledger("t", mem, failures, **kw)
    return failures, buf.getvalue()


def main():
    gate = load_gate()
    assert hasattr(gate, "assert_memory_ledger"), "visual_gate.py has no assert_memory_ledger (pre-T491)"
    assert hasattr(gate, "report_rss_target_gap"), "visual_gate.py has no report_rss_target_gap (pre-T491 addendum)"
    assert isinstance(gate.LEDGER_UNATTRIBUTED_MAX_PCT, float)
    assert isinstance(gate.LEDGER_SLACK_MAX_PCT, float)
    assert isinstance(gate.LEDGER_NONHEAP_MAX_BYTES, int)
    assert gate.RSS_CEILING_BYTES == 256 * MIB
    assert gate.RSS_TARGET_BYTES == 64 * MIB

    # 1. no ledger object -> failure
    f, _ = run(gate, {"totalEstimate": 1})
    assert len(f) == 1 and 'no "ledger"' in f[0], f

    # 2. hooks off -> failure
    f, _ = run(gate, {"ledger": {"ledgerEnabled": False}})
    assert len(f) == 1 and "OFF" in f[0], f

    # 3. clean (inside the ceiling, inside every bound) -> no failure
    mem = {
        "ledger": ledger_dict(100 * MIB, 95 * MIB, {"zone_parse": 10 * MIB, "ui": MIB}, slack=3 * MIB),
        "ledgerBudgets": {"zone_parse": 16 * MIB, "ui": 0},
    }
    f, out = run(gate, mem, max_unattributed_pct=10.0)
    assert f == [], f
    assert "NO BUDGET CONFIGURED for ui" in out, out

    # 4. THE ATTRIBUTION RATCHET: bytes charged to no category. This, not
    #    "RssAnon - ledger", is what a code change in this repo moves.
    f, _ = run(gate, {"ledger": ledger_dict(100 * MIB, 100 * MIB,
                                            {"zone_parse": 90 * MIB,
                                             "unattributed": 10 * MIB})},
               max_unattributed_pct=1.0)
    assert any("charged to no category" in x and "10.000%" in x
               and "over the 1.00% bound" in x and "MemoryScope" in x for x in f), f

    # 4b. the SAME RssAnon/ledger gap with everything OWNED is clean: the gap
    #     is glibc's free lists, which no attribution work can move. That
    #     distinction is the whole point of the T491 split.
    f, _ = run(gate, {"ledger": ledger_dict(100 * MIB, 80 * MIB,
                                            {"zone_parse": 80 * MIB}, slack=20 * MIB)},
               max_unattributed_pct=1.0)
    assert f == [], f

    # 4c. slack past ITS own (deliberately loose) bound is still a failure
    f, _ = run(gate, {"ledger": ledger_dict(100 * MIB, 50 * MIB,
                                            {"zone_parse": 50 * MIB}, slack=50 * MIB)},
               max_unattributed_pct=1.0)
    assert any("allocator slack" in x and "50.0%" in x for x in f), f

    # 4d. the smaps census: the ledger must cover every RESIDENT heap byte,
    #     and non-heap anon (stacks, mmap, JIT) is bounded in BYTES.
    mem = {"ledger": ledger_dict(100 * MIB, 60 * MIB, {"zone_parse": 60 * MIB}, slack=10 * MIB),
           "mappings": {"available": True, "totalAnonBytes": 100 * MIB, "mappings": 3,
                        "owners": [{"name": "[heap]", "anonBytes": 90 * MIB, "mappings": 1},
                                   {"name": "so:libLLVM.so.20.1", "anonBytes": 10 * MIB,
                                    "mappings": 4}]}}
    f, out = run(gate, mem)
    assert any("does not cover the resident heap" in x for x in f), f
    assert "[mappings]" in out and "so:libLLVM.so.20.1" in out, out

    # 4e. non-heap anon over the byte bound
    nonheap = gate.LEDGER_NONHEAP_MAX_BYTES + MIB
    mem = {"ledger": ledger_dict(200 * MIB, 190 * MIB, {"zone_parse": 190 * MIB}, slack=10 * MIB),
           "mappings": {"available": True, "totalAnonBytes": 200 * MIB, "mappings": 2,
                        "owners": [{"name": "[heap]", "anonBytes": 200 * MIB - nonheap,
                                    "mappings": 1},
                                   {"name": "[anon]", "anonBytes": nonheap, "mappings": 290}]}}
    f, _ = run(gate, mem)
    assert any("non-heap anonymous RSS" in x for x in f), f

    # 5. category over budget -> failure naming the category
    mem = {"ledger": ledger_dict(100 * MIB, 99 * MIB, {"zone_parse": 17 * MIB}, slack=MIB),
           "ledgerBudgets": {"zone_parse": 16 * MIB, "ui": 0}}
    f, _ = run(gate, mem)
    assert any("zone_parse" in x and "exceeds budget" in x for x in f), f

    # 6. default bound is the module constant, not a literal
    over = int(100 * MIB * (gate.LEDGER_UNATTRIBUTED_MAX_PCT + 0.5) / 100)
    mem = {"ledger": ledger_dict(100 * MIB, 100 * MIB,
                                 {"zone_parse": 100 * MIB - over, "unattributed": over})}
    f, _ = run(gate, mem)
    assert any("charged to no category" in x for x in f), f

    # 7. T491 addendum: over the 64 MB target is PRINTED with the gap and the
    #    biggest buckets first, and is NOT a failure on its own.
    mem = {"ledger": ledger_dict(100 * MIB, 95 * MIB,
                                 {"zone_mesh": 60 * MIB, "zone_parse": 30 * MIB, "ui": 5 * MIB},
                                 slack=3 * MIB)}
    f, out = run(gate, mem)
    assert f == [], f
    assert "must shrink 36.0 MiB" in out, out
    order = [ln.split()[0] for ln in out.splitlines() if ln.startswith("      ")]
    assert order[:3] == ["zone_mesh", "zone_parse", "ui"], order

    # 8. T491 addendum: over the 256 MB interim ceiling IS a failure
    mem = {"ledger": ledger_dict(300 * MIB, 295 * MIB, {"zone_mesh": 290 * MIB}, slack=3 * MIB)}
    f, _ = run(gate, mem)
    assert any("interim ceiling" in x and "44.0 MiB" in x for x in f), f

    # 9. under the target says so and prints no bucket table
    mem = {"ledger": ledger_dict(40 * MIB, 38 * MIB, {"zone_mesh": 30 * MIB}, slack=MIB)}
    f, out = run(gate, mem)
    assert f == [], f
    assert "WITHIN the 64 MiB target" in out, out

    # 10. B813 revert (Will, 2026-09-28): --zones selects zones and nothing
    #     else. An unknown or empty list is an error, and the filter applies to
    #     BOTH the golden-image zones and memoryCoverage.zones.
    manifest = {"zones": [{"zone": "a"}, {"zone": "b"}, {"zone": "c"}],
                "memoryCoverage": {"zones": [{"zone": "a"}, {"zone": "c"}]},
                "benchmark": {}}
    sel, skipped = gate.select_zones(manifest, "c,a")
    assert [z["zone"] for z in sel["zones"]] == ["a", "c"], sel
    assert [z["zone"] for z in sel["memoryCoverage"]["zones"]] == ["a", "c"], sel
    assert skipped == ["b"], skipped
    assert [z["zone"] for z in manifest["zones"]] == ["a", "b", "c"], "input mutated"
    for bad in ("", "a,nope"):
        try:
            gate.select_zones(manifest, bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"select_zones accepted {bad!r}")

    # 11. The two CI gate jobs' --zones lists PARTITION the manifest's zones:
    #     every zone is verified by exactly one job, so no zone can leave CI
    #     by editing one list. (The blocking/non-blocking split is Will's
    #     2026-09-28 ruling; docs/decisions.md, docs/ci.md.)
    import json
    import re
    root = GATE.parents[2]
    ci_text = (root / ".gitlab-ci.yml").read_text()
    lists = re.findall(r"visual_gate\.sh --zones ([A-Za-z0-9_,]+)", ci_text)
    assert len(lists) == 2, f"expected exactly two --zones gate invocations in .gitlab-ci.yml, got {lists}"
    ci_zones = [z for lst in lists for z in lst.split(",")]
    real = json.loads((root / "tests" / "goldens" / "manifest.json").read_text())
    manifest_zones = [z["zone"] for z in real["zones"]]
    coverage_zones = [z["zone"] for z in real.get("memoryCoverage", {}).get("zones", [])]
    assert len(ci_zones) == len(set(ci_zones)), f"a zone is in both CI gate jobs: {lists}"
    assert sorted(ci_zones) == sorted(manifest_zones), (lists, manifest_zones)
    assert set(coverage_zones) <= set(ci_zones), (lists, coverage_zones)

    print("test_visual_gate_ledger: 16 checks OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
