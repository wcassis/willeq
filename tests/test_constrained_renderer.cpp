#include <gtest/gtest.h>
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eq/dds_decoder.h"
#include <string>
#include <cmath>
#include <fstream>
#include <cstring>

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

TEST_F(ConstrainedRendererConfigTest, ParsePreset_MaxMapsToOrangePi) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("max"), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("MAX"), ConstrainedRenderingPreset::OrangePi);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_NoneOffDisabled_MapToOrangePi) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("none"), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("NONE"), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("off"), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("disabled"), ConstrainedRenderingPreset::OrangePi);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Invalid) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("invalid"), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset(""), ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("voodoo3"), ConstrainedRenderingPreset::OrangePi);
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

TEST_F(ConstrainedRendererConfigTest, PresetName_OrangePi) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::OrangePi), "OrangePi");
}

// =============================================================================
// Preset Configuration Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo1) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    // P32: presets are renderer-independent (budgets/features only) —
    // GLES2 is the only backend, regardless of preset.
    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);
    EXPECT_EQ(config.framebufferMemoryBytes, 2 * 1024 * 1024);  // 2MB
    EXPECT_EQ(config.textureMemoryBytes, 2 * 1024 * 1024);      // 2MB
    EXPECT_EQ(config.maxTextureDimension, 64);  // Very constrained - fits ~128 textures
    EXPECT_EQ(config.colorDepthBits, 16);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo2) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);

    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);
    EXPECT_EQ(config.framebufferMemoryBytes, 4 * 1024 * 1024);  // 4MB
    EXPECT_EQ(config.textureMemoryBytes, 8 * 1024 * 1024);      // 8MB
    EXPECT_EQ(config.maxTextureDimension, 128);  // Fits ~128 textures in 8MB
    EXPECT_EQ(config.colorDepthBits, 16);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_TNT) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);

    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);
    EXPECT_EQ(config.framebufferMemoryBytes, 8 * 1024 * 1024);  // 8MB
    EXPECT_EQ(config.textureMemoryBytes, 16 * 1024 * 1024);     // 16MB
    EXPECT_EQ(config.maxTextureDimension, 512);
    EXPECT_EQ(config.colorDepthBits, 16);
    EXPECT_TRUE(config.enableShaders);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_Backend) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);

    // P32: GLES2 is the only backend now, no #ifdef needed.
    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);
    EXPECT_TRUE(config.useDRM);
    EXPECT_EQ(config.framebufferMemoryBytes, 10 * 1024 * 1024);  // 10MB
    EXPECT_EQ(config.textureMemoryBytes, 64 * 1024 * 1024);      // 64MB
    EXPECT_EQ(config.colorDepthBits, 16);
    EXPECT_EQ(config.maxTextureDimension, 512);
    // Rendering feature defaults
    EXPECT_TRUE(config.fog);
    EXPECT_TRUE(config.playerLight);
    EXPECT_TRUE(config.objectLights);
    EXPECT_TRUE(config.directionalLight);
    EXPECT_TRUE(config.fireEffects);
    EXPECT_TRUE(config.skyRendering);
    EXPECT_TRUE(config.nameTagsEnabled);
    EXPECT_FALSE(config.wireframe);
    EXPECT_FALSE(config.frameTimingEnabled);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_BlessedOccluderBudget) {
    // P14 (blessed by Will): 256, not the struct/pre-P14 default of 16.
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    EXPECT_EQ(config.occlusionMaxOccluderRegions, 256);
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

// =============================================================================
// GPU Feature Flag Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_GpuFeatures) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);

    EXPECT_EQ(config.framebufferMemoryBytes, 10 * 1024 * 1024);  // 10MB
    EXPECT_EQ(config.textureMemoryBytes, 64 * 1024 * 1024);      // 64MB
    EXPECT_EQ(config.maxTextureDimension, 512);
    EXPECT_EQ(config.colorDepthBits, 16);
    EXPECT_FLOAT_EQ(config.clipDistance, 300.0f);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 300.0f);
    EXPECT_EQ(config.maxPolygonsPerFrame, 80000);

    // GPU feature flags
    EXPECT_TRUE(config.enableMipmaps);
    EXPECT_TRUE(config.enableNPOT);
    EXPECT_TRUE(config.enableStencilBuffer);
    EXPECT_TRUE(config.enableAlphaToCoverage);
    EXPECT_EQ(config.antiAliasLevel, 4);
    EXPECT_FALSE(config.enableCompressedTextures);  // Mali 400 via Lima software-decodes S3TC; use ETC1 atlas instead
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo1_NoGpuFeatures) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);

    // Legacy presets should not have GPU features enabled
    EXPECT_FALSE(config.enableMipmaps);
    EXPECT_FALSE(config.enableNPOT);
    EXPECT_FALSE(config.enableStencilBuffer);
    EXPECT_FALSE(config.enableAlphaToCoverage);
    EXPECT_EQ(config.antiAliasLevel, 0);
    EXPECT_FALSE(config.enableCompressedTextures);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo2_NoGpuFeatures) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);

    EXPECT_FALSE(config.enableMipmaps);
    EXPECT_FALSE(config.enableStencilBuffer);
    EXPECT_EQ(config.antiAliasLevel, 0);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_TNT_Mipmaps) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);

    EXPECT_TRUE(config.enableMipmaps);
    // TNT doesn't have stencil/NPOT/A2C by default
    EXPECT_FALSE(config.enableStencilBuffer);
    EXPECT_FALSE(config.enableAlphaToCoverage);
    EXPECT_EQ(config.antiAliasLevel, 0);
}

