#!/usr/bin/env python3
"""B1213 (#532): generate the worn-FX row of characters.

Outputs (all under refactor/B1213/):
  preseed/row_preseed.sql   - accounts + login_accounts + characters + inventory
  configs/<char>.json        - one willeq wearer config per character
  tables/row_layout.tsv      - char -> item(s), class, row index, position
  tables/timestamps.tsv      - per-character t_start/t_end (filled by the run)

Design (Will, issue #532): every row of refactor/B1206/tables/items.tsv is worn
by a character standing in a row at fixed scripted positions in qeynos2
(zone_id=2). The observer (official client, B1134 rig) walks the row at close
range, 20 s each, one continuous video. The positive control (Fiery Avenger,
which Will confirmed glows) sits at the head of the row (index 0).

The class mask in items.tsv is `classes = 1 << (class# - 1)` (eqemu common/
classes.h: Warrior=1, Cleric=2, Paladin=3, Ranger=4, ShadowKnight=5, Druid=6,
Monk=7, Bard=8, Rogue=9, Shaman=10, Necromancer=11, Wizard=12, Magician=13,
Enchanter=14, Beastlord=15). Each character's class is chosen from its item's
mask; the equipment slot is the `slot` column from items.tsv (proven on this
server by B1206).
"""
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)
ZONE_ID = 2          # qeynos2
ROW_X = -74.0
ROW_Y0 = 420.0       # first character's y (Testcaster sits at 428; we start at 420)
ROW_DY = 8.0         # spacing along the row (down -y)
ROW_Z = 3.128
HEADING = 160
LEVEL = 90
EXP = 616137024
DEITY = 206
PASS_HASH = None     # set from the lane (login_accounts hash of WillEqH03Pass)
ACCT_PASS = "WillEqH03Pass"  # plaintext for willeq configs (known test password)
BASE_ACCT_ID = 10000 # start new account ids well clear of the existing 1..80
BASE_LOGIN_ID = 20000 # login_accounts.id is NOT auto-increment (explicit ids)
BASE_CHAR_ID = 5000  # start new character ids well clear of the existing 1..104
BASE_GUID = 900000000

# class -> (eqemu class#, a valid race for it)
# Human (1) is valid for every class here except Shaman, which uses Orc (6).
CLASS_RACE = {
    "Warrior": (1, 1), "Cleric": (2, 1), "Paladin": (3, 1), "Ranger": (4, 1),
    "ShadowKnight": (5, 1), "Druid": (6, 1), "Monk": (7, 1), "Bard": (8, 1),
    "Rogue": (9, 1), "Shaman": (10, 6), "Necromancer": (11, 1),
    "Wizard": (12, 1), "Magician": (13, 1), "Enchanter": (14, 1),
    "Beastlord": (15, 1),
}

# Roster: (row_index, char_name, class, [(item_id, item_name, slot), ...], note)
# Every row of items.tsv is covered exactly once. Index 0 = positive control.
ROSTER = [
    (0,  "FxR01", "Paladin",      [(11050, "Fiery Avenger", 13)],            "POSITIVE CONTROL (head of row)"),
    (1,  "FxR02", "Warrior",      [(66176, "Blade of Tactics", 13),
                                   (66177, "Blade of Strategy", 14)],        "warrior A 1hs pair (expect red + blue)"),
    (2,  "FxR03", "Warrior",      [(66175, "Jagged Blade of War", 13)],      "warrior B 2hs (expect dual-colour)"),
    (3,  "FxR04", "Ranger",       [(20487, "Swiftwind", 14),
                                   (20488, "Earthcaller", 13)],              "ranger pair (expect lightning)"),
    (4,  "FxR05", "Paladin",      [(10099, "Fiery Defender", 13)],           "paladin epic"),
    (5,  "FxR06", "Wizard",       [(14341, "Staff of the Four", 13)],        "wizard epic"),
    (6,  "FxR07", "Rogue",        [(11057, "Ragebringer", 13)],              "rogue epic"),
    (7,  "FxR08", "Bard",         [(20542, "Singing Short Sword", 14)],      "bard epic"),
    (8,  "FxR09", "Necromancer",  [(20544, "Scythe of the Shadowed Soul", 13)], "necromancer epic"),
    (9,  "FxR10", "Monk",         [(10652, "Celestial Fists", 12)],          "monk epic"),
    (10, "FxR11", "Enchanter",    [(10650, "Staff of the Serpent", 13)],     "enchanter epic"),
    (11, "FxR12", "ShadowKnight", [(14383, "Innoruuk's Curse", 13)],         "shadow knight epic"),
    (12, "FxR13", "Cleric",       [(5532, "Water Sprinkler of Nem Ankh", 13)], "cleric epic"),
    (13, "FxR14", "Magician",     [(28034, "Orb of Mastery", 13)],           "magician epic"),
    (14, "FxR15", "Shaman",       [(10651, "Spear of Fate", 13)],            "shaman epic"),
    (15, "FxR16", "Druid",        [(20490, "Nature Walkers Scimitar", 13)],  "druid epic"),
    (16, "FxR17", "Paladin",      [(5504, "SoulFire", 13)],                  "non-epic effect item"),
    (17, "FxR18", "Paladin",      [(5403, "Ghoulbane", 13)],                 "non-epic effect item"),
    (18, "FxR19", "Paladin",      [(25210, "Blade of Carnage", 14)],         "non-epic effect item"),
    (19, "FxR20", "Warrior",      [(6612, "Jade Mace", 14)],                 "non-epic (no FX expected)"),
    (20, "FxR21", "Warrior",      [(50949, "Red Dragon Scale", 9)],          "non-epic (no FX expected)"),
    (21, "FxR22", "Rogue",        [(14649, "Jaded Electrum Bracelet", 9)],   "non-epic (no FX expected), items.tsv row 14649"),
    (22, "FxR23", "Rogue",        [(16698, "Jaded Electrum Bracelet", 9)],   "non-epic (no FX expected), items.tsv row 16698"),
]

