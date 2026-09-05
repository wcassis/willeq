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
// THE NUMBERS. The two chromaticities are the oracle's own measured
// added-light colours (the per-channel linear deltas across the landing on
// the near-wall crop, each normalised to unit mean), and the relative
// magnitude 0.480 is the ratio of the two total deltas. Only ONE scalar --
// kVisionAmbientGain -- was left free, and it was solved by A/B against the
// oracle ratio, not chosen by argument. See the plan's Review for the search.

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

// THE TWO SOLVED PARAMETERS. Everything else here is measured; these two are
// what the A/B search solved. Probe 1 put both floors at gain 0.400 and
// measured fable's own night frame at the same spot; the frame's linear-light
// response to the mean ambient turned out to be a clean power law, and the
// two probe points (Ultravision and Infravision, different chromaticities and
// different magnitudes) independently give the SAME exponent -- 1.1402 and
// 1.1409. Inverting it for the oracle's two frame ratios gives the gains
// below. Units are "ambient light level", the same units as the day/night
// ambient in IrrlichtRenderer::updateTimeOfDay() (night is 0.08/0.08/0.15,
// day 0.5/0.5/0.5).
inline constexpr float kUltravisionAmbientGain = 0.1521f;
inline constexpr float kInfravisionAmbientGain = 0.0956f;

// Oracle-measured added-light chromaticities, each normalised to unit mean:
// the per-channel linear deltas across each landing on the near-wall crop.
// Ultravision's added light is near-neutral, very slightly blue -- the
// official client's own flavour text is "'s eyes glow violet". Infravision's
// is strongly red ("'s eyes glow red"), R/B = 2.49 as measured.
inline constexpr float kUltravisionTint[3] = {1.009f, 0.848f, 1.143f};
inline constexpr float kInfravisionTint[3] = {1.539f, 0.843f, 0.617f};

// NOTE on why the two gains are not in the oracle's 0.480 added-light ratio.
// Under a FLOOR, a channel already above the floor keeps its night value, so
// Infravision -- whose floor is red-heavy -- only actually raises R, and needs
// a proportionally larger gain (0.629 of Ultravision's, not 0.480) to move the
// whole frame by the oracle's x1.253. The 0.480 is the ratio of the ADDED
// LIGHT; 0.629 is the ratio of the gains that reproduce the oracle's FRAMES.

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

// THE ORDER (T396, board B211). The scene ambient is composed from four
// things and the order is load-bearing, not incidental:
//
//   1. the day/night curve                (IrrlichtRenderer::updateTimeOfDay)
//   2. x the user's ambient multiplier    (Page Up / Page Down), clamped to 1
//   3. x the weather modifier             (WeatherEffectsController)
//   4. raised to the vision floor         <- LAST
//
// The floor goes last because it is a floor on WHAT THE PLAYER SEES. A vision
// buff is a promise about the darkest the world can get; if weather were
// allowed to multiply it afterwards, a storm would take Ultravision below the
// level the oracle measured it at, and -- the reason B211 exists -- a vision
// RATIO measured live would be inflated in proportion to the weather at the
// time, because weather darkens the base while leaving the floor fixed. That
// is exactly what happened to T377's session (server rain at intensity 9,
// modifier 0.460, uncontrolled), which is why every cross-session vision
// number in T358/T361/T377/T383/T386 is unpinned.
//
// This function IS the production composition -- updateTimeOfDay() calls it
// rather than open-coding the four steps -- so the order above is pinned by
// tests/test_vision_ambient.cpp instead of by a comment.
AmbientRGB composeSceneAmbient(AmbientRGB dayNight, float ambientMultiplier,
                               float weatherModifier, VisionType vision);

}  // namespace Graphics
}  // namespace EQT