TEST_F(ConstrainedRendererConfigTest, DefaultConfig_NoGpuFeatures) {
    // Default-constructed config should have all GPU features disabled
    ConstrainedRendererConfig config;

    EXPECT_FALSE(config.enableMipmaps);
    EXPECT_FALSE(config.enableCompressedTextures);
    EXPECT_FALSE(config.enableNPOT);
    EXPECT_FALSE(config.enableStencilBuffer);
    EXPECT_FALSE(config.enableAlphaToCoverage);
    EXPECT_EQ(config.antiAliasLevel, 0);
}

// =============================================================================
// OrangePi Resolution Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, MaxResolution_10MB_OrangePi) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);

    // Max resolution should fit in 10MB
    size_t usage = config.calculateFramebufferUsage(config.maxResolutionWidth, config.maxResolutionHeight);
    EXPECT_LE(usage, 10 * 1024 * 1024);

    // Should support at least 1024x768 (10MB / 8 bytes = 1.25M pixels)
    EXPECT_GE(config.maxResolutionWidth, 1024);
    EXPECT_GE(config.maxResolutionHeight, 768);

    // Resolution should be multiples of 8
    EXPECT_EQ(config.maxResolutionWidth % 8, 0);
    EXPECT_EQ(config.maxResolutionHeight % 8, 0);
}

// =============================================================================
// JSON Override Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, JsonOverride_AppliesValues) {
    // Create a temporary JSON file
    const std::string testJsonPath = "/tmp/test_constrained_presets.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "testpreset": {
                    "maxTextureDimension": 1024,
                    "clipDistance": 500.0,
                    "enableMipmaps": true,
                    "antiAliasLevel": 8
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.maxTextureDimension = 256;
    config.clipDistance = 200.0f;
    config.enableMipmaps = false;
    config.antiAliasLevel = 0;

    bool applied = config.loadJsonOverrides("testpreset", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.maxTextureDimension, 1024);
    EXPECT_FLOAT_EQ(config.clipDistance, 500.0f);
    EXPECT_TRUE(config.enableMipmaps);
    EXPECT_EQ(config.antiAliasLevel, 8);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_CaseInsensitive) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_ci.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "OrangePi": {
                    "maxTextureDimension": 256
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.maxTextureDimension = 128;

    bool applied = config.loadJsonOverrides("orangepi", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.maxTextureDimension, 256);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_MissingFile) {
    ConstrainedRendererConfig config;
    config.maxTextureDimension = 128;

    bool applied = config.loadJsonOverrides("orangepi", "/tmp/nonexistent.json");
    EXPECT_FALSE(applied);
    EXPECT_EQ(config.maxTextureDimension, 128);  // Unchanged
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_MissingPreset) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_mp.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({ "presets": { "other": { "maxTextureDimension": 1024 } } })";
    }

    ConstrainedRendererConfig config;
    config.maxTextureDimension = 128;

    bool applied = config.loadJsonOverrides("orangepi", testJsonPath);
    EXPECT_FALSE(applied);
    EXPECT_EQ(config.maxTextureDimension, 128);  // Unchanged

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_PartialOverride) {
    // Only override some fields, leave others at preset defaults
    const std::string testJsonPath = "/tmp/test_constrained_presets_po.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "orangepi": {
                    "maxTextureDimension": 256
                }
            }
        })";
    }

    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    bool applied = config.loadJsonOverrides("orangepi", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.maxTextureDimension, 256);  // Overridden
    EXPECT_FLOAT_EQ(config.clipDistance, 300.0f);  // Kept from preset
    EXPECT_TRUE(config.enableMipmaps);  // Kept from preset

    std::remove(testJsonPath.c_str());
}

