#!/usr/bin/env python3
"""B1150 (#470): emit tests/spell_fx_shape_oracle.h -- the BANDS of the shape
words B1150 retires from config/spell_effects.json.

Run against the config BEFORE the retirement (or against `git show
<commit>:config/spell_effects.json` piped as a file) so the header pins the
retired values: the B1130/B1132 measured Source rows (anchor class, lo/hi/
halfWidth/centroid in caster heights, record 22's halo disc words, the orbit
spiral), the shaped Target rows (anchor class, lo/hi/halfWidth/centroid in
target heights, the head bone's height) and the six spellOverrides' own
measured shapes (38/41/54/656/253/458 -- all spells with a spellsnew.eff
field-145 record, so the file's spawn geometry replaces them).

The values are bands (review/B1150/gate), not inputs: nothing in the code
reads them. The test that consumes the header (tests/test_spell_spawn_
shapes.cpp) asserts the bands still agree with the oracle's typed numbers
and that the retired config keys are gone for record spells.

Usage: gen_shape_bands.py <pre-retirement spell_effects.json> > tests/spell_fx_shape_oracle.h
"""
import json
import sys


def anchor_int(name, which):
    # SpellEffectsConfig::SourceAnchor / TargetAnchor (spell_effects_config.h).
    # The two enums DISAGREE on "head" (Source 4, Target 2), so the caller
    # names its table.
    tables = {
        "source": {"body": 0, "hands": 1, "feet-ring": 2, "orbit": 3, "head": 4},
        "target": {"body": 0, "caster": 1, "head": 2, "feet": 3, "column": 4},
    }
    if name not in tables[which]:
        raise SystemExit(f"unknown {which} anchor {name!r}")
    return tables[which][name]


def main():
    path = sys.argv[1]
    c = json.load(open(path))
    lines = []
    ap = lines.append
    ap("// B1150 (#470): GENERATED -- do not edit. refactor/B1150/scripts/"
       "gen_shape_bands.py over the pre-retirement config/spell_effects.json.")
    ap("// The shape words B1150 retires for spells with a spellsnew.eff field-145")
    ap("// record: the file's +0xA8..+0xD0 words replace them. These are the")
    ap("// BANDS the gate table (refactor/B1150/README.md) compares the harness")
    ap("// rows against -- values, not inputs: no code reads this header's rows")
    ap("// for behaviour, tests assert them against the oracle's typed numbers.")
    ap("//")
    ap("// Heights are the measured units (Source: caster heights above the")
    ap("// caster's feet; Target: target heights above the target's feet),")
    ap("// exactly as the config carried them.")
    ap("#ifndef EQT_SPELL_FX_SHAPE_ORACLE_H")
    ap("#define EQT_SPELL_FX_SHAPE_ORACLE_H")
    ap("#include <cfloat>")
    ap("namespace spell_fx_shape_oracle {")
    ap("")
    ap("struct SourceBand {")
    ap("    int record;")
    ap("    int anchor;      // SpellEffectsConfig::SourceAnchor")
    ap("    float lo, hi, halfWidth, centroid;")
    ap("    bool spiral;")
    ap("    float discLo, discHi, discHalfWidth;  // record 22's halo; 0 = none")
    ap("};")
    ap("")
    ap("struct TargetBand {")
    ap("    int record;")
    ap("    int anchor;      // SpellEffectsConfig::TargetAnchor")
    ap("    float lo, hi, halfWidth, centroid;")
    ap("    float boneAt;    // the Head row's bone height; 0 = none")
    ap("};")
    ap("")
    ap("struct OverrideShape {")
    ap("    unsigned spell;")
    ap("    const char* style;");
    ap("    int shape;       // SpawnShape (unified_particle.h)");
    ap("    float extX, extY, extZ;")
    ap("};")
    ap("")

    # ---- Source rows (the measured rows only: one carrying lo/hi/halfWidth)
    shape_names = {}  # name -> SpawnShape enum ordinal (unified_particle.h)
    src_rows = []
    for rec, row in c["sourceRecords"]["records"].items():
        if not ("lo" in row and "hi" in row and "halfWidth" in row):
            continue
        src_rows.append(
            "    {int(%s), %d, %g, %g, %g, %g, %s, %g, %g, %g},"
            % (rec, anchor_int(row["anchor"], "source"), row["lo"], row["hi"],
               row["halfWidth"], row["centroid"], "true" if row.get("spiral") else "false",
               row.get("discLo", 0.0), row.get("discHi", 0.0), row.get("discHalfWidth", 0.0)))
    ap("// %d measured Source rows (the B1130/B1134 survey)." % len(src_rows))
    ap("constexpr SourceBand kSource[] = {")
    ap("\n".join(src_rows))
    ap("};")
    ap("")

    # ---- Target rows: EVERY row carries measured lo/hi/halfWidth/centroid
    # (the shaped ones were inputs via hasShape, the body/caster ones were
    # already evidence-only) -- all of it is retired with the shape block.
    tgt_rows = []
    for rec, row in c["targetRecords"]["records"].items():
        if not ("lo" in row and "hi" in row and "halfWidth" in row):
            continue
        tgt_rows.append(
            "    {int(%s), %d, %g, %g, %g, %g, %g},"
            % (rec, anchor_int(row.get("anchor", "body"), "target"), row["lo"], row["hi"],
               row["halfWidth"], row["centroid"], row.get("boneAt", 0.0)))
    ap("// %d measured Target rows (B1132's survey; anchor = TargetAnchor)." % len(tgt_rows))
    ap("constexpr TargetBand kTarget[] = {")
    ap("\n".join(tgt_rows))
    ap("};")
    ap("")

    # ---- spellOverrides' own shapes (B1120/B1122's measured clouds)
    shape_enum = {
        "POINT": 0, "BOX": 1, "SPHERE": 2, "RING": 3, "DISC": 4,
    }
    ovs = []
    for sp, styles in c.get("spellOverrides", {}).items():
        for style, ov in styles.items():
            if "spawnShape" not in ov:
                continue
            shape_enum[ov["spawnShape"]]  # KeyError = a new shape name to map
            ext = ov.get("spawnExtents", [0, 0, 0])
            ovs.append("    {unsigned(%s), \"%s\", %d, %g, %g, %g},"
                       % (sp, style, shape_enum[ov["spawnShape"]],
                          ext[0], ext[1], ext[2] if len(ext) > 2 else 0.0))
    ap("// The spellOverrides' own measured shapes (B1120/B1122). Every one of")
    ap("// these spells has a field-145 record, so the file's geometry draws it.")
    ap("constexpr OverrideShape kOverrideShape[] = {")
    ap("\n".join(ovs))
    ap("};")
    ap("")
    ap("constexpr int kSourceRows = int(sizeof(kSource) / sizeof(kSource[0]));")
    ap("constexpr int kTargetRows = int(sizeof(kTarget) / sizeof(kTarget[0]));")
    ap("constexpr int kOverrideShapes = int(sizeof(kOverrideShape) / sizeof(kOverrideShape[0]));")
    ap("")
    ap("}")  # namespace
    ap("#endif")
    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
