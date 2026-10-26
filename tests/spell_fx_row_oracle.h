// B1153 (#473): the B1130/B1134 survey's measured Source/Target rows,
// REGRESSED AS A BAND. The rows left config/spell_effects.json (the
// runtime carries only the two default rows); every record is drawn by
// its spellsnew.eff record's EDD emitters (B1149 population/sprite,
// B1150 shape, B1151 motion, B1152 the missile arrival), and this
// header is the evidence the survey measured: per record, the Source
// anchor class, the Target anchor class and the face onset (record 5's
// head glow, e5 f8233). Generated from the pre-retirement config
// (git show e83a097d:config/spell_effects.json) by
// refactor/B1153/scripts/gen_row_bands.py -- re-run it if the retired
// file is ever re-measured; a changed band is a red test.
// The shape words (+0xA8..+0xD0) are tests/spell_fx_shape_oracle.h, the
// populations tests/spell_fx_population_oracle.h, the timings
// tests/spell_fx_timing_oracle.h, and the field-145 record -> row
// mapping tests/spell_anim_rows.h.
#pragma once

#include <cstddef>
#include <cstdint>

namespace spell_fx_row_oracle {

struct Row {
    int record;      // the B1130 survey id (spells.eff record)
    const char* srcAnchor;  // the sourceRecords row's anchor class
    const char* tgtAnchor;  // the targetRecords row's anchor class
    float faceOnset;  // Act+ s the head glow lights the face (0 = none)
};

const Row kRows[] = {
    {0, "Hands", "Body", 0.00f},
    {1, "FeetRing", "Body", 0.00f},
    {2, "Hands", "Body", 0.00f},
    {3, "Body", "Column", 0.00f},
    {4, "Orbit", "Body", 0.00f},
    {5, "Orbit", "Head", 0.41f},
    {6, "Body", "Body", 0.00f},
    {7, "Orbit", "Body", 0.00f},
    {8, "Orbit", "Caster", 0.00f},
    {9, "Body", "Caster", 0.00f},
    {10, "Orbit", "Body", 0.00f},
    {11, "Orbit", "Caster", 0.00f},
    {12, "Orbit", "Body", 0.00f},
    {13, "Body", "Body", 0.00f},
    {14, "Hands", "Body", 0.00f},
    {15, "Body", "Body", 0.00f},
    {16, "FeetRing", "FeetGround", 0.00f},
    {17, "Orbit", "Body", 0.00f},
    {18, "Hands", "Body", 0.00f},
    {19, "Body", "Caster", 0.00f},
    {20, "Body", "Body", 0.00f},
    {21, "Orbit", "Body", 0.00f},
    {22, "Head", "Body", 0.00f},
    {23, "Body", "Body", 0.00f},
    {24, "Body", "Body", 0.00f},
    {25, "Body", "Body", 0.00f},
    {26, "Body", "Body", 0.00f},
    {27, "Body", "Caster", 0.00f},
    {28, "Body", "FeetGround", 0.00f},
    {29, "Hands", "Body", 0.00f},
    {30, "Orbit", "Body", 0.00f},
    {31, "FeetRing", "Body", 0.00f},
    {32, "FeetRing", "Body", 0.00f},
    {33, "Orbit", "Body", 0.00f},
    {34, "Orbit", "Body", 0.00f},
    {35, "Orbit", "Body", 0.00f},
    {40, "Body", "Caster", 0.00f},
    {41, "Hands", "Body", 0.00f},
    {42, "Hands", "Body", 0.00f},
    {43, "Hands", "Caster", 0.00f},
    {51, "FeetRing", "Caster", 0.00f},
};
inline std::size_t size() { return std::size(kRows); }

// The class census (review/B1130/anchor_table.tsv; record 51 added by
// B1134 with the observer rig).
inline int sourceAnchorCount(const char* a) {
    int n = 0; for (const auto& r : kRows) n += r.srcAnchor == a; return n; }
inline int targetAnchorCount(const char* a) {
    int n = 0; for (const auto& r : kRows) n += r.tgtAnchor == a; return n; }

// B1130: hands 8, feet-ring 5,
// orbit 13, body 14, head 1;
// Target: body 29, caster 8,
// head 1, feet 2, column 1.

// The live default rows (what the config keeps) and the B1120 scale.
struct Default {
    const char* srcAnchor; float onset; float holdOther; float holdSelf;
    const char* tgtAnchor;
};
constexpr Default kDefault = {"Body", 0.41f, 4.00f, 0.33f, "Body"};
constexpr float kCasterHeight = 6.5f;
constexpr float kAnchorAboveFeet = 3.6f;
constexpr float kOrbitAngularVelocity = 3.0f;

}  // namespace spell_fx_row_oracle