// P32: RenderingBackend is GLES2-only now (docs/decisions.md 2026-07-28
// addendum) — the "renderingBackend"/"useGLES2" JSON preset keys no longer
// change anything (accepted for backward file-compat, ignored/warned).
// This test now covers that ignoring behavior, plus that other overrides
// in the same preset still apply normally.
TEST_F(ConstrainedRendererConfigTest, JsonOverride_RenderingBackendKeyIgnored) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_rb.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "renderingBackend": "opengl",
                    "fog": false,
                    "playerLight": false,
                    "frameTimingEnabled": true
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.fog = true;
    config.playerLight = true;
    config.frameTimingEnabled = false;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);
    EXPECT_FALSE(config.fog);
    EXPECT_FALSE(config.playerLight);
    EXPECT_TRUE(config.frameTimingEnabled);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_OcclusionMaxOccluderRegions) {
    // P39: JSON preset load previously had no parse for this key at all —
    // it was silently un-tunable from a config file, regardless of the
    // C++ preset default. Verify the key now round-trips.
    const std::string testJsonPath = "/tmp/test_constrained_presets_occ.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "occlusionMaxOccluderRegions": 64
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.occlusionMaxOccluderRegions = 16;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.occlusionMaxOccluderRegions, 64);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_OcclusionMaxOccluderRegions_AbsentLeavesBaseline) {
    // Confirm the key is genuinely optional: a preset block that omits it
    // leaves whatever fromPreset()/the caller already set untouched (the
    // documented last-writer-wins precedence, not a forced reset to the
    // struct default).
    const std::string testJsonPath = "/tmp/test_constrained_presets_occ_absent.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({ "presets": { "custom": { "maxTextureDimension": 128 } } })";
    }

    ConstrainedRendererConfig config;
    config.occlusionMaxOccluderRegions = 256;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.occlusionMaxOccluderRegions, 256);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, ShippedOrangePiPreset_ActivelyCarries256) {
    // P39 active-proof: the REAL shipped config/constrained_presets.json
    // (not a synthetic copy) must now carry occlusionMaxOccluderRegions
    // explicitly, and the exact sequence Application::initialize()
    // (application.cpp) runs -- fromPreset then loadJsonOverrides -- must
    // resolve to the P14-blessed 256.
#ifdef WILLEQ_CONSTRAINED_PRESETS_JSON
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    bool applied = config.loadJsonOverrides("orangepi", WILLEQ_CONSTRAINED_PRESETS_JSON);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.occlusionMaxOccluderRegions, 256);
#else
    GTEST_SKIP() << "WILLEQ_CONSTRAINED_PRESETS_JSON not defined by build";
#endif
}

// P42: same-pattern fix for the 3 keys P39's audit flagged as sharing the
// exact occlusionMaxOccluderRegions loss pattern (non-default fromPreset()
// value, no isMember() parse) — totalMemoryBudgetBytes, occlusionBufferWidth,
// occlusionBufferHeight.

