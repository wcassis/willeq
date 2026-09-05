#!/usr/bin/env python3
"""T380 - spell visual data census (B022).

Answers, with numbers rather than argument:

  1. How many of spells.eff's 256 fixed-size records carry any content
     (a sprite name or an attach tag in any of their three emitter blocks)?
  2. For every spell in spells_us.txt, does its SpellAffectIndex (field 123)
     land on a populated record, on an empty PADDING record, or is it the
     -1 "no visual" marker?
  3. What do the padding-hit spells look like by class and by minimum level
     -- specifically, are they spells a low-level test character will cast?

The record layout is the one pinned by include/client/spell/spells_eff_reader.h:
  256 records x 2792 bytes; each record = 8-byte header + 3 emitter blocks of
  928 bytes; each emitter block = 4 x 32-byte NUL-terminated strings
  (3 sprite names + 1 attach tag) followed by 800 unread bytes.

This script reads ONLY; it changes nothing. Default paths match the dev VM.

Usage:
  python3 scripts/refactor/t380_spell_visual_census.py \
      [--eq-path /mnt/projects/EverQuestP1999] [--db data/willeq.db]
"""

import argparse
import collections
import os
import sqlite3
import sys

REC_SIZE = 2792
REC_COUNT = 256
REC_HEADER = 8
EMITTERS_PER_RECORD = 3
EMITTER_SIZE = 928
EMITTER_STRINGS = 4

# spells_us.txt field indices (include/client/spell/spell_constants.h).
F_ID = 0
F_NAME = 1
F_RESIST = 85
F_CLASS_LEVEL_1 = 104
F_CAST_ANIM = 120
F_TARGET_ANIM = 121
F_TRAVEL_TYPE = 122
F_AFFECT_INDEX = 123

# PlayerClass enum order, fields 104..119.
CLASS_NAMES = [
    "Warrior", "Cleric", "Paladin", "Ranger", "ShadowKnight", "Druid",
    "Monk", "Bard", "Rogue", "Shaman", "Necromancer", "Wizard",
    "Magician", "Enchanter", "Beastlord", "Berserker",
]


def cstr32(buf, off):
    end = buf.find(b"\x00", off, off + 32)
    if end < 0:
        end = off + 32
    return buf[off:end].decode("latin-1")


def load_eff(path):
    """Return a list of 256 dicts: {'content': bool, 'emitters': [...]}"""
    data = open(path, "rb").read()
    expected = REC_SIZE * REC_COUNT
    if len(data) != expected:
        sys.exit("spells.eff size mismatch: expected %d, got %d" % (expected, len(data)))
    records = []
    for i in range(REC_COUNT):
        base = i * REC_SIZE
        emitters = []
        for b in range(EMITTERS_PER_RECORD):
            eb = base + REC_HEADER + b * EMITTER_SIZE
            strings = [cstr32(data, eb + s * 32) for s in range(EMITTER_STRINGS)]
            emitters.append({
                "sprites": strings[:3],
                "attach": strings[3],
                "content": any(s for s in strings),
            })
        records.append({
            "index": i,
            "emitters": emitters,
            "content": any(e["content"] for e in emitters),
        })
    return records


def load_spells(path):
    spells = []
    with open(path, "r", encoding="latin-1") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            f = line.split("^")
            if len(f) < 200:
                continue
            try:
                sid = int(f[F_ID])
            except ValueError:
                continue
            if sid <= 0:
                continue

            def iv(idx):
                try:
                    return int(f[idx])
                except (ValueError, IndexError):
                    return 0

            levels = [iv(F_CLASS_LEVEL_1 + c) for c in range(16)]
            spells.append({
                "id": sid,
                "name": f[F_NAME],
                "affect": iv(F_AFFECT_INDEX),
                "resist": iv(F_RESIST),
                "cast_anim": iv(F_CAST_ANIM),
                "target_anim": iv(F_TARGET_ANIM),
                "travel_type": iv(F_TRAVEL_TYPE),
                "levels": levels,
            })
    return spells


