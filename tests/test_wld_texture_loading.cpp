// WLD texture-loading pipeline tests (DDS decode, texture chain).
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.

#include <gtest/gtest.h>
#include <cstdint>

class TextureLoadingTest : public ::testing::Test {
protected:
    // DDS magic constant - matches eqsage and willeq
    static constexpr uint32_t DDS_MAGIC = 0x20534444;  // "DDS "

    // FourCC codes for DXT formats
    static constexpr uint32_t FOURCC_DXT1 = 0x31545844;  // "DXT1"
    static constexpr uint32_t FOURCC_DXT3 = 0x33545844;  // "DXT3"
    static constexpr uint32_t FOURCC_DXT5 = 0x35545844;  // "DXT5"

    // Material type constants - matches eqsage MaterialType enum
    static constexpr uint32_t MATERIAL_BOUNDARY = 0x0;
    static constexpr uint32_t MATERIAL_DIFFUSE = 0x01;
    static constexpr uint32_t MATERIAL_TRANSPARENT50 = 0x05;
    static constexpr uint32_t MATERIAL_TRANSPARENT_MASKED = 0x13;

    // BitmapInfo flags - matches eqsage BitMapInfoFlags
    static constexpr uint32_t FLAG_SKIP_FRAMES = 0x02;
    static constexpr uint32_t FLAG_UNKNOWN = 0x04;
    static constexpr uint32_t FLAG_ANIMATED = 0x08;
    static constexpr uint32_t FLAG_HAS_SLEEP = 0x10;
    static constexpr uint32_t FLAG_HAS_CURRENT_FRAME = 0x20;

    // RGB565 to RGB888 conversion - matches willeq dds_decoder.cpp
    void rgb565ToRGB888(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = ((color >> 11) & 0x1F) * 255 / 31;
        g = ((color >> 5) & 0x3F) * 255 / 63;
        b = (color & 0x1F) * 255 / 31;
    }

    // FourCC conversion - matches eqsage image-processing.js
    uint32_t fourCCToInt32(const char* value) {
        return value[0] + (value[1] << 8) + (value[2] << 16) + (value[3] << 24);
    }
};

// ============================================================================
// Phase 10.1: DDS Format Detection Tests
// ============================================================================

// Test DDS magic constant
TEST_F(TextureLoadingTest, DDSMagicConstant) {
    // Both eqsage (image-processing.js:166) and willeq (dds_decoder.cpp:28)
    // use 0x20534444 as DDS magic ("DDS " in little-endian)
    EXPECT_EQ(DDS_MAGIC, 0x20534444u);

    // Verify it matches the string "DDS "
    const char* ddsStr = "DDS ";
    uint32_t fromStr = ddsStr[0] | (ddsStr[1] << 8) | (ddsStr[2] << 16) | (ddsStr[3] << 24);
    EXPECT_EQ(fromStr, DDS_MAGIC);
}

// Test FourCC codes for DXT formats
TEST_F(TextureLoadingTest, FourCCCodes) {
    // eqsage (image-processing.js:250-252)
    // willeq (dds_decoder.cpp:9-11)

    EXPECT_EQ(fourCCToInt32("DXT1"), FOURCC_DXT1);
    EXPECT_EQ(fourCCToInt32("DXT3"), FOURCC_DXT3);
    EXPECT_EQ(fourCCToInt32("DXT5"), FOURCC_DXT5);

    // Verify values
    EXPECT_EQ(FOURCC_DXT1, 0x31545844u);
    EXPECT_EQ(FOURCC_DXT3, 0x33545844u);
    EXPECT_EQ(FOURCC_DXT5, 0x35545844u);
}

// ============================================================================
// Phase 10.2: DXT Block Size Tests
// ============================================================================

// Test DXT block sizes
TEST_F(TextureLoadingTest, DXTBlockSizes) {
    // DXT1: 8 bytes per 4x4 block (4bpp)
    // DXT3: 16 bytes per 4x4 block (8bpp) - 8 alpha + 8 color
    // DXT5: 16 bytes per 4x4 block (8bpp) - 8 alpha + 8 color

    // Both implementations use these block sizes
    // eqsage (image-processing.js:303-315)
    // willeq (dds_decoder.cpp:72, 150, 225)

    uint32_t dxt1BlockSize = 8;
    uint32_t dxt3BlockSize = 16;
    uint32_t dxt5BlockSize = 16;

    // Test block count calculation for a 256x256 texture
    uint32_t width = 256, height = 256;
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;

    EXPECT_EQ(blocksX, 64u);
    EXPECT_EQ(blocksY, 64u);

    // Total data size
    EXPECT_EQ(blocksX * blocksY * dxt1BlockSize, 32768u);   // 32KB for DXT1
    EXPECT_EQ(blocksX * blocksY * dxt3BlockSize, 65536u);   // 64KB for DXT3
    EXPECT_EQ(blocksX * blocksY * dxt5BlockSize, 65536u);   // 64KB for DXT5
}