TEST_F(ConstrainedRendererConfigTest, JsonOverride_TotalMemoryBudgetBytes) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_membudget.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "totalMemoryBudgetBytes": 67108864
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.totalMemoryBudgetBytes = 0;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.totalMemoryBudgetBytes, static_cast<size_t>(67108864));

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_OcclusionBufferWidthHeight) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_occbuf.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "occlusionBufferWidth": 128,
                    "occlusionBufferHeight": 64
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.occlusionBufferWidth = 0;
    config.occlusionBufferHeight = 0;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.occlusionBufferWidth, 128);
    EXPECT_EQ(config.occlusionBufferHeight, 64);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_MemoryAndOcclusionBufferKeys_AbsentLeavesBaseline) {
    // Same optionality guarantee as occlusionMaxOccluderRegions: a preset
    // block that omits these keys leaves whatever fromPreset()/the caller
    // already set untouched.
    const std::string testJsonPath = "/tmp/test_constrained_presets_membuf_absent.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({ "presets": { "custom": { "maxTextureDimension": 128 } } })";
    }

    ConstrainedRendererConfig config;
    config.totalMemoryBudgetBytes = 128 * 1024 * 1024;
    config.occlusionBufferWidth = 64;
    config.occlusionBufferHeight = 32;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.totalMemoryBudgetBytes, static_cast<size_t>(128 * 1024 * 1024));
    EXPECT_EQ(config.occlusionBufferWidth, 64);
    EXPECT_EQ(config.occlusionBufferHeight, 32);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, ShippedOrangePiPreset_ActivelyCarriesMemoryAndOcclusionBufferKeys) {
    // Active-proof against the REAL shipped config/constrained_presets.json:
    // the same fromPreset()-then-loadJsonOverrides() sequence must resolve
    // totalMemoryBudgetBytes/occlusionBufferWidth/occlusionBufferHeight to
    // their P42-documented shipped values (which match the C++ preset
    // defaults — the JSON now makes them explicit/tunable, not a change in
    // the resolved values).
#ifdef WILLEQ_CONSTRAINED_PRESETS_JSON
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    bool applied = config.loadJsonOverrides("orangepi", WILLEQ_CONSTRAINED_PRESETS_JSON);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.totalMemoryBudgetBytes, static_cast<size_t>(128 * 1024 * 1024));
    EXPECT_EQ(config.occlusionBufferWidth, 64);
    EXPECT_EQ(config.occlusionBufferHeight, 32);
#else
    GTEST_SKIP() << "WILLEQ_CONSTRAINED_PRESETS_JSON not defined by build";
#endif
}

// T109: entity load distance gate. entityLoadRadius must be at least
// entityRenderDistance (you cannot draw what you did not load) — validate()
// enforces that. The gate mechanism itself SHIPS INERT (see the field's
// comment in constrained_renderer_config.h for why: the post-load
// background-streaming pickup path was found live-broken for entities
// deferred at zone load, a permanently-missing-NPC risk). No preset sets
// entityLoadRadius — every preset's fromPreset() output, and the shipped
// JSON, must therefore resolve to the struct's own (huge, effectively
// unbounded) default, not some smaller per-preset value.

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_EntityLoadRadius_ShipsInert) {
    ConstrainedRendererConfig defaults;  // struct-default baseline
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 300.0f);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, defaults.entityLoadRadius);
    EXPECT_GE(config.entityLoadRadius, config.entityRenderDistance);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo1_EntityLoadRadius_ShipsInert) {
    ConstrainedRendererConfig defaults;
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo1);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 150.0f);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, defaults.entityLoadRadius);
    EXPECT_GE(config.entityLoadRadius, config.entityRenderDistance);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_Voodoo2_EntityLoadRadius_ShipsInert) {
    ConstrainedRendererConfig defaults;
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Voodoo2);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 250.0f);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, defaults.entityLoadRadius);
    EXPECT_GE(config.entityLoadRadius, config.entityRenderDistance);
}

TEST_F(ConstrainedRendererConfigTest, FromPreset_TNT_EntityLoadRadius_ShipsInert) {
    ConstrainedRendererConfig defaults;
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 400.0f);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, defaults.entityLoadRadius);
    EXPECT_GE(config.entityLoadRadius, config.entityRenderDistance);
}

