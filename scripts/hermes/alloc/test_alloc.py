#!/usr/bin/env python3
"""B1204 (#523): unit test of the allocator's admission logic (alloc.decide / actions_for)
over fixture cards. Plain python asserts, no pytest, no I/O:

  python3 -I scripts/hermes/alloc/test_alloc.py      -> "ALLOC TESTS PASS (N)"
"""
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import alloc as A  # noqa: E402

CFG = A.load_config()
QUIET = {"load1": 1.0, "runq_per_core": 0.1, "model_viewer": 0, "eqgame": 0, "measure": 0,
         "leases": {"1": "FREE", "2": "FREE", "3": "FREE", "4": "FREE", "5": "FREE"},
         "codex": {"used": 10.0, "line": 20.0, "resets_in_h": 100.0, "record_age_h": 0.5, "verdict": "GO"}}
N = [0]


def body(leg, tier, needs, model, phases):
    return ("## Manifest\nleg: %s\ntier: %s\nneeds: %s\nmodel: %s\nphases: %s\nest: turns=10, wall_min=10\n"
            % (leg, tier, needs, model, phases))


def card(cid, prio, b, status="ready", created=0, **kw):
    m, flag = A.card_manifest(b, CFG)
    c = {"id": cid, "status": status, "priority": prio, "created_at": created, "title": cid,
         "assignee": "dev", "model_override": None, "manifest": m, "flag": flag, "hold_reason": "",
         "job_wait": ""}
    c.update(kw)
    return c


def run(cid_m, endpoint="hermes-a"):
    return {"id": cid_m[0], "manifest": A.card_manifest(cid_m[1], CFG)[0], "endpoint": endpoint}


REVIEW = body("review", "light", "none", "any", "review:any")
FIX = body("fix", "medium", "cpu-build", "any", "code:any, build:none")
RENDER2 = body("fix", "heavy", "thinkpad-render:2, cpu-build", "any", "code:any, render:none")
LANE5 = body("diagnose", "heavy", "lane:5, thinkpad-render:1", "reasoning", "trace:reasoning, leg:none")
QUIETB = body("diagnose", "heavy", "thinkpad-quiet, thinkpad-render:1", "any", "render:none")
STRONG = body("fix", "medium", "none", "strong", "code:strong")


def check(name, cond):
    N[0] += 1
    assert cond, name


def by_id(ds):
    return {d["task_id"]: d for d in ds}


