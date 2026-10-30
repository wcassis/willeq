# B1209 (#528): which mounted-data spells route into the B1189 P13/P14
# addresses 0x491cff (the stage-FX visibility gate in FUN_0049b260) and
# 0x44d0d0 (the spell-id range validator), and which measured records carry
# the ring/disc EDD entries that are the client's actual aura / ground-circle
# visuals.
#
# Every table is produced over the mounted P1999 files; nothing is retyped.
# Re-run:
#   python3 refactor/B1209/scripts/census.py
# (reads /mnt/projects/EverQuestP1999, tests/spell_anim_rows.h and
#  refactor/B1184/tables/tour.tsv relative to the checkout root; writes
#  refactor/B1209/tables/*.tsv)

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "refactor", "B1189", "scripts"))
sys.path.insert(0, HERE)

import spellsnew_lib as lib  # noqa: E402
import call_sites as cs      # noqa: E402  (count_calls; import is side-effect free)

EQ = "/mnt/projects/EverQuestP1999"
OUT = os.path.join(ROOT, "refactor", "B1209", "tables")

SHAPE_NAME = {0: "point", 1: "even ring", 2: "stacked rings", 3: "cylinder",
              4: "disc", 5: "sphere", 6: "box", 7: "line/cone", 8: "torus",
              9: "random ring"}
RING_DISC = (1, 4, 9)          # the ring/disc spawn shapes
STAGE_NAME = {0: "Source", 1: "Middle", 2: "Target"}
ANCHOR_NAME = {0: "fixed world position", 1: "caster", 2: "missile", 3: "target"}
ATTACH_NAME = {0: "actor position", 1: "DEFAULT", 2: "CHEST", 3: "HEAD",
               4: "LHAND", 5: "RHAND", 6: "LFOOT", 7: "RFOOT", 8: "actor origin",
               9: "LEYE", 10: "REYE", 11: "MOUTH", 12: "CHEST->floor"}


def measured_records():
    """The 42 measured spellsnew.eff records: the 41 pinned `anim` values of
    tests/spell_anim_rows.h plus the records of the 7 tour spells
    (refactor/B1184/tables/tour.tsv)."""
    src = open(os.path.join(ROOT, "tests", "spell_anim_rows.h")).read()
    rows = re.findall(r"\{(\d+),\s*(\d+)\}", src)
    anim = {}
    for r, a in rows:
        anim[int(a)] = int(r)
    tour = {}
    with open(os.path.join(ROOT, "refactor", "B1184", "tables", "tour.tsv")) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            if c and c[0].isdigit():
                tour[int(c[0])] = c
    return anim, tour