TEST_F(ConstrainedRendererConfigTest, Validate_EntityLoadRadiusBelowRenderDistance_Fails) {
    // Fails in one direction: a radius smaller than the render distance means
    // the renderer could try to draw entities it never loaded.
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    config.entityRenderDistance = 300.0f;
    config.entityLoadRadius = 200.0f;  // below render distance — invalid
    std::string errors;
    EXPECT_FALSE(config.validate(errors));
    EXPECT_NE(errors.find("entityLoadRadius"), std::string::npos);
}

TEST_F(ConstrainedRendererConfigTest, Validate_EntityLoadRadiusAtOrAboveRenderDistance_Passes) {
    // ...and passes in the other direction, proving the check isn't
    // vacuously true/always-failing.
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    config.entityRenderDistance = 300.0f;
    config.entityLoadRadius = 300.0f;  // exactly equal — allowed
    std::string errors;
    EXPECT_TRUE(config.validate(errors)) << errors;

    config.entityLoadRadius = 400.0f;  // exceeds — also allowed
    EXPECT_TRUE(config.validate(errors)) << errors;
}

TEST_F(ConstrainedRendererConfigTest, Validate_EntityLoadRadiusZeroOrNegative_Fails) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    config.entityLoadRadius = 0.0f;
    std::string errors;
    EXPECT_FALSE(config.validate(errors));
    EXPECT_NE(errors.find("entityLoadRadius"), std::string::npos);
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_EntityLoadRadius) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_loadradius.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "entityLoadRadius": 555.0
                }
            }
        })";
    }

    ConstrainedRendererConfig config;
    config.entityLoadRadius = 300.0f;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, 555.0f);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_EntityLoadRadius_AbsentLeavesBaseline) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_loadradius_absent.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({ "presets": { "custom": { "maxTextureDimension": 128 } } })";
    }

    ConstrainedRendererConfig config;
    config.entityLoadRadius = 321.0f;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, 321.0f);

    std::remove(testJsonPath.c_str());
}

TEST_F(ConstrainedRendererConfigTest, ShippedOrangePiPreset_EntityLoadRadiusEnabled_T111) {
    // Active-proof against the REAL shipped config/constrained_presets.json.
    // T109 shipped this preset WITHOUT an entityLoadRadius key (gate inert)
    // because the post-load deferred-entity pickup path was live-confirmed
    // broken. T111 fixed that pickup path (checkProgressiveLoadingComplete()
    // completion-test bug + progressive-loading re-arm on entities becoming
    // visible + a pre-existing WilleqDatabase SQLite thread-safety bug the
    // fix's own increased background-thread activity exposed), proved the
    // pickup converges to the full population via multiple live sessions
    // (see refactor/T111_progressive_loading_rearm.md's Review), and per the
    // plan's own instruction ("if — and only if — the pickup proof is
    // unambiguous, set entityLoadRadius... to entityRenderDistance + 100")
    // now ships the preset with the gate ACTIVE at 400 (300 + 100). This
    // replaces T109's "...ShipsInert" test, which asserted the opposite of
    // what's shipped as of this unit — a real code change, not a drift.
#ifdef WILLEQ_CONSTRAINED_PRESETS_JSON
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    bool applied = config.loadJsonOverrides("orangepi", WILLEQ_CONSTRAINED_PRESETS_JSON);
    EXPECT_TRUE(applied);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, config.entityRenderDistance + 100.0f);
    EXPECT_FLOAT_EQ(config.entityLoadRadius, 400.0f);
#else
    GTEST_SKIP() << "WILLEQ_CONSTRAINED_PRESETS_JSON not defined by build";
#endif
}

TEST_F(ConstrainedRendererConfigTest, JsonOverride_UseGLES2KeyIgnored) {
    const std::string testJsonPath = "/tmp/test_constrained_presets_bc.json";
    {
        std::ofstream f(testJsonPath);
        f << R"({
            "presets": {
                "custom": {
                    "useGLES2": true
                }
            }
        })";
    }

    ConstrainedRendererConfig config;

    bool applied = config.loadJsonOverrides("custom", testJsonPath);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.renderingBackend, RenderingBackend::GLES2);

    std::remove(testJsonPath.c_str());
}

// =============================================================================
// DDSDecoder::extractCompressed Tests
// =============================================================================

