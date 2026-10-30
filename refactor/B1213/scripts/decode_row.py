#!/usr/bin/env python3
"""B1213 -- per-character item serialisation + spawn/wear words from the pcap.

The lane-5 capture was taken with `tcpdump -i any`, so it holds every client
session (the observer plus all 23 row wearers), each with its own zone
connection. For each character this extracts:

  spawn words   -- the 385-byte Spawn_Struct the server sends for that
                   character (include/common/packets/spawn.h): the nine
                   equipment model words (@197, the IT model numbers per
                   docs: equipment[] is a MATERIAL/IT model number, never a
                   db item id), the six 0x73/115 effect words, and the tint.
                   Taken from the character's OWN ZoneEntry (its own
                   perspective) and cross-checked against another client's
                   NewSpawn of the same character.
  inventory     -- the character's own OP_CharInventory, decoded against
                   eqemu's Titanium::SerializeItem field list (the full
                   static item record: ID, IDFile, Slots, Material, Color and
                   the five effect words). This is the richest "item
                   serialisation" -- how the item actually goes on the wire.

session -> owner mapping: a session's owner is the name of the FIRST spawn
record in that session's own ZoneEntry (the self-spawn is always first).

usage:
  decode_row.py --app <app_packets.tsv> --out <dir>
outputs: <dir>/spawns_wire.tsv  <dir>/items_wire.tsv  <dir>/wearchange_wire.tsv
         <dir>/item_words.tsv   (one consolidated row per FxR character)
"""
import argparse
import struct
import sys
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wire_tables import rec_fields, decode_char_inventory  # noqa: E402

OP_CHARINV = str(0x5394)
OP_ZONEENTRY = str(0x7213)
OP_NEWSPAWN = str(0x1860)
OP_WEAR = str(0x7441)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--keep-names", default=None,
                    help="space or comma separated names to keep (default FxR*)")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    rows = []
    with open(args.app) as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) >= 9 and p[1] == "s2c":
                rows.append(p)

    # pass 1: map session -> owner (first record of the first ZoneEntry)
    owner = {}
    for p in rows:
        sess, op, data = p[0], p[5], p[8]
        if op == OP_ZONEENTRY and sess not in owner:
            try:
                body = bytes.fromhex(data)[2:]
                owner[sess] = rec_fields(body[:385])["name"]
            except Exception:
                owner[sess] = "?"

    spawns, items, wears = [], [], []
    for p in rows:
        sess, op, ts, data_hex = p[0], p[5], p[4], p[8]
        try:
            data = bytes.fromhex(data_hex)
        except Exception:
            continue
        if op in (OP_ZONEENTRY, OP_NEWSPAWN):
            body = data[2:]
            for i in range(len(body) // 385):
                rec = rec_fields(body[i * 385:(i + 1) * 385])
                rec["op"] = "ZoneEntry" if op == OP_ZONEENTRY else "NewSpawn"
                rec["sess"] = sess
                rec["ts"] = ts
                spawns.append(rec)
        elif op == OP_CHARINV:
            for inst, st in decode_char_inventory(data[2:]):
                row = {"owner": owner.get(sess, "?"), "sess": sess,
                       "instSlot": inst.get("instSlot", "")}
                row.update({k: st.get(k, "") for k in
                            ("ID", "IDFile", "Slots", "Material", "Color",
                             "CharmFile", "CharmFileID", "ClickEffect",
                             "ProcEffect", "WornEffect", "FocusEffect",
                             "ScrollEffect", "ItemType")})
                items.append(row)
        elif op == OP_WEAR:
            body = data[2:]
            if len(body) == 9:
                sid, mat = struct.unpack("<HH", body[0:4])
                col, = struct.unpack("<I", body[4:8])
                wears.append({"sess": sess, "ts": ts, "spawn_id": sid,
                              "material": mat, "color": hex(col),
                              "wear_slot": body[8]})

    # spawns_wire / items_wire / wearchange_wire (all sessions)
    def dump(fn, cols, recs):
        with open(out / fn, "w") as f:
            f.write("\t".join(cols) + "\n")
            for r in recs:
                f.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")

    dump("spawns_wire.tsv", ["op", "sess", "ts", "name", "level", "cls", "race",
                             "spawnId", "equipment", "effect115", "tint"], spawns)
    dump("items_wire.tsv", ["owner", "sess", "instSlot", "ID", "IDFile", "Slots",
                            "Material", "Color", "CharmFileID", "ClickEffect",
                            "ProcEffect", "WornEffect", "ItemType"], items)
    dump("wearchange_wire.tsv", ["sess", "ts", "spawn_id", "material", "color",
                                 "wear_slot"], wears)

    # consolidated item_words.tsv: one row per kept character.
    # self-spawn = the ZoneEntry whose owning session maps to the character.
    keep = args.keep_names
    by_self = {}
    by_seen = {}
    for s in spawns:
        nm = s["name"]
        is_self = (s["op"] == "ZoneEntry" and owner.get(s["sess"]) == nm)
        if is_self:
            by_self[nm] = s
        else:
            by_seen.setdefault(nm, []).append(s)

    def want(nm):
        if keep:
            keepset = set(k for k in keep.replace(",", " ").split())
            return nm in keepset
        return nm.startswith("FxR")

    inv_by_owner = defaultdict(list)
    for it in items:
        if want(it["owner"]):
            inv_by_owner[it["owner"]].append(it)

    names = sorted(set(by_self) | set(by_seen), key=lambda n: (not want(n), n))
    with open(out / "item_words.tsv", "w") as f:
        f.write("char\tself_zoneentry\tseen_newspawn\tequipment_model_words\t"
                "effect115_words\ttint\tinv_items\tinv_IDFiles\n")
        for nm in names:
            if not want(nm):
                continue
            s = by_self.get(nm)
            seen = by_seen.get(nm, [])
            if s:
                equip, eff, tint = s["equipment"], s["effect115"], s["tint"]
            elif seen:
                equip, eff, tint = (seen[0][k] for k in
                                    ("equipment", "effect115", "tint"))
            else:
                equip = eff = tint = ""
            inv = inv_by_owner.get(nm, [])
            idfiles = " ".join(sorted(set(i["IDFile"] for i in inv
                                          if i["IDFile"])))
            f.write(f"{nm}\t{1 if s else 0}\t{len(seen)}\t{equip}\t{eff}\t"
                    f"{tint}\t{len(inv)}\t{idfiles}\n")

    print(f"spawns={len(spawns)} items={len(items)} wears={len(wears)} "
          f"owners={len(owner)} -> {out}")
    print("self-zoneentries for FxR: " +
          ", ".join(sorted(n for n in by_self if n.startswith("FxR"))))


if __name__ == "__main__":
    main()
