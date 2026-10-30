#!/usr/bin/env python3
# B1189 (#508): rank the uncovered EDD/EFF vocabulary values by how many spells
# reach them (all spells, and the castable subset), from spells_us.txt + the
# eff/edd files. Writes tables/ranking.tsv. Read-only, no network.
import os, sys, collections

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

# castable = at least one of the 16 class-min-level fields (0-based 103..118) > 0.
# These columns sit before the variable-width region, so they are stable.
def castable(fields):
    for i in range(103, 119):
        if i < len(fields) and fields[i].lstrip("-").isdigit() and int(fields[i]) > 0:
            return True
    return False

# spell -> record (union of candidate columns), and the set of EDD entries + slot
# values it reaches.
spell_rec = {}
for sid, f in spells.items():
    rec, col = L.spellsnew_record(f, eff)
    if rec is not None:
        spell_rec[sid] = rec

# For each EDD entry, which vocab "value keys" does it carry?
def edd_value_keys(e):
    keys = set()
    keys.add(("shape", e.shape))
    keys.add(("sprite_mode", e.sprite_mode))
    for attr, tname in (("orient_track", "orient_track"),
                        ("no_depth", "no_depth"),
                        ("additive", "additive_blend"),
                        ("attach_scale", "attach_scale"),
                        ("live_follow", "live_follow"),
                        ("neg_radius", "neg_radius"),
                        ("shape_scale", "shape_scale")):
        keys.add(("flag:" + tname, getattr(e, attr)))
    keys.add(("frames", L.frames_bucket(e.frames)))
    keys.add(("fps", L.fps_bucket(e.fps)))
    keys.add(("motion", L.motion_signature(e)))
    return keys

# counts: value key -> (num spells, num castable spells)
c_all = collections.Counter()
c_cast = collections.Counter()
# also per-EDD-entry reach for the file-wide "used by any spell" set
for sid, rec in spell_rec.items():
    reach = set()
    for _, slots in eff[rec].stages:
        for s in slots:
            if s.edd >= 1:
                reach.add(s.edd)
    is_cast = castable(spells[sid])
    seen_keys = set()
    for i in reach:
        for k in edd_value_keys(edd[i]):
            if k not in seen_keys:
                seen_keys.add(k)
                c_all[k] += 1
                if is_cast:
                    c_cast[k] += 1

# EFF slot values reached by spells (anchor/attach)
anchor_all = collections.Counter(); anchor_cast = collections.Counter()
attach_all = collections.Counter(); attach_cast = collections.Counter()
for sid, rec in spell_rec.items():
    is_cast = castable(spells[sid])
    seen_a = set(); seen_p = set()
    for _, slots in eff[rec].stages:
        for s in slots:
            if s.anchor not in seen_a:
                seen_a.add(s.anchor)
                anchor_all[s.anchor] += 1
                if is_cast: anchor_cast[s.anchor] += 1
            if s.attach not in seen_p:
                seen_p.add(s.attach)
                attach_all[s.attach] += 1
                if is_cast: attach_cast[s.attach] += 1

# ---- cross-reference each value against the in_measured flags (vocabulary.py) --
def load_col(fname, cols):
    """cols: list of column names to join into the key (last is the flag)."""
    m = {}
    with open(os.path.join(TABLES, fname)) as f:
        hdr = f.readline().rstrip("\n").split("\t")
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < len(hdr):
                continue
            key = tuple(c[hdr.index(n)] for n in cols)
            m[key] = (c[hdr.index("in_measured")] == "Y")
    return m

im = {}
for k, v in load_col("edd_shape.tsv", ["value", "in_measured"]).items():
    im[("shape", k[0])] = v
for k, v in load_col("edd_sprite_mode.tsv", ["value", "in_measured"]).items():
    im[("sprite_mode", k[0])] = v
for k, v in load_col("edd_flags.tsv", ["flag", "value", "in_measured"]).items():
    im[("flag:" + k[0], k[1])] = v
for k, v in load_col("edd_frames.tsv", ["bucket", "in_measured"]).items():
    im[("frames", k[0])] = v
for k, v in load_col("edd_fps.tsv", ["bucket", "in_measured"]).items():
    im[("fps", k[0])] = v
for k, v in load_col("edd_motion.tsv", ["signature", "in_measured"]).items():
    im[("motion", k[0])] = v
for k, v in load_col("eff_anchor.tsv", ["anchor", "in_measured"]).items():
    im[("anchor", k[0])] = v
for k, v in load_col("eff_attach.tsv", ["attach", "in_measured"]).items():
    im[("attach", k[0])] = v

# Full ranking (all values) with the in_measured flag.
rows = []
for k, n in c_all.items():
    kind, val = k
    rows.append((kind, str(val), n, c_cast.get(k, 0), "Y" if im.get((kind, str(val)), False) else "N"))
for v in range(4):
    rows.append(("anchor", str(v), anchor_all.get(v, 0), anchor_cast.get(v, 0),
                 "Y" if im.get(("anchor", str(v)), False) else "N"))
for v in range(13):
    rows.append(("attach", str(v), attach_all.get(v, 0), attach_cast.get(v, 0),
                 "Y" if im.get(("attach", str(v)), False) else "N"))
rows.sort(key=lambda r: (-r[3], -r[2], r[0], str(r[1])))

with open(os.path.join(TABLES, "ranking.tsv"), "w") as f:
    f.write("value_kind\tvalue\tspells_reaching\tcastable_reaching\tin_measured\n")
    for r in rows:
        f.write("\t".join(str(x) for x in r) + "\n")

cast_total = sum(1 for sid, f in spells.items() if castable(f))
print("total spells:", len(spells), " castable:", cast_total,
      " with spellsnew record:", len(spell_rec))
print("\nUNCOVERED values (in_measured=N), ranked by castable reach:")
uncovered = [r for r in rows if r[4] == "N" and r[3] > 0]
for r in uncovered[:40]:
    print("  %-12s %-20s cast=%-5d spells=%d" % (r[0], r[1], r[3], r[2]))
print("\nwrote", os.path.join(TABLES, "ranking.tsv"))
