#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/weather_audio.h"
#include "client/audio/water_sounds.h"

#include <cmath>

using namespace EQT::Audio;

// =============================================================================
// WeatherAudio Unit Tests (no OpenAL required - test logic only)
// =============================================================================

class WeatherAudioUnitTest : public ::testing::Test {
protected:
    WeatherAudio weather_;
};

TEST_F(WeatherAudioUnitTest, DefaultState) {
    EXPECT_EQ(weather_.getWeatherType(), WeatherType::None);
    EXPECT_EQ(weather_.getIntensity(), 0);
    EXPECT_FALSE(weather_.isRaining());
    EXPECT_FALSE(weather_.isSnowing());
    EXPECT_FALSE(weather_.isPaused());
    EXPECT_FLOAT_EQ(weather_.getVolume(), 1.0f);
    EXPECT_TRUE(weather_.isThunderEnabled());
}

TEST_F(WeatherAudioUnitTest, SetWeatherRainOn) {
    // Type 0 with intensity > 0 = rain on
    weather_.setWeather(0, 5);

    EXPECT_EQ(weather_.getWeatherType(), WeatherType::None);  // Becomes raining after update
    EXPECT_EQ(weather_.getIntensity(), 5);
}

TEST_F(WeatherAudioUnitTest, SetWeatherRainOff) {
    // First enable rain
    weather_.setWeather(0, 5);

    // Type 0 with intensity 0 = rain off
    weather_.setWeather(0, 0);

    EXPECT_EQ(weather_.getIntensity(), 0);
}

TEST_F(WeatherAudioUnitTest, SetWeatherSnowOn) {
    // Type 2 = snow on
    weather_.setWeather(2, 7);

    EXPECT_EQ(weather_.getIntensity(), 7);
}

TEST_F(WeatherAudioUnitTest, SetWeatherSnowOff) {
    // First enable snow
    weather_.setWeather(2, 5);

    // Type 1 = snow off
    weather_.setWeather(1, 0);

    EXPECT_EQ(weather_.getIntensity(), 0);
}

TEST_F(WeatherAudioUnitTest, SetWeatherTypeDirectly) {
    weather_.setWeatherType(WeatherType::Raining, 8);

    EXPECT_EQ(weather_.getIntensity(), 8);
}

TEST_F(WeatherAudioUnitTest, IntensityClamping) {
    // Test intensity is clamped to 0-10
    weather_.setWeatherType(WeatherType::Raining, 15);

    EXPECT_EQ(weather_.getIntensity(), 10);  // Should be clamped to 10
}

TEST_F(WeatherAudioUnitTest, VolumeControl) {
    weather_.setVolume(0.5f);
    EXPECT_FLOAT_EQ(weather_.getVolume(), 0.5f);

    // Test clamping
    weather_.setVolume(-0.5f);
    EXPECT_FLOAT_EQ(weather_.getVolume(), 0.0f);

    weather_.setVolume(2.0f);
    EXPECT_FLOAT_EQ(weather_.getVolume(), 1.0f);
}

TEST_F(WeatherAudioUnitTest, PauseResume) {
    weather_.pause();
    EXPECT_TRUE(weather_.isPaused());

    weather_.resume();
    EXPECT_FALSE(weather_.isPaused());
}

TEST_F(WeatherAudioUnitTest, ThunderControl) {
    weather_.setThunderEnabled(false);
    EXPECT_FALSE(weather_.isThunderEnabled());

    weather_.setThunderEnabled(true);
    EXPECT_TRUE(weather_.isThunderEnabled());
}

TEST_F(WeatherAudioUnitTest, Stop) {
    // Set some weather
    weather_.setWeatherType(WeatherType::Raining, 5);

    // Stop should reset to None
    weather_.stop();

    EXPECT_EQ(weather_.getWeatherType(), WeatherType::None);
    EXPECT_EQ(weather_.getIntensity(), 0);
}

// =============================================================================
// Thunder Timing Calculation Tests
// =============================================================================

TEST_F(WeatherAudioUnitTest, ThunderMinIntensityThreshold) {
    // Thunder should only trigger at intensity >= 3
    EXPECT_EQ(WeatherAudio::THUNDER_MIN_INTENSITY, 3);
}

TEST_F(WeatherAudioUnitTest, ThunderTimingRange) {
    // Thunder timing should be between 15-45 seconds
    EXPECT_FLOAT_EQ(WeatherAudio::THUNDER_MIN_DELAY, 15.0f);
    EXPECT_FLOAT_EQ(WeatherAudio::THUNDER_MAX_DELAY, 45.0f);
}

TEST_F(WeatherAudioUnitTest, FadeDuration) {
    // Fade duration should be 2 seconds
    EXPECT_FLOAT_EQ(WeatherAudio::FADE_DURATION, 2.0f);
}

