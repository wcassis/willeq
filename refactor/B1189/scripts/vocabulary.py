#!/usr/bin/env python3
# B1189 (#508): EDD/EFF vocabulary exercised by the 47 measured records vs the whole
# spellsnew.edd population, plus the legacy-path population (spells with no
# spellsnew.eff record). Writes tables to refactor/B1189/tables/*.tsv and prints a
# summary. Read-only over the mounted P1999 data; no network.
import os, sys, re, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spellsnew_lib as L

P1999 = os.environ.get("B1189_P1999", "/mnt/projects/EverQuestP1999")
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TABLES = os.path.join(REPO, "refactor", "B1189", "tables")
os.makedirs(TABLES, exist_ok=True)

eff = L.parse_eff(os.path.join(P1999, "spellsnew.eff"))
edd_ver, edd = L.parse_edd(os.path.join(P1999, "spellsnew.edd"))
spells = L.load_spells(os.path.join(P1999, "spells_us.txt"))


# ---- the measured record set -------------------------------------------------
# 1) the committed ground truth: tests/spell_anim_rows.h (B1137), one anim
#    (spellsnew.eff record index) per measured Source/Target row.
def measured_from_anim_rows():
    p = os.path.join(REPO, "tests", "spell_anim_rows.h")
    anims, rows = [], {}
    in_arr = False
    for line in open(p):
        m = re.match(r"\{(\d+),\s*(\d+)\}", line.strip())
        if m:
            rows[int(m.group(1))] = int(m.group(2))
            anims.append(int(m.group(2)))
    return rows, anims

row_anim, anim_rows = measured_from_anim_rows()

# 2) the tour-7 spells (refactor/B1184/tables/tour.tsv), resolved to records via the
#    P1999 spellsnew column.
def tour_records():
    p = os.path.join(REPO, "refactor", "B1184", "tables", "tour.tsv")
    recs, detail = [], {}
    with open(p) as f:
        hdr = f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            sid = int(c[0])
            rec, col = L.spellsnew_record(spells[sid], eff) if sid in spells else (None, None)
            detail[sid] = rec
            if rec is not None:
                recs.append(rec)
    return recs, detail

tour_recs, tour_detail = tour_records()

measured = sorted(set(anim_rows) | set(tour_recs))

# ---- which EDD entries do the measured records reference? --------------------
measured_edd = set()
measured_slots = []            # (record, stage, slot) for the EFF vocab
for r in measured:
    rec = eff[r]
    for stage, (sound, slots) in enumerate(rec.stages):
        for s in slots:
            measured_slots.append((r, stage, s.anchor, s.attach, s.min_level))
            if s.edd >= 1:
                measured_edd.add(s.edd)


def count_values(entries, fn):
    c = collections.Counter()
    for e in entries:
        c[fn(e)] += 1
    return c


# The whole-file population = every EDD entry that is referenced by ANY spell (not
# just the measured ones). "Any spell" = any spellsnew.eff record whose name is
# non-empty and that some spell points at. We also report the literal whole file.
file_edd_referenced = set()
for r, rec in enumerate(eff):
    if rec.name == "":
        continue
    for _, slots in rec.stages:
        for s in slots:
            if s.edd >= 1:
                file_edd_referenced.add(s.edd)

measured_entries = [edd[i] for i in sorted(measured_edd)]
ref_entries = [edd[i] for i in sorted(file_edd_referenced)]
all_entries = list(edd)

edds = sorted(set(measured_edd) | set(file_edd_referenced) | set(range(len(edd))))


def vocab_table(fn, order=None):
    cm = count_values(measured_entries, fn)
    cf = count_values(ref_entries, fn)
    ca = count_values(all_entries, fn)
    vals = order if order else sorted(set(cm) | set(cf) | set(ca),
                                      key=lambda v: (isinstance(v, str), v))
    rows = []
    for v in vals:
        rows.append((str(v), cm.get(v, 0), cf.get(v, 0), ca.get(v, 0),
                     "Y" if cm.get(v, 0) else "N"))
    return rows


def write_tsv(name, header, rows):
    p = os.path.join(TABLES, name)
    with open(p, "w") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    return p


# ---- EDD entry vocabulary ----------------------------------------------------
SHAPES = list(range(10))
shapes = vocab_table(lambda e: e.shape, SHAPES)
p_shapes = write_tsv("edd_shape.tsv", ["value", "measured", "file_ref", "file_all", "in_measured"], shapes)

sprite_mode = vocab_table(lambda e: e.sprite_mode, [0, 1, 2])
p_sm = write_tsv("edd_sprite_mode.tsv", ["value", "measured", "file_ref", "file_all", "in_measured"], sprite_mode)