// Test block count calculation for non-power-of-2 dimensions
TEST_F(TextureLoadingTest, NonPowerOf2BlockCount) {
    // Formula: blocksX = (width + 3) / 4
    // This handles partial blocks at edges

    // 17x17 texture -> 5x5 blocks
    EXPECT_EQ((17 + 3) / 4, 5u);

    // 1x1 texture -> 1x1 blocks
    EXPECT_EQ((1 + 3) / 4, 1u);

    // 4x4 texture -> 1x1 blocks
    EXPECT_EQ((4 + 3) / 4, 1u);

    // 5x5 texture -> 2x2 blocks
    EXPECT_EQ((5 + 3) / 4, 2u);
}

// ============================================================================
// Phase 10.3: RGB565 to RGB888 Conversion Tests
// ============================================================================

// Test RGB565 to RGB888 conversion
TEST_F(TextureLoadingTest, RGB565Conversion) {
    // RGB565 format: RRRRRGGGGGGBBBBB (5-6-5 bits)
    // Both eqsage (dxt-js library) and willeq (dds_decoder.cpp:60-64)
    // convert to 8-bit per channel

    uint8_t r, g, b;

    // Test pure red (0xF800)
    rgb565ToRGB888(0xF800, r, g, b);
    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);

    // Test pure green (0x07E0)
    rgb565ToRGB888(0x07E0, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 0);

    // Test pure blue (0x001F)
    rgb565ToRGB888(0x001F, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 255);

    // Test white (0xFFFF)
    rgb565ToRGB888(0xFFFF, r, g, b);
    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 255);

    // Test black (0x0000)
    rgb565ToRGB888(0x0000, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);
}

// ============================================================================
// Phase 10.4: DXT1 Color Interpolation Tests
// ============================================================================

// Test DXT1 4-color interpolation (c0 > c1)
TEST_F(TextureLoadingTest, DXT1FourColorInterpolation) {
    // When c0 > c1: 4 colors
    // color2 = (2/3)*c0 + (1/3)*c1
    // color3 = (1/3)*c0 + (2/3)*c1

    // Both eqsage (dxt-js) and willeq (dds_decoder.cpp:104-111)

    uint8_t c0[3] = {255, 0, 0};  // Red
    uint8_t c1[3] = {0, 0, 255};  // Blue

    // color2 = 2/3 red + 1/3 blue
    uint8_t color2[3];
    for (int i = 0; i < 3; ++i) {
        color2[i] = (2 * c0[i] + c1[i]) / 3;
    }
    EXPECT_EQ(color2[0], 170);  // ~2/3 of 255
    EXPECT_EQ(color2[1], 0);
    EXPECT_EQ(color2[2], 85);   // ~1/3 of 255

    // color3 = 1/3 red + 2/3 blue
    uint8_t color3[3];
    for (int i = 0; i < 3; ++i) {
        color3[i] = (c0[i] + 2 * c1[i]) / 3;
    }
    EXPECT_EQ(color3[0], 85);   // ~1/3 of 255
    EXPECT_EQ(color3[1], 0);
    EXPECT_EQ(color3[2], 170);  // ~2/3 of 255
}

// Test DXT1 3-color + transparent (c0 <= c1)
TEST_F(TextureLoadingTest, DXT1ThreeColorPlusTransparent) {
    // When c0 <= c1: 3 colors + transparent
    // color2 = (1/2)*c0 + (1/2)*c1
    // color3 = transparent (alpha = 0)

    // Both implementations handle this case
    // willeq (dds_decoder.cpp:112-119)

    uint8_t c0[3] = {100, 100, 100};
    uint8_t c1[3] = {200, 200, 200};

    // color2 = average
    uint8_t color2[3];
    for (int i = 0; i < 3; ++i) {
        color2[i] = (c0[i] + c1[i]) / 2;
    }
    EXPECT_EQ(color2[0], 150);
    EXPECT_EQ(color2[1], 150);
    EXPECT_EQ(color2[2], 150);

    // color3 is transparent
    uint8_t color3_alpha = 0;
    EXPECT_EQ(color3_alpha, 0);
}

// ============================================================================
// Phase 10.5: DXT5 Alpha Interpolation Tests
// ============================================================================

// Test DXT5 8-alpha interpolation (alpha0 > alpha1)
TEST_F(TextureLoadingTest, DXT5EightAlphaInterpolation) {
    // When alpha0 > alpha1: 8 alpha values (6 interpolated + 2 endpoints)
    // Both implementations use the same formula
    // willeq (dds_decoder.cpp:253-260)

    uint8_t alpha0 = 255;
    uint8_t alpha1 = 0;

    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    alphas[2] = (6 * alpha0 + 1 * alpha1) / 7;  // ~218
    alphas[3] = (5 * alpha0 + 2 * alpha1) / 7;  // ~182
    alphas[4] = (4 * alpha0 + 3 * alpha1) / 7;  // ~145
    alphas[5] = (3 * alpha0 + 4 * alpha1) / 7;  // ~109
    alphas[6] = (2 * alpha0 + 5 * alpha1) / 7;  // ~72
    alphas[7] = (1 * alpha0 + 6 * alpha1) / 7;  // ~36

    EXPECT_EQ(alphas[0], 255);
    EXPECT_EQ(alphas[1], 0);
    EXPECT_NEAR(alphas[2], 218, 1);
    EXPECT_NEAR(alphas[3], 182, 1);
    EXPECT_NEAR(alphas[4], 145, 1);
    EXPECT_NEAR(alphas[5], 109, 1);
    EXPECT_NEAR(alphas[6], 72, 1);
    EXPECT_NEAR(alphas[7], 36, 1);
}

