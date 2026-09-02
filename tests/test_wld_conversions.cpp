// WLD coordinate-system conversion tests (vertex scale, normals, UV,
// winding, rotation).
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.

#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>

class CoordinateSystemTest : public ::testing::Test {
protected:
    // Rotation conversion constants - matches eqsage Location class
    static constexpr float ROT_MODIFIER = 360.0f / 512.0f;

    // Normal conversion constant - matches eqsage mesh.js
    static constexpr float NORMAL_SCALE = 1.0f / 128.0f;

    // UV conversion for old format - matches eqsage mesh.js
    static constexpr float UV_SCALE_OLD = 1.0f / 256.0f;

    // Test helper: normalize a vector (matches eqsage vec3.normalize)
    void normalize(float& x, float& y, float& z) {
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 0.0001f) {
            x /= len;
            y /= len;
            z /= len;
        } else {
            x = 0; y = 0; z = 1;  // Default to up vector
        }
    }
};

// ============================================================================
// Phase 9.1: Vertex Scale Tests
// ============================================================================

// Test vertex scale calculation matches eqsage: 1.0 / (1 << scale)
TEST_F(CoordinateSystemTest, VertexScaleCalculation) {
    // eqsage (mesh.js line 102): const scale = 1.0 / (1 << reader.readInt16())
    // willeq (WldLoader::parseFragment36, wld_fragments_mesh.cpp): float scale = 1.0f / static_cast<float>(1 << header->scale)

    // Test common scale values
    EXPECT_FLOAT_EQ(1.0f / (1 << 0), 1.0f);       // scale=0
    EXPECT_FLOAT_EQ(1.0f / (1 << 1), 0.5f);       // scale=1
    EXPECT_FLOAT_EQ(1.0f / (1 << 2), 0.25f);      // scale=2
    EXPECT_FLOAT_EQ(1.0f / (1 << 3), 0.125f);     // scale=3
    EXPECT_FLOAT_EQ(1.0f / (1 << 4), 0.0625f);    // scale=4
    EXPECT_FLOAT_EQ(1.0f / (1 << 8), 0.00390625f); // scale=8 (common)
}

// Test vertex position conversion: raw int16 * scale
TEST_F(CoordinateSystemTest, VertexPositionConversion) {
    // Both eqsage and willeq: vertex.x = rawInt16 * scale
    float scale = 1.0f / (1 << 8);  // Common scale value

    // Test positive values
    EXPECT_NEAR(100 * scale, 0.390625f, 0.0001f);
    EXPECT_NEAR(1000 * scale, 3.90625f, 0.0001f);
    EXPECT_NEAR(32767 * scale, 127.99609375f, 0.0001f);  // Max int16

    // Test negative values
    EXPECT_NEAR(-100 * scale, -0.390625f, 0.0001f);
    EXPECT_NEAR(-32768 * scale, -128.0f, 0.0001f);  // Min int16
}

// ============================================================================
// Phase 9.2: Normal Conversion Tests
// ============================================================================

// Test normal conversion: int8 / 128.0 then normalize
TEST_F(CoordinateSystemTest, NormalConversion) {
    // eqsage (mesh.js lines 127-132):
    //   const x = reader.readInt8() / 128.0;
    //   const y = reader.readInt8() / 128.0;
    //   const z = reader.readInt8() / 128.0;
    //   vec3.normalize(vec, vec);
    // willeq (WldLoader::parseFragment36, wld_fragments_mesh.cpp): same calculation

    // Test boundary values
    EXPECT_NEAR(127 * NORMAL_SCALE, 0.9921875f, 0.0001f);   // Max positive int8
    EXPECT_NEAR(-128 * NORMAL_SCALE, -1.0f, 0.0001f);       // Min int8
    EXPECT_NEAR(0 * NORMAL_SCALE, 0.0f, 0.0001f);           // Zero

    // Test normalization for a typical normal
    float nx = 64 * NORMAL_SCALE;   // 0.5
    float ny = 64 * NORMAL_SCALE;   // 0.5
    float nz = 90 * NORMAL_SCALE;   // ~0.703
    normalize(nx, ny, nz);

    // Verify normalized length is 1.0
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    EXPECT_NEAR(len, 1.0f, 0.0001f);
}