def usable(levels):
    """[(class_name, level)] for classes that can actually cast the spell.

    255 = class cannot use it; 0 also appears as a 'not usable' marker in
    the Titanium file (no spell is castable at level 0).
    """
    out = []
    for name, lvl in zip(CLASS_NAMES, levels):
        if 0 < lvl < 255:
            out.append((name, lvl))
    return out


def classify(sp, records):
    idx = sp["affect"]
    if idx < 0:
        return "NoAffectIndex"
    if idx >= REC_COUNT:
        return "OutOfRange"
    return "Resolved" if records[idx]["content"] else "EmptyRecord"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eq-path", default="/mnt/projects/EverQuestP1999")
    ap.add_argument("--db", default=None, help="willeq.db to cross-check (optional)")
    ap.add_argument("--list-padding", type=int, default=40,
                    help="how many padding-hit spells to list in full")
    args = ap.parse_args()

    eff_path = os.path.join(args.eq_path, "spells.eff")
    txt_path = os.path.join(args.eq_path, "spells_us.txt")
    records = load_eff(eff_path)
    spells = load_spells(txt_path)

    populated = [r for r in records if r["content"]]
    print("== spells.eff ==")
    print("file:              %s" % eff_path)
    print("records:           %d (%d bytes each)" % (REC_COUNT, REC_SIZE))
    print("records WITH sprite names: %d" % len(populated))
    print("records WITHOUT sprite names (the 'padding' population): %d"
          % (REC_COUNT - len(populated)))
    pop_idx = [r["index"] for r in populated]
    print("sprite-bearing indices: %s" % (
        "%d..%d contiguous" % (pop_idx[0], pop_idx[-1])
        if pop_idx and pop_idx == list(range(pop_idx[0], pop_idx[-1] + 1))
        else pop_idx))
    # Per-stage presence over the populated records.
    stage_names = ["Source", "Middle", "Target"]
    for b, sname in enumerate(stage_names):
        n = sum(1 for r in populated if r["emitters"][b]["content"])
        print("  populated records with a %-6s stage: %3d" % (sname, n))

    print()
    print("== spells_us.txt ==")
    print("file:    %s" % txt_path)
    print("spells:  %d (id > 0)" % len(spells))

    buckets = collections.Counter()
    by_class = collections.defaultdict(collections.Counter)
    padding_hits = []
    hit_counts = collections.Counter()
    for sp in spells:
        cls = classify(sp, records)
        buckets[cls] += 1
        if cls in ("Resolved", "EmptyRecord"):
            hit_counts[sp["affect"]] += 1
        if cls == "EmptyRecord":
            padding_hits.append(sp)

    print()
    print("== affect-index resolution over all spells ==")
    total = len(spells)
    for k in ("Resolved", "EmptyRecord", "NoAffectIndex", "OutOfRange"):
        print("%-14s %6d  %5.1f%%" % (k, buckets[k], 100.0 * buckets[k] / total))

    # The population that actually matters: spells a player character can cast.
    # 255 = class cannot use it; 254 marks an AA / discipline (granted, not
    # level-gated) and still counts as player-castable.
    player = [sp for sp in spells if usable(sp["levels"])]
    print()
    print("== player-castable spells (any class level in 1..254) ==")
    print("count: %d of %d" % (len(player), total))
    pb = collections.Counter(classify(sp, records) for sp in player)
    for k in ("Resolved", "EmptyRecord", "NoAffectIndex", "OutOfRange"):
        print("%-14s %6d" % (k, pb[k]))
    aa_only = sum(1 for sp in player
                  if min(l for _, l in usable(sp["levels"])) == 254)
    print("(of those, %d are AA/discipline-only rows -- every class level is 254)"
          % aa_only)

    print()
    print("== padding-hit spells (index in [0,255], record is empty) ==")
    print("count: %d" % len(padding_hits))
    idx_hist = collections.Counter(sp["affect"] for sp in padding_hits)
    print("indices hit: %s" % sorted(idx_hist.items()))

    # Class / level summary.
    unusable = 0
    lvl_buckets = collections.Counter()
    for sp in padding_hits:
        u = usable(sp["levels"])
        if not u:
            unusable += 1
            continue
        for cname, lvl in u:
            by_class[cname][lvl] += 1
        lvl_buckets[min(l for _, l in u)] += 1

    print()
    print("padding-hit spells no player class can cast: %d" % unusable)
    print("padding-hit spells by MINIMUM castable level:")
    for lvl in sorted(lvl_buckets):
        print("  level %3d : %d" % (lvl, lvl_buckets[lvl]))
    print()
    print("padding-hit spells by class (count, lowest level that class gets one):")
    for cname in CLASS_NAMES:
        c = by_class.get(cname)
        if not c:
            continue
        print("  %-13s %4d  (lowest level %d)" % (cname, sum(c.values()), min(c)))

    print()
    print("padding-hit spells castable at level <= 5 by ANY class:")
    low = [sp for sp in padding_hits
           if usable(sp["levels"]) and min(l for _, l in usable(sp["levels"])) <= 5]
    print("count: %d" % len(low))
    for sp in sorted(low, key=lambda s: (min(l for _, l in usable(s["levels"])), s["id"]))[:args.list_padding]:
        u = usable(sp["levels"])
        print("  %5d  idx=%3d  %-28s %s" % (
            sp["id"], sp["affect"], sp["name"],
            ", ".join("%s %d" % (c, l) for c, l in sorted(u, key=lambda x: x[1]))))

    print()
    print("== hottest populated records (spells per index) ==")
    for idx, n in hit_counts.most_common(10):
        state = "populated" if records[idx]["content"] else "PADDING"
        print("  idx %3d : %5d spells  (%s)" % (idx, n, state))

    # Titanium fields the db builder does NOT store, for the Leg B plan.
    print()
    print("== per-spell visual fields present in spells_us.txt ==")
    for key, fld in (("cast_anim", F_CAST_ANIM), ("target_anim", F_TARGET_ANIM),
                     ("travel_type", F_TRAVEL_TYPE)):
        vals = collections.Counter(sp[key] for sp in spells)
        nonzero = sum(n for v, n in vals.items() if v != 0)
        print("  field %3d %-12s distinct=%-4d nonzero=%d  top=%s" % (
            fld, key, len(vals), nonzero, vals.most_common(5)))

    # ------------------------------------------------------------------
    # Record bodies: "empty" means "no sprite names", NOT "unwritten".
    # ------------------------------------------------------------------
    raw = open(eff_path, "rb").read()
    print()
    print("== record bodies (the 800 'unknown' bytes the reader discards) ==")
    allzero = sum(1 for i in range(REC_COUNT)
                  if not any(raw[i * REC_SIZE:(i + 1) * REC_SIZE]))
    print("records that are entirely zero bytes: %d" % allzero)
    hdr = collections.Counter(
        int.from_bytes(raw[i * REC_SIZE:i * REC_SIZE + 4], "little", signed=True)
        for i in range(REC_COUNT))
    print("header_a values over all 256 records: %s" % sorted(hdr.items()))
    hdr_pop = collections.Counter(
        int.from_bytes(raw[r["index"] * REC_SIZE:r["index"] * REC_SIZE + 4],
                       "little", signed=True)
        for r in populated)
    print("header_a over the sprite-bearing records:   %s" % sorted(hdr_pop.items()))
    print("  -> header_a is NOT the populated/empty flag")
    groups = collections.defaultdict(list)
    for r in records:
        if r["content"]:
            continue
        import hashlib
        groups[hashlib.md5(raw[r["index"] * REC_SIZE:(r["index"] + 1) * REC_SIZE])
               .hexdigest()].append(r["index"])
    print("distinct byte-images among the %d sprite-less records: %d"
          % (REC_COUNT - len(populated), len(groups)))
    for digest, idxs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        span = "%d..%d" % (idxs[0], idxs[-1]) if len(idxs) > 4 else str(idxs)
        print("  %s  n=%3d  %s" % (digest[:8], len(idxs), span))

    # ------------------------------------------------------------------
    # Sprite name -> texture file, the mapping SpellsEffReader ships.
    # ------------------------------------------------------------------
    sprite_uses = collections.Counter()
    for r in records:
        for e in r["emitters"]:
            for s in e["sprites"]:
                if s:
                    sprite_uses[s] += 1
    fx_dir = os.path.join(args.eq_path, "SpellEffects")
    on_disk = set(x.lower() for x in os.listdir(fx_dir)) if os.path.isdir(fx_dir) else set()

    def map_current(n):
        """SpellsEffReader::effectNameToTexture() as shipped: digit verbatim,
        '0' deliberately excluded."""
        if len(n) < 5 or not n.startswith("GEN"):
            return ""
        letter, digit = n[3], n[4]
        if not ("A" <= letter <= "Z") or not ("1" <= digit <= "9"):
            return ""
        return "spel%s%s.tga" % (letter.lower(), digit)

    def map_zero_based(n):
        """Hypothesis only: the digit is a 0-based index, so GENx0 -> spelx1.
        NOT settled -- it is an oracle question (T380 Review, B160)."""
        if len(n) < 5 or not n.startswith("GEN"):
            return ""
        letter, digit = n[3], n[4]
        if not ("A" <= letter <= "Z") or not digit.isdigit():
            return ""
        return "spel%s%d.tga" % (letter.lower(), int(digit) + 1)

    print()
    print("== sprite name -> SpellEffects/ texture ==")
    print("distinct sprite names in spells.eff: %d (%d sprite slots used)"
          % (len(sprite_uses), sum(sprite_uses.values())))
    total_slots = sum(sprite_uses.values())
    for label, fn in (("shipped mapping (digit verbatim, '0' rejected)", map_current),
                      ("hypothesis: digit is 0-based (GENx0 -> spelx1)", map_zero_based)):
        mapped = {n: fn(n) for n in sprite_uses}
        n_mapped = sum(1 for t in mapped.values() if t)
        n_on_disk = sum(1 for t in mapped.values() if t and t in on_disk)
        slots = sum(c for n, c in sprite_uses.items()
                    if mapped[n] and mapped[n] in on_disk)
        print("  %-46s names %2d/%d, on disk %2d, slots covered %d/%d (%.0f%%)"
              % (label, n_mapped, len(sprite_uses), n_on_disk, slots, total_slots,
                 100.0 * slots / total_slots if total_slots else 0.0))
    unresolved = sorted(n for n in sprite_uses if not map_current(n))
    print("  names the shipped mapping cannot resolve (%d): %s"
          % (len(unresolved), unresolved))

    if args.db:
        print()
        print("== willeq.db cross-check ==")
        con = sqlite3.connect("file:%s?mode=ro" % args.db, uri=True)
        rows = con.execute("SELECT COUNT(*) FROM spells").fetchone()[0]
        print("db spells rows: %d" % rows)
        dbidx = dict(con.execute("SELECT id, spell_affect_index FROM spells"))
        mismatch = [sp["id"] for sp in spells
                    if dbidx.get(sp["id"]) != sp["affect"]]
        print("rows whose spell_affect_index differs from the text file: %d" % len(mismatch))
        cols = [r[1] for r in con.execute("PRAGMA table_info(spells)")]
        for want in ("cast_anim", "target_anim", "travel_type", "spell_visual"):
            print("  column %-12s %s" % (want, "present" if want in cols else "ABSENT"))
        con.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
