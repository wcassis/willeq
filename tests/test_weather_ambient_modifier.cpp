// T396 (board B211): the weather ambient modifier that multiplies the night
// ambient in IrrlichtRenderer::updateTimeOfDay(), and which no vision
// measurement so far has controlled for.
//
// WHERE THE 0.460 CAME FROM. T394 bisected B203's unexplained +55% blue
// night-base shift to no commit at all and found this instead: T377's vision
// session logged
//
//   [10:35:38.603] updateTimeOfDay: weatherMod=0.460, applying to ambient
//                  r=0.080 g=0.080 b=0.150
//
// (review/T377/logs/fable_session_vis.log:9193) with no `#set weather` in the
// recipe. It is NOT a phantom and it is not a fade artifact. The SERVER sent
// it: the same log carries
//
//   [10:35:04.014] [ZONE]     Weather packet: type=0, intensity=9
//   [10:35:14.652] [GRAPHICS] WeatherEffectsController::setWeather: packet
//                             type=0 intensity=9 -> decoded newType=1
//                             newIntensity=9
//   [10:35:14.761] [GRAPHICS] SimWorker: Activated weather type=1 intensity=9
//
// so qeynos2 was RAINING at intensity 9 for the whole measurement window.
// setWeather() turns that into
//
//   targetDarkening_ = storm.maxDarkening * (intensity / 10)
//                    = 0.6 * 0.9 = 0.54
//   modifier         = 1 - 0.54 = 0.460
//
// exactly, and the two overlay multipliers contributed nothing because the
// unified GLES2 weather particles were active (getRainDaylightMultiplier()
// returns false in that case). `storm.maxDarkening = 0.6` is the shipped
// default and the value the session's own config load reported
// ("Loaded storm settings (lightning=true, darkening=0.6)", line 288);
// lightning was off in that session ("Applied low preset to weather config:
// lightning=false", line 294), which is why the flash term never lifted it.
//
// THE ORACLE'S RULE. The official client does not darken the world for CLEAR
// weather -- T361/T367 measured the oracle's night frame at the same spot as a
// stable cross-session reference, which it could not be if a clear sky moved
// it. So with no weather the modifier must be exactly 1.0. The value for
// ACTIVE weather is UNMEASURED against the oracle and is deliberately left
// alone here (boarded); this file pins the clear-weather rule, the
// reproduction of the 0.460, and the fact that a zone-in cannot inherit a
// previous zone's darkening.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/weather_effects_controller.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/cmake/renderer_guards.cmake)"
#endif

namespace fs = std::filesystem;

using EQT::Graphics::WeatherEffectsConfig;
using EQT::Graphics::WeatherEffectsController;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The controller needs no device for any of this: the ambient modifier reads
// currentDarkening_ plus the two overlay multipliers, and the overlays are
// only constructed by initialize() on the non-GLES2 path. Constructing with
// four nulls and pushing the config in directly is what makes the arithmetic
// above reproducible without a server, a zone or a GL context.
std::unique_ptr<WeatherEffectsController> makeController() {
    auto c = std::make_unique<WeatherEffectsController>(nullptr, nullptr, nullptr, nullptr);
    WeatherEffectsConfig cfg;                    // shipped defaults
    cfg.storm.maxDarkening = 0.6f;               // == the session's loaded value
    cfg.storm.lightningEnabled = false;          // == the session's 'low' preset
    c->setConfig(cfg);
    return c;
}

// Run the storm-darkening integrator the way SimulationWorker does --
// computeState() off-thread, applyState() back on -- until it settles.
void settle(WeatherEffectsController& c, float seconds = 4.0f, float dt = 0.05f) {
    for (float t = 0.0f; t < seconds; t += dt) {
        c.applyState(c.computeState(dt));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The rule: clear weather does not darken.
// ---------------------------------------------------------------------------

TEST(WeatherAmbientModifierTest, NoWeatherPacketLeavesTheModifierAtExactlyOne) {
    auto c = makeController();
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f);
    settle(*c);
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f)
        << "clear weather darkened the world; the oracle's night frame is a "
           "stable cross-session reference, which requires 1.0 here";
}

// The packet qeynos2 actually sent first (log line 436): type=255 is not a
// weather type at all and decodes to "off".
TEST(WeatherAmbientModifierTest, TheGarbageZoneInPacketDecodesToNoDarkening) {
    auto c = makeController();
    c->setWeather(255, 49);
    settle(*c);
    EXPECT_FLOAT_EQ(c->getTargetDarkening(), 0.0f);
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f);
}

// ---------------------------------------------------------------------------
// The reproduction: where T377's 0.460 came from.
// ---------------------------------------------------------------------------

