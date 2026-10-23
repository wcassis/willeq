// B1137 (#457): the spellsnew.eff record (spells_us.txt field 145) each
// measured Source/Target row was captured on -- the B1130 survey's
// representative spell (review/B1130/record_table.tsv spell_id) and its field
// 145. Typed from the survey, not read from config/spell_effects.json, so a
// changed `animRecords` block is a red test. Shared by the anchor tests, which
// note each representative with BOTH indices, as the game thread does.
#pragma once

#include <cstdint>

namespace spell_anim_rows {

struct RowAnim {
    int row;   // the sourceRecords / targetRecords survey id
    int anim;  // field 145 of the spell that row was captured on
};

// 40 rows; 51 (disciplines, not gem-castable) was never captured.
const RowAnim kRowAnim[] = {
    {0, 80},
    {1, 73},
    {2, 86},
    {3, 90},
    {4, 83},
    {5, 74},
    {6, 1},
    {7, 88},
    {8, 94},
    {9, 95},
    {10, 96},
    {11, 81},
    {12, 87},
    {13, 101},
    {14, 102},
    {15, 103},
    {16, 109},
    {17, 110},
    {18, 84},
    {19, 111},
    {20, 89},
    {21, 91},
    {22, 112},
    {23, 82},
    {24, 116},
    {25, 115},
    {26, 114},
    {27, 113},
    {28, 108},
    {29, 72},
    {30, 107},
    {31, 85},
    {32, 106},
    {33, 121},
    {34, 105},
    {35, 104},
    {40, 78},
    {41, 100},
    {42, 99},
    {43, 98},
};

inline int AnimOfRow(int row) {
    for (const auto& r : kRowAnim)
        if (r.row == row) return r.anim;
    return 0;
}

}  // namespace spell_anim_rows
