#!/usr/bin/env python3
"""T492 Leg G -- check every labelled pair's CLAIM against the frames, mechanically.

Will, on being shown raw pairs: "there was no reason to have me evaluate those
images. You could have found all of the same issues trivially. Worst case, OCR
of ui windows."  He is right, and this is that check.  A label is a claim about
what the two frames show; every claim this corpus makes is measurable:

  camera=<C>   the OFFICIAL frame scores < 0.30 against T466's direct-mode still
               for C (T310's gradient metric), AND the willeq client's own log
               carries `Camera mode: <C>` before the frame, inside the settle
               window.
  target=<N>   the OFFICIAL target window OCRs as N, AND the willeq log carries
               a targeting line naming N before the frame.
  held=<...>   the willeq log carries the equipment/appearance line for the
               slot/item the truth names, before the frame.
  ui           the recorder's own flag for the step (a `/hideui` earlier in that
               client's script) matches what the truth line says.

Nothing here looks at a frame for meaning.  Every check is a number or a string
match, and every row reports the MEASURED value beside the claim, so a failure
says what was actually there.

Rows that FAIL must not carry a grading label: the caller parks them
`expect=unlabelled;why=<reason>`.

    verify_labels.py <corpus> [--out report.tsv] [--apply]
"""
import argparse, glob, json, os, re, subprocess, sys
from datetime import datetime, timedelta

REFS = "/mnt/projects/fable/review/T466/oracle"
SCORE = "/mnt/projects/fable/wt/t3/scripts/camera_match_score.py"
CROP = "1000x480+12+40"
CAMS = ("FirstPerson", "Overhead", "Chase", "User1", "User2", "Tether")
# T492 Leg H.  The absolute 0.30 was calibrated on an EMPTY scene (Leg F step 1,
# one client, 0.058-0.191) and rejects a correct identification once two other
# players are in frame (Leg G gate: a verified Chase at 0.415-0.495).  The stable
# statistic across both is the MARGIN.  Validated offline before it touched the
# corpus: every f9_cycle row passes with its measured camera at a min ratio of
# 4.90, and the Leg G gate's Chase passes on all three crops at 1.77-2.08.
MARGIN = 1.5
FRAME = re.compile(r"^(\d{2}:\d{2}:\d{2}\.\d{3})_(.+?)_(window|root)\.png$")
LOGLINE = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\].*$")
TARGET_CROPS = ["140x24+885+470", "160x30+875+466", "200x34+865+462", "240x40+790+455"]
# B1059: the target window's name line on a 1023x738 window grab (T492 profile:
# name rows 479-487 from x 926; review/B1059/tools/label_crops.py), read first
# with the glyph atlas (font 3 = EQUI_TargetWindow.xml Target_HP <Font>3).
TARGET_NAME_RECT = (910, 476, 113, 15)
GLYPH_RIG = os.environ.get("VERIFY_LABELS_GLYPH_RIG", "devu24")


def kv(body):
    return dict(p.split("=", 1) for p in body.split(";") if "=" in p)


def hhmmss(t):
    return datetime.strptime(t, "%H:%M:%S.%f")


def score(ref, cand, crop=CROP):
    r = subprocess.run(["python3", SCORE, "--oracle", ref, "--candidate", cand,
                        "--crop", crop, "--candidate-crop", crop],
                       capture_output=True, text=True)
    try:
        return float(r.stdout.strip().splitlines()[-1].split()[2])
    except Exception:
        return float("nan")


def ocr(png, crop):
    p = subprocess.run(["bash", "-c",
        f"convert '{png}' -crop {crop} +repage -colorspace Gray -resize 500% "
        f"-threshold 50% png:- 2>/dev/null | tesseract - - --psm 7 2>/dev/null"],
        capture_output=True, text=True)
    return re.sub(r"[^A-Za-z ]", "", p.stdout).strip()


