#pragma once

// Vision type -> global ambient light (T361, board B067).
//
// WHY THIS EXISTS. Before T361 the player's vision type reached exactly two
// places, both of which scale the zone's DATA-ONLY point lights
// (SimulationWorker::applyVisionWeatherToZoneLights and
// ::computeLightAnimations). It never reached the global ambient, so at night
// a full Ultravision buff was worth +4.9% on a world crop while the day/night
// cycle was worth 4x (measured live, T358). A player could not see in the
// dark with Ultravision up.
//
// WHAT THE OFFICIAL CLIENT DOES (measured T361, review/T361/README.md; the
// oracle is the Titanium client under Wine/Xvfb, same server, same zone, same
// spot, forced 22:00, character Oraclealpha -- a HUMAN, so no racial floor
// confounds the buff):
//
//   * Ultravision lifts the whole scene by a LOT: x1.554 in linear light on a
//     700x420 world crop (x1.334 on the same crop's sRGB grey mean), against
//     a still-to-still noise floor of ~1%. The video resolves the two edges
//     to a single frame each.
//   * Infravision (Serpent Sight, spell 276) lifts LESS and shifts RED:
//     x1.253 linear, with R x1.328 against B x1.198.
//   * The SKY does not change (x1.02 on a sky-only crop) -- so it is not a
//     post-process on the frame.
//   * NEAR geometry lifts x1.325 and DISTANT geometry x3.25. A fog or
//     draw-distance change could produce the distant lift but NOT the near
//     one (fog is ~0 up close), so the mechanism is a global AMBIENT raise
//     showing up against an almost-black base at distance.
//
// THE MECHANISM CHOSEN. Vision raises a FLOOR under the global ambient, per
// channel. A floor -- rather than a multiply or an add -- is what makes the
// buff do nothing in daylight (fable's day ambient is 0.5 on every channel,
// already above every floor here) while dominating at night (fable's night
// ambient is 0.08/0.08/0.15), which is how EQ's vision buffs read.
//
// THE NUMBERS (T383, board B171 -- these SUPERSEDE T361's). T361 left only the
// two SCALAR gains free and took the chromaticities from its own near-wall
// added-light deltas. T377 then measured BOTH clients in one session -- same
// spot, same crop, video-stepped on both sides -- and the per-channel shape
// came out wrong even where the scalar was close:
//
//                       oracle R / G / B          fable R / G / B  (T377)
//     Ultravision   x1.4581  x1.4633  x1.8507   x1.4609 x1.2814 x1.6629
//     Infravision   x1.3284  x1.2111  x1.2079   x1.6002 x1.1264 x0.8209
//
// fable's Ultravision green lagged its red by 12.5 points (a magenta cast
// where the oracle's lift is near-neutral with blue on top), and fable's
// Infravision drove BLUE DOWN -- the only channel in either table that moved
// the wrong way. T383 re-solves all SIX floor channels against that oracle
// table instead of two scalars against two grey ratios. See the plan's
// Review for the fit and its residuals (all six within 0.11%).

#include <cstdint>

#include "client/graphics/renderer_types.h"

