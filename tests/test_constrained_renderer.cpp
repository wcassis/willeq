#include <gtest/gtest.h>
#include "client/graphics/constrained_renderer_config.h"
#include <string>
#include <cmath>

using namespace EQT::Graphics;

class ConstrainedRendererConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// Preset Parsing Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Voodoo1) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("voodoo1"), ConstrainedRenderingPreset::Voodoo1);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("VOODOO1"), ConstrainedRenderingPreset::Voodoo1);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("Voodoo1"), ConstrainedRenderingPreset::Voodoo1);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Voodoo2) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("voodoo2"), ConstrainedRenderingPreset::Voodoo2);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("VOODOO2"), ConstrainedRenderingPreset::Voodoo2);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_TNT) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("tnt"), ConstrainedRenderingPreset::TNT);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("TNT"), ConstrainedRenderingPreset::TNT);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_None) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("none"), ConstrainedRenderingPreset::None);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("NONE"), ConstrainedRenderingPreset::None);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Invalid) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("invalid"), ConstrainedRenderingPreset::None);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset(""), ConstrainedRenderingPreset::None);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("voodoo3"), ConstrainedRenderingPreset::None);
}

// =============================================================================
// Preset Name Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, PresetName_Voodoo1) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::Voodoo1), "Voodoo1");
}

TEST_F(ConstrainedRendererConfigTest, PresetName_Voodoo2) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::Voodoo2), "Voodoo2");
}

TEST_F(ConstrainedRendererConfigTest, PresetName_TNT) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::TNT), "TNT");
}

TEST_F(ConstrainedRendererConfigTest, PresetName_None) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::None), "None");
}

// =============================================================================
// Preset Configuration Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo1) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.framebufferMemoryBytes, 2 * 1024 * 1024);  // 2MB
    EXPECT_EQ(config.textureMemoryBytes, 2 * 1024 * 1024);      // 2MB
    EXPECT_EQ(config.maxTextureDimension, 64);  // Very constrained - fits ~128 textures
    EXPECT_EQ(config.colorDepthBits, 16);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo2) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.framebufferMemoryBytes, 4 * 1024 * 1024);  // 4MB
    EXPECT_EQ(config.textureMemoryBytes, 8 * 1024 * 1024);      // 8MB
    EXPECT_EQ(config.maxTextureDimension, 128);  // Fits ~128 textures in 8MB
    EXPECT_EQ(config.colorDepthBits, 16);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_TNT) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.framebufferMemoryBytes, 8 * 1024 * 1024);  // 8MB
    EXPECT_EQ(config.textureMemoryBytes, 16 * 1024 * 1024);     // 16MB
    EXPECT_EQ(config.maxTextureDimension, 512);
    EXPECT_EQ(config.colorDepthBits, 16);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_None) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::None);

    EXPECT_FALSE(config.enabled);
}

// =============================================================================
// Framebuffer Usage Calculation Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FramebufferUsage_640x480_16bit) {
    ConstrainedRendererConfig config;
    config.colorDepthBits = 16;

    // At 16-bit: front(2) + back(2) + z(2) = 6 bytes per pixel
    // 640 * 480 * 6 = 1,843,200 bytes
    size_t usage = config.calculateFramebufferUsage(640, 480);
    EXPECT_EQ(usage, 640 * 480 * 6);
    EXPECT_EQ(usage, 1843200);
}

TEST_F(ConstrainedRendererConfigTest, FramebufferUsage_800x600_16bit) {
    ConstrainedRendererConfig config;
    config.colorDepthBits = 16;

    // 800 * 600 * 6 = 2,880,000 bytes
    size_t usage = config.calculateFramebufferUsage(800, 600);
    EXPECT_EQ(usage, 800 * 600 * 6);
    EXPECT_EQ(usage, 2880000);
}

TEST_F(ConstrainedRendererConfigTest, FramebufferUsage_1024x768_16bit) {
    ConstrainedRendererConfig config;
    config.colorDepthBits = 16;

    // 1024 * 768 * 6 = 4,718,592 bytes
    size_t usage = config.calculateFramebufferUsage(1024, 768);
    EXPECT_EQ(usage, 1024 * 768 * 6);
    EXPECT_EQ(usage, 4718592);
}

// =============================================================================
// Resolution Calculation Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, MaxResolution_2MB_FBI) {
    // Voodoo1: 2MB FBI - verify calculated resolution fits in memory
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    // Max resolution should be calculated and fit in 2MB
    size_t usage = config.calculateFramebufferUsage(config.maxResolutionWidth, config.maxResolutionHeight);
    EXPECT_LE(usage, 2 * 1024 * 1024);

    // Resolution should be at least 640x480 (classic Voodoo1 resolution)
    EXPECT_GE(config.maxResolutionWidth, 640);
    EXPECT_GE(config.maxResolutionHeight, 480);

    // Resolution should be multiples of 8
    EXPECT_EQ(config.maxResolutionWidth % 8, 0);
    EXPECT_EQ(config.maxResolutionHeight % 8, 0);
}

TEST_F(ConstrainedRendererConfigTest, MaxResolution_4MB_FBI) {
    // Voodoo2: 4MB FBI - verify calculated resolution fits in memory
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);

    // Max resolution should be calculated and fit in 4MB
    size_t usage = config.calculateFramebufferUsage(config.maxResolutionWidth, config.maxResolutionHeight);
    EXPECT_LE(usage, 4 * 1024 * 1024);

    // Resolution should be at least 800x600
    EXPECT_GE(config.maxResolutionWidth, 800);
    EXPECT_GE(config.maxResolutionHeight, 600);

    // Resolution should be multiples of 8
    EXPECT_EQ(config.maxResolutionWidth % 8, 0);
    EXPECT_EQ(config.maxResolutionHeight % 8, 0);
}