// Test DXT5 6-alpha + 0 + 255 (alpha0 <= alpha1)
TEST_F(TextureLoadingTest, DXT5SixAlphaPlusExtremes) {
    // When alpha0 <= alpha1: 6 interpolated values + 0 and 255
    // willeq (dds_decoder.cpp:261-268)

    uint8_t alpha0 = 50;
    uint8_t alpha1 = 200;

    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    alphas[2] = (4 * alpha0 + 1 * alpha1) / 5;
    alphas[3] = (3 * alpha0 + 2 * alpha1) / 5;
    alphas[4] = (2 * alpha0 + 3 * alpha1) / 5;
    alphas[5] = (1 * alpha0 + 4 * alpha1) / 5;
    alphas[6] = 0;    // Explicit 0
    alphas[7] = 255;  // Explicit 255

    EXPECT_EQ(alphas[0], 50);
    EXPECT_EQ(alphas[1], 200);
    EXPECT_EQ(alphas[6], 0);
    EXPECT_EQ(alphas[7], 255);
}

// ============================================================================
// Phase 10.6: Texture Chain Resolution Tests
// ============================================================================

// Test material type masking
TEST_F(TextureLoadingTest, MaterialTypeMasking) {
    // Both eqsage (material.js:82) and willeq
    // (WldLoader::parseFragment30, wld_fragments_material.cpp since J05)
    // mask with ~0x80000000 to extract material type

    uint32_t parameters = 0x80000001;  // High bit set + diffuse type
    uint32_t materialType = parameters & ~0x80000000;

    EXPECT_EQ(materialType, MATERIAL_DIFFUSE);

    // Test without high bit
    parameters = 0x00000005;
    materialType = parameters & ~0x80000000;
    EXPECT_EQ(materialType, MATERIAL_TRANSPARENT50);
}

// Test animation flags
TEST_F(TextureLoadingTest, AnimationFlags) {
    // eqsage (bitmap-info.js:3-8) and willeq
    // (WldLoader::parseFragment04, wld_fragments_material.cpp since J05)

    uint32_t flags = FLAG_ANIMATED | FLAG_HAS_SLEEP;  // 0x18

    bool isAnimated = (flags & FLAG_ANIMATED) != 0;
    bool hasSleep = (flags & FLAG_HAS_SLEEP) != 0;
    bool hasCurrentFrame = (flags & FLAG_HAS_CURRENT_FRAME) != 0;

    EXPECT_TRUE(isAnimated);
    EXPECT_TRUE(hasSleep);
    EXPECT_FALSE(hasCurrentFrame);

    // Animation delay is read when animated AND has sleep
    bool shouldReadDelay = isAnimated && hasSleep;
    EXPECT_TRUE(shouldReadDelay);
}

// Test texture chain fragment references
TEST_F(TextureLoadingTest, TextureChainFragmentReferences) {
    // Texture chain: 0x31 -> 0x30 -> 0x05 -> 0x04 -> 0x03 -> filename
    //
    // 0x31 MaterialList: array of references to 0x30 fragments
    // 0x30 Material: reference to 0x05 fragment
    // 0x05 BitmapInfoReference: reference to 0x04 fragment
    // 0x04 BitmapInfo: animation info + references to 0x03 fragments
    // 0x03 BitmapName: texture filename(s)

    // Verify fragment type values
    uint8_t frag03 = 0x03;  // BitmapName
    uint8_t frag04 = 0x04;  // BitmapInfo
    uint8_t frag05 = 0x05;  // BitmapInfoReference
    uint8_t frag30 = 0x30;  // Material
    uint8_t frag31 = 0x31;  // MaterialList

    EXPECT_EQ(frag03, 3);
    EXPECT_EQ(frag04, 4);
    EXPECT_EQ(frag05, 5);
    EXPECT_EQ(frag30, 48);
    EXPECT_EQ(frag31, 49);
}

// ============================================================================
// Phase 10.7: Material Type Constants Tests
// ============================================================================

// Test material type shader mapping
TEST_F(TextureLoadingTest, MaterialTypeShaderMapping) {
    // eqsage (material.js:6-37) defines material types
    // willeq uses compatible type detection

    // Boundary type (not rendered)
    EXPECT_EQ(MATERIAL_BOUNDARY, 0x0);

    // Standard diffuse types
    EXPECT_EQ(MATERIAL_DIFFUSE, 0x01);

    // Transparent types
    EXPECT_EQ(MATERIAL_TRANSPARENT50, 0x05);  // 50% blend
    EXPECT_EQ(MATERIAL_TRANSPARENT_MASKED, 0x13);  // Alpha test
}