namespace EQT {
namespace Graphics {

// A global ambient colour, linear, one float per channel.
struct AmbientRGB {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// THE SIX SOLVED PARAMETERS (T383). Each vision's floor is written as a gain
// (the floor's channel mean) times a tint (the floor normalised to unit mean),
// purely to keep T361's shape -- but unlike T361 the tint is NOT a measured
// chromaticity any more: all three channels of each floor were solved.
//
// THE SOLVE. Invert the power law below, per channel, for the oracle's
// per-channel frame ratio against fable's night ambient (0.08/0.08/0.15):
//
//     floor_c = night_c * (oracle_ratio_c) ^ (1 / 1.1405)
//
//   Ultravision  required ambient ratio 1.3919 / 1.3963 / 1.7156
//                floor                  0.1113 / 0.1118 / 0.2573
//   Infravision  required ambient ratio 1.2827 / 1.1829 / 1.1801
//                floor                  0.1027 / 0.0947 / 0.1771
//
// Forward-checked with the rounded constants below, the predicted per-channel
// ratios land within 0.11% of every one of the six oracle numbers, and the
// whole-frame grey ratios they imply on fable's own measured night channels
// come out 1.5561 / 1.2581 against the oracle's 1.5612 / 1.2555 (-0.33% /
// +0.21%) -- so the channels and the scalar are reproduced TOGETHER.
//
// WHY THE FLOORS ARE BLUE-HEAVY WHERE THE ORACLE'S ADDED LIGHT IS NOT. The
// target is the oracle's per-channel frame RATIO, and a ratio is measured
// against each client's own base. fable's night ambient is blue-dominant
// (0.08/0.08/0.15) while the oracle's night frame is red-dominant
// (0.0385/0.0354/0.0257), so reaching the same ratios from a blue-dominant
// base needs proportionally more blue in the floor. Ratios are the only
// composition-invariant quantity across the two clients (T367's rule) and
// they are what B171 asks to match.
//
// Units are "ambient light level", the same units as the day/night ambient in
// IrrlichtRenderer::updateTimeOfDay() (night 0.08/0.08/0.15, day 0.5/0.5/0.5).
// Every floor is still below daylight, so a vision buff is still invisible at
// noon.
inline constexpr float kUltravisionAmbientGain = 0.1601f;
inline constexpr float kInfravisionAmbientGain = 0.1248f;

inline constexpr float kUltravisionTint[3] = {0.695f, 0.698f, 1.607f};
inline constexpr float kInfravisionTint[3] = {0.823f, 0.759f, 1.419f};

// NOT SOLVED HERE, and NOT the cause of fable's B x0.821. The second place
// vision reaches the frame is the zone-light arm
// (visionLightModifiers(), client/graphics/vision_type.h). Its Infravision
// redShift was 0.3, a 1.857:1 red-to-blue spread on every lamp against the
// oracle's measured 1.0998; T383 re-solved it to 0.0508 from the same oracle
// row. It could never have DARKENED blue, though: Infravision's blue
// multiplier is 0.75*(1-s), above Normal's flat 0.25 for every s < 0.667.
// Nothing in the tree can lower a channel under a vision buff, so fable's
// measured B x0.821 is unexplained by the code and the live re-measure owed
// to the next lock unit has to account for it.

// THE RESPONSE MODEL (T361 probe 1, re-used by T383). fable's linear-light
// frame value responds to the ambient as a power law; the two T361 probe
// points gave the exponent independently as 1.1402 and 1.1409. It is the one
// measured quantity that lets a floor be solved against an ORACLE FRAME
// RATIO, and T383 applies it per channel.
inline constexpr float kVisionFrameExponent = 1.1405f;

// The per-channel frame ratio this mapping predicts against `base` -- i.e.
// what a buffed frame is worth over an unbuffed one, per channel, under the
// power law above. This is the quantity the oracle table is expressed in
// (review/T377/README.md section 2) and the quantity tests/test_vision_ambient.cpp
// pins. Not used in production rendering: it is the model the constants were
// solved from, written down so it can be checked.
AmbientRGB visionFrameRatios(VisionType vision, AmbientRGB base);

// The ambient floor a vision type imposes. Normal imposes none.
AmbientRGB visionAmbientFloor(VisionType vision);

// The racial vision floor, measured against the official Titanium client
// (T361, board B060; the table and the method are documented on the one
// caller, PlayerCameraComponent::setPlayerRace). Ultravision belongs to the
// Dark Elf alone; Human, Barbarian and Erudite are Normal; every other
// measured playable race is Infravision. Froglok (330) measured a distinct
// fourth level this enum cannot express and is left Normal -- see B060.
VisionType racialBaseVision(uint16_t raceId);

// Raise `base` to that floor, per channel. Never lowers anything: a daylight
// ambient is already above every floor, which is why Ultravision is invisible
// at noon and transforms midnight.
AmbientRGB applyVisionAmbientFloor(VisionType vision, AmbientRGB base);

}  // namespace Graphics
}  // namespace EQT