flags = []
for name, attr, vals in [
    ("orient_track", "orient_track", [0, 1]),
    ("no_depth", "no_depth", [0, 1]),
    ("additive_blend", "additive", [0, 1]),
    ("attach_scale", "attach_scale", [0, 1]),
    ("live_follow", "live_follow", [0, 1]),
    ("neg_radius", "neg_radius", [0, 1]),
    ("shape_scale", "shape_scale", [0, 1]),
]:
    cm = count_values(measured_entries, lambda e, a=attr: getattr(e, a))
    cf = count_values(ref_entries, lambda e, a=attr: getattr(e, a))
    ca = count_values(all_entries, lambda e, a=attr: getattr(e, a))
    for v in vals:
        flags.append((name, str(v), cm.get(v, 0), cf.get(v, 0), ca.get(v, 0),
                      "Y" if cm.get(v, 0) else "N"))
p_flags = write_tsv("edd_flags.tsv", ["flag", "value", "measured", "file_ref", "file_all", "in_measured"], flags)

frames = vocab_table(lambda e: L.frames_bucket(e.frames),
                     ["1 (whole)", "2-4 (2x2)", "5-8 (4x2)", "9-16 (4x4)", ">16 (whole)"])
p_frames = write_tsv("edd_frames.tsv", ["bucket", "measured", "file_ref", "file_all", "in_measured"], frames)

fps = vocab_table(lambda e: L.fps_bucket(e.fps),
                  ["0", "<1", "1-10", "11-30", "31-60", ">60"])
p_fps = write_tsv("edd_fps.tsv", ["bucket", "measured", "file_ref", "file_all", "in_measured"], fps)

motion = vocab_table(lambda e: L.motion_signature(e))
p_motion = write_tsv("edd_motion.tsv", ["signature", "measured", "file_ref", "file_all", "in_measured"], motion)

tex = vocab_table(lambda e: e.texture or "(none)")
p_tex = write_tsv("edd_texture.tsv", ["texture", "measured", "file_ref", "file_all", "in_measured"], tex)

# ---- EFF slot vocabulary (measured records vs all non-empty records) ---------
all_slots = []
for r, rec in enumerate(eff):
    if rec.name == "":
        continue
    for stage, (sound, slots) in enumerate(rec.stages):
        for s in slots:
            all_slots.append((s.anchor, s.attach, s.min_level))

def slotcount(rows, which):
    c = collections.Counter()
    for row in rows:
        c[(row[0], row[1])] += 1
    return c

anchor_cm = collections.Counter(a for (r, st, a, at, ml) in measured_slots)
anchor_cf = collections.Counter(a for (a, at, ml) in all_slots)
att_cm = collections.Counter(at for (r, st, a, at, ml) in measured_slots)
att_cf = collections.Counter(at for (a, at, ml) in all_slots)
ml_cm = collections.Counter(ml for (r, st, a, at, ml) in measured_slots)
ml_cf = collections.Counter(ml for (a, at, ml) in all_slots)

anchor_rows = []
for v in range(4):
    anchor_rows.append((str(v), anchor_cm.get(v, 0), anchor_cf.get(v, 0),
                        "Y" if anchor_cm.get(v, 0) else "N"))
p_anchor = write_tsv("eff_anchor.tsv", ["anchor", "measured", "file_ref", "in_measured"], anchor_rows)
attach_rows = []
for v in range(13):
    attach_rows.append((str(v), att_cm.get(v, 0), att_cf.get(v, 0),
                        "Y" if att_cm.get(v, 0) else "N"))
p_attach = write_tsv("eff_attach.tsv", ["attach", "measured", "file_ref", "in_measured"], attach_rows)
ml_vals = sorted(set(ml_cm) | set(ml_cf))
ml_rows = [(str(v), ml_cm.get(v, 0), ml_cf.get(v, 0), "Y" if ml_cm.get(v, 0) else "N") for v in ml_vals]
p_ml = write_tsv("eff_minlevel.tsv", ["min_level", "measured", "file_ref", "in_measured"], ml_rows)

# ---- legacy population: spells with NO spellsnew.eff record ------------------
legacy = []
sp_with_rec = 0
col_hits = collections.Counter()
for sid in sorted(spells):
    rec, col = L.spellsnew_record(spells[sid], eff)
    if rec is None:
        legacy.append(sid)
    else:
        sp_with_rec += 1
        col_hits[col] += 1
p_legacy = write_tsv("legacy_spells.tsv", ["spell_id"], [(s,) for s in legacy])

# ---- summary -----------------------------------------------------------------
print("P1999:", P1999)
print("EFF records:", len(eff), " EDD version:", edd_ver, " EDD entries:", len(edd))
print("spells_us spells:", len(spells))
print("anim_rows (committed):", len(anim_rows), "distinct records:", len(set(anim_rows)))
print("tour records:", tour_recs, " detail:", tour_detail)
print("MEASURED distinct records:", len(measured), "=", measured)
print("MEASURED distinct EDD entries referenced:", len(measured_edd))
print("FILE non-empty EFF records:", sum(1 for r in eff if r.name != ""))
print("FILE EDD entries referenced by any non-empty EFF record:", len(file_edd_referenced))
print("spells WITH a spellsnew record:", sp_with_rec, " by column:", dict(col_hits))
print("LEGACY spells (no spellsnew record):", len(legacy), " examples:", legacy[:20])
print("tables written to", TABLES)
