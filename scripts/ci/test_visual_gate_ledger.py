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
import re
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


def ledger_dict(rss_anon, ledger_total, cats, slack=0, uncharged=0, gpu_buffers=0,
                gpu_source="logical_in_gpu_driver"):
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
        # B933/B978: the target gap is on owned = RssAnon - gpu_driver + gpu_buffers
        # (the buffers only when outside RssAnon).
        "overTargetBytes": max(0, rss_anon - cats.get("gpu_driver", 0)
                               + (gpu_buffers if gpu_source != "logical_in_gpu_driver" else 0) - 64 * MIB),
        "gpuBuffersBytes": gpu_buffers,
        "gpuBuffers": {"gpuBuffersBytes": gpu_buffers, "source": gpu_source,
                       "outsideRssAnon": gpu_source != "logical_in_gpu_driver"},
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

    # 7b. B933 (Will, 2026-09-29: "64 MB excludes driver memory"): on x86
    #     (llvmpipe) the same 100 MiB RssAnon with 19 MiB of gpu_driver is 81
    #     MiB OWNED -- the 22 MiB of GPU buffers are INSIDE gpu_driver there and
    #     are NOT added again (B978: no double count). The gap is 17.0 MiB,
    #     gpu_driver is shown as EXCLUDED and is not a shrink bucket, and the
    #     whole-RssAnon figure is printed under its own name.
    mem = {"ledger": ledger_dict(100 * MIB, 95 * MIB,
                                 {"zone_mesh": 50 * MIB, "gpu_driver": 19 * MIB,
                                  "zone_parse": 20 * MIB, "ui": 5 * MIB},
                                 slack=3 * MIB, gpu_buffers=22 * MIB)}
    f, out = run(gate, mem)
    assert f == [], f
    assert gate.owned_bytes(100 * MIB, 19 * MIB) == 81 * MIB
    assert gate.owned_bytes(10 * MIB, 19 * MIB) == 0
    assert "owned=RssAnon-gpu_driver+gpu_buffers 81.0 MiB" in out, out
    assert "gpu_buffers 22.0 inside gpu_driver, not added again" in out, out
    assert "must shrink 17.0 MiB of owned memory" in out, out
    assert "whole-RssAnon gap 36.0 MiB, driver included" in out, out
    assert "gpu_driver" in out and "EXCLUDED from the gap" in out, out
    order = [ln.split()[0] for ln in out.splitlines() if ln.startswith("      ")]
    assert order[-1] == "gpu_driver" and "gpu_driver" not in order[:-1], order
    assert "gpu_buffers" not in order, order
    # driver bigger than the gap: within target on owned memory, still under the ceiling
    mem = {"ledger": ledger_dict(80 * MIB, 78 * MIB, {"zone_mesh": 40 * MIB, "gpu_driver": 19 * MIB},
                                 slack=MIB)}
    f, out = run(gate, mem)
    assert f == [] and "WITHIN the 64 MiB target" in out and "owned=RssAnon-gpu_driver+gpu_buffers 61.0 MiB" in out, out

    # 7c. B978 (Will, 2026-10-01: "1) yes", GPU buffers count toward the 64 MB
    #     target): on the Pi the GPU buffers are Lima GEM OUTSIDE RssAnon, so
    #     RssAnon 100, gpu_driver 19, gpu_buffers 22 -> owned 103, gap 39, and
    #     gpu_buffers is a shrink bucket of its own.
    mem = {"ledger": ledger_dict(100 * MIB, 95 * MIB,
                                 {"zone_mesh": 50 * MIB, "gpu_driver": 19 * MIB,
                                  "zone_parse": 20 * MIB, "ui": 5 * MIB},
                                 slack=3 * MIB, gpu_buffers=22 * MIB, gpu_source="memcg_shmem")}
    f, out = run(gate, mem)
    assert f == [], f
    assert gate.owned_bytes(100 * MIB, 19 * MIB, 22 * MIB, True) == 103 * MIB
    assert "owned=RssAnon-gpu_driver+gpu_buffers 103.0 MiB" in out, out
    assert "gpu_buffers 22.0 outside RssAnon, added" in out, out
    assert "must shrink 39.0 MiB of owned memory" in out, out
    assert "whole-RssAnon gap 36.0 MiB, driver included" in out, out
    order = [ln.split()[0] for ln in out.splitlines() if ln.startswith("      ")]
    assert order[:2] == ["zone_mesh", "gpu_buffers"], order
    # the buffers alone can put owned over the target
    mem = {"ledger": ledger_dict(60 * MIB, 58 * MIB, {"zone_mesh": 40 * MIB, "gpu_driver": 19 * MIB},
                                 slack=MIB, gpu_buffers=30 * MIB, gpu_source="memcg_shmem")}
    f, out = run(gate, mem)
    assert f == [] and "owned=RssAnon-gpu_driver+gpu_buffers 71.0 MiB" in out and "must shrink 7.0 MiB" in out, out

    # 7d. the Pi summarizers read the [memgpu] line through owned_memory: both
    #     the pre-B978 shape (owned=RssAnon-gpu_driver + owned_plus_gpu_buffers,
    #     the committed Pi fixtures under tests/data/b962) and the B978 shape
    #     (owned_rss_anon= + owned=) give the same owned figure.
    om = gate.owned_memory
    root = GATE.parents[2]
    for stem, anon, gpu in (("qeynos2_steady_memgpu", 48537616, 23433216),
                            ("qeynos2_steady_legacy_memgpu", 47755144, 38735872)):
        line = (root / "tests" / "data" / "b962" / (stem + ".txt")).read_text()
        kv = dict(re.findall(r"([a-z_0-9]+)=([^ \n]+)", line[line.index("[memgpu]"):]))
        assert om.owned_from_memgpu(kv) == (anon + gpu, anon, gpu), (stem, om.owned_from_memgpu(kv))
        new = dict(kv)
        del new["owned_plus_gpu_buffers"]
        new["owned_rss_anon"], new["owned"] = str(anon), str(anon + gpu)
        assert om.owned_from_memgpu(new) == (anon + gpu, anon, gpu), new
    x86 = {"gpu_buffers": "35651584", "source": "logical_in_gpu_driver", "outside_rss_anon": "0",
           "owned_rss_anon": "84934656", "owned": "84934656"}
    assert om.owned_from_memgpu(x86) == (84934656, 84934656, 0), om.owned_from_memgpu(x86)
    assert om.OWNED_LABEL == "owned=RssAnon-gpu_driver+gpu_buffers"

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

    # 11. The ONE blocking CI gate job's --zones list IS the manifest's zones:
    #     every zone is verified, once, so no zone can leave CI by editing the
    #     list. (B903/B904, 2026-09-29, ended the 2026-09-28 blocking/
    #     non-blocking split; docs/decisions.md, docs/ci.md.)
    import json
    ci_text = (root / ".gitlab-ci.yml").read_text()
    lists = re.findall(r"visual_gate\.sh --zones ([A-Za-z0-9_,]+)", ci_text)
    assert len(lists) == 1, f"expected exactly one --zones gate invocation in .gitlab-ci.yml, got {lists}"
    assert "allow_failure: true" not in ci_text.split("\ngate:", 1)[1], "the gate job must block"
    ci_zones = [z for lst in lists for z in lst.split(",")]
    real = json.loads((root / "tests" / "goldens" / "manifest.json").read_text())
    manifest_zones = [z["zone"] for z in real["zones"]]
    coverage_zones = [z["zone"] for z in real.get("memoryCoverage", {}).get("zones", [])]
    assert len(ci_zones) == len(set(ci_zones)), f"a zone is listed twice: {lists}"
    assert sorted(ci_zones) == sorted(manifest_zones), (lists, manifest_zones)
    assert set(coverage_zones) <= set(ci_zones), (lists, coverage_zones)

    # 12. B1016 (#337): the [gldriver] net, beside the ledger assertions --
    #     assert_gldriver_line() on synthetic benchmark stdout (no benchmark
    #     run): a real line is accepted, an absent line is a failure, an
    #     unparseable line (the format drift the audit found unpinned) is a
    #     failure.
    assert hasattr(gate, "assert_gldriver_line"), "visual_gate.py has no assert_gldriver_line (pre-B1016)"
    good = ("[gldriver] renderer='llvmpipe (LLVM 20.1.2, 256 bits)' "
            "gallium=/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so "
            "llvm=/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1")
    f, out = [], io.StringIO()
    with redirect_stdout(out):
        gate.assert_gldriver_line("t", "before\n" + good + "\nafter", f)
    assert f == [], f
    assert "[gldriver] t:" in out.getvalue(), out.getvalue()
    f = []
    gate.assert_gldriver_line("t", "no driver line here\n", f)
    assert len(f) == 1 and "no [gldriver] line" in f[0], f
    f = []
    gate.assert_gldriver_line("t", "[gldriver] renderer='x' gallium=/g\n", f)
    assert len(f) == 1 and "parse" in f[0], f
    print("test_visual_gate_ledger: gldriver-line checks OK")

    # 13. B928 (#246): the zone_parse PEAK gate. assert_zone_parse_peak() on
    #     synthetic memory dicts: a peak above a PRESENT budget FAILS (the
    #     whole point -- the live value can be inside the budget while the
    #     mid-load high-water is not), a peak inside a budget passes, a
    #     missing (unblessed) budget prints and never fails, and a memory
    #     JSON with no peak field (an older benchmark) prints and never
    #     fails. ledger_dict() sets peak == live per category; the peak is
    #     overridden here.
    assert hasattr(gate, "assert_zone_parse_peak"), \
        "visual_gate.py has no assert_zone_parse_peak (pre-B928)"

    def peak_mem(live, peak, rss=None, total=None):
        m = {"ledger": ledger_dict(rss or 100 * MIB, total or live,
                                   {"zone_parse": live})}
        m["ledger"]["categories"]["zone_parse"]["peak"] = peak
        return m

    # (a) a mid-load peak ABOVE the blessed budget is a failure, even with the
    #     end-of-run live value FAR below it -- the live check alone passes.
    mem = peak_mem(2 * MIB, 20 * MIB)
    f = []
    gate.assert_zone_parse_peak("t", mem, 16 * MIB, f)
    assert len(f) == 1 and "zone_parse PEAK" in f[0] and "exceeds" in f[0], f
    # and the same numbers with no budget: prints, never fails.
    f = []
    with redirect_stdout(io.StringIO()) as out:
        gate.assert_zone_parse_peak("t", mem, None, f)
    assert f == [], f
    assert "NO BUDGET BLESSED" in out.getvalue(), out.getvalue()

    # (b) a peak inside the budget passes (and is printed with the share).
    mem = peak_mem(2 * MIB, 8 * MIB)
    f = []
    with redirect_stdout(io.StringIO()) as out:
        gate.assert_zone_parse_peak("t", mem, 16 * MIB, f)
    assert f == [], f
    assert "50% of budget" in out.getvalue(), out.getvalue()

    # (c) no peak field (a benchmark older than the peak field): printed,
    #     not failed, regardless of budget presence.
    mem = {"ledger": ledger_dict(100 * MIB, 10 * MIB, {"zone_parse": 10 * MIB})}
    del mem["ledger"]["categories"]["zone_parse"]["peak"]
    f = []
    with redirect_stdout(io.StringIO()) as out:
        gate.assert_zone_parse_peak("t", mem, 16 * MIB, f)
    assert f == [], f
    assert "peak not reported" in out.getvalue(), out.getvalue()
    print("test_visual_gate_ledger: zone_parse-peak checks OK")

    print("test_visual_gate_ledger: 20 checks OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
