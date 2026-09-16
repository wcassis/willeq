#!/usr/bin/env python3
"""T492 Leg D: write the corpus README from the corpus itself.

Everything in it is read back out of `manifest.json`, `steps.json`, the
per-player files and the checker's own output -- so the README cannot drift
from the corpus the way a hand-written one does.  The prose that is NOT
derivable (why a label means what it means, what is missing) is appended from
--notes.

    write_readme.py <corpus-dir> [--checker-output FILE] [--notes FILE]
                                 [--sheets DIR]
"""
import argparse, glob, json, os, re
from collections import Counter, defaultdict


def kv(body):
    return dict(p.split("=", 1) for p in body.split(";") if "=" in p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--checker-output")
    ap.add_argument("--notes")
    ap.add_argument("--sheets")
    a = ap.parse_args()
    C = a.corpus
    man = json.load(open(os.path.join(C, "manifest.json")))
    steps = json.load(open(os.path.join(C, "steps.json")))["steps"]

    L = []
    W = L.append
    W(f"# T492 — three-player verification corpus for the Inspur LLM serving benchmark")
    W("")
    W("Recorded ONCE, three simultaneous interacting players against the live eqemu")
    W("server, for `/mnt/projects/archive/docs/willeq-multiplayer-llm-benchmark-handoff.md`")
    W("(its §9 is the WillEQ session's binding answers). The corpus is meant to be")
    W("**replayed without live clients**: everything the replay driver needs is in")
    W("`manifest.json` and the per-player files, and this README is for a human.")
    W("")
    W("**Nothing here is a visual judgement.** Every `truth.tsv` line is what the driver")
    W("COMMANDED, never what a frame appeared to show, and no model looked at a frame")
    W("during the recording. `expect=` comes only from a `refactor/BOARD.md` B-number or")
    W("a prior unit's oracle fit; everything else is `unlabelled` and the grader ignores")
    W("it. **The corpus is not blessed until Will has looked at the sheets.**")
    W("")
    W("## Hosts, accounts, characters")
    W("")
    W("| player | client | host | run.log | marks | truth | frames |")
    W("|---|---|---|---:|---:|---:|---:|")
    for p in man["players"]:
        W(f"| {p['id']} | {p['client']} | {p['host']} | "
          f"{'yes' if p.get('run_log') else '—'} | {p['marks_lines']} | "
          f"{p['truth_lines']} | {p['frames']} |")
    W("")
    for p in man["players"]:
        f = os.path.join(C, p["client_txt"])
        if os.path.exists(f):
            W(f"`players/{p['id']}/client.txt`:")
            W("")
            W("```")
            W(open(f).read().rstrip())
            W("```")
            W("")
    W("`Testchar` was NOT logged in: it sits at z=−495 deliberately as T489 evidence.")
    W("")
    W("## Geometry and clock")
    W("")
    g = man["frame_geometry"]
    W(f"Official window grab **{g['official_window']}** (the Wine window is at root")
    W(f"(−1, 30)), official root grab **{g['official_root']}**, willeq **{g['willeq']}**.")
    W("Crops are taken against the WINDOW, never the root.")
    W("")
    W(f"One clock: {man['clock']}. hermes-ext's offset was measured with")
    W("`scripts/llmbench/clock_offset.sh` over a ControlMaster socket (which takes the")
    W("ssh handshake out of every sample after the first) and is")
    W(f"**{man['hermes_clock_offset_s']:+.4f} s**; it is already applied to p3's")
    W("`marks.tsv` and `truth.tsv`. p3's `run.log` keeps hermes-ext's own stamps.")
    W(f"T0 (the driver's step clock zero) was {man['t0_local']} local.")
    W("")
    W("## Running order")
    W("")
    W(f"`shared/timeline.tsv` is the single timeline all three players were driven from,")
    W(f"and `steps.tsv` / `steps.json` are the compiled step table: {len(steps)} steps.")
    W("The two willeq players ran `--script` files compiled from it")
    W("(`players/p2/script.txt`, `players/p3/script.txt`); the official client was driven")
    W("live by `scripts/llmbench/record.sh`. Own actions per player:")
    W("")
    own = Counter(s["player"] for s in steps)
    for k in sorted(own):
        W(f"  * **{k}** — {own[k]} own actions, {len(steps)} recorded steps "
          f"(every step frames every player)")
    W("")
    W("Content, in order:")
    W("")
    blocks, seen = [], set()
    for s in steps:
        b = s["label"].split("_")[0]
        if b not in seen:
            seen.add(b)
            blocks.append((b, s["label"]))
    names = {"A": "zone in; /loc, /who, /camera get — p1 stays in its untouched login camera",
             "B": "/say and /tell between all three, so the logs cross-reference",
             "C": "targeting each other (F8 on the official client, /target on willeq)",
             "D": "the camera cycle: F9 on the official client, the named modes on willeq",
             "E": "held items: #wc on the official client, in-game /equip on willeq",
             "F": "movement, then the sky and the distant tree line in frame",
             "G": "a fight on a training dummy, then a death and a respawn",
             "H": "inventory, then the qeynos2 → qeytoqrg zone line for all three",
             "I": "camp out, p3 and p2 from their own scripts then p1"}
    for b, first in blocks:
        W(f"  {b}. {names.get(b, first)}")
    W("")
    W("## `expect=` labels")
    W("")
    tot = Counter()
    for p in man["players"]:
        f = os.path.join(C, "players", p["id"], "truth.tsv")
        if not os.path.exists(f):
            continue
        for line in open(f):
            _, _, body = line.rstrip("\n").partition("\t")
            tot[kv(body).get("expect", "unlabelled")] += 1
    for k, n in sorted(tot.items()):
        W(f"  * `{k}` — {n} step-rows across all players")
    W("")
    W("`match` in THIS corpus means: both clients are in the **First Person** camera —")
    W("the one camera pair a prior unit fitted against the oracle (T466/T471 measured")
    W("the official client's untouched login camera as First Person) — in the same zone")
    W("at the same heading. It does **not** mean the two frames are pixel pairs: the")
    W("three players stand 10 units apart, because a three-player corpus needs them to")
    W("see each other. A `discrepancy:B###` label names the board entry and nothing")
    W("else was used to choose one.")
    W("")
    W("## Acceptance (handoff §7)")
    W("")
    W("`scripts/llmbench/check_corpus.py`, run on this corpus:")
    W("")
    W("```")
    if a.checker_output and os.path.exists(a.checker_output):
        W(open(a.checker_output).read().rstrip())
    else:
        W("(not run)")
    W("```")
    W("")
    if a.sheets and os.path.isdir(a.sheets):
        W("## Sheets for Will's bless")
        W("")
        W(f"`{os.path.relpath(a.sheets, os.path.dirname(C))}/` — one tile row per labelled")
        W("step: the official window frame beside the willeq window frame from the same")
        W("step, captioned with the label, both timestamps and the `expect=` label.")
        W("")
        for f in sorted(glob.glob(os.path.join(a.sheets, "*.png"))):
            W(f"  * `{os.path.basename(f)}`")
        W("")
    W("## Server state")
    W("")
    W("`shared/db_before/` and `shared/db_after/` hold the `character_data` +")
    W("`character_currency` snapshot (scenario.sh's contract) and the full-row dumps.")
    W("The restore runs with the eqemu-server **container stopped** (mariadb alone up)")
    W("and as one statement batch — see board B784 for why `pkill` plus per-row")
    W("statements is not enough. Runtime spawns and corpses this run created were")
    W("deleted by id before the restore (B720).")
    W("")
    if a.notes and os.path.exists(a.notes):
        W(open(a.notes).read().rstrip())
        W("")
    out = os.path.join(C, "README.md")
    open(out, "w").write("\n".join(L) + "\n")
    print(f"wrote {out} ({len(L)} lines)")


if __name__ == "__main__":
    main()
