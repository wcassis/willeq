// tools/model_viewer/readback_probe.cpp -- B1198 (#517): the dirty-gate's
// probe rectangle in SURFACE pixels. The union of the caster and target pose
// boxes over EVERY plan placement (each cast re-places the target at its
// distance, so the initial placement alone would not cover the run), grown by
// 100 % of the union's own width and height on every side (the spell flares
// reach beyond the pose boxes) and clamped to the requested frame; requested
// (x, y) is surface (x*Sx, y*Sy), the stretched-canvas rule writeSceneBoxes'
// header states. The run's audit (refactor/B1198/scripts/dirty_audit.py) then
// checks every SKIPPED frame against the driver baseline run's pixels: a
// skipped frame whose driver baseline differs from the previous captured
// frame is a false clean, and the row says so.
//
// Pure move from model_viewer_fx_harness.cpp (same symbols, same body); it
// reads the harness scene globals through their extern declarations
// (model_viewer_state.h, model_viewer_fx_harness.h) and calls the harness's
// placeHarnessTarget to step through the placements.

#include "readback_probe.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "fx_target_fit.h"  // Pinhole, makePinhole (the harness's projection)
#include "model_viewer_state.h"  // camera, animatedNode, targetAnimatedNode

void placeHarnessTarget(bool self, float distance);  // the harness target placement

eqt::fxharness::readback::ProbeRect readbackProbeForPlan(const std::vector<HarnessCast>& plan,
                                                         uint32_t sw, uint32_t sh) {
    eqt::fxharness::readback::ProbeRect out;  // invalid until the loop below fills it
    if (!camera || !animatedNode) return out;
    const float w = static_cast<float>(harnessOpts.width), h = static_cast<float>(harnessOpts.height);
    const Pinhole ph = makePinhole(camera->getPosition(), camera->getTarget(), camera->getFOV(),
                                   camera->getAspectRatio(), w, h);
    auto unionBoxes = [&](int& x0, int& y0, int& x1, int& y1) {
        EQAnimatedMeshSceneNode* const nodes[] = {animatedNode, targetAnimatedNode};
        for (auto* node : nodes) {
            if (!node || !node->isVisible()) continue;
            node->forceAnimationUpdate();
            core::aabbox3df box = node->getBoundingBox();
            node->getAbsoluteTransformation().transformBoxEx(box);
            core::vector3df c[8];
            box.getEdges(c);
            for (const auto& p : c) {
                const core::vector2df s = ph.project(p);
                x0 = std::min(x0, static_cast<int>(s.X));
                y0 = std::min(y0, static_cast<int>(s.Y));
                x1 = std::max(x1, static_cast<int>(s.X));
                y1 = std::max(y1, static_cast<int>(s.Y));
            }
        }
    };
    std::set<std::pair<bool, float>> placements;
    for (const auto& c : plan) placements.insert({c.self, c.distance});
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (const auto& [self, distance] : placements) {
        placeHarnessTarget(self, distance);
        unionBoxes(x0, y0, x1, y1);
    }
    placeHarnessTarget(plan[0].self, plan[0].distance);  // restore the run's opening placement
    if (x0 >= x1 || y0 >= y1) return out;
    const int mw = std::max(1, x1 - x0), mh = std::max(1, y1 - y0);  // 100 % on every side
    x0 = std::max(0, x0 - mw);
    y0 = std::max(0, y0 - mh);
    x1 = std::min(static_cast<int>(w), x1 + mw);
    y1 = std::min(static_cast<int>(h), y1 + mh);
    if (x0 >= x1 || y0 >= y1) return out;
    out.x0 = static_cast<int>(std::lround(x0 * sw / w));
    out.y0 = static_cast<int>(std::lround(y0 * sh / h));
    out.x1 = static_cast<int>(std::lround(x1 * sw / w));
    out.y1 = static_cast<int>(std::lround(y1 * sh / h));
    out.valid = out.x1 > out.x0 && out.y1 > out.y0;
    return out;
}
