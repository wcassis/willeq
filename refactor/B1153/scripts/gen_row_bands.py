#!/usr/bin/env python3
"""B1153: generate tests/spell_fx_row_oracle.h from the PRE-retirement
config/spell_effects.json (at e83a097d).

The measured rows (sourceRecords.records / targetRecords.records) move out
of the shipped config into this typed band; the runtime then carries only
the two default rows. Command (from the repo root, before running
retire_config_rows.py):

    git show e83a097d:config/spell_effects.json > /tmp/b1153_pre.json
    python3 refactor/B1153/scripts/gen_row_bands.py /tmp/b1153_pre.json

The output is committed as tests/spell_fx_row_oracle.h and re-generated any
time the retired file changes (a changed band is a red test).
"""
import json
import sys

SRC_ANCHORS = {"body": "Body", "hands": "Hands", "feet-ring": "FeetRing",
               "orbit": "Orbit", "head": "Head"}
TGT_ANCHORS = {"body": "Body", "caster": "Caster", "head": "Head",
               "feet": "FeetGround", "column": "Column"}


def main():
    p = sys.argv[1] if len(sys.argv) > 1 else "config/spell_effects.json"
    doc = json.load(open(p))
    sr = doc["sourceRecords"]
    tr = doc["targetRecords"]
    rows = []
    for rec in sorted(sr["records"], key=int):
        s = sr["records"][rec]
        t = tr["records"][rec]
        assert s["anchor"] in SRC_ANCHORS, (rec, s["anchor"])
        assert t["anchor"] in TGT_ANCHORS, (rec, t["anchor"])
        rows.append((int(rec), SRC_ANCHORS[s["anchor"]], TGT_ANCHORS[t["anchor"]],
                     float(t.get("faceOnset", 0.0))))
    # No row carries a shape word any more (B1150) -- the band is the anchor
    # classes plus the face onset. Anything else in a row would be a
    # re-measurement, not a retirement.
    for rec, s, t in ((r, sr["records"][r], tr["records"][r]) for r in sr["records"]):
        for bad in ("lo", "hi", "halfWidth", "centroid", "spiral", "discLo", "discHi",
                    "discHalfWidth", "discShare", "onset", "holdOther", "holdSelf",
                    "end", "boneAt", "unmeasured", "measuredAs"):
            assert bad not in s, (rec, "src", bad)
        for bad in ("lo", "hi", "halfWidth", "centroid", "onset", "end", "boneAt",
                    "unmeasured"):
            assert bad not in t, (rec, "tgt", bad)
    # The default rows (the live no-record fallback) and the B1120 scale.
    d = sr["default"]
    scale = sr["scale"]
    assert d == {"anchor": "body", "onset": 0.41, "holdOther": 4.0, "holdSelf": 0.33}, d
    assert tr["default"] == {"anchor": "body"}, tr["default"]

    def counts(anchors, i):
        out = {}
        for r in rows:
            out[r[i]] = out.get(r[i], 0) + 1
        return out

    src_counts = counts(SRC_ANCHORS, 1)
    tgt_counts = counts(TGT_ANCHORS, 2)

    lines = []
    a = lines.append
    a("// B1153 (#473): the B1130/B1134 survey's measured Source/Target rows,")
    a("// REGRESSED AS A BAND. The rows left config/spell_effects.json (the")
    a("// runtime carries only the two default rows); every record is drawn by")
    a("// its spellsnew.eff record's EDD emitters (B1149 population/sprite,")
    a("// B1150 shape, B1151 motion, B1152 the missile arrival), and this")
    a("// header is the evidence the survey measured: per record, the Source")
    a("// anchor class, the Target anchor class and the face onset (record 5's")
    a("// head glow, e5 f8233). Generated from the pre-retirement config")
    a("// (git show e83a097d:config/spell_effects.json) by")
    a("// refactor/B1153/scripts/gen_row_bands.py -- re-run it if the retired")
    a("// file is ever re-measured; a changed band is a red test.")
    a("// The shape words (+0xA8..+0xD0) are tests/spell_fx_shape_oracle.h, the")
    a("// populations tests/spell_fx_population_oracle.h, the timings")
    a("// tests/spell_fx_timing_oracle.h, and the field-145 record -> row")
    a("// mapping tests/spell_anim_rows.h.")
    a("#pragma once")
    a("")
    a("#include <cstddef>")
    a("#include <cstdint>")
    a("")
    a("namespace spell_fx_row_oracle {")
    a("")
    a("struct Row {")
    a("    int record;      // the B1130 survey id (spells.eff record)")
    a("    const char* srcAnchor;  // the sourceRecords row's anchor class")
    a("    const char* tgtAnchor;  // the targetRecords row's anchor class")
    a("    float faceOnset;  // Act+ s the head glow lights the face (0 = none)")
    a("};")
    a("")
    a("const Row kRows[] = {")
    for rec, sa, ta, fo in rows:
        a(f"    {{{rec}, \"{sa}\", \"{ta}\", {fo:.2f}f}},")
    a("};")
    a(f"inline std::size_t size() {{ return std::size(kRows); }}")
    a("")
    a("// The class census (review/B1130/anchor_table.tsv; record 51 added by")
    a("// B1134 with the observer rig).")
    a(f"inline int sourceAnchorCount(const char* a) {{")
    a(f"    int n = 0; for (const auto& r : kRows) n += r.srcAnchor == a; return n; }}")
    a(f"inline int targetAnchorCount(const char* a) {{")
    a(f"    int n = 0; for (const auto& r : kRows) n += r.tgtAnchor == a; return n; }}")
    a("")
    a(f"// B1130: hands {src_counts.get('Hands', 0)}, feet-ring {src_counts.get('FeetRing', 0)},")
    a(f"// orbit {src_counts.get('Orbit', 0)}, body {src_counts.get('Body', 0)}, head {src_counts.get('Head', 0)};")
    a(f"// Target: body {tgt_counts.get('Body', 0)}, caster {tgt_counts.get('Caster', 0)},")
    a(f"// head {tgt_counts.get('Head', 0)}, feet {tgt_counts.get('FeetGround', 0)}, column {tgt_counts.get('Column', 0)}.")
    a("")
    a("// The live default rows (what the config keeps) and the B1120 scale.")
    a(f"struct Default {{")
    a(f"    const char* srcAnchor; float onset; float holdOther; float holdSelf;")
    a(f"    const char* tgtAnchor;")
    a(f"}};")
    a(f"constexpr Default kDefault = {{\"{SRC_ANCHORS[d['anchor']]}\", {d['onset']:.2f}f, "
      f"{d['holdOther']:.2f}f, {d['holdSelf']:.2f}f, \"{TGT_ANCHORS[tr['default']['anchor']]}\"}};")
    a(f"constexpr float kCasterHeight = {scale['casterHeight']:.1f}f;")
    a(f"constexpr float kAnchorAboveFeet = {scale['anchorAboveFeet']:.1f}f;")
    a(f"constexpr float kOrbitAngularVelocity = {scale['orbitAngularVelocity']:.1f}f;")
    a("")
    a("}  // namespace spell_fx_row_oracle")
    a("")
    out = "\n".join(lines) + "\n"
    dest = sys.argv[2] if len(sys.argv) > 2 else "tests/spell_fx_row_oracle.h"
    open(dest, "w").write(out)
    print(f"wrote {dest}: {len(rows)} rows (src {src_counts}, tgt {tgt_counts})")


if __name__ == "__main__":
    main()