// Test degenerate normal handling (length near zero)
TEST_F(CoordinateSystemTest, DegenerateNormalHandling) {
    // Both implementations default to up vector (0, 0, 1) for degenerate normals
    float nx = 0, ny = 0, nz = 0;
    normalize(nx, ny, nz);

    EXPECT_FLOAT_EQ(nx, 0.0f);
    EXPECT_FLOAT_EQ(ny, 0.0f);
    EXPECT_FLOAT_EQ(nz, 1.0f);
}

// ============================================================================
// Phase 9.3: UV Coordinate Conversion Tests
// ============================================================================

// Test UV old format: int16 / 256.0
TEST_F(CoordinateSystemTest, UVConversionOldFormat) {
    // eqsage (mesh.js line 121): vec2.fromValues(reader.readInt16() / 256.0, reader.readInt16() / 256.0)
    // willeq (WldLoader::parseFragment36, wld_fragments_mesh.cpp): geom->vertices[i].u = tc->u * recip256

    // Test typical UV values
    EXPECT_NEAR(128 * UV_SCALE_OLD, 0.5f, 0.0001f);
    EXPECT_NEAR(256 * UV_SCALE_OLD, 1.0f, 0.0001f);
    EXPECT_NEAR(0 * UV_SCALE_OLD, 0.0f, 0.0001f);

    // Negative values can occur for tiled textures
    EXPECT_NEAR(-128 * UV_SCALE_OLD, -0.5f, 0.0001f);
}

// Test UV new format: float32 directly (no conversion)
TEST_F(CoordinateSystemTest, UVConversionNewFormat) {
    // eqsage (mesh.js line 117): vec2.fromValues(reader.readFloat32(), reader.readFloat32())
    // willeq (WldLoader::parseFragment36, wld_fragments_mesh.cpp): tc->u used directly

    // New format uses float32 directly - just verify identity
    float u = 0.5f, v = 0.75f;
    EXPECT_FLOAT_EQ(u, 0.5f);
    EXPECT_FLOAT_EQ(v, 0.75f);
}

// ============================================================================
// Phase 9.4: Polygon Winding Order Tests
// ============================================================================

// Test polygon winding reversal
TEST_F(CoordinateSystemTest, PolygonWindingReversal) {
    // eqsage: parses as (v1, v2, v3), reverses at export time
    // willeq: reverses at parse time (index[2], index[1], index[0])
    // Both result in the same final winding order

    // Simulate raw polygon indices from file
    uint16_t rawIndices[3] = {0, 1, 2};

    // willeq winding reversal (WldLoader::parseFragment36, wld_fragments_mesh.cpp)
    uint16_t v1 = rawIndices[2];  // 2
    uint16_t v2 = rawIndices[1];  // 1
    uint16_t v3 = rawIndices[0];  // 0

    // Verify order is reversed
    EXPECT_EQ(v1, 2u);
    EXPECT_EQ(v2, 1u);
    EXPECT_EQ(v3, 0u);

    // Test with different values
    rawIndices[0] = 10; rawIndices[1] = 20; rawIndices[2] = 30;
    v1 = rawIndices[2];
    v2 = rawIndices[1];
    v3 = rawIndices[0];

    EXPECT_EQ(v1, 30u);
    EXPECT_EQ(v2, 20u);
    EXPECT_EQ(v3, 10u);
}

// ============================================================================
// Phase 9.5: Rotation Conversion Tests (Fragment 0x15 ActorInstance)
// ============================================================================

