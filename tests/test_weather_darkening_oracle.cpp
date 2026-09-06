// T418 (board B231): the ACTIVE-weather darkening, measured against the
// official Titanium client and mimicked.
//
// T396 fixed the CLEAR-weather rule (the modifier is exactly 1.0 with no
// weather) and deliberately left the active-weather curve alone, because
// nobody had ever compared it with the client it imitates. That curve was
// `1 - storm.maxDarkening * intensity/10`, maxDarkening = 0.6, so server rain
// at intensity 9 more than halved the world's scene ambient AND its sun. It is
// the number that corrupted T377's entire vision measurement window.
//
// The oracle was measured on 2026-09-06 (qeynos2, `#goto -74 428 3.751 160`,
// x11grab at 5 fps, plateaus read out of the video, every weather state paired
// with its own clear reference taken 30 s earlier under a freshly re-pinned
// world clock). Rain at intensities 2, 5 and 9 and snow at 5, at 11:00 and at
// 22:00. THE OFFICIAL CLIENT DOES NOT DARKEN THE WORLD FOR WEATHER AT ANY
// INTENSITY -- it brightens it, by swapping the sky and fog to an overcast
// state, and the ratio it produces grows with DEPTH, which no ambient scalar
// can do. So the measured ambient curve is flat at 1.000 and that is what is
// committed; see include/client/graphics/weather_ambient_oracle.h for the
// numbers and review/T418/README.md for the frames.
//
// What IS intensity-independent and IS mimicable is the transition: the
// official client takes ~9.3 s to move between weather states, at every
// intensity and in snow as well as rain. fable took 1.08 s at intensity 9.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/weather_ambient_oracle.h"
#include "client/graphics/weather_effects_controller.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/cmake/renderer_guards.cmake)"
#endif

namespace fs = std::filesystem;

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

// Four nulls: nothing this exercises needs a device (T396's constructor trick).
std::unique_ptr<WeatherEffectsController> makeController() {
    return std::make_unique<WeatherEffectsController>(nullptr, nullptr, nullptr, nullptr);
}

