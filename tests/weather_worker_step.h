#pragma once

// T423 (board B361): drive the weather state the way the CLIENT does.
//
// The weather ramp had three implementations and production ran exactly one:
// the render thread snapshots WeatherEffectsController into a SimulationInput
// (renderer_sim_bridge.cpp), SimulationWorker::computeWeatherEffectsState()
// advances it off-thread, and WeatherEffectsController::applyState() writes
// the result back. The other two -- the controller's own computeState() and
// its update()/updateStormAtmosphere() -- had no production caller, and the
// tests drove computeState(). So the ramp under test and the ramp on screen
// were different code, and planting a 4x rate in the production copy left all
// nineteen weather cases green. T423 deleted the two dead copies; this header
// is the hop that keeps the remaining tests honest.
//
// The snapshot below is field-for-field the one renderer_sim_bridge.cpp
// builds (its `input.weather*` block) and the apply is field-for-field its
// weatherEffectsState arm. If either drifts, this header must move with it.

#include "client/graphics/simulation_worker.h"
#include "client/graphics/weather_effects_controller.h"

namespace willeq_test {

// One frame of the production hop: snapshot -> worker -> apply.
inline void weatherWorkerStep(EQT::Graphics::WeatherEffectsController& c, float dt) {
    EQT::Graphics::SimulationInput in;
    in.deltaTime = dt;
    in.weatherEnabled = true;
    in.weatherTransitionProgress = c.getTransitionProgress();
    in.weatherTransitionDuration = c.getTransitionDuration();
    in.weatherCurrentDarkening = c.getCurrentDarkening();
    in.weatherTargetDarkening = c.getTargetDarkening();
    in.weatherDarkeningRate = c.getDarkeningRate();
    in.weatherLightningFlashTimer = c.getLightningFlashTimer();
    in.weatherLightningBoltTimer = c.getLightningBoltTimer();
    in.weatherLightningTimer = c.getLightningTimer();
    in.weatherLightningActive = c.isLightningActive();
    in.weatherLightningEnabled = c.isLightningEnabled();
    in.weatherType = c.getCurrentType();
    in.weatherIntensity = c.getCurrentIntensity();

    EQT::Graphics::SimulationOutput out;
    EQT::Graphics::SimulationWorker::computeWeatherEffectsState(in, out);

    EQT::Graphics::WeatherEffectsController::WeatherEffectsState wes;
    wes.transitionProgress = out.weatherEffectsState.transitionProgress;
    wes.currentDarkening = out.weatherEffectsState.currentDarkening;
    wes.lightningFlashTimer = out.weatherEffectsState.lightningFlashTimer;
    wes.lightningBoltTimer = out.weatherEffectsState.lightningBoltTimer;
    wes.lightningActive = out.weatherEffectsState.lightningActive;
    wes.triggerLightningFlash = out.weatherEffectsState.triggerLightningFlash;
    c.applyState(wes);
}

// `seconds` of simulated time in `dt` steps of that hop.
inline void weatherWorkerAdvance(EQT::Graphics::WeatherEffectsController& c,
                                 float seconds, float dt = 0.05f) {
    for (float t = 0.0f; t < seconds; t += dt) weatherWorkerStep(c, dt);
}

}  // namespace willeq_test