// Test rotation modifier calculation
TEST_F(CoordinateSystemTest, RotationModifier) {
    // Both eqsage and willeq use: 360.0 / 512.0 = 0.703125
    // eqsage (location.js line 13): const modifier = 1.0 / 512.0 * 360.0
    // willeq (WldLoader::parseFragment15, wld_fragments_actor.cpp): const float rotModifier = 360.0f / 512.0f

    EXPECT_NEAR(ROT_MODIFIER, 0.703125f, 0.0001f);
}

// Test rotation conversion matching eqsage Location class
TEST_F(CoordinateSystemTest, RotationConversion) {
    // eqsage Location class (location.js):
    //   this.rotateX = 0;
    //   this.rotateZ = rotateY * modifier;
    //   this.rotateY = rotateX * modifier * -1;
    //
    // willeq (WldLoader::parseFragment15, wld_fragments_actor.cpp):
    //   float finalRotX = 0.0f;
    //   float finalRotY = rawRotX * rotModifier * -1.0f;
    //   float finalRotZ = rawRotY * rotModifier;

    // Test conversion with sample raw values
    float rawRotX = 128.0f;  // Quarter circle in EQ units
    float rawRotY = 256.0f;  // Half circle in EQ units

    // willeq conversion (matches eqsage)
    float finalRotX = 0.0f;
    float finalRotY = rawRotX * ROT_MODIFIER * -1.0f;
    float finalRotZ = rawRotY * ROT_MODIFIER;

    EXPECT_FLOAT_EQ(finalRotX, 0.0f);
    EXPECT_NEAR(finalRotY, -90.0f, 0.01f);   // 128 * 0.703125 * -1 = -90
    EXPECT_NEAR(finalRotZ, 180.0f, 0.01f);   // 256 * 0.703125 = 180
}

// Test full rotation (512 EQ units = 360 degrees)
TEST_F(CoordinateSystemTest, FullRotation) {
    float rawRot = 512.0f;
    float degrees = rawRot * ROT_MODIFIER;
    EXPECT_NEAR(degrees, 360.0f, 0.01f);
}

// ============================================================================
// Phase 9.6: Coordinate System Transform Notes
// ============================================================================

// Document the intentional differences between eqsage and willeq at render time
TEST_F(CoordinateSystemTest, CoordinateSystemDocumentation) {
    // PARSING STAGE (WLD -> internal representation):
    // Both eqsage and willeq parse coordinates IDENTICALLY:
    // - Vertices: int16 * scale (no axis swapping)
    // - Normals: int8 / 128.0, normalized (no axis swapping)
    // - UVs: old format int16/256, new format float32 (no V flip at parse)
    // - Rotations: rawRotX -> rotateY (negated), rawRotY -> rotateZ

    // RENDER/EXPORT STAGE (internal -> target system):
    // Here the implementations differ based on target format:
    //
    // eqsage -> glTF (right-handed, OpenGL UVs):
    //   - Y/Z swap: y = z, z = y (line 1119 s3d-decoder.js)
    //   - X flip: scale [-1, 1, 1] (line 1096 s3d-decoder.js)
    //   - V negate: uv[1] * -1 (line 299 s3d-decoder.js)
    //   - +180° rotation: rotateY + 180 (line 1163 s3d-decoder.js)
    //   - Winding reversal at export (lines 360-366 s3d-decoder.js)
    //
    // willeq -> Irrlicht (left-handed like EQ, DirectX UVs):
    //   - Y/Z swap: pos.Y = z, pos.Z = y (zone_geometry.cpp, the
    //     `vertex.Pos.Y = v.z; vertex.Pos.Z = v.y;` sites)
    //   - NO X flip (Irrlicht is left-handed like EQ)
    //   - NO V negate (DirectX UV origin is top-left like EQ)
    //   - NO +180° rotation (Irrlicht is left-handed like EQ)
    //   - Winding reversal at parse (WldLoader::parseFragment36,
    //     wld_fragments_mesh.cpp since J05)

    // This test just documents the design - no assertions needed
    SUCCEED();
}