// Drive the controller the way the render thread does -- compute off-thread,
// apply back on -- for `seconds` of simulated time in `dt` steps.
void advance(WeatherEffectsController& c, float seconds, float dt = 0.05f) {
    for (float t = 0.0f; t < seconds; t += dt) {
        c.applyState(c.computeState(dt));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The measured curve.
// ---------------------------------------------------------------------------

TEST(WeatherDarkeningOracleT418Test, TheAmbientModifierIsOneAtEveryIntensityInRain) {
    for (int i = 1; i <= 10; ++i) {
        auto c = makeController();
        c->setWeather(0, static_cast<uint8_t>(i));   // OP_Weather rain payload
        ASSERT_EQ(c->getCurrentType(), 1) << "intensity " << i;
        advance(*c, 30.0f);
        EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f)
            << "rain intensity " << i << ": the official client's settled "
               "geometry plateau under rain 5 and rain 9 is the SAME as under "
               "snow 5 to better than 0.5%, and it is BRIGHTER than clear, not "
               "darker -- no ambient darkening is measurable at any intensity";
    }
}

TEST(WeatherDarkeningOracleT418Test, TheAmbientModifierIsOneAtEveryIntensityInSnow) {
    for (int i = 1; i <= 10; ++i) {
        auto c = makeController();
        c->setWeather(2, static_cast<uint8_t>(i));
        ASSERT_EQ(c->getCurrentType(), 2) << "intensity " << i;
        advance(*c, 30.0f);
        EXPECT_FLOAT_EQ(c->getAmbientLightModifier(), 1.0f) << "snow intensity " << i;
    }
}

TEST(WeatherDarkeningOracleT418Test, TheCommittedTableIsMeasuredAtEveryKnot) {
    using EQT::Graphics::kOracleRainAmbient;
    using EQT::Graphics::kOracleRainAmbientCount;
    using EQT::Graphics::kOracleSnowAmbient;
    using EQT::Graphics::kOracleSnowAmbientCount;
    using EQT::Graphics::oracleWeatherAmbientModifier;

    ASSERT_EQ(kOracleRainAmbientCount, 5u) << "rain knots: 0, 2, 5, 9, 10";
    EXPECT_EQ(kOracleRainAmbient[1].intensity, 2);
    EXPECT_EQ(kOracleRainAmbient[2].intensity, 5);
    EXPECT_EQ(kOracleRainAmbient[3].intensity, 9);
    ASSERT_EQ(kOracleSnowAmbientCount, 3u) << "snow knots: 0, 5, 10";
    EXPECT_EQ(kOracleSnowAmbient[1].intensity, 5);

    for (std::size_t i = 0; i < kOracleRainAmbientCount; ++i) {
        EXPECT_FLOAT_EQ(kOracleRainAmbient[i].modifier, 1.0f);
    }
    for (std::size_t i = 0; i < kOracleSnowAmbientCount; ++i) {
        EXPECT_FLOAT_EQ(kOracleSnowAmbient[i].modifier, 1.0f);
    }

    // Linear interpolation between measured knots, and clear is untouched.
    for (int i = 0; i <= 10; ++i) {
        EXPECT_FLOAT_EQ(oracleWeatherAmbientModifier(1, static_cast<uint8_t>(i)), 1.0f);
        EXPECT_FLOAT_EQ(oracleWeatherAmbientModifier(2, static_cast<uint8_t>(i)), 1.0f);
        EXPECT_FLOAT_EQ(oracleWeatherAmbientModifier(0, static_cast<uint8_t>(i)), 1.0f);
    }
}

// The old curve is gone from the source, not merely shadowed: `maxDarkening`
// must no longer reach the ambient modifier.
TEST(WeatherDarkeningOracleT418Test, TheAmbientModifierNoLongerReadsTheDarkening) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/weather_effects_controller.cpp");
    const std::size_t at = src.find("float WeatherEffectsController::getAmbientLightModifier");
    ASSERT_NE(at, std::string::npos);
    const std::string body = src.substr(at, 1400);
    EXPECT_EQ(body.find("1.0f - currentDarkening_"), std::string::npos)
        << "the ambient modifier is the measured table now, not one minus the "
           "invented darkening";
    EXPECT_NE(body.find("oracleWeatherAmbientModifier"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 2. The fade.
// ---------------------------------------------------------------------------

TEST(WeatherDarkeningOracleT418Test, TheTransitionTakesTheOracleMeasuredSeconds) {
    const float kT = EQT::Graphics::kOracleWeatherTransitionSeconds;
    EXPECT_NEAR(kT, 9.3f, 0.001f)
        << "measured 9.0-9.4 s over four transitions, 5%-to-95% of the ramp";

    for (int intensity : {2, 5, 9}) {
        auto c = makeController();
        c->setWeather(0, static_cast<uint8_t>(intensity));
        const float target = c->getTargetDarkening();
        ASSERT_GT(target, 0.0f);

        advance(*c, 0.5f * kT);
        EXPECT_NEAR(c->getCurrentDarkening() / target, 0.5f, 0.05f)
            << "intensity " << intensity << ": halfway at half the time";

        advance(*c, 0.4f * kT);   // now at 0.9 kT
        EXPECT_LT(c->getCurrentDarkening(), target)
            << "intensity " << intensity << ": settled EARLY -- the old ramp "
               "was a fixed 0.5 units/s, which finished intensity 9 in 1.08 s";

        advance(*c, 0.2f * kT);   // past kT
        EXPECT_FLOAT_EQ(c->getCurrentDarkening(), target)
            << "intensity " << intensity << ": settled by the measured time";
    }
}

TEST(WeatherDarkeningOracleT418Test, ClearingTakesTheSameMeasuredTime) {
    const float kT = EQT::Graphics::kOracleWeatherTransitionSeconds;
    auto c = makeController();
    c->setWeather(0, 9);
    advance(*c, 2.0f * kT);
    ASSERT_NEAR(c->getCurrentDarkening(), c->getTargetDarkening(), 1e-6f);
    const float from = c->getCurrentDarkening();
    ASSERT_GT(from, 0.0f);

    c->setWeather(1, 0);   // rain off
    ASSERT_FLOAT_EQ(c->getTargetDarkening(), 0.0f);
    advance(*c, 0.5f * kT);
    EXPECT_NEAR(c->getCurrentDarkening() / from, 0.5f, 0.05f);
    advance(*c, 0.9f * kT);
    EXPECT_FLOAT_EQ(c->getCurrentDarkening(), 0.0f);
}

// ---------------------------------------------------------------------------
// 3. The ramp the CLIENT runs is the ramp the test drives.
// ---------------------------------------------------------------------------
//
// WeatherEffectsController::computeState() has no production caller: the
// render thread hands a snapshot to SimulationWorker, which advances its OWN
// copy in sim_weather.cpp and hands the result back through applyState().
// WeatherEffectsController::update()/updateStormAtmosphere() has no caller
// either. For years the only ramp under test was one nothing executed
// (board B361). All three now call one function.

TEST(WeatherDarkeningOracleT418Test, EveryCopyOfTheRampCallsTheOneDefinition) {
    for (const char* rel : {"src/client/graphics/sim_weather.cpp",
                            "src/client/graphics/weather_effects_controller.cpp"}) {
        const std::string src = readFile(kRoot / rel);
        EXPECT_NE(src.find("advanceWeatherDarkening"), std::string::npos)
            << rel << " does not use the shared ramp";
        EXPECT_EQ(src.find("float darkeningSpeed = 0.5f"), std::string::npos)
            << rel << " still carries its own 0.5 units/second ramp";
    }
}

TEST(WeatherDarkeningOracleT418Test, TheWorkerRampMatchesTheControllerRamp) {
    using EQT::Graphics::advanceWeatherDarkening;
    auto c = makeController();
    c->setWeather(0, 9);
    const float target = c->getTargetDarkening();
    const float rate = c->getDarkeningRate();
    EXPECT_NEAR(rate, target / EQT::Graphics::kOracleWeatherTransitionSeconds, 1e-6f);

    // The worker is stateless: it is handed current/target/rate and returns the
    // next value. Stepping it beside the controller must not diverge.
    float worker = 0.0f;
    for (int i = 0; i < 400; ++i) {
        worker = advanceWeatherDarkening(worker, target, rate, 0.05f);
        c->applyState(c->computeState(0.05f));
        ASSERT_NEAR(worker, c->getCurrentDarkening(), 1e-6f) << "step " << i;
    }
    EXPECT_FLOAT_EQ(worker, target);
}

TEST(WeatherDarkeningOracleT418Test, TheWorkerIsHandedTheRate) {
    for (const char* rel : {"src/client/graphics/sim/renderer_sim_bridge.cpp",
                            "src/client/graphics/zone_load/renderer_zone_load_env.cpp"}) {
        const std::string src = readFile(kRoot / rel);
        EXPECT_NE(src.find("input.weatherDarkeningRate = ctx_.weatherEffects->getDarkeningRate();"),
                  std::string::npos)
            << rel << " snapshots the target but not the rate, so the worker "
                      "would fall back to a snap";
    }
}

// ---------------------------------------------------------------------------
// 4. The fog blend must not be deleted along with the darkening.
// ---------------------------------------------------------------------------
//
// updateTimeOfDay() blended the weather fog by `1 - getAmbientLightModifier()`,
// which was the darkening only because the modifier was DEFINED as
// `1 - currentDarkening_`. With the modifier now flat at 1.000 that expression
// is identically zero, and the weather fog would have disappeared silently.

TEST(WeatherDarkeningOracleT418Test, TheWeatherFogBlendReadsTheDarkeningNotTheModifier) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/renderer/irrlicht_renderer.cpp");
    const std::size_t at = src.find("getWeatherFogColor()");
    ASSERT_NE(at, std::string::npos);
    const std::string body = src.substr(at, 1600);
    EXPECT_NE(body.find("blendFactor = weatherEffects_->getCurrentDarkening()"),
              std::string::npos)
        << "the fog blend must read the darkening term directly";
    EXPECT_EQ(body.find("blendFactor = 1.0f - weatherMod"), std::string::npos)
        << "with the measured modifier this is identically zero and deletes "
           "the weather fog";
}
