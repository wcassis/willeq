#!/usr/bin/env python3
"""B1206 -- item and appearance wire tables from a decoded leg capture.

Input: the app-packet TSV that refactor/B1206/scripts/pcap_decode.py emits
for a leg's capture.pcap. Output (one TSV per --out-dir):

  items_wire.tsv      -- every OP_CharInventory record, decoded against the
                         field list of eqemu's Titanium::SerializeItem
                         (code/common/patches/titanium.cpp, the lane's
                         akk-tree copy; the field list here is that function
                         read line by line, instance block then quoted
                         static block, five effect blocks at the end).
  spawns_wire.tsv     -- every player Spawn_Struct: OP_ZoneEntry (self) and
                         OP_NewSpawn (others), 385-byte records
                         (include/common/packets/spawn.h Spawn_Struct; the
                         equipment words @197, the six 0x73/115 words, the
                         tint @348).
  wearchange_wire.tsv -- every OP_WearChange (spawn_id, material, color,
                         wear_slot; entities_component_appearance.cpp:427).

Usage:
  python3 wire_tables.py --app app.tsv --session 34202 --out-dir <dir>

The session arg is the SERVER port the caster's zone session used (the
pcap_decode TSV's sess column). The caster is whichever player spawn's
name matches --caster (default Testcaster); observer-side runs pass the
observer's zone session and read the caster from its NewSpawn.
"""
import argparse
import re
import struct
import sys

# --- item static field list: Titanium::SerializeItem, in order -------------
STATIC_FIELDS = (
    ["ItemClass", "Name", "Lore", "IDFile", "ID", "Weight", "NoRent",
     "NoDrop", "Size", "Slots", "Price", "Icon", "rs0", "rs1",
     "BenefitFlag", "Tradeskills", "CR", "DR", "PR", "MR", "FR",
     "AStr", "ASta", "AAgi", "ADex", "ACha", "AInt", "AWis", "HP",
     "Mana", "AC", "Deity", "SkillModValue", "SkillModMax", "SkillModType",
     "BaneDmgRace", "BaneDmgAmt", "BaneDmgBody", "Magic", "CastTime",
     "ReqLevel", "BardType", "BardValue", "Light", "Delay", "RecLevel",
     "RecSkill", "ElemDmgType", "ElemDmgAmt", "Range", "Damage", "Color",
     "Classes", "Races", "rs2", "MaxCharges", "ItemType", "Material",
     "SellRate", "rs3", "CastTime2", "rs4", "ProcRate", "CombatEffects",
     "Shielding", "StunResist", "StrikeThrough", "ExtraDmgSkill",
     "ExtraDmgAmt", "SpellShield", "Avoidance", "Accuracy", "CharmFileID",
     "FactionMod1", "FactionMod2", "FactionMod3", "FactionMod4",
     "FactionAmt1", "FactionAmt2", "FactionAmt3", "FactionAmt4",
     "CharmFile", "AugType",
     "AugSlotType0", "AugSlotVisible0", "AugSlotType1", "AugSlotVisible1",
     "AugSlotType2", "AugSlotVisible2", "AugSlotType3", "AugSlotVisible3",
     "AugSlotType4", "AugSlotVisible4",
     "LDoNTheme", "LDoNPrice", "LDoNSold", "BagType", "BagSlots", "BagSize",
     "BagWR", "Book", "BookType", "Filename", "BaneDmgRaceAmt",
     "AugRestrict", "LoreGroup", "PendingLoreFlag", "ArtifactFlag",
     "SummonedFlag", "Favor", "FVNoDrop", "Endur", "DotShielding",
     "Attack", "Regen", "ManaRegen", "EnduranceRegen", "Haste",
     "DamageShield", "RecastDelay", "RecastType", "GuildFavor",
     "AugDistiller", "rs5", "rs6", "Attuneable", "NoPet", "rs7",
     "PointType", "PotionBelt", "PotionBeltSlots", "StackSize",
     "NoTransfer", "Stackable",
     "ClickEffect", "ClickType", "ClickLevel2", "ClickLevel", "ClickName",
     "ProcEffect", "ProcType", "ProcLevel2", "ProcLevel", "ProcName",
     "WornEffect", "WornType", "WornLevel2", "WornLevel", "WornName",
     "FocusEffect", "FocusType", "FocusLevel2", "FocusLevel", "FocusName",
     "ScrollEffect", "ScrollType", "ScrollLevel2", "ScrollLevel",
     "ScrollName"])
# (titanium.cpp SerializeItem: the five trailing blocks are
# Click/Proc/Worn/Focus/Scroll, each Effect|Type|Level2|Level|name, with the
# name always written as the literal "0".)

INST_FIELDS = ["instStack", "instUnknown0", "instSlot", "instMerchantPrice",
               "instCount", "instExp", "instSerial", "instRecastTs",
               "instCharges", "instAttuned", "instUnknown1"]