TEST_F(ConstrainedRendererConfigTest, MaxResolution_8MB_FBI) {
    // TNT: 8MB FBI - verify calculated resolution fits in memory
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);

    // Max resolution should be calculated and fit in 8MB
    size_t usage = config.calculateFramebufferUsage(config.maxResolutionWidth, config.maxResolutionHeight);
    EXPECT_LE(usage, 8 * 1024 * 1024);

    // Resolution should be at least 1024x768
    EXPECT_GE(config.maxResolutionWidth, 1024);
    EXPECT_GE(config.maxResolutionHeight, 768);

    // Resolution should be multiples of 8
    EXPECT_EQ(config.maxResolutionWidth % 8, 0);
    EXPECT_EQ(config.maxResolutionHeight % 8, 0);
}

// =============================================================================
// Resolution Clamping Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, ClampResolution_OversizedClamped) {
    // Request much larger resolution than max → should clamp
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    int width = 1920;
    int height = 1080;
    bool clamped = config.clampResolution(width, height);

    EXPECT_TRUE(clamped);
    EXPECT_LE(width, config.maxResolutionWidth);
    EXPECT_LE(height, config.maxResolutionHeight);

    // Verify result fits in memory
    size_t usage = config.calculateFramebufferUsage(width, height);
    EXPECT_LE(usage, config.framebufferMemoryBytes);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_UndersizedAllowed) {
    // Request 320x240 with Voodoo1 preset → should pass through unchanged
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    int width = 320;
    int height = 240;
    bool clamped = config.clampResolution(width, height);

    EXPECT_FALSE(clamped);
    EXPECT_EQ(width, 320);
    EXPECT_EQ(height, 240);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_ExactMaxAllowed) {
    // Request exactly max resolution → should pass through unchanged
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    int width = config.maxResolutionWidth;
    int height = config.maxResolutionHeight;
    int origWidth = width;
    int origHeight = height;
    bool clamped = config.clampResolution(width, height);

    EXPECT_FALSE(clamped);
    EXPECT_EQ(width, origWidth);
    EXPECT_EQ(height, origHeight);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_WidthOnlyExceedsMax) {
    // Request width > max with height <= max → width should be clamped
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    int width = config.maxResolutionWidth + 200;
    int height = config.maxResolutionHeight - 100;
    bool clamped = config.clampResolution(width, height);

    EXPECT_TRUE(clamped);
    EXPECT_LE(width, config.maxResolutionWidth);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_HeightOnlyExceedsMax) {
    // Request height > max with width <= max → height should be clamped
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    int width = config.maxResolutionWidth - 100;
    int height = config.maxResolutionHeight + 200;
    bool clamped = config.clampResolution(width, height);

    EXPECT_TRUE(clamped);
    EXPECT_LE(height, config.maxResolutionHeight);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_Voodoo2Preset) {
    // Request very large resolution with Voodoo2 preset → should clamp
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);

    int width = 1920;
    int height = 1080;
    bool clamped = config.clampResolution(width, height);

    EXPECT_TRUE(clamped);
    EXPECT_LE(width, config.maxResolutionWidth);
    EXPECT_LE(height, config.maxResolutionHeight);

    // Verify result fits in memory
    size_t usage = config.calculateFramebufferUsage(width, height);
    EXPECT_LE(usage, config.framebufferMemoryBytes);
}

TEST_F(ConstrainedRendererConfigTest, ClampResolution_TNTPreset) {
    // Request 1920x1080 with TNT preset → should clamp
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);

    int width = 1920;
    int height = 1080;
    bool clamped = config.clampResolution(width, height);

    EXPECT_TRUE(clamped);
    EXPECT_LE(width, config.maxResolutionWidth);
    EXPECT_LE(height, config.maxResolutionHeight);

    // Verify result fits in memory
    size_t usage = config.calculateFramebufferUsage(width, height);
    EXPECT_LE(usage, config.framebufferMemoryBytes);
}

// =============================================================================
// Custom Configuration Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, CustomConfig_ResolutionFromMemory) {
    // Test custom configuration with 3MB framebuffer
    ConstrainedRendererConfig config;
    config.enabled = true;
    config.framebufferMemoryBytes = 3 * 1024 * 1024;  // 3MB
    config.colorDepthBits = 16;
    config.calculateMaxResolution();

    // 3MB / 6 bytes per pixel = 524,288 pixels
    // sqrt(524288 * 4/3) ≈ 836, rounded down to multiple of 8 = 832
    // 832 * 3/4 = 624, rounded down to multiple of 8 = 624
    // But the actual implementation may differ, so just verify it fits
    size_t usage = config.calculateFramebufferUsage(config.maxResolutionWidth, config.maxResolutionHeight);
    EXPECT_LE(usage, 3 * 1024 * 1024);

    // Should be larger than 640x480 (which needs ~1.8MB)
    EXPECT_GE(config.maxResolutionWidth, 640);
    EXPECT_GE(config.maxResolutionHeight, 480);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, ZeroResolution_Handled) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);
    config.calculateMaxResolution();

    int width = 0;
    int height = 0;
    // Should not crash, behavior is implementation-defined
    config.clampResolution(width, height);

    // Zero or positive result expected
    EXPECT_GE(width, 0);
    EXPECT_GE(height, 0);
}

TEST_F(ConstrainedRendererConfigTest, FramebufferUsage_ZeroResolution) {
    ConstrainedRendererConfig config;
    config.colorDepthBits = 16;

    size_t usage = config.calculateFramebufferUsage(0, 0);
    EXPECT_EQ(usage, 0);
}