// Helper to build a synthetic DDS file header
static std::vector<char> makeSyntheticDDS(uint32_t fourCC, uint32_t width, uint32_t height, size_t blockDataSize) {
    std::vector<char> data(128 + blockDataSize, 0);
    // Magic: "DDS "
    data[0] = 'D'; data[1] = 'D'; data[2] = 'S'; data[3] = ' ';
    // Header size (124)
    uint32_t headerSize = 124;
    std::memcpy(&data[4], &headerSize, 4);
    // Flags
    uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    std::memcpy(&data[8], &flags, 4);
    // Height
    std::memcpy(&data[12], &height, 4);
    // Width
    std::memcpy(&data[16], &width, 4);
    // Pixel format offset = 76
    uint32_t pfSize = 32;
    std::memcpy(&data[76], &pfSize, 4);
    uint32_t pfFlags = 0x4;  // DDPF_FOURCC
    std::memcpy(&data[80], &pfFlags, 4);
    std::memcpy(&data[84], &fourCC, 4);
    // Fill block data with non-zero pattern
    for (size_t i = 128; i < data.size(); ++i) {
        data[i] = static_cast<char>(i & 0xFF);
    }
    return data;
}

TEST_F(ConstrainedRendererConfigTest, ExtractCompressed_DXT1) {
    // 4x4 DXT1: 1 block = 8 bytes
    auto dds = makeSyntheticDDS(0x31545844, 4, 4, 8);  // "DXT1"
    auto result = EQT::Graphics::DDSDecoder::extractCompressed(dds);

    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.glFormat, 0x83F0u);  // GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.dataSize, 8u);
}

TEST_F(ConstrainedRendererConfigTest, ExtractCompressed_DXT3) {
    // 8x8 DXT3: 4 blocks * 16 bytes = 64 bytes
    auto dds = makeSyntheticDDS(0x33545844, 8, 8, 64);  // "DXT3"
    auto result = EQT::Graphics::DDSDecoder::extractCompressed(dds);

    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.glFormat, 0x83F2u);  // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    EXPECT_EQ(result.width, 8u);
    EXPECT_EQ(result.height, 8u);
    EXPECT_EQ(result.dataSize, 64u);
}

TEST_F(ConstrainedRendererConfigTest, ExtractCompressed_DXT5) {
    // 4x4 DXT5: 1 block = 16 bytes
    auto dds = makeSyntheticDDS(0x35545844, 4, 4, 16);  // "DXT5"
    auto result = EQT::Graphics::DDSDecoder::extractCompressed(dds);

    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.glFormat, 0x83F3u);  // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.dataSize, 16u);
}

TEST_F(ConstrainedRendererConfigTest, ExtractCompressed_NotDDS) {
    std::vector<char> bmp = {'B', 'M', 0, 0};
    auto result = EQT::Graphics::DDSDecoder::extractCompressed(bmp);
    EXPECT_FALSE(result.isValid());
}

TEST_F(ConstrainedRendererConfigTest, ExtractCompressed_TooSmall) {
    std::vector<char> tiny = {'D', 'D', 'S', ' '};
    auto result = EQT::Graphics::DDSDecoder::extractCompressed(tiny);
    EXPECT_FALSE(result.isValid());
}

TEST_F(ConstrainedRendererConfigTest, CompressedSize_DXT1) {
    // 256x256 DXT1: 64*64 blocks * 8 bytes = 32768
    size_t size = EQT::Graphics::DDSDecoder::compressedSize(0x83F0, 256, 256);
    EXPECT_EQ(size, 32768u);
}

TEST_F(ConstrainedRendererConfigTest, CompressedSize_DXT5) {
    // 256x256 DXT5: 64*64 blocks * 16 bytes = 65536
    size_t size = EQT::Graphics::DDSDecoder::compressedSize(0x83F3, 256, 256);
    EXPECT_EQ(size, 65536u);
}

TEST_F(ConstrainedRendererConfigTest, CompressedSize_NPOT) {
    // 5x5 DXT1: ceil(5/4)*ceil(5/4) = 2*2 = 4 blocks * 8 = 32
    size_t size = EQT::Graphics::DDSDecoder::compressedSize(0x83F0, 5, 5);
    EXPECT_EQ(size, 32u);
}

