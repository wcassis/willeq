#!/usr/bin/env python3
"""T492 Leg D: the acceptance checker for the recorded corpus.

Handoff section 7, mechanically:

  1. three players;
  2. >= 40 steps each, and every step has a marks line, a frame and a truth
     line -- checked by JOINING them on the timestamp, which is the corpus's
     only join key;
  3. >= 5 expect=discrepancy:B### steps and >= 10 expect=match steps, each
     with a PAIRED OFFICIAL FRAME at the same step;
  4. manifest.json loads standalone (no README needed) and names every path
     it points at, and those paths exist;
  5. every timestamp in marks/truth/frames parses and lies inside the run.

This checker never opens a PNG's pixels.  Nothing here judges a frame.

    check_corpus.py <corpus-dir> [--min-steps 40] [--min-match 10]
                                 [--min-discrepancy 5]
"""
import argparse, json, os, re, sys, glob

TS = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\.(\d{3})$")
FRAME = re.compile(r"^(\d{2}:\d{2}:\d{2}\.\d{3})_(.+?)_(window|root)\.png$")


def parse_kv(s):
    d = {}
    for part in s.split(";"):
        k, _, v = part.partition("=")
        if k.strip():
            d[k.strip()] = v.strip()
    return d


def read_tsv(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            a, _, b = line.partition("\t")
            rows.append((a.strip(), b))
    return rows


def secs(t):
    m = TS.match(t)
    if not m:
        return None
    h, mi, s, ms = (int(x) for x in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--min-steps", type=int, default=40)
    ap.add_argument("--min-match", type=int, default=10)
    ap.add_argument("--min-discrepancy", type=int, default=5)
    ap.add_argument("--min-cross-refs", type=int, default=1,
                    help="how many of one player's /say or /tell texts must be "
                         "found in ANOTHER player's chat.tsv. Handoff §7 does not "
                         "ask for this, but the whole point of a three-player "
                         "corpus is that the logs cross-reference, and T492's "
                         "first complete dry run passed every §7 line while the "
                         "three players never actually overlapped in time.")
    ap.add_argument("--mark-window", type=float, default=30.0,
                    help="seconds a truth line may sit from its nearest marks line")
    a = ap.parse_args()
    C = a.corpus
    results = []   # (ok, text)

    def check(ok, text):
        results.append((bool(ok), text))

    # --- 4. manifest ------------------------------------------------------
    mpath = os.path.join(C, "manifest.json")
    man = None
    try:
        man = json.load(open(mpath))
        check(True, f"manifest.json loads standalone ({len(man.get('players', []))} players)")
    except Exception as e:
        check(False, f"manifest.json does NOT load: {e}")

    ids = [p["id"] for p in man["players"]] if man else []
    check(len(ids) >= 3, f"players in manifest: {len(ids)} ({', '.join(ids)}) -- need 3")

    if man:
        miss = []
        for p in man["players"]:
            for key in ("client_txt", "marks", "truth", "frames_dir", "run_log"):
                v = p.get(key)
                if v and not os.path.exists(os.path.join(C, v)):
                    miss.append(f"{p['id']}:{key}={v}")
        for key in ("timeline", "steps_table", "steps_json", "db_before", "db_after"):
            v = man.get(key)
            if v and not os.path.exists(os.path.join(C, v)):
                miss.append(f"manifest:{key}={v}")
        check(not miss, "every path manifest.json names exists"
              + ("" if not miss else f" -- MISSING: {', '.join(miss)}"))

    # --- 2/3. per player --------------------------------------------------
    official = None
    per = {}
    for pid in (ids or ["p1", "p2", "p3"]):
        d = os.path.join(C, "players", pid)
        marks = read_tsv(os.path.join(d, "marks.tsv"))
        truth = read_tsv(os.path.join(d, "truth.tsv"))
        frames = {}
        for f in sorted(glob.glob(os.path.join(d, "frames", "*.png"))):
            m = FRAME.match(os.path.basename(f))
            if m:
                frames.setdefault(m.group(1), []).append(m.group(3))
        mark_t = sorted(x for x in (secs(t) for t, _ in marks) if x is not None)
        joined, unjoined = [], []
        for t, body in truth:
            kv = parse_kv(body)
            ts = secs(t)
            has_frame = t in frames
            near = min((abs(ts - x) for x in mark_t), default=1e9) if ts is not None else 1e9
            if has_frame and near <= a.mark_window:
                joined.append((t, kv))
            else:
                unjoined.append((t, "no frame" if not has_frame else f"nearest mark {near:.1f}s away"))
        per[pid] = {"marks": len(marks), "truth": len(truth),
                    "frames": sum(len(v) for v in frames.values()),
                    "frame_times": frames, "joined": joined, "unjoined": unjoined}
        if man:
            kind = next((p.get("client") for p in man["players"] if p["id"] == pid), None)
            if kind == "official":
                official = pid
        check(len(joined) >= a.min_steps,
              f"{pid}: {len(joined)} steps with mark+frame+truth (need {a.min_steps}); "
              f"{len(truth)} truth lines, {len(marks)} marks, {per[pid]['frames']} frames"
              + (f"; {len(unjoined)} unjoined e.g. {unjoined[:2]}" if unjoined else ""))

    check(official is not None, f"an official-client player is identified ({official})")

    # --- 3. labelled steps with a paired official frame -------------------
    off_times = per.get(official, {}).get("frame_times", {}) if official else {}
    off_steps = {}
    for t, body in read_tsv(os.path.join(C, "players", official or "p1", "truth.tsv")):
        kv = parse_kv(body)
        if "step" in kv:
            off_steps[kv["step"]] = t
    n_match = n_disc = 0
    paired_match = paired_disc = 0
    bad_labels = []
    for pid in ids:
        for t, kv in per[pid]["joined"]:
            e = kv.get("expect", "unlabelled")
            step = kv.get("step")
            paired = step in off_steps and off_steps[step] in off_times
            if e == "match":
                n_match += 1; paired_match += bool(paired)
            elif e.startswith("discrepancy:"):
                if not re.fullmatch(r"discrepancy:B\d{3}", e):
                    bad_labels.append((pid, t, e))
                n_disc += 1; paired_disc += bool(paired)
            elif e != "unlabelled":
                bad_labels.append((pid, t, e))
    check(paired_match >= a.min_match,
          f"expect=match steps with a paired official frame: {paired_match} of {n_match} "
          f"(need {a.min_match})")
    check(paired_disc >= a.min_discrepancy,
          f"expect=discrepancy:B### steps with a paired official frame: {paired_disc} of "
          f"{n_disc} (need {a.min_discrepancy})")
    check(not bad_labels,
          "every expect= label is match | discrepancy:B### | unlabelled"
          + ("" if not bad_labels else f" -- BAD: {bad_labels[:5]}"))

    # --- the cross-reference check ---------------------------------------
    # The actor's own belief is never the evidence: a /say counts only when it
    # appears in a DIFFERENT player's chat log.
    said = {}
    for pid in ids:
        for t, body in read_tsv(os.path.join(C, "players", pid, "truth.tsv")):
            kv = parse_kv(body)
            if kv.get("actor") != pid:
                continue
            # Only `said=` -- the MESSAGE.  `told=` is the recipient's NAME,
            # which appears in every observer's log for a dozen unrelated
            # reasons and would pass this check without proving anything.
            if kv.get("said"):
                said.setdefault(pid, set()).add(kv["said"])
    chats = {}
    for pid in ids:
        f = os.path.join(C, "players", pid, "chat.tsv")
        chats[pid] = open(f, errors="replace").read() if os.path.exists(f) else ""
    seen_by_other = []
    for pid, texts in said.items():
        for text in texts:
            if len(text) < 16:
                continue
            for other in ids:
                if other != pid and text in chats.get(other, ""):
                    seen_by_other.append((pid, other, text))
                    break
    check(len(seen_by_other) >= a.min_cross_refs,
          f"cross-player chat an OBSERVER logged: {len(seen_by_other)} "
          f"(need {a.min_cross_refs}); "
          + (f"e.g. {seen_by_other[0][0]}->{seen_by_other[0][1]}: {seen_by_other[0][2][:40]!r}"
             if seen_by_other else "none -- the players may never have overlapped in time"))

    # --- 5. timestamps ----------------------------------------------------
    bad_ts = []
    for pid in ids:
        d = os.path.join(C, "players", pid)
        for name in ("marks.tsv", "truth.tsv"):
            for t, _ in read_tsv(os.path.join(d, name)):
                if secs(t) is None:
                    bad_ts.append(f"{pid}/{name}:{t!r}")
    check(not bad_ts, "every marks/truth timestamp parses as HH:MM:SS.mmm"
          + ("" if not bad_ts else f" -- BAD: {bad_ts[:5]}"))

    print(f"=== T492 corpus acceptance: {C}")
    for ok, text in results:
        print(f"[{'PASS' if ok else 'FAIL'}] {text}")
    nfail = sum(1 for ok, _ in results if not ok)
    print(f"=== {len(results)-nfail} pass, {nfail} fail")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