TEST(WeatherAmbientModifierTest, ServerRainIntensityNineReproducesT377sPointFourSixZero) {
    auto c = makeController();
    c->setWeather(0, 9);   // the literal OP_Weather payload from the session log

    EXPECT_EQ(c->getCurrentType(), 1) << "type=0 with intensity>0 decodes to rain";
    EXPECT_EQ(c->getCurrentIntensity(), 9);
    EXPECT_NEAR(c->getTargetDarkening(), 0.54f, 1e-5f)
        << "0.6 maxDarkening * 9/10";

    settle(*c);
    EXPECT_NEAR(c->getCurrentDarkening(), 0.54f, 1e-5f);
    EXPECT_NEAR(c->getAmbientLightModifier(), 0.460f, 1e-4f)
        << "this is the weatherMod=0.460 at fable_session_vis.log:9193 -- "
           "server rain, not a client defect";
}

// It ramps, so a session sampled mid-transition sees a DIFFERENT number: this
// is why one logged value cannot pin a whole measurement window.
TEST(WeatherAmbientModifierTest, TheDarkeningRampsAtHalfAUnitPerSecond) {
    auto c = makeController();
    c->setWeather(0, 9);
    c->applyState(c->computeState(0.5f));
    EXPECT_NEAR(c->getCurrentDarkening(), 0.25f, 1e-5f);
    EXPECT_NEAR(c->getAmbientLightModifier(), 0.75f, 1e-4f);
}

TEST(WeatherAmbientModifierTest, ClearingTheWeatherReturnsTheModifierToOne) {
    auto c = makeController();
    c->setWeather(0, 9);
    settle(*c);
    ASSERT_NEAR(c->getAmbientLightModifier(), 0.460f, 1e-4f);

    c->setWeather(1, 0);   // "rain off"
    settle(*c);
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f);
}

// ---------------------------------------------------------------------------
// B211: a zone-in must not inherit the previous zone's weather.
// ---------------------------------------------------------------------------

TEST(WeatherAmbientModifierTest, ZoneEnterClearsStaleWeatherDarkening) {
    auto c = makeController();
    c->setWeather(0, 9);
    settle(*c);
    ASSERT_NEAR(c->getAmbientLightModifier(), 0.460f, 1e-4f);

    // Zoning out of the rain and into somewhere else. Nothing here has told
    // the client it is raining in the NEW zone -- until its OP_Weather
    // arrives (if it arrives at all) the world must be lit normally.
    c->onZoneEnter("qeynos2");

    EXPECT_EQ(c->getCurrentType(), 0);
    EXPECT_EQ(c->getCurrentIntensity(), 0);
    EXPECT_FLOAT_EQ(c->getTargetDarkening(), 0.0f);
    EXPECT_FLOAT_EQ(c->getCurrentDarkening(), 0.0f);
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f)
        << "the previous zone's rain followed the player through the zone line";
}

TEST(WeatherAmbientModifierTest, ZoneLeaveClearsStaleWeatherDarkening) {
    auto c = makeController();
    c->setWeather(0, 9);
    settle(*c);
    ASSERT_NEAR(c->getAmbientLightModifier(), 0.460f, 1e-4f);

    c->onZoneLeave();
    EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f);
}

// ---------------------------------------------------------------------------
// B211's reachability half. onZoneEnter()/onZoneLeave() existed with ZERO
// callers tree-wide (grepped src/ include/ tests/ tools/ benchmarks/ docker/),
// so the reset above could never run in a real client. Text-level, for the
// same reason test_reachability_sweep.cpp is text-level: a compile-time check
// cannot see that a caller is MISSING.
// ---------------------------------------------------------------------------

TEST(WeatherAmbientModifierTest, TheZoneEnterResetIsReachedFromZoneLoad) {
    const std::string env =
        readFile(kRoot / "src/client/graphics/zone_load/renderer_zone_load_env.cpp");
    EXPECT_NE(env.find("weatherEffects->onZoneEnter("), std::string::npos)
        << "WeatherEffectsController::onZoneEnter() has no production caller, "
           "so weather state survives a zone change";
}

// ---------------------------------------------------------------------------
// B211's observability half: the log line fired only when the modifier both
// CHANGED and was below 0.99, so a return to 1.0 was never logged and a
// session could not be shown to have been weather-free from its own log.
// ---------------------------------------------------------------------------

TEST(WeatherAmbientModifierTest, TheWeatherModifierLogIsNotGatedToDarkeningOnly) {
    const std::string rend =
        readFile(kRoot / "src/client/graphics/renderer/irrlicht_renderer.cpp");
    EXPECT_EQ(rend.find("weatherMod < 0.99f"), std::string::npos)
        << "the weather-modifier log still fires only below 0.99, so a return "
           "to clear is invisible in a session log";
    EXPECT_NE(rend.find("weatherModLogged_"), std::string::npos)
        << "the last-logged modifier is still a function-static, which is "
           "shared by every renderer instance in the process";
}
