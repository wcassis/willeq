// tools/model_viewer/readback_probe.h -- B1198 (#517): the dirty-gate's
// probe rectangle, split out of model_viewer_fx_harness.cpp as a pure move
// (fx_target_fit.h's precedent: the harness sat over the 1,500-line ceiling
// with the B1198 additions).
#pragma once

#include <cstdint>
#include <vector>

#include "model_viewer_fx_harness.h"
#include "readback_candidates.h"

// The dirty-gate's probe rectangle in SURFACE pixels for a plan (see
// readback_probe.cpp for the construction). Invalid (!valid) when the scene
// nodes are not up yet or nothing projects (the Dirty mode then refuses to
// start, ReadbackContext::create's rule).
eqt::fxharness::readback::ProbeRect readbackProbeForPlan(const std::vector<HarnessCast>& plan,
                                                         uint32_t surfaceW, uint32_t surfaceH);