def main():
    os.makedirs(OUT, exist_ok=True)
    eff = lib.parse_eff(os.path.join(EQ, "spellsnew.eff"))
    edd_ver, edd = lib.parse_edd(os.path.join(EQ, "spellsnew.edd"))
    spells = lib.load_spells(os.path.join(EQ, "spells_us.txt"))

    # --- route every spell through the two census questions --------------
    sn, legacy = [], []
    for sid in sorted(spells):
        idx, col = lib.spellsnew_record(spells[sid], eff)
        if idx is None:
            legacy.append(sid)
        else:
            sn.append(sid)

    anim, tour = measured_records()
    tour_records = {}
    for sid, c in tour.items():
        idx, col = lib.spellsnew_record(spells[sid], eff)
        tour_records[sid] = (idx, col)

    with open(os.path.join(OUT, "gate_reach.tsv"), "w") as f:
        f.write("kind\tcount\tnote\n")
        f.write("spells_total\t%d\trows in spells_us.txt\n" % len(spells))
        f.write("spellsnew_record\t%d\tcarry a non-empty spellsnew.eff record; "
                "every stage FX goes FUN_0049b260 -> FUN_00491cff "
                "(eqgame.exe.c:122018) on their draw\n" % len(sn))
        f.write("legacy_spells\t%d\tNO spellsnew record; their stage FX goes "
                "FUN_0049b260 -> the 0x49ab78 legacy arm, still gated by "
                "FUN_00491cff (eqgame.exe.c:122022)\n" % len(legacy))
        f.write("measured_records\t%d\tthe 41 pinned `anim` values of "
                "tests/spell_anim_rows.h (42 distinct records after dedupe; "
                "see auracircle_shaped.tsv for the record list)\n"
                % len(set(anim)))
        f.write("measured_tour\t%d\trefactor/B1184/tables/tour.tsv spells "
                "(records %s)\n" % (len(tour_records),
                  ", ".join(str(t[0]) for t in sorted(tour_records.values())
                            if t[0] is not None)))

    with open(os.path.join(OUT, "gate_reach_measured.tsv"), "w") as f:
        f.write("record\tsource\tspells\tedd_ring_disc_entries\n")
        recs = sorted(set(anim) | {t[0] for t in tour_records.values() if t[0]})
        for r in recs:
            src = ("spell_anim_rows:%s" % r) if r in anim else "tour"
            spell_ids = [s for s, a in tour_records.items() if a[0] == r]
            nrd = sum(1 for st in eff[r].stages for sl in st[1]
                      if 0 < sl.edd < len(edd) and edd[sl.edd].shape in RING_DISC)
            f.write("%d\t%s\t%s\t%d\n" % (r, src, ",".join(map(str, spell_ids)) or "-", nrd))

    # --- 0x44d0d0: spell id 1..9999 ----------------------------------------
    in_range = [s for s in spells if 1 <= s <= 9999]
    out_range = [s for s in spells if s < 1 or s > 9999]
    with open(os.path.join(OUT, "idcheck_reach.tsv"), "w") as f:
        f.write("kind\tcount\tnote\n")
        f.write("spells_total\t%d\trows in spells_us.txt\n" % len(spells))
        f.write("id_in_range_1_9999\t%d\tFUN_0044d0d0 returns 1 for them "
                "(eqgame.exe.c:68316: 1 <= id <= 9999)\n" % len(in_range))
        f.write("id_out_of_range\t%d\tids %s; every call site treats them as "
                "invalid\n" % (len(out_range),
                               ",".join(map(str, out_range)) or "none"))
        f.write("call_sites\t%d\tfull list: call_sites.tsv (call_sites.py)\n"
                % cs.count_calls("FUN_0044d0d0("))

    # --- the client's actual aura / ground-circle visuals ------------------
    # Ring/disc EDD entries of the measured records, per stage and slot.
    with open(os.path.join(OUT, "auracircle_shaped.tsv"), "w") as f:
        f.write("record\tstage\tslot\tedd_idx\tedd_name\tshape\tattach\t"
                "anchor\ttexture\tlifetime_s\tframes\tfps\n")
        for r in recs:
            for stage, (sound, slots) in enumerate(eff[r].stages):
                for si, sl in enumerate(slots):
                    if not (0 < sl.edd < len(edd)):
                        continue
                    e = edd[sl.edd]
                    if e.shape not in RING_DISC:
                        continue
                    f.write("%d\t%s\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%.3f\t%d\t%.1f\n"
                            % (r, STAGE_NAME[stage], si, sl.edd, e.name,
                               SHAPE_NAME[e.shape],
                               ATTACH_NAME.get(sl.attach, str(sl.attach)),
                               ANCHOR_NAME.get(sl.anchor, str(sl.anchor)),
                               e.texture, e.lifetime, e.frames, e.fps))

    # --- aura-named spells + the legacy population (P8 buff-tick auras) ----
    with open(os.path.join(OUT, "aura_candidates.tsv"), "w") as f:
        f.write("spell_id\tname\troute\thas_ring_disc_edd\trecord\n")
        for sid in sorted(spells):
            name = spells[sid][3] if len(spells[sid]) > 3 else ""
            idx, col = lib.spellsnew_record(spells[sid], eff)
            if idx is not None:
                route = "spellsnew"
            else:
                route = "legacy"
            nrd = sum(1 for st in eff[idx].stages for sl in st[1]
                      if 0 < sl.edd < len(edd) and edd[sl.edd].shape in RING_DISC) \
                if idx is not None else 0
            if "aura" in name.lower() or route == "legacy":
                f.write("%d\t%s\t%s\t%s\t%s\n"
                        % (sid, name, route,
                           "Y" if nrd else "N", idx if idx is not None else "-"))

    # --- spells.eff shape of the mounted legacy file -----------------------
    raw = open(os.path.join(EQ, "spells.eff"), "rb").read()
    with open(os.path.join(OUT, "legacy_file.tsv"), "w") as f:
        f.write("fact\tvalue\n")
        f.write("spells.eff_bytes\t%d\n" % len(raw))
        f.write("records_of_stride_0x2ba\t%d\t0x2ba is the stride the client "
                "loads with (eqgame.exe.c:49813: &DAT_008513b8 + idx*0x2ba)\n"
                % (len(raw) // 0x2ba))
        f.write("division_exact\t%s\n" % (len(raw) % 0x2ba == 0))

    # --- the P8 buff-tick gate's effect column -----------------------------
    # The per-frame buff walk (eqgame.exe.c:49746) ticks a legacy aura only
    # for a buff whose +0x22 char is '5' (eqgame.exe.c:49805). The server
    # sets that word from its own spells table; the P1999 column below is
    # the same logical field (effect), so this is the mounted-data census
    # of its VALUES, not a claim about the server's word.
    dist = {}
    for sid, f_ in spells.items():
        col = 11 if len(f_) == 209 else 12
        v = f_[col] if len(f_) > col else ""
        dist[v] = dist.get(v, 0) + 1
    with open(os.path.join(OUT, "effect_column.tsv"), "w") as f:
        f.write("effect_value\tspells\tnote\n")
        for v in sorted(dist, key=lambda x: (not x.isdigit(), int(x) if x.isdigit() else 0)):
            note = "the P8 buff-tick char ('5', eqgame.exe.c:49805)" if v == "5" else ""
            f.write("%s\t%d\t%s\n" % (v, dist[v], note))
        # the P8 class (effect '5') split by route
        def effcol(row):
            return row[11] if len(row) == 209 else (row[12] if len(row) > 12 else "")
        eff5 = [s for s in spells if effcol(spells[s]) == "5"]
        eff5_leg = [s for s in eff5
                    if lib.spellsnew_record(spells[s], eff)[0] is None]
        f.write("effect5_split_spellsnew\t%d\tthe P8 class WITH a spellsnew "
                "record: their CAST FX still goes the record path (122018); "
                "the buff tick is the buff half\n" % (len(eff5) - len(eff5_leg)))
        f.write("effect5_split_legacy\t%d\tthe P8 class with NO spellsnew "
                "record: the buff tick (49746-49823) is their only aura path\n"
                % len(eff5_leg))
        f.write("effect5_split_legacy_ids\t%s\n"
                % (",".join(map(str, sorted(eff5_leg))) or "none"))

    print("wrote", OUT)
    for t in ("gate_reach.tsv", "idcheck_reach.tsv"):
        print("==", t)
        print(open(os.path.join(OUT, t)).read())
    print("ring/disc rows:",
          sum(1 for _ in open(os.path.join(OUT, "auracircle_shaped.tsv"))) - 1)
    print("aura candidates:",
          sum(1 for _ in open(os.path.join(OUT, "aura_candidates.tsv"))) - 1)


if __name__ == "__main__":
    main()