// =============================================================================
// T293 — per-subsystem memory budgets (Will's ruling, 2026-08-26)
//
// T69 wired the CI gate to the per-subsystem memory accounting but could only
// ENFORCE the two categories that already had a configured ceiling; it
// explicitly refused to invent the rest. Will ruled: derive them from the
// assets on disk, a 1280x720 maximum resolution, and the Orange Pi One's real
// envelope. The derivation is in docs/decisions.md and in
// constrained_renderer_config.h's block comment on these fields; these tests
// pin the resolved values so a silent edit to either the C++ preset or the
// shipped JSON is a test failure rather than an unremarked budget change.
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_T293PerSubsystemBudgets) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);

    EXPECT_EQ(config.gpuTextureMemoryBytes,       static_cast<size_t>(64) * 1024 * 1024);
    EXPECT_EQ(config.zoneGeometryMemoryBytes,     static_cast<size_t>(16) * 1024 * 1024);
    EXPECT_EQ(config.placeableObjectsMemoryBytes, static_cast<size_t>(24) * 1024 * 1024);
    EXPECT_EQ(config.s3dZoneSourceMemoryBytes,    static_cast<size_t>(24) * 1024 * 1024);
    EXPECT_EQ(config.vboEboMemoryBytes,           static_cast<size_t>(40) * 1024 * 1024);
    EXPECT_EQ(config.characterModelsMemoryBytes,  static_cast<size_t>(40) * 1024 * 1024);
    EXPECT_EQ(config.eqe1MappedAddressSpaceBytes, static_cast<size_t>(256) * 1024 * 1024);

    // VBO/EBO is derived BY IDENTITY, not by scaling: the GPU hardware
    // buffers hold the same vertex+index data as the CPU-side zone geometry
    // and placeable objects (measured on gfaydark: 10.22 MiB of VBO/EBO
    // against 4.76 + 5.36 = 10.12 MiB CPU-side). If either feeder budget
    // moves, this one must move with it.
    EXPECT_EQ(config.vboEboMemoryBytes,
              config.zoneGeometryMemoryBytes + config.placeableObjectsMemoryBytes);
}

TEST_F(ConstrainedRendererConfigTest, ShippedOrangePiPreset_ActivelyCarriesT293Budgets) {
    // Active-proof against the REAL shipped config/constrained_presets.json,
    // same pattern as ShippedOrangePiPreset_ActivelyCarriesMemoryAndOcclusion-
    // BufferKeys above. P39 and P42 both found preset keys that shipped in the
    // JSON with no parse behind them (silently un-tunable, and misleading to
    // anyone reading the JSON as the source of truth); this pins that these
    // seven do not repeat that.
#ifdef WILLEQ_CONSTRAINED_PRESETS_JSON
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    bool applied = config.loadJsonOverrides("orangepi", WILLEQ_CONSTRAINED_PRESETS_JSON);
    EXPECT_TRUE(applied);
    EXPECT_EQ(config.gpuTextureMemoryBytes,       static_cast<size_t>(64) * 1024 * 1024);
    EXPECT_EQ(config.zoneGeometryMemoryBytes,     static_cast<size_t>(16) * 1024 * 1024);
    EXPECT_EQ(config.placeableObjectsMemoryBytes, static_cast<size_t>(24) * 1024 * 1024);
    EXPECT_EQ(config.s3dZoneSourceMemoryBytes,    static_cast<size_t>(24) * 1024 * 1024);
    EXPECT_EQ(config.vboEboMemoryBytes,           static_cast<size_t>(40) * 1024 * 1024);
    EXPECT_EQ(config.characterModelsMemoryBytes,  static_cast<size_t>(40) * 1024 * 1024);
    EXPECT_EQ(config.eqe1MappedAddressSpaceBytes, static_cast<size_t>(256) * 1024 * 1024);
#else
    GTEST_SKIP() << "WILLEQ_CONSTRAINED_PRESETS_JSON not defined by build";
#endif
}

