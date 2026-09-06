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
// THE NUMBERS ARE T405's, AND THEY WERE SOLVED LIVE (board B203/B252/B253).
// Every earlier value here was fitted against a measurement with an
// uncontrolled confound, and each was refuted by the next unit that
// controlled it:
//
//   * T377/T361's session ran under SERVER RAIN at weatherMod 0.460, which
//     darkens the base a ratio is measured against while leaving the floor
//     fixed -- so every ratio it produced was INFLATED (B211, closed T396;
//     B252, closed T405).
//   * T383 fitted a forward `ambient^1.1405` power law to those numbers and
//     extrapolated it; T386 measured the result and it was WORSE on both
//     scalars. T394 reverted it. A refuted model is reverted, not patched.
//   * T401 measured under a proven-clear sky and found the ORACLE's own
//     per-channel table did not reproduce between sessions -- because the
//     official client's yaw on login is not the character_data heading, so
//     two "same spot" captures were looking at different scenes (10.3% RMSE
//     against a 2.7% control). B253.
//
// T405 pinned BOTH confounds before measuring anything:
//
//   * weather: `#weather 0 0` plus qeynos2's rain/snow chances zeroed for the
//     session, and the modifier PROVEN 1.000 for the whole window from the
//     client's own log (T396's per-zone-in line, and zero
//     `updateTimeOfDay: weatherMod ... -> ...` lines, which fire on every
//     change in both directions);
//   * heading: `#goto -74 428 3.751 160` -- eqemu goto.cpp's optional 4th
//     argument -> MovePC, the only placement that also AIMS. Two independent
//     oracle sessions then agreed to RMSE 0.389% (fable 1.540%), against
//     T386's 2.7% bar.
//
// The heading-pinned oracle, video-stepped over two sessions that agree to
// better than 0.2% on every channel (review/T405/):
//
//        vision        R        G        B
//        Ultravision   x2.2531  x1.4399  x1.7687
//        Infravision   x1.8744  x1.1956  x1.1936
//
// The six constants below are the LIVE A/B solution to that table -- six
// probe builds, each re-measured in its own session, the search log in
// review/T405/README.md and refactor/T405_vision_resolve_pinned.md. They are
// not a desk fit: the desk fit is what T383 did.
//
// WHAT THE SOLVED CONSTANTS REACH (mean of two confirmation sessions on
// exactly these values, p6 and p6b):
//
//        vision        R         G         B
//        Ultravision   +0.58%    +2.30%    +1.88%
//        Infravision   -0.35%    -3.18%    -2.56%
//
// GREY IS NOT A TARGET fable can be held to, and this is arithmetic, not an
// excuse. "grey" in every table since T361 is the unweighted mean of the
// three channel means, so a grey RATIO is a base-weighted average of the
// three channel ratios -- weighted by the CLIENT'S OWN night base. fable's
// night base is red-heavy (R .00540 G .00453 B .00445); the oracle's is
// green-heavy (R .01900 G .03333 B .02797). Two clients whose per-channel
// ratios agree exactly still report different greys. Held against the grey
// fable WOULD report at exact per-channel agreement -- computed from fable's
// own measured base -- the residuals are +1.39% (Ultravision) and -1.67%
// (Infravision). tests/test_vision_ambient.cpp pins that arithmetic so the
// next unit cannot mistake the gap for a defect.
//
// A CONSTANT HERE MAY NOT BE MOVED BY ARGUMENT. It moves when a live session
// under a proven-clear sky and a pinned heading says so, and the session that
// moved it is named in the commit.

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

// THE SOLVED FLOORS. Gain x tint is the floor itself; the split into a
// scalar and a unit-mean chromaticity is presentational, and BOTH halves are
// solved -- there is no longer one free scalar over a measured chromaticity,
// because T405 found the three channels are COUPLED (raising the R and B
// floors measurably LOWERS the frame's G ratio at a fixed G floor: probe 1's
// G fell to x1.2162 from x1.2769 while its G floor went UP). Units are
// "ambient light level", the same units as the day/night ambient in
// IrrlichtRenderer::updateTimeOfDay() (night is 0.08/0.08/0.15, day
// 0.5/0.5/0.5). The floors these express are
//
//     Ultravision  (0.26545, 0.19653, 0.27623)   probe 6
//     Infravision  (0.18760, 0.11500, 0.18511)   probe 6
inline constexpr float kUltravisionAmbientGain = 0.24607f;
inline constexpr float kInfravisionAmbientGain = 0.16257f;

// The chromaticity half of each solved floor, normalised to unit mean. These
// are NOT the oracle's added-light colours any more (T361 used those; the
// coupling above is why that could not work): they are the chromaticity of
// the floor that REPRODUCES the oracle's frame, solved live with the gain.
inline constexpr float kUltravisionTint[3] = {1.07877f, 0.79866f, 1.12256f};
inline constexpr float kInfravisionTint[3] = {1.15398f, 0.70739f, 1.13864f};

// Both Infravision floors now sit ABOVE the night base on every channel
// (0.08/0.08/0.15), which the pre-T405 values did not: the old Infravision
// floor was (0.147, 0.081, 0.059), so G was a hair above the base and B was
// BELOW it, and the buff could not move those two channels at all. That is
// why every Infravision fit before T405 was solved from one usable channel.

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
