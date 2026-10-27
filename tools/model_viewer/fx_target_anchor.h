// B1166 (#485): the b1130 fixed_npc target anchor -- ONE definition, used by
// the harness's target solve (fitFixedNpcTarget), by its self-check on the
// written frame 0, and READ (regex on the kB1130TargetAnchor line below) by the
// measure scripts -- refactor/B1155/scripts/harness_measure_b1130.py and
// refactor/B1162/scripts/harness_measure_b1130_action.py (GEOM 'fixed_npc'
// tfeet/tmpx), refactor/B1162/scripts/harness_measure_b1162_anchor.py (PRESET
// 'fixed_npc') and refactor/B1151/scripts/oracle_anchor.sh (LF/LM); each keeps
// FX_TFEET=940,652 FX_TMPX=325 as the pre-B1166 override -- so the harness and
// the measure compare the SAME body against the oracle. Keep that line's
// `{x.f, y.f, px.f}` form: the scripts parse it.
//
// The rule (refactor/B1166/README.md sec. 1): the anchor is the oracle's own
// target box in r51's still, MEASURED by difference against a target-free still
// at r51's camera (refactor/B1166/scripts/oracle_target_box.py on
// review/B1130/oracle/r51/26_tgt_npc_settle.png minus
// review/B1164/worker/dev3/oracle/ab_b_away_slain_25fps/16_empty_r51cam.png,
// diff threshold 20, the selection ring excluded by colour, a row counts with
// >= 3 px): head top row 328, last figure row above the ring 615, so 288 px
// head top to feet, column centroid 938.9 (939.9-940.3 at thresholds 25-40;
// B1130's 940 kept). The threshold sweep (40/30/25/20 -> 265/268/274/288 px) is
// in the README: above 20 the dark boot (diff 17-27 vs the ground) is cut. B1130's geometry file (review/B1130/geom/r51.json tfeet
// 940,652 / tmpx 325) is NOT the target's box: its extent began at the name
// label's top row (290) and its feet row is not the boots'. The harness solves
// the feet (row, column) -- the two world unknowns x, z -- and the height is
// then the model's own, ratcheted at its measured 285 px (below).
//
// Header-only and Irrlicht-free so the geometry is unit-tested offline
// (tests/test_fx_harness.cpp, B1166TargetAnchor*).
#pragma once

#include <cmath>
#include <cstdlib>