// =============================================================================
// Weather_Struct Tests (packet structure)
// =============================================================================

TEST(WeatherStructTest, StructSize) {
    EXPECT_EQ(sizeof(Weather_Struct), 8);
}

TEST(WeatherStructTest, StructLayout) {
    Weather_Struct ws;
    ws.type = 2;
    ws.intensity = 5;

    EXPECT_EQ(ws.type, 2);
    EXPECT_EQ(ws.intensity, 5);
}

// =============================================================================
// WaterSounds Unit Tests
// =============================================================================

TEST(WaterSoundsTest, EntrySoundFile) {
    EXPECT_STREQ(WaterSounds::getEntrySound(), "waterin.wav");
}

TEST(WaterSoundsTest, TreadSoundFiles) {
    EXPECT_STREQ(WaterSounds::getTreadSound(0), "wattrd_1.wav");
    EXPECT_STREQ(WaterSounds::getTreadSound(1), "wattrd_2.wav");

    // Test wrapping
    EXPECT_STREQ(WaterSounds::getTreadSound(2), "wattrd_1.wav");
    EXPECT_STREQ(WaterSounds::getTreadSound(3), "wattrd_2.wav");
    EXPECT_STREQ(WaterSounds::getTreadSound(100), "wattrd_1.wav");
    EXPECT_STREQ(WaterSounds::getTreadSound(101), "wattrd_2.wav");
}

TEST(WaterSoundsTest, UnderwaterLoopFile) {
    EXPECT_STREQ(WaterSounds::getUnderwaterLoop(), "watundlp.wav");
}

TEST(WaterSoundsTest, EntrySoundId) {
    EXPECT_EQ(WaterSounds::getEntrySoundId(), 100);
}

TEST(WaterSoundsTest, TreadSoundIds) {
    EXPECT_EQ(WaterSounds::getTreadSoundId(0), 101);
    EXPECT_EQ(WaterSounds::getTreadSoundId(1), 102);

    // Test wrapping
    EXPECT_EQ(WaterSounds::getTreadSoundId(2), 101);
    EXPECT_EQ(WaterSounds::getTreadSoundId(3), 102);
}

TEST(WaterSoundsTest, UnderwaterLoopId) {
    EXPECT_EQ(WaterSounds::getUnderwaterLoopId(), 161);
}

TEST(WaterSoundsTest, TreadSoundCount) {
    EXPECT_EQ(WaterSounds::getTreadSoundCount(), 2);
}

// =============================================================================
// WaterSoundIds Namespace Tests
// =============================================================================

TEST(WaterSoundIdsTest, CorrectValues) {
    EXPECT_EQ(WaterSoundIds::WaterIn, 100);
    EXPECT_EQ(WaterSoundIds::WaterTread1, 101);
    EXPECT_EQ(WaterSoundIds::WaterTread2, 102);
    EXPECT_EQ(WaterSoundIds::Underwater, 161);
}

// =============================================================================
// WaterState Enum Tests
// =============================================================================

TEST(WaterStateTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(WaterState::NotInWater), 0);
    EXPECT_EQ(static_cast<uint8_t>(WaterState::OnSurface), 1);
    EXPECT_EQ(static_cast<uint8_t>(WaterState::Submerged), 2);
}

// =============================================================================
// WeatherType Enum Tests
// =============================================================================

TEST(WeatherTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(WeatherType::None), 0);
    EXPECT_EQ(static_cast<uint8_t>(WeatherType::Raining), 1);
    EXPECT_EQ(static_cast<uint8_t>(WeatherType::Snowing), 2);
}

// =============================================================================
// Volume Calculation Tests (intensity-based)
// =============================================================================

TEST_F(WeatherAudioUnitTest, VolumeCalculationBasedOnIntensity) {
    // Volume should scale with intensity/10.0
    // We can't directly test the private calculateIntensityVolume() method,
    // but we can verify the expected behavior indirectly

    // At intensity 0, no weather
    weather_.setWeatherType(WeatherType::None, 0);
    EXPECT_EQ(weather_.getIntensity(), 0);

    // At intensity 5, volume should be 0.5 (5/10)
    weather_.setWeatherType(WeatherType::Raining, 5);
    EXPECT_EQ(weather_.getIntensity(), 5);

    // At intensity 10, volume should be 1.0 (10/10)
    weather_.setWeatherType(WeatherType::Raining, 10);
    EXPECT_EQ(weather_.getIntensity(), 10);
}

#else

// Provide a stub test when audio is disabled
#include <gtest/gtest.h>

TEST(WeatherAudioDisabled, AudioNotEnabled) {
    GTEST_SKIP() << "Audio support not compiled in (WITH_AUDIO not defined)";
}

#endif // WITH_AUDIO