# stats template (from Testcaster, char 67) - high enough for level 90, valid all classes
STATS = dict(cur_hp=1218, mana=3091, endurance=1400,
             str=60, sta=80, cha=60, dex=75, int=134, agi=90, wis=83)


def item_ids():
    return [iid for _, _, _, items, _ in ROSTER for (iid, _, _) in items]


def write_preseed_sql(path, pass_hash):
    L = []
    L.append("-- B1213 (#532) worn-FX row preseed. "
             "Runs inside the lane lock after the DB snapshot; the snapshot")
    L.append("-- restore removes every account/character/inventory row created here.")
    L.append("START TRANSACTION;")
    acct_rows, char_rows, inv_rows = [], [], []
    for idx, name, cls, items, note in ROSTER:
        acct_id = BASE_ACCT_ID + idx
        char_id = BASE_CHAR_ID + idx
        cls_num, race = CLASS_RACE[cls]
        y = ROW_Y0 - ROW_DY * idx
        acct_rows.append(
            f"INSERT INTO account (id, name, auto_login_charname, status, ls_id, lsaccount_id) "
            f"VALUES ({acct_id}, 'fxrow_{name.lower()}', '{name}', 0, 'local', {BASE_LOGIN_ID + idx});")
        acct_rows.append(
            f"INSERT INTO login_accounts (id, account_name, account_email, account_password, source_loginserver, last_ip_address, last_login_date) "
            f"VALUES ({BASE_LOGIN_ID + idx}, 'fxrow_{name.lower()}', 'local_creation', '{pass_hash}', 'local', '127.0.0.1', NOW());")
        col_vals = (f"{char_id},{acct_id},'{name}',0,{ZONE_ID},0,{ROW_X},{y},{ROW_Z},{HEADING},"
                    f"1,{race},{cls_num},{LEVEL},{DEITY},0")
        col_names = ("id,account_id,name,suffix,zone_id,zone_instance,x,y,z,heading,"
                     "gender,race,class,level,deity,birthday")
        stat_part = (f"{STATS['cur_hp']},{STATS['mana']},{STATS['endurance']},"
                     f"{STATS['str']},{STATS['sta']},{STATS['cha']},{STATS['dex']},"
                     f"{STATS['int']},{STATS['agi']},{STATS['wis']},{EXP}")
        stat_names = ("cur_hp,mana,endurance,str,sta,cha,dex,`int`,agi,wis,exp")
        char_rows.append(
            f"INSERT INTO character_data ({col_names},{stat_names}) "
            f"VALUES ({col_vals},{stat_part});")
        for iid, iname, slot in items:
            guid = BASE_GUID + char_id + slot
            inv_rows.append(
                f"INSERT INTO inventory (character_id,slot_id,item_id,charges,color,"
                f"augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,"
                f"instnodrop,ornament_icon,ornament_idfile,ornament_hero_model,guid) "
                f"VALUES ({char_id},{slot},{iid},0,4278190080,0,0,0,0,0,0,0,0,0,0,{guid});")
    L.extend(acct_rows)
    L.extend(char_rows)
    L.extend(inv_rows)
    L.append("COMMIT;")
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")
    return len(ROSTER)


def write_configs(cfgdir, server, host, port):
    for idx, name, cls, items, note in ROSTER:
        cfg = {
            "clients": [{
                "host": host, "port": port,
                "user": f"fxrow_{name.lower()}", "pass": ACCT_PASS,
                "server": server, "character": name,
                "eq_client_path": "/mnt/projects/EverQuestP1999",
            }]
        }
        os.makedirs(cfgdir, exist_ok=True)
        with open(os.path.join(cfgdir, f"{name}.json"), "w") as f:
            json.dump(cfg, f, indent=2)
            f.write("\n")


def write_layout(path):
    L = ["row_index\tchar\tclass\titems\tslots\tx\ty\tnote"]
    for idx, name, cls, items, note in ROSTER:
        y = ROW_Y0 - ROW_DY * idx
        it = "; ".join(f"{n}({i})" for i, n, _ in items)
        sl = "; ".join(str(s) for _, _, s in items)
        L.append(f"{idx}\t{name}\t{cls}\t{it}\t{sl}\t{ROW_X}\t{y:.3f}\t{note}")
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")


def write_timestamps(path):
    L = ["row_index\tchar\titem\tt_start\tt_end\tstill\tinstrument_signal"]
    for idx, name, cls, items, note in ROSTER:
        for iid, iname, slot in items:
            L.append(f"{idx}\t{name}\t{iname}\t\t\t\t")
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")


def main():
    pass_hash = os.environ.get("B1213_PASS_HASH", "").strip()
    os.makedirs(os.path.join(OUT, "preseed"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "tables"), exist_ok=True)
    n = write_preseed_sql(os.path.join(OUT, "preseed", "row_preseed.sql"), pass_hash)
    write_configs(os.path.join(OUT, "configs"), os.environ.get("B1213_SERVER", "WillEQ"),
                  os.environ.get("B1213_HOST", "127.0.0.1"),
                  int(os.environ.get("B1213_PORT", "5998")))
    write_layout(os.path.join(OUT, "tables", "row_layout.tsv"))
    write_timestamps(os.path.join(OUT, "tables", "timestamps.tsv"))
    print(f"generated {n} characters, {len(item_ids())} item slots")
    print("items covered:", ", ".join(str(i) for i in item_ids()))


if __name__ == "__main__":
    main()