TEST_F(ConstrainedRendererConfigTest, T293Budgets_UnsetOnUnbudgetedPresets) {
    // Zero means "no budget configured" and the CI gate skips the assertion
    // rather than asserting usage <= 0. Only OrangePi is budgeted today; the
    // Voodoo/TNT presets predate the accounting entirely and must NOT pick up
    // an Orange-Pi-derived ceiling by accident.
    for (auto preset : {ConstrainedRenderingPreset::Voodoo1,
                        ConstrainedRenderingPreset::Voodoo2,
                        ConstrainedRenderingPreset::TNT}) {
        auto config = ConstrainedRendererConfig::fromPreset(preset);
        EXPECT_EQ(config.gpuTextureMemoryBytes, 0u)
            << "preset " << ConstrainedRendererConfig::presetName(preset);
        EXPECT_EQ(config.zoneGeometryMemoryBytes, 0u);
        EXPECT_EQ(config.placeableObjectsMemoryBytes, 0u);
        EXPECT_EQ(config.s3dZoneSourceMemoryBytes, 0u);
        EXPECT_EQ(config.vboEboMemoryBytes, 0u);
        EXPECT_EQ(config.characterModelsMemoryBytes, 0u);
        EXPECT_EQ(config.eqe1MappedAddressSpaceBytes, 0u);
    }
}

TEST_F(ConstrainedRendererConfigTest, OrangePi_FramebufferBudgetHolds1280x720) {
    // T293 scope item 3, checked explicitly because the margin is not large
    // and depends entirely on colorDepthBits. 1280x720 is the stated maximum
    // resolution; the shipped preset is 16-bit colour with a D24S8
    // depth-stencil, so 2*2 + 4 = 8 bytes per pixel.
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    ASSERT_EQ(config.colorDepthBits, 16);
    ASSERT_TRUE(config.enableStencilBuffer);

    const size_t usage720p = config.calculateFramebufferUsage(1280, 720);
    EXPECT_EQ(usage720p, static_cast<size_t>(1280) * 720 * 8);  // 7,372,800
    EXPECT_EQ(usage720p, 7372800u);
    EXPECT_LE(usage720p, config.framebufferMemoryBytes);        // FITS: 67% of 10 MiB

    std::string error;
    EXPECT_TRUE(config.validateResolution(1280, 720, error)) << error;

    // ...and 1280x720 survives clamping untouched, which is the property that
    // actually matters at runtime.
    int w = 1280, h = 720;
    EXPECT_FALSE(config.clampResolution(w, h));
    EXPECT_EQ(w, 1280);
    EXPECT_EQ(h, 720);
}

TEST_F(ConstrainedRendererConfigTest, OrangePi_FramebufferBudgetWouldNotHold1280x720At32Bit) {
    // The other half of the same finding, pinned so the reason 10 MiB is
    // sufficient does not get lost: it is sufficient ONLY because this preset
    // is 16-bit. At 32-bit colour the same resolution needs 2*4 + 4 = 12
    // bytes per pixel = 11,059,200 bytes, which is 573,440 bytes OVER the
    // 10,485,760-byte budget. The budget is deliberately NOT raised to cover
    // this -- 16-bit is the shipped configuration and Mali 400 has no reason
    // to want 32 -- but a future edit flipping colorDepthBits must fail here
    // rather than silently clamping the display.
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
    config.colorDepthBits = 32;

    const size_t usage720p32 = config.calculateFramebufferUsage(1280, 720);
    EXPECT_EQ(usage720p32, 11059200u);
    EXPECT_GT(usage720p32, config.framebufferMemoryBytes);
    EXPECT_EQ(usage720p32 - config.framebufferMemoryBytes, 573440u);

    std::string error;
    EXPECT_FALSE(config.validateResolution(1280, 720, error));
    EXPECT_FALSE(error.empty());

    // And the clamp that would silently happen instead: 1280 narrows to the
    // recomputed max width, 720 is left alone (it is already under the max
    // height).
    config.calculateMaxResolution();
    EXPECT_EQ(config.maxResolutionWidth, 1072);
    EXPECT_EQ(config.maxResolutionHeight, 808);
    int w = 1280, h = 720;
    EXPECT_TRUE(config.clampResolution(w, h));
    EXPECT_EQ(w, 1072);
    EXPECT_EQ(h, 720);
}
