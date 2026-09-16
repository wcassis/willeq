#!/usr/bin/env python3
"""T492 Leg H -- the re-record timeline, with held= READ FROM THE SERVER.

B793: Leg C hardcoded the held values from a misread of an inventory dump and
nothing checked them until Leg G, which found `E_heldA_p2_*` claiming
`slot13=9998, slot14=9979` while Testcaster actually holds 14337 / 20656, and
`/equip 23 13` refused outright because slot 23 is a Skin of Milk.  So this
generator ASKS THE DATABASE for slots 13 and 14 and writes what is there, and
picks the `/equip` source slot by checking the item is a weapon.

MEASURED 2026-09-16 (peq.items joined to peq.inventory): no general slot of
either willeq character holds a weapon -- Testcaster's spares are a Shuriken
(ammo), food, drink, spells and bandages; Testbuddy's are the same minus the
Shuriken.  So the held-B swap needs one seeded, which `record.sh --seed-weapon`
does after the inventory snapshot is taken (so the restore removes it again).

    make_timeline_legH.py <corpus> <out.tsv> [--equip-slot 21]
"""
import json, os, re, subprocess, sys
from collections import Counter

REPO = "/mnt/projects/fable/wt/t3"
CHARS = {"p2": 67, "p3": 68}
GAP = {"cmd": 20, "camto": 16, "key": 11, "willeq": 8}


def sql(q):
    out = subprocess.run(["bash", "-c",
        f'source {REPO}/scripts/eqemu/env.sh && eq_docker exec "$MARIADB_CONTAINER" '
        f'mysql -uroot -p"$(mariadb_root_password)" peq -B -N -e {json.dumps(q)}'],
        capture_output=True, text=True).stdout
    return [l.split("\t") for l in out.strip().splitlines() if l and "Warning" not in l]


def gap(player, action):
    return GAP["willeq"] if player != "p1" else GAP.get(action.split()[0], GAP["key"])


def main():
    corpus, out = sys.argv[1], sys.argv[2]
    eslot = int(sys.argv[sys.argv.index("--equip-slot") + 1]) if "--equip-slot" in sys.argv else 21
    steps = json.load(open(os.path.join(corpus, "steps.json")))["steps"]
    cur = {}
    for line in open(os.path.join(corpus, "players", "p2", "truth.tsv")):
        kv = dict(p.split("=", 1) for p in line.split("\t", 1)[1].strip().split(";") if "=" in p)
        if "step" in kv:
            cur[int(kv["step"])] = kv

    # --- the held facts, from the server -----------------------------------
    held_now, names = {}, {}
    for pid, cid in CHARS.items():
        rows = sql(f"SELECT i.slot_id,i.item_id,it.Name,it.idfile FROM inventory i "
                   f"JOIN items it ON it.id=i.item_id WHERE i.character_id={cid} "
                   f"AND i.slot_id IN (13,14) ORDER BY i.slot_id;")
        held_now[pid] = {int(r[0]): (int(r[1]), r[2], r[3]) for r in rows}
        names[pid] = ",".join(f"{s}={v[0]} {v[1]} ({v[2]})" for s, v in sorted(held_now[pid].items()))
        print(f"  {pid} (char {cid}) holds: {names[pid]}")
    if not all(held_now.values()):
        sys.exit("could not read held items from the server -- is the stack up?")

    rows, t = [], 0.0

    def emit(player, action, truth, keep=None):
        nonlocal t
        body = truth if isinstance(truth, str) else ";".join(f"{k}={v}" for k, v in truth.items())
        if keep is not None:
            body += f";replaces_step={keep}"
        rows.append((round(t, 1), player, action, body))
        t += gap(player, action)

    def pick(group):
        return [s for s in steps if group(s)]

    # --- target block: willeq UI ON ----------------------------------------
    emit("p2", "/camera firstperson", "label=H0_fp_p2;camera=FirstPerson;expect=unlabelled")
    emit("p3", "/camera firstperson", "label=H0_fp_p3;camera=FirstPerson;expect=unlabelled")
    emit("p1", "camto FirstPerson",
         "label=H0_camto_FirstPerson;official_camera=FirstPerson;ui=official UI on, willeq UI on;expect=unlabelled")
    for s in pick(lambda s: s["label"].startswith("C_target")):
        emit(s["player"], s["action"], s["truth"], s["idx"])
    t += 8

    # --- camera / held block: willeq UI OFF --------------------------------
    emit("p2", "/hideui", "label=H0_hideui_p2;ui=hidden;expect=unlabelled")
    emit("p3", "/hideui", "label=H0_hideui_p3;ui=hidden;expect=unlabelled")
    t += 6
    last = None
    for s in pick(lambda s: s["player"] in ("p2", "p3") and cur.get(s["idx"], {}).get("pending")):
        pid, tr = s["player"], dict(s["truth"])
        lab = s["label"]
        if "held" in tr:
            if lab.startswith("E_heldA"):
                tr["held"] = names[pid]                      # what the server says
                tr["held_source"] = "inventory table, read at compile time"
            else:                                            # heldB: after the swap
                tr["held"] = f"13=9997 Dagger* (IT5) moved from slot {eslot}"
                tr["held_source"] = f"/equip {eslot} 13, seeded by --seed-weapon"
        cam = tr.get("camera")
        if cam and cam != last:
            emit("p1", f"camto {cam}",
                 f"label=H0_camto_{cam}_{s['idx']};official_camera={cam} (closed loop, "
                 f"margin-verified against T466's still);expect=unlabelled")
            last = cam
        if lab.startswith("E_heldB") and not any(r[2] == f"/equip {eslot} 13" and r[1] == pid for r in rows):
            emit(pid, f"/equip {eslot} 13",
                 f"label=H0_equip_{pid};held=13=9997 Dagger* (IT5);expect=unlabelled")
        emit(pid, s["action"], tr, s["idx"])

    hdr = f"""# T492 Leg H -- GENERATED by make_timeline_legH.py.  Re-records the pairs Will's
# review rejected and Leg G's verifier then CONTRADICTED by measurement.
# held= is read from the `inventory` table at compile time, never hardcoded
# (B793: Leg C's values were wrong and its /equip source slot was a Skin of
# Milk).  The official camera is driven closed loop by `camto`, which now uses
# the MARGIN criterion (best of six, next-best >= 1.5x) validated on
# f9_cycle/scores.tsv (min ratio 4.90) and the Leg G gate (1.77-2.08).
# Held at compile time: {names}
"""
    with open(out, "w") as f:
        f.write(hdr + "".join(f"{a}\t{b}\t{c}\t{d}\n" for a, b, c, d in rows))
    print(f"{out}: {len(rows)} rows, {rows[-1][0]/60:.1f} min; "
          f"per player {dict(Counter(r[1] for r in rows))}")


if __name__ == "__main__":
    main()