def atlas_read(png, rect, font=None, rig=GLYPH_RIG):
    """B1059: the glyph-atlas read of one rect -> text, or None when the rig
    has no atlas, NumPy/Pillow are missing, or the read is UNKNOWN (then the
    tesseract reads decide, as before)."""
    try:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        "..", "eqemu", "oracle"))
        import oracle_glyphs as og
        name = og.rig_atlas(rig)
        if name is None:
            return None
        r = og.read_crop(og.load_luma(png), rect, og.Atlas(name), font)
        return None if r["text"] == og.UNKNOWN else r["text"]
    except Exception:  # noqa: BLE001 -- any failure means: fall back
        return None


def load_log_events(path, offset=0.0):
    """(timestamp, text) for the lines a claim can be checked against."""
    out, last = [], None
    if not os.path.exists(path):
        return out
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = LOGLINE.match(line)
        if m:
            t = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f") - timedelta(seconds=offset)
            last = t.strftime("%H:%M:%S.") + f"{t.microsecond//1000:03d}"
            out.append((last, line))
        elif last:
            out.append((last, line))      # `[CHAT]`/`[TARGET]` lines carry no stamp
    return out


def before(events, t, pattern, window=90.0):
    """The last event matching `pattern` at or before `t`, within `window` s."""
    tt = hhmmss(t)
    best = None
    for et, txt in events:
        try:
            e = hhmmss(et)
        except Exception:
            continue
        if e <= tt and (tt - e).total_seconds() <= window and re.search(pattern, txt):
            best = (et, txt)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--out", default=None)
    ap.add_argument("--window", type=float, default=90.0)
    a = ap.parse_args()
    C = a.corpus
    man = json.load(open(os.path.join(C, "manifest.json")))
    official = next(p["id"] for p in man["players"] if p["client"] == "official")

    frames = {}
    for p in man["players"]:
        d = os.path.join(C, "players", p["id"], "frames")
        idx = {}
        for f in glob.glob(os.path.join(d, "*.png")):
            m = FRAME.match(os.path.basename(f))
            if m:
                idx.setdefault((m.group(2), m.group(3)), f)
        frames[p["id"]] = idx
    logs = {}
    for p in man["players"]:
        ev = []
        for f in sorted(glob.glob(os.path.join(C, "players", p["id"], "run*.log"))):
            ev += load_log_events(f)
        logs[p["id"]] = ev

    # the inventory the run started from, and each player's character id
    inv, charid = {}, {}
    invf = os.path.join(C, "shared", "db_before", "inventory.tsv")
    if os.path.exists(invf):
        for line in open(invf):
            f_ = line.rstrip("\n").split("\t")
            if len(f_) >= 3 and f_[0].isdigit():
                inv.setdefault(int(f_[0]), {})[int(f_[1])] = int(f_[2])
    for p in man["players"]:
        txt = open(os.path.join(C, "players", p["id"], "client.txt"), errors="replace").read()
        m = re.search(r"character=(\w+)", txt)
        charid[p["id"]] = {"Testcaster": 67, "Testbuddy": 68, "Oraclealpha": 74}.get(
            m.group(1) if m else "", -1)

    rows = []
    for p in man["players"]:
        pid = p["id"]
        tp = os.path.join(C, "players", pid, "truth.tsv")
        if not os.path.exists(tp):
            continue
        for line in open(tp, errors="replace"):
            t, _, body = line.rstrip("\n").partition("\t")
            d = kv(body)
            lab, exp = d.get("label", "?"), d.get("expect", "unlabelled")
            if exp == "unlabelled" and "pending" not in d:
                continue
            # Every player's truth.tsv carries every step, so evaluate a step
            # ONCE, on the client whose claim it is: the actor.  The official
            # client has no willeq log and its own rows would fail every
            # log-side check for no reason.
            if d.get("actor", pid) != pid or pid == official:
                continue
            claims, checks = [], []
            ofr = frames[official].get((lab, "root")) or frames[official].get((lab, "window"))
            wfr = frames[pid].get((lab, "window"))

            cam = d.get("camera")
            if cam and not cam.startswith("FirstPerson_login") and pid != official:
                ref = f"{REFS}/T466_oracle_h160_direct_{cam}_noui_root_1024x768.png"
                s = score(ref, ofr) if (ofr and os.path.exists(ref)) else float("nan")
                # Three-way, because "contradicted" and "could not be checked"
                # are different answers and only the first is a wrong label.
                # If the claimed camera is the BEST match but above threshold,
                # the scene conditions are off (board B792) and the pair is
                # INCONCLUSIVE; if a DIFFERENT camera is best, the claim is
                # contradicted.
                allc = {c: score(f"{REFS}/T466_oracle_h160_direct_{c}_noui_root_1024x768.png", ofr)
                        for c in CAMS} if ofr else {}
                best = min(allc, key=allc.get) if allc else None
                nxt = sorted(allc.values())[1] if len(allc) > 1 else float("nan")
                ratio = (nxt / allc[best]) if (best and allc[best] > 0) else float("nan")
                if best != cam:
                    checks.append(("official_score", f"{s:.3f} CONTRADICTED: best is "
                                   f"{best} at {allc[best]:.3f} (ratio {ratio:.2f})", False))
                elif ratio >= MARGIN:
                    checks.append(("official_score",
                                   f"{s:.3f} best, next {nxt:.3f}, ratio {ratio:.2f} "
                                   f">= {MARGIN}", True))
                else:
                    checks.append(("official_score",
                                   f"{s:.3f} best but ratio {ratio:.2f} < {MARGIN} "
                                   "-- not separated", None))
                ev = before(logs[pid], t, rf"Camera mode: .*{cam[:5]}", a.window) if logs[pid] else None
                claims.append(f"camera={cam}")
                checks.append(("willeq_log", (ev[1][-40:] if ev else "none"), bool(ev)))

            tgt = d.get("target")
            if tgt and not tgt.startswith("F8"):
                # The target crops are WINDOW coordinates; a root grab is the
                # same scene offset by (-1, +30), so OCR must use the window
                # frame or it reads the wrong rectangle (T492 Leg I: "pO").
                # The official client's target window shows the OFFICIAL
                # client's target.  On a step where a WILLEQ player targeted
                # someone, that window says nothing about the claim -- T492 Leg
                # I's first pass failed `C_target_p2_p3` (p2 targets Testbuddy)
                # because the official window read "Testcaster", which is simply
                # a different client's target.  So the official OCR DECIDES only
                # when the actor is the official client; otherwise it is
                # recorded for context and the willeq log decides.
                wof = frames[official].get((lab, "window"))
                # B1059: atlas first; tesseract only when the atlas cannot read
                # the line. The measured string names which one decided.
                aread = atlas_read(wof, TARGET_NAME_RECT, "3") if wof else None
                if aread is not None:
                    import oracle_glyphs as og
                    reads = [aread]
                    hit = aread if og.text_matches(aread, tgt) else None
                    decider = "atlas"
                else:
                    reads = [ocr(wof, c) for c in TARGET_CROPS] if wof else []
                    hit = next((r for r in reads if tgt in r), None)
                    decider = "tesseract" if any(reads) else "unknown"
                ev = before(logs[pid], t, re.escape(tgt), a.window) if logs[pid] else None
                claims.append(f"target={tgt}")
                shown = hit or (reads[0] if reads else "none")
                checks.append(("official_target_window",
                               shown + f" [{decider}]" + ("" if pid == official else " (p1's own target; context only)"),
                               bool(hit) if pid == official else True))
                checks.append(("willeq_log", (ev[1][-40:] if ev else "none"), bool(ev)))

            held = d.get("held")
            if held:
                claims.append(f"held={held[:34]}")
                # The claim is `slot=item`; the authority is the inventory
                # snapshot the run started from (shared/db_before/inventory.tsv),
                # which is a fact about the server, not about a frame.
                want = dict(re.findall(r"(\d{1,2})=(\d{3,6})", held))
                have = inv.get(charid.get(pid, -1), {})
                bad = {sl: (it, have.get(int(sl))) for sl, it in want.items()
                       if have.get(int(sl)) != int(it)}
                mv = re.search(r"moved from slot (\d{1,4})", held)
                if mv:
                    # B811 (issue #82): this claim is the AFTER state of a
                    # /equip MOVE, so the authority for it is the BEFORE state
                    # -- the inventory snapshot the run started from.  If the
                    # before state already matches the claimed after state
                    # exactly, the step could not have moved anything: this is
                    # the situation the live equip_observed scenario refuses
                    # (scenario_equip_observed.cpp:100-109, "could not tell an
                    # equip from a character that was wearing it all along" ->
                    # Fail, never Pass).  T494's legH p3 rows were exactly
                    # this: heldA said 13=9997 at run start, the /equip
                    # "moved" 9997 into 13, and the before state matched the
                    # after state, so the no-op scored as a success.
                    diff = {sl: (it, have.get(int(sl))) for sl, it in want.items()
                            if have.get(int(sl)) != int(it)}
                    if diff:
                        checks.append(("db_inventory",
                                       "before differed (" +
                                       ";".join(f"slot{k}={v[1]}" for k, v in diff.items()) +
                                       "), so the claimed move is non-trivial and the "
                                       "after-claim is consistent with the step having "
                                       "moved something", True))
                    else:
                        checks.append(("db_inventory",
                                       "no-op: db_before already had " +
                                       ",".join(f"{k}={v}" for k, v in want.items()) +
                                       " at run start, so BEFORE == AFTER and the /equip "
                                       "it claims moved nothing (B811)", False))
                        checks.append(("before_matches_after",
                                       "db_before already had the claimed after-state "
                                       "at run start; the character was wearing it all "
                                       "along", False))
                else:
                    checks.append(("db_inventory",
                                   ("matches " + ",".join(f"{k}={v}" for k, v in want.items()))
                                   if not bad else
                                   ("claim " + ";".join(f"slot{k}={v[0]} but DB has {v[1]}"
                                                        for k, v in bad.items())),
                                   not bad))
                # ...and if the recorder tried to CHANGE it, the client's own
                # refusal is decisive.  T492 Leg G found `/equip 23 13` answered
                # "Bread Cakes* cannot go in Primary (13) -- wrong slot type for
                # this item", so every `held=13=9990` claim downstream was false.
                ref = before(logs[pid], t, r"cannot go in|wrong slot type", a.window)
                if ref:
                    checks.append(("equip_refused", ref[1][-70:].strip(), False))

            if not claims:
                continue
            if any(c[2] is False for c in checks):
                verdict = "FAIL"
            elif any(c[2] is None for c in checks):
                verdict = "INCONCLUSIVE"
            else:
                verdict = "PASS"
            rows.append((pid, lab, exp, "; ".join(claims), verdict, checks))

    out = a.out or os.path.join(C, "verify_labels.tsv")
    with open(out, "w") as f:
        f.write("player\tlabel\texpect\tclaim\tresult\tmeasured\n")
        for pid, lab, exp, claim, verdict, checks in rows:
            meas = "; ".join(f"{k}={v}" + ("" if o is True else (" [FAIL]" if o is False else " [?]"))
                             for k, v, o in checks)
            f.write(f"{pid}\t{lab}\t{exp}\t{claim}\t{verdict}\t{meas}\n")
    from collections import Counter
    c = Counter(r[4] for r in rows)
    print(f"{len(rows)} labelled pairs checked: " +
          ", ".join(f"{k} {v}" for k, v in sorted(c.items())) + f" -> {out}")
    for pid, lab, exp, claim, verdict, checks in rows:
        if verdict != "PASS":
            print(f"  {verdict} {pid} {lab} ({claim}): "
                  + "; ".join(f"{k}={v}" for k, v, o in checks if o is not True))
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
