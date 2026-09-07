/*
 * The oracle's volume unit — an integer step 0..20 (T439, board B432).
 *
 * T434 MEASURED this: /mnt/projects/EverQuestP1999/eqclient.ini records
 * `Music=18` / `SoundVolume=10`, and the live capture of that same client
 * (review/T377/oracle/T377_oracle_options_alt_o_1024x768.png) shows it
 * displaying 90 % and 50 %. 18/20 and 10/20 — the official client stores a
 * volume as one of 21 integer positions and prints `value * 5` as a percent.
 *
 * WHY THIS HEADER EXISTS RATHER THAN options_layout.h's SLIDER_STEPS.
 * The step is no longer a renderer-side display detail: since T439 the two
 * volume intents carry an ABSOLUTE step and `VolumesChangedData` carries the
 * applied step back, so the GAME thread (BridgeComponent, the slash
 * commands) converts between a step and the 0..1 float `AudioManager` takes.
 * options_layout.h is full of `irr::s32` and cannot be included game-side, so
 * the conversion lives here — device-free, dependency-free, one definition.
 * options_panel.cpp delegates to it and static_asserts that
 * `OptionsLayout::SLIDER_STEPS` still equals `kVolumeSteps`, so the slider's
 * geometry and the wire unit cannot drift apart.
 */

#pragma once

namespace eqt {
namespace audio {

/** The oracle's slider travel: positions 0..kVolumeSteps, i.e. 21 of them. */
constexpr int kVolumeSteps = 20;

/** Clamp an arbitrary integer onto the 0..kVolumeSteps band. */
constexpr int clampVolumeStep(int step) {
    if (step < 0) return 0;
    if (step > kVolumeSteps) return kVolumeSteps;
    return step;
}

/** A step as the 0..1 fraction `AudioManager::setMusicVolume()` takes. */
constexpr float volumeFractionFromStep(int step) {
    return static_cast<float>(clampVolumeStep(step)) /
           static_cast<float>(kVolumeSteps);
}

/**
 * A 0..1 fraction as the nearest step. Lossy on purpose: a `/musicvolume 53`
 * lands on step 11 and the slider then reads 55 %, because 21 positions is
 * what the oracle gives this control. The audio manager keeps the exact
 * float it was told; only the readout rounds.
 */
constexpr int volumeStepFromFraction(float fraction) {
    return clampVolumeStep(
        static_cast<int>(fraction * static_cast<float>(kVolumeSteps) + 0.5f));
}

}  // namespace audio
}  // namespace eqt
