// tools/model_viewer/fx_target_fit.h -- B1172 (#491) FIX leg: the b1130
// fixed_npc target fit, split out of model_viewer_fx_harness.cpp as a pure
// move (no behaviour change, identical symbols) after the fable merge put
// the harness over the 1,500-line ceiling. fx_target_fit.cpp defines the
// B1166/B1177 target-fit solver fitFixedNpcTarget, its solved-placement
// globals and the frame-0 self-check, unchanged from the harness; this
// header declares them and carries the PresetGeometry facts the two fits
// share (the harness's fitFaceOnCamera uses them too).
#pragma once

#include <cmath>

#include <irrlicht.h>  // as model_viewer_state.h includes it

namespace irr { namespace video { class IImage; } }

// B1198 (#517): the pinhole Irrlicht's buildProjectionMatrixPerspectiveFovLH
// draws: the pixel a world point lands on for a camera at `eye` looking at
// `look`. Lives here (not in the harness's anonymous namespace) so the
// dirty-gate's probe rectangle (readback_probe.cpp) shares the exact same
// projection the harness's framing code uses. `irr::` is spelled out: the
// harness TUs that use the bare `core::` names carry model_viewer_state.h's
// `using namespace irr;`, a fresh TU does not.
struct Pinhole {
    irr::core::vector3df eye, right, up, fwd;
    float fovY, aspect, w, h;
    irr::core::vector2df project(const irr::core::vector3df& p) const {
        const irr::core::vector3df d = p - eye;
        const float zc = d.dotProduct(fwd);
        const float t = std::tan(fovY * 0.5f);
        return {w * 0.5f + d.dotProduct(right) / zc * (w * 0.5f) / (t * aspect),
                h * 0.5f - d.dotProduct(up) / zc * (h * 0.5f) / t};
    }
};

inline Pinhole makePinhole(const irr::core::vector3df& eye, const irr::core::vector3df& look,
                           float fovY, float aspect, float w, float h) {
    Pinhole ph;
    ph.eye = eye;
    ph.fwd = (look - eye).normalize();
    ph.right = irr::core::vector3df(0, 1, 0).crossProduct(ph.fwd).normalize();  // left-handed
    ph.up = ph.fwd.crossProduct(ph.right).normalize();
    ph.fovY = fovY;
    ph.aspect = aspect;
    ph.w = w;
    ph.h = h;
    return ph;
}

// B1118 tour framing (review/B1118/README.md, review/B1132/gate): chase
// camera `az 180 pitch 12`, i.e. in front of the caster looking back at her,
// 12 degrees down; the caster's feet at (640,605) and 235 px head top to feet
// on a 1280x800 grab. The distance and the look-at height are FITTED here to
// those two pixel facts with the client's projection (62.8 degree horizontal
// FOV, T466); the fit is printed so the report can quote it.
struct PresetGeometry {
    float pitchDeg = 12.0f;
    float feetX = 640.0f, feetY = 605.0f, casterPx = 235.0f;
    // B1155 b1130: the fixed_npc target's recorded screen facts (feet column,
    // feet row, pixel height) from review/B1130/README.md "Geometry".
    float targetFeetX = 0.0f, targetFeetY = 0.0f, targetPx = 0.0f;
};

// B1155 b1130: the solved target placement (world units), set by
// fitFixedNpcTarget and used by placeHarnessTarget.
extern float b1130TargetX, b1130TargetY, b1130TargetZ;
// B1166: the standing target's pixel height the solve rendered (requested px).
extern float b1130TargetFitPx;

// B1155/B1166: the b1130 fixed_npc target solve -- the target's world offset
// (x, z) from its two feet pixels (fx_target_fit.cpp).
bool fitFixedNpcTarget(const PresetGeometry& g);

// B1166 (#485): the frame-0 self-check on a written b1130 frame -- feet row,
// column centroid and the two standing heights against the preset's anchor
// (fx_target_fit.cpp; fx_target_anchor.h).
bool b1130TargetSelfCheck(irr::video::IImage* img);