def main():
    cfg = copy.deepcopy(CFG)
    # 1. greedy by priority, out of order: the high-priority render card takes 2 of 3 render
    #    slots, the next render card (2 more) is held, a lower-priority review still admits.
    ds = by_id(A.decide([card("a", 9, RENDER2), card("b", 8, RENDER2), card("c", 1, REVIEW)], [], QUIET, cfg))
    check("1a admit", ds["a"]["decision"] == "admit")
    check("1b hold render", ds["b"]["decision"] == "hold" and ds["b"]["reason"].startswith("thinkpad-render"))
    check("1c out of order", ds["c"]["decision"] == "admit")
    # 2. cpu-build capacity 2: two running fix cards hold both slots
    ds = by_id(A.decide([card("f", 5, FIX)], [run(("r1", FIX)), run(("r2", FIX))], QUIET, cfg))
    check("2 cpu-build full", ds["f"]["decision"] == "hold" and ds["f"]["reason"].startswith("cpu-build (2/2"))
    # 3. load guard closes renders, not reviews
    hot = dict(QUIET, load1=14.0)
    ds = by_id(A.decide([card("a", 9, RENDER2), card("c", 1, REVIEW)], [], hot, cfg))
    check("3a load closes render", ds["a"]["decision"] == "hold" and "load1 14.0" in ds["a"]["reason"])
    check("3b review unaffected", ds["c"]["decision"] == "admit")
    runq = dict(QUIET, runq_per_core=2.5)
    check("3c runq closes render", A.decide([card("a", 9, RENDER2)], [], runq, cfg)[0]["decision"] == "hold")
    # 4. a HELD lease closes lane 5; EXPIRED does not; eqgame closes lane 5
    held = dict(QUIET, leases=dict(QUIET["leases"], **{"5": "HELD"}))
    check("4a held lease", A.decide([card("l", 5, LANE5)], [], held, cfg)[0]["reason"].startswith("lane-5 (lease HELD"))
    exp = dict(QUIET, leases=dict(QUIET["leases"], **{"5": "EXPIRED"}))
    check("4b expired lease is open", A.decide([card("l", 5, LANE5)], [], exp, cfg)[0]["decision"] == "admit")
    eq = dict(QUIET, eqgame=1)
    check("4c eqgame closes lane 5", A.decide([card("l", 5, LANE5)], [], eq, cfg)[0]["decision"] == "hold")
    # 5. thinkpad-quiet: refused while any model_viewer runs; when admitted it takes every render slot
    busy = dict(QUIET, model_viewer=1)
    check("5a quiet refused", A.decide([card("q", 5, QUIETB)], [], busy, cfg)[0]["decision"] == "hold")
    ds = by_id(A.decide([card("q", 5, QUIETB), card("a", 4, RENDER2)], [], QUIET, cfg))
    check("5b quiet admitted", ds["q"]["decision"] == "admit")
    check("5c quiet is exclusive", ds["a"]["decision"] == "hold")
    # 6. routing: strong -> codex when GO; one codex seat; then local fallback
    ds = by_id(A.decide([card("s1", 5, STRONG), card("s2", 4, STRONG)], [], QUIET, cfg))
    check("6a strong to codex", ds["s1"]["route"] == "codex")
    check("6b codex seat 1 -> local", ds["s2"]["route"] == "hermes-a" and "seat codex 1/1" in ds["s2"]["reason"])
    # 7. stale codex record -> CODEX_USAGE_STALE, local
    stale = dict(QUIET, codex=dict(QUIET["codex"], record_age_h=228.0))
    d = A.decide([card("s1", 5, STRONG)], [], stale, cfg)[0]
    check("7 stale", d["route"] == "hermes-a" and "CODEX_USAGE_STALE" in d["reason"])
    # 8. verdict STOP (ahead of the pace line) -> local
    stop = dict(QUIET, codex=dict(QUIET["codex"], verdict="STOP"))
    check("8 stop", A.decide([card("s1", 5, STRONG)], [], stop, cfg)[0]["route"] == "hermes-a")
    # 9. window-end rule: resets_in < 72 h and used < 60 % -> medium goes to codex; not at used 70 %
    end = dict(QUIET, codex=dict(QUIET["codex"], resets_in_h=40.0, used=30.0))
    check("9a medium to codex", A.decide([card("m", 5, FIX)], [], end, cfg)[0]["route"] == "codex")
    late = dict(QUIET, codex=dict(QUIET["codex"], resets_in_h=40.0, used=70.0))
    check("9b used >= 60 stays local", A.decide([card("m", 5, FIX)], [], late, cfg)[0]["route"] == "hermes-a")
    check("9c light never codex", A.decide([card("r", 5, REVIEW)], [], end, cfg)[0]["route"] == "hermes-a")
    # 10. seats: 4 running on hermes-a -> a new local card is held for a seat
    full = [run(("r%d" % i, REVIEW)) for i in range(4)]
    d = A.decide([card("c", 5, REVIEW)], full, QUIET, cfg)[0]
    check("10 seat hold", d["decision"] == "hold" and d["reason"].startswith("seat: seat hermes-a 4/4"))
    # 11. board slots: max_in_progress 4 with 3 running -> first fitting card admits, the next queues
    three = [run(("r%d" % i, REVIEW)) for i in range(3)]
    cfg2 = copy.deepcopy(cfg); cfg2["seats"]["hermes-a"] = 8
    ds = by_id(A.decide([card("x", 5, REVIEW), card("y", 4, REVIEW)], three, QUIET, cfg2))
    check("11a admit", ds["x"]["decision"] == "admit")
    check("11b queue", ds["y"]["decision"] == "queue")
    # 12. no manifest -> heavy + strong + cpu-build, flagged
    d = A.decide([card("n", 5, "# no manifest here")], [], QUIET, cfg)[0]
    check("12 unmanifested", d["flag"] == "NO_MANIFEST" and d["tier"] == "heavy" and d["model_class"] == "strong"
          and d["needs"] == "cpu-build:1")
    # 13. job gating: a parent's JOB: line without a done-file holds the child
    d = A.decide([card("j", 5, REVIEW, job_wait="gate_after:kb-b1-x")], [], QUIET, cfg)[0]
    check("13 job wait", d["decision"] == "hold" and d["reason"] == "job:gate_after:kb-b1-x")
    # 14. lane:any takes the first free lane of lane_any; a held lane is skipped
    anyb = body("diagnose", "heavy", "lane:any", "any", "leg:none")
    l1held = dict(QUIET, leases=dict(QUIET["leases"], **{"1": "HELD"}))
    ds = by_id(A.decide([card("u", 5, anyb), card("v", 4, anyb)], [], l1held, cfg))
    check("14 lane any", ds["u"]["decision"] == "admit" and ds["v"]["decision"] == "admit")
    # 15. no ThinkPad census -> renders closed, conservative
    check("15 no census", A.decide([card("a", 9, RENDER2)], [], {"load1": None}, cfg)[0]["decision"] == "hold")
    # 16. actions: hold on ready -> schedule "awaiting ..."; admit of a held card -> unblock;
    #     codex route -> assign codex; a held card stays held without a new action
    c = card("a", 9, RENDER2)
    d = {"decision": "hold", "reason": "thinkpad-render (3/3 used)", "route": ""}
    check("16a schedule", A.actions_for(d, c, cfg) == [["schedule", "a", "awaiting thinkpad-render"]])
    c2 = card("b", 9, STRONG, status="scheduled", hold_reason="awaiting seat")
    d = {"decision": "admit", "reason": "", "route": "codex"}
    check("16b assign+unblock", A.actions_for(d, c2, cfg) == [["assign", "b", "codex"], ["unblock", "b"]])
    d = {"decision": "hold", "reason": "cpu-build (2/2 used)", "route": ""}
    check("16c held stays", A.actions_for(d, c2, cfg) == [])
    d = {"decision": "hold", "reason": "seat: codex verdict STOP (used 0%); seat hermes-a 4/4", "route": ""}
    check("16e seat key", A.actions_for(d, card("s", 1, REVIEW), cfg) == [["schedule", "s", "awaiting seat"]])
    c3 = card("c", 1, REVIEW, model_override="q38-x")
    d = {"decision": "admit", "reason": "", "route": "hermes-a"}
    check("16d clear override", A.actions_for(d, c3, cfg) == [["set-model", "c", "none"]])
    # 17. parsers
    cx = A.parse_codex_usage("CODEX_USAGE used=4.0% line=12.3% goal=90% verdict=STOP (ahead of the line) "
                             "next_go_at=x anchor=y resets_in=0.0h resets_at=z plan=p record_age=228.0h concurrency=1")
    check("17a codex parse", cx == {"used": 4.0, "line": 12.3, "resets_in_h": 0.0, "record_age_h": 228.0,
                                    "verdict": "STOP"})
    lz = A.parse_leases("lane  state ...\nlane1 FREE     /\nlane5 HELD     t_1/x\nxu4   FREE /\n")
    check("17b lease parse", lz == {"1": "FREE", "5": "HELD"})
    print("ALLOC TESTS PASS (%d)" % N[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