RECORD_RE = re.compile(rb"(\d+(?:\|\d+){10})\|\"")


def decode_char_inventory(body):
    """Yield (inst dict, static dict) for each SerializeItem record."""
    pos = 0
    while True:
        m = RECORD_RE.search(body, pos)
        if not m:
            return
        inst = [int(x) for x in m.group(1).split(b"|")]
        if len(inst) != len(INST_FIELDS):
            raise ValueError(f"inst block {len(inst)} fields")
        q = m.end()  # first char after the opening quote
        end = body.find(b"\"", q)
        while end != -1 and body[end - 1:end] == b"\\":
            end = body.find(b"\"", end + 1)
        static = [s.decode("latin1") for s in body[q:end].split(b"|")]
        if len(static) != len(STATIC_FIELDS):
            raise ValueError(
                f"static block {len(static)} fields != {len(STATIC_FIELDS)}")
        yield dict(zip(INST_FIELDS, inst)), dict(zip(STATIC_FIELDS, static))
        pos = end + 1


def rec_fields(rec):
    return dict(
        name=rec[7:71].split(b"\x00")[0].decode("latin1"),
        gm=rec[1], npc=rec[83], level=rec[151], cls=rec[331],
        race=struct.unpack("<I", rec[284:288])[0],
        spawnId=struct.unpack("<I", rec[340:344])[0],
        equipment=" ".join(map(str, struct.unpack("<9I", rec[197:233]))),
        effect115=" ".join(map(str, struct.unpack("<6I", rec[115:139]))),
        tint=" ".join(f"{i}:{hex(v)}" for i, v in
                      enumerate(struct.unpack("<9I", rec[348:384])) if v),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--session", required=True, help="zone server port")
    ap.add_argument("--caster", default="Testcaster")
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args()

    rows = []
    with open(args.app) as f:
        next(f)
        for line in f:
            rows.append(line.rstrip("\n").split("\t"))

    items, spawns, wears = [], [], []
    for r in rows:
        if r[0] != args.session or r[1] != "s2c":
            continue
        data = bytes.fromhex(r[8])
        if r[5] == str(0x5394):  # CharInventory
            for inst, st in decode_char_inventory(data[2:]):
                row = {"leg_item": st["Name"], "instSlot": inst["instSlot"]}
                row.update({k: v for k, v in st.items()
                            if k in ("ID", "IDFile", "Slots", "Material",
                                     "Color", "CharmFile", "CharmFileID",
                                     "ClickEffect", "ProcEffect",
                                     "WornEffect", "FocusEffect",
                                     "ScrollEffect", "ProcType",
                                     "WornType", "ItemType", "BagType")})
                items.append(row)
        elif r[5] in (str(0x7213), str(0x1860)):  # ZoneEntry / NewSpawn
            body = data[2:]
            for i in range(len(body) // 385):
                f = rec_fields(body[i * 385:(i + 1) * 385])
                f["op"] = "ZoneEntry" if r[5] == str(0x7213) else "NewSpawn"
                f["ts"] = r[4]
                spawns.append(f)
        elif r[5] == str(0x7441):  # WearChange (9-byte body)
            body = data[2:]
            if len(body) == 9:
                sid, mat = struct.unpack("<HH", body[0:4])
                col, = struct.unpack("<I", body[4:8])
                wears.append({"ts": r[4], "spawn_id": sid, "material": mat,
                              "color": hex(col), "wear_slot": body[8]})

    import os
    os.makedirs(args.out_dir, exist_ok=True)
    if items:
        cols = ["leg_item", "instSlot", "ID", "IDFile", "Slots", "Material",
                "Color", "CharmFile", "CharmFileID", "ClickEffect",
                "ProcEffect", "WornEffect", "FocusEffect", "ScrollEffect",
                "ProcType", "WornType", "ItemType", "BagType"]
        with open(f"{args.out_dir}/items_wire.tsv", "w") as f:
            f.write("\t".join(cols) + "\n")
            for row in items:
                f.write("\t".join(str(row.get(c, "")) for c in cols) + "\n")
    if spawns:
        cols = ["op", "ts", "name", "spawnId", "level", "cls", "race",
                "equipment", "effect115", "tint"]
        with open(f"{args.out_dir}/spawns_wire.tsv", "w") as f:
            f.write("\t".join(cols) + "\n")
            for row in spawns:
                f.write("\t".join(str(row.get(c, "")) for c in cols) + "\n")
    if wears:
        cols = ["ts", "spawn_id", "material", "color", "wear_slot"]
        with open(f"{args.out_dir}/wearchange_wire.tsv", "w") as f:
            f.write("\t".join(cols) + "\n")
            for row in wears:
                f.write("\t".join(str(row[c]) for c in cols) + "\n")

    print(f"items={len(items)} spawns={len(spawns)} wears={len(wears)} "
          f"-> {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