namespace fxharness {

struct TargetAnchor {
    float feetX, feetY, px;  // feet column, feet (lowest) row, head top to feet in px (1280x800 frame)
};

// r51's measured oracle target (see above).
constexpr TargetAnchor kB1130TargetAnchor{940.0f, 615.0f, 288.0f};

// The self-check's stated tolerances (px, in the requested 1280x800 frame):
// - feet row +-1 vs the anchor (the solver's own convergence bound,
//   fitFixedNpcTarget's `ok`);
// - column centroid +-3 vs the anchor (a centroid is not a feet column: the
//   oracle's own reads 938.9-940.3 across thresholds);
// - height +-3 vs the STANDING height the solve rendered (the written frame
//   must show the body the solve fitted -- the B1155 defect was a 50 px fit vs
//   a 316 px frame);
// - AND the standing height itself, 285 +-3 px (ratio 0.990 of the anchor's
//   288). The height is NOT solved: the two feet pixels fix the world (x, z),
//   and the height is the model's own. B1166 ratcheted the human male (race 1,
//   gender 0, size 6) at 238 px (0.826x) when fable drew it at scale 1.0;
//   B1172 (#491) draws it at the official client's actor scale, size x 0.2 =
//   1.2 (include/client/graphics/actor_scale.h), and it renders rows 331..615
//   = 285 px (review/B1172/worker/wrong_model/default.log, the self-check
//   line; refactor/B1172/README.md sec. 2). This ratchets that measured value
//   so a model or scale regression fails. At the anchor with `--target`
//   (review/B1172/worker/wrong_model/*.log): human female 1:1:6 260 px,
//   dark-elf female 6:1:6 261, the human at size 7 332, troll 9:0:6 280 --
//   all rejected; the barbarian 2:0:6 renders 284, inside the band: under the
//   client's rule every classic race at the same size word is drawn about the
//   same height, and a body of the SAME height cannot be told apart by height;
//   this check does not claim to.
constexpr float kFeetTolPx = 1.0f;
constexpr float kColumnTolPx = 3.0f;
constexpr float kHeightTolPx = 3.0f;
constexpr float kStandingPx = 285.0f;
constexpr float kStandingTolPx = 3.0f;

// The harness's clear colour (50,50,80) -- the ground plane paints it too --
// and its off-clear rule: a pixel is FIGURE when |r-50|+|g-50|+|b-80| > 12, the
// rule fitFaceOnCamera / fitFixedNpcTarget / checkB1130Frame all use.
inline bool offClear(int r, int g, int b) {
    return std::abs(r - 50) + std::abs(g - 50) + std::abs(b - 80) > 12;
}

struct Silhouette {
    int top = -1, bottom = -1, n = 0;
    double col = 0.0;  // column centroid
    int height() const { return n ? bottom - top + 1 : 0; }
};

// The off-clear silhouette inside [x0,x1] x [y0,y1] (inclusive, clipped to w x
// h). `rgb(x, y, r, g, b)` reads one pixel.
template <class Rgb>
Silhouette measureSilhouette(Rgb rgb, int w, int h, int x0, int y0, int x1, int y1) {
    Silhouette s;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    double sx = 0.0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            int r, g, b;
            rgb(x, y, r, g, b);
            if (!offClear(r, g, b)) continue;
            if (s.top < 0) s.top = y;
            s.bottom = y;
            sx += x;
            ++s.n;
        }
    if (s.n) s.col = sx / s.n;
    return s;
}

// The search window for the target in a frame: the anchor's column +- half
// its height (the caster, at column 640, stays out of it), every row.
struct Window { int x0, y0, x1, y1; };
inline Window targetWindow(const TargetAnchor& a, int h) {
    const int half = static_cast<int>(std::lround(0.5f * a.px));
    return {static_cast<int>(std::lround(a.feetX)) - half, 0, static_cast<int>(std::lround(a.feetX)) + half, h - 1};
}

struct AnchorCheck {
    float dFeet = 0, dCol = 0, dPx = 0, dStand = 0, ratio = 0;  // ratio = height / anchor px (reported)
    bool found = false, feetOk = false, colOk = false, pxOk = false, standOk = false;
    bool ok() const { return found && feetOk && colOk && pxOk && standOk; }
};

// `fitPx` = the standing height the solve rendered (requested px).
inline AnchorCheck checkTargetAnchor(const Silhouette& s, const TargetAnchor& a, float fitPx,
                                     float feetTol = kFeetTolPx, float colTol = kColumnTolPx,
                                     float pxTol = kHeightTolPx, float standPx = kStandingPx,
                                     float standTol = kStandingTolPx) {
    AnchorCheck c;
    c.found = s.n > 0;
    if (!c.found) return c;
    c.dFeet = static_cast<float>(s.bottom) - a.feetY;
    c.dCol = static_cast<float>(s.col) - a.feetX;
    c.dPx = static_cast<float>(s.height()) - fitPx;
    c.dStand = static_cast<float>(s.height()) - standPx;
    c.ratio = static_cast<float>(s.height()) / a.px;
    c.feetOk = std::fabs(c.dFeet) <= feetTol;
    c.colOk = std::fabs(c.dCol) <= colTol;
    c.pxOk = std::fabs(c.dPx) <= pxTol;
    c.standOk = std::fabs(c.dStand) <= standTol;
    return c;
}

}  // namespace fxharness
