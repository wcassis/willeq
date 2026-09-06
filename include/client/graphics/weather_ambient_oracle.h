// T418 (board B231): the ACTIVE-weather ambient response, MEASURED against the
// official Titanium client instead of invented.
//
// WHAT WAS HERE BEFORE.  WeatherEffectsController::getAmbientLightModifier()
// returned `1 - storm.maxDarkening * intensity/10` with maxDarkening = 0.6, so
// server rain at intensity 9 more than halved the world's scene ambient AND the
// sun (modifier 0.460).  Nobody had ever compared that curve with the client it
// is supposed to imitate; it is the number that corrupted T377's whole vision
// measurement window (the struck B211).
//
// WHAT THE ORACLE ACTUALLY DOES.  Measured 2026-09-06 in qeynos2 at
// `#goto -74 428 3.751 160`, both clients, x11grab at 5 fps, plateaus read out
// of the video, every state paired with its own clear reference taken 30 s
// earlier under a freshly re-pinned world clock (B281).  Rain at intensities
// 2, 5 and 9 and snow at intensity 5, at 11:00 and at 22:00:
//
//   * THE WORLD DOES NOT GET DARKER.  It gets BRIGHTER and warmer, because the
//     official client swaps the sky and the fog to an overcast state.  In a
//     geometry-only band the settled ratio against the paired clear reference
//     is R x1.48 / G x1.18 / B x1.11 (grey x1.25) by day.
//   * IT IS NOT A SCENE-AMBIENT TERM.  The ratio grows monotonically with
//     DEPTH -- x1.42/x1.06/x1.01 on the nearest wall strip, x2.73/x1.65/x1.25
//     on the farthest -- which is a fog blend, not a light multiplier.  A
//     scalar on the ambient cannot produce it and must not pretend to.
//   * IT DOES NOT DEPEND ON INTENSITY, OR ON RAIN vs SNOW.  Rain 5, rain 9 and
//     snow 5 settle to the same plateau to better than 0.5% on every channel
//     (0.070611 / 0.070572 / 0.070302 grey), off clear references that agree to
//     0.03%.  The server's intensity byte reaches the client -- the particle
//     density visibly follows it -- but the lighting does not.
//
// SO THE MEASURED AMBIENT CURVE IS FLAT AT 1.000.  Every knot below is a
// measured point (rain 2/5/9 and snow 5, at two hours, in two sessions);
// intermediate intensities are linearly interpolated between knots, which for
// a flat table is 1.000 everywhere.  This is the plan's stated answer for the
// case where the oracle shows no darkening, and it is what was found.
//
// The overcast sky/fog swap the oracle DOES perform is a separate, larger job
// with no target yet -- board B360.  The existing fog blend is deliberately
// left byte-for-byte as it was (it is driven by currentDarkening_, which is
// unchanged); only the AMBIENT and SUN term moved.
#ifndef EQT_GRAPHICS_WEATHER_AMBIENT_ORACLE_H
#define EQT_GRAPHICS_WEATHER_AMBIENT_ORACLE_H

#include <cstddef>
#include <cstdint>

namespace EQT {
namespace Graphics {

/// One measured point of the oracle's intensity -> ambient-modifier curve.
struct WeatherAmbientKnot {
    uint8_t intensity;
    float modifier;
};

/// Rain (OP_Weather type 1). Knots 2, 5 and 9 are measured; 0 and 10 bound the
/// table so a lookup never extrapolates.
extern const WeatherAmbientKnot kOracleRainAmbient[];
extern const std::size_t kOracleRainAmbientCount;

/// Snow (OP_Weather type 2). Knot 5 is measured and lands on the rain plateau.
extern const WeatherAmbientKnot kOracleSnowAmbient[];
extern const std::size_t kOracleSnowAmbientCount;

/// Seconds the official client takes to move from one weather state to the
/// next, measured frame-by-frame off the 5 fps capture as the 5%-to-95% span
/// of the transition. It does not depend on the intensity, because the
/// endpoints do not either.
extern const float kOracleWeatherTransitionSeconds;

/// The committed curve: type is 0 (clear), 1 (rain) or 2 (snow); intensity is
/// the server's 0-10 byte. Linear interpolation between the measured knots.
float oracleWeatherAmbientModifier(uint8_t type, uint8_t intensity);

/// The weather transition ramp. ONE definition: SimulationWorker's per-tick
/// copy and WeatherEffectsController's own both call this, so the measured
/// duration cannot land in one and not the other (board B361).
/// `rate` is units per second, captured when the target last changed so that
/// the whole move takes kOracleWeatherTransitionSeconds whatever its size.
float advanceWeatherDarkening(float current, float target, float rate, float deltaTime);

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_WEATHER_AMBIENT_ORACLE_H
