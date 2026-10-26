// B1150 (#470): GENERATED -- do not edit. refactor/B1150/scripts/gen_shape_bands.py over the pre-retirement config/spell_effects.json.
// The shape words B1150 retires for spells with a spellsnew.eff field-145
// record: the file's +0xA8..+0xD0 words replace them. These are the
// BANDS the gate table (refactor/B1150/README.md) compares the harness
// rows against -- values, not inputs: no code reads this header's rows
// for behaviour, tests assert them against the oracle's typed numbers.
//
// Heights are the measured units (Source: caster heights above the
// caster's feet; Target: target heights above the target's feet),
// exactly as the config carried them.
#ifndef EQT_SPELL_FX_SHAPE_ORACLE_H
#define EQT_SPELL_FX_SHAPE_ORACLE_H
#include <cfloat>
namespace spell_fx_shape_oracle {

struct SourceBand {
    int record;
    int anchor;      // SpellEffectsConfig::SourceAnchor
    float lo, hi, halfWidth, centroid;
    bool spiral;
    float discLo, discHi, discHalfWidth;  // record 22's halo; 0 = none
};

struct TargetBand {
    int record;
    int anchor;      // SpellEffectsConfig::TargetAnchor
    float lo, hi, halfWidth, centroid;
    float boneAt;    // the Head row's bone height; 0 = none
};

struct OverrideShape {
    unsigned spell;
    const char* style;
    int shape;       // SpawnShape (unified_particle.h)
    float extX, extY, extZ;
};

// 41 measured Source rows (the B1130/B1134 survey).
constexpr SourceBand kSource[] = {
    {int(0), 1, 0.07, 1.25, 0.59, 0.71, false, 0, 0, 0},
    {int(1), 2, 0.06, 1.17, 0.64, 0.48, false, 0, 0, 0},
    {int(2), 1, 0.3, 0.66, 0.205, 0.48, false, 0, 0, 0},
    {int(3), 0, 0.28, 0.99, 0.42, 0.64, false, 0, 0, 0},
    {int(4), 3, 0.1, 1.86, 1.195, 0.82, true, 0, 0, 0},
    {int(5), 3, 0.26, 1.09, 0.415, 0.7, false, 0, 0, 0},
    {int(6), 0, 0.09, 1.03, 0.55, 0.57, false, 0, 0, 0},
    {int(7), 3, 0.41, 1.55, 0.325, 0.9, false, 0, 0, 0},
    {int(8), 3, 0.06, 1.06, 0.885, 0.49, false, 0, 0, 0},
    {int(9), 0, 0.1, 1.02, 0.95, 0.56, false, 0, 0, 0},
    {int(10), 3, 0.12, 1.86, 1.27, 0.79, false, 0, 0, 0},
    {int(11), 3, 0.1, 0.86, 0.85, 0.58, false, 0, 0, 0},
    {int(12), 3, 0.13, 1.07, 0.61, 0.64, false, 0, 0, 0},
    {int(13), 0, 0.21, 1.96, 0.65, 0.91, false, 0, 0, 0},
    {int(14), 1, 0.09, 0.71, 0.35, 0.51, false, 0, 0, 0},
    {int(15), 0, 0.1, 1.34, 0.615, 0.67, false, 0, 0, 0},
    {int(16), 2, 0.24, 0.81, 0.595, 0.51, false, 0, 0, 0},
    {int(17), 3, 0.1, 1.51, 0.82, 0.68, false, 0, 0, 0},
    {int(18), 1, 0.16, 1.08, 0.575, 0.63, false, 0, 0, 0},
    {int(19), 0, 0.12, 1.53, 0.855, 0.76, false, 0, 0, 0},
    {int(20), 0, 0.07, 1.71, 0.64, 0.88, false, 0, 0, 0},
    {int(21), 3, 0.04, 1.73, 0.62, 0.72, false, 0, 0, 0},
    {int(22), 4, 0.03, 1.09, 0.375, 0.68, false, 0.88, 1.12, 0.43},
    {int(23), 0, 0.08, 1.26, 0.61, 0.61, false, 0, 0, 0},
    {int(24), 0, 0.05, 0.84, 0.63, 0.39, false, 0, 0, 0},
    {int(25), 0, 0.08, 1.56, 0.58, 0.83, false, 0, 0, 0},
    {int(26), 0, 0.14, 1.25, 0.59, 0.62, false, 0, 0, 0},
    {int(27), 0, 0.22, 1.27, 0.35, 0.54, false, 0, 0, 0},
    {int(28), 0, 0.07, 1.47, 0.645, 0.72, false, 0, 0, 0},
    {int(29), 1, 0.22, 1.1, 0.615, 0.69, false, 0, 0, 0},
    {int(30), 3, 0.07, 1.47, 0.63, 0.69, false, 0, 0, 0},
    {int(31), 2, 0.02, 1.13, 0.63, 0.41, false, 0, 0, 0},
    {int(32), 2, 0.01, 0.68, 0.595, 0.26, false, 0, 0, 0},
    {int(33), 3, 0.17, 0.84, 0.35, 0.51, true, 0, 0, 0},
    {int(34), 3, 0.02, 1.27, 0.52, 0.52, false, 0, 0, 0},
    {int(35), 3, 0.31, 2.19, 0.65, 1.36, false, 0, 0, 0},
    {int(40), 0, 0.14, 1.33, 0.785, 0.66, false, 0, 0, 0},
    {int(41), 1, 0.17, 1.5, 0.56, 0.79, false, 0, 0, 0},
    {int(42), 1, 0.35, 1.7, 1.09, 0.75, false, 0, 0, 0},
    {int(43), 1, 0.18, 1.64, 1.25, 0.74, false, 0, 0, 0},
    {int(51), 2, -0.05, 0.74, 0.345, 0.21, false, 0, 0, 0},
};

// 40 measured Target rows (B1132's survey; anchor = TargetAnchor).
constexpr TargetBand kTarget[] = {
    {int(0), 0, 0.25, 1.46, 0.46, 0.8, 0},
    {int(1), 0, 0.23, 1.22, 0.59, 0.69, 0},
    {int(2), 0, 0.32, 1.59, 0.535, 0.89, 0},
    {int(3), 4, 0, 1.82, 1.52, 0.91, 0},
    {int(4), 0, 0.11, 2.17, 1.36, 0.95, 0},
    {int(5), 2, 0.66, 1.04, 0.35, 0.87, 0.9},
    {int(6), 0, 0.09, 0.97, 0.51, 0.52, 0},
    {int(7), 0, 0.25, 0.94, 0.57, 0.59, 0},
    {int(8), 1, 0.12, 1.94, 1.185, 0.96, 0},
    {int(9), 1, 0.13, 1.35, 0.9, 0.66, 0},
    {int(10), 0, 0.1, 2.26, 1.325, 0.94, 0},
    {int(11), 1, 0.12, 1.06, 0.72, 0.58, 0},
    {int(12), 0, 0.22, 1.29, 0.575, 0.72, 0},
    {int(13), 0, 0.23, 1.63, 0.56, 0.77, 0},
    {int(14), 0, 0.06, 1.29, 1.12, 0.62, 0},
    {int(15), 0, 0.25, 1.66, 0.52, 0.84, 0},
    {int(16), 3, 0.21, 1.03, 0.565, 0.53, 0},
    {int(17), 0, 0.13, 2.18, 1.09, 1, 0},
    {int(18), 0, 0.22, 1.11, 0.56, 0.64, 0},
    {int(19), 1, 0.09, 1.56, 0.99, 0.74, 0},
    {int(20), 0, 0.25, 1.34, 0.55, 0.74, 0},
    {int(21), 0, 0.22, 1.28, 0.58, 0.72, 0},
    {int(22), 0, 0.27, 1.49, 0.56, 0.87, 0},
    {int(23), 0, 0.2, 1.08, 0.55, 0.59, 0},
    {int(24), 0, 0.22, 1.2, 0.515, 0.68, 0},
    {int(25), 0, 0.21, 1.12, 0.585, 0.62, 0},
    {int(26), 0, 0.24, 1.56, 0.585, 0.9, 0},
    {int(27), 1, 0.1, 1.47, 0.97, 0.72, 0},
    {int(28), 3, 0.2, 0.85, 0.52, 0.52, 0},
    {int(29), 0, 0.19, 0.98, 0.49, 0.6, 0},
    {int(30), 0, 0.3, 1.72, 0.55, 0.99, 0},
    {int(31), 0, 0.2, 1.21, 0.57, 0.6, 0},
    {int(32), 0, 0.29, 1.66, 0.585, 0.95, 0},
    {int(33), 0, 0.24, 1.54, 0.585, 0.81, 0},
    {int(34), 0, 0.36, 1.54, 0.39, 1, 0},
    {int(35), 0, 0.27, 1.67, 0.47, 0.94, 0},
    {int(40), 1, 0.07, 1.67, 0.615, 0.87, 0},
    {int(41), 0, 0.26, 1, 0.59, 0.63, 0},
    {int(42), 0, 0.05, 1.49, 0.845, 0.63, 0},
    {int(43), 1, 0.13, 1.42, 1.18, 0.59, 0},
};

// The spellOverrides' own measured shapes (B1120/B1122). Every one of
// these spells has a field-145 record, so the file's geometry draws it.
constexpr OverrideShape kOverrideShape[] = {
    {unsigned(54), "impact", 2, 0.65, 0, 0},
    {unsigned(54), "spellComplete", 1, 0, 4.225, 0},
    {unsigned(54), "castGlow", 1, 0, 3.575, 0},
    {unsigned(656), "castGlow", 1, 0, 3.575, 0},
    {unsigned(656), "spellComplete", 1, 0, 4.225, 0},
    {unsigned(656), "impact", 2, 0.65, 0, 0},
    {unsigned(458), "castGlow", 1, 2.34, 2.925, 2.34},
    {unsigned(458), "spellComplete", 2, 1.3, 0, 0},
    {unsigned(458), "impact", 1, 5.2, 4.55, 5.2},
    {unsigned(41), "impact", 1, 2.275, 0.325, 2.275},
    {unsigned(41), "spellComplete", 2, 2.6, 0, 0},
    {unsigned(41), "castSpray", 1, 7.475, 5.2, 7.475},
    {unsigned(253), "impact", 1, 14.95, 8.125, 6.5},
    {unsigned(253), "spellComplete", 1, 14.95, 8.125, 6.5},
    {unsigned(253), "castGlow", 1, 12.35, 7.215, 6.5},
};

constexpr int kSourceRows = int(sizeof(kSource) / sizeof(kSource[0]));
constexpr int kTargetRows = int(sizeof(kTarget) / sizeof(kTarget[0]));
constexpr int kOverrideShapes = int(sizeof(kOverrideShape) / sizeof(kOverrideShape[0]));

}
#endif
