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

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Max) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("max"), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("MAX"), ConstrainedRenderingPreset::Max);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_NoneOffDisabled_MapToMax) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("none"), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("NONE"), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("off"), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("disabled"), ConstrainedRenderingPreset::Max);
}

TEST_F(ConstrainedRendererConfigTest, ParsePreset_Invalid) {
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("invalid"), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset(""), ConstrainedRenderingPreset::Max);
    EXPECT_EQ(ConstrainedRendererConfig::parsePreset("voodoo3"), ConstrainedRenderingPreset::Max);
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

TEST_F(ConstrainedRendererConfigTest, PresetName_Max) {
    EXPECT_EQ(ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset::Max), "Max");
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

TEST_F(ConstrainedRendererConfigTest, FromPreset_Max) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::Max);

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.framebufferMemoryBytes, 256 * 1024 * 1024);  // 256MB
    EXPECT_EQ(config.textureMemoryBytes, 256 * 1024 * 1024);      // 256MB
    EXPECT_EQ(config.colorDepthBits, 32);
    EXPECT_EQ(config.maxTextureDimension, 4096);
    EXPECT_FLOAT_EQ(config.clipDistance, 99999.0f);
    EXPECT_FLOAT_EQ(config.entityRenderDistance, 99999.0f);
    EXPECT_EQ(config.maxVisibleEntities, 10000);
    EXPECT_EQ(config.maxPolygonsPerFrame, 10000000);
    EXPECT_EQ(config.occlusionBufferWidth, 256);
    EXPECT_EQ(config.occlusionBufferHeight, 128);
    EXPECT_EQ(config.occlusionMaxOccluderRegions, 64);
    EXPECT_EQ(config.totalMemoryBudgetBytes, 0);  // No RAM constraint
    EXPECT_FALSE(config.lazyPfsLoading);
    EXPECT_FALSE(config.releaseTextureDataAfterUpload);
    // GPU feature flags
    EXPECT_TRUE(config.enableMipmaps);
    EXPECT_TRUE(config.enableNPOT);
    EXPECT_TRUE(config.enableStencilBuffer);
    EXPECT_FALSE(config.enableAlphaToCoverage);  // Off by default (needs MSAA)
    EXPECT_EQ(config.antiAliasLevel, 0);
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

// =============================================================================
// GPU Feature Flag Tests
// =============================================================================

TEST_F(ConstrainedRendererConfigTest, FromPreset_OrangePi_GpuFeatures) {
    auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);

    EXPECT_TRUE(config.enabled);
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
    EXPECT_TRUE(config.enableTextureAtlas);
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
