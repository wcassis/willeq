#include <gtest/gtest.h>
#include "common/simd_detect.h"
#include "common/net/crc32.h"
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

// ============================================================================
// Scalar reference implementations for comparison
// ============================================================================

struct RefMat4 {
    float m[16];

    RefMat4() {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    RefMat4 operator*(const RefMat4& rhs) const {
        RefMat4 result;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                result.m[c*4 + r] =
                    m[0*4 + r] * rhs.m[c*4 + 0] +
                    m[1*4 + r] * rhs.m[c*4 + 1] +
                    m[2*4 + r] * rhs.m[c*4 + 2] +
                    m[3*4 + r] * rhs.m[c*4 + 3];
            }
        }
        return result;
    }

    void transformPoint(float& x, float& y, float& z) const {
        float nx = m[0]*x + m[4]*y + m[8]*z  + m[12];
        float ny = m[1]*x + m[5]*y + m[9]*z  + m[13];
        float nz = m[2]*x + m[6]*y + m[10]*z + m[14];
        x = nx; y = ny; z = nz;
    }

    static RefMat4 fromQuaternion(float qx, float qy, float qz, float qw) {
        RefMat4 mat;
        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;
        mat.m[0]  = 1.0f - 2.0f * (yy + zz);
        mat.m[1]  = 2.0f * (xy + wz);
        mat.m[2]  = 2.0f * (xz - wy);
        mat.m[4]  = 2.0f * (xy - wz);
        mat.m[5]  = 1.0f - 2.0f * (xx + zz);
        mat.m[6]  = 2.0f * (yz + wx);
        mat.m[8]  = 2.0f * (xz + wy);
        mat.m[9]  = 2.0f * (yz - wx);
        mat.m[10] = 1.0f - 2.0f * (xx + yy);
        return mat;
    }
};

// We need to include the actual BoneMat4 header to test SIMD paths
// Forward declare what we need from the graphics code
namespace EQT { namespace Graphics { struct BoneMat4; } }
#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/frustum_culler.h"

using EQT::Graphics::BoneMat4;
using EQT::Graphics::FrustumCuller;

// ============================================================================
// Helper: Random matrix generation
// ============================================================================

static std::mt19937 rng(42);  // Fixed seed for reproducibility

static BoneMat4 randomBoneMat4() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    // Generate a random rotation quaternion
    float qx = dist(rng), qy = dist(rng), qz = dist(rng), qw = dist(rng);
    float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
    if (len > 0.0001f) { qx /= len; qy /= len; qz /= len; qw /= len; }

    BoneMat4 rot = BoneMat4::fromQuaternion(qx, qy, qz, qw);
    BoneMat4 trans = BoneMat4::translate(dist(rng) * 10.0f, dist(rng) * 10.0f, dist(rng) * 10.0f);
    return trans * rot;
}

static RefMat4 boneToRef(const BoneMat4& b) {
    RefMat4 r;
    memcpy(r.m, b.m, sizeof(float) * 16);
    return r;
}

// ============================================================================
// BoneMat4 Multiply Tests
// ============================================================================

TEST(SimdMath, BoneMat4Multiply_MatchesScalar) {
    for (int trial = 0; trial < 100; ++trial) {
        BoneMat4 a = randomBoneMat4();
        BoneMat4 b = randomBoneMat4();

        // SIMD (or optimized) path
        BoneMat4 result = a * b;

        // Scalar reference
        RefMat4 refA = boneToRef(a);
        RefMat4 refB = boneToRef(b);
        RefMat4 refResult = refA * refB;

        for (int i = 0; i < 16; ++i) {
            EXPECT_NEAR(result.m[i], refResult.m[i], 1e-4f)
                << "Mismatch at element " << i << " trial " << trial;
        }
    }
}

TEST(SimdMath, BoneMat4Multiply_Identity) {
    BoneMat4 a = randomBoneMat4();
    BoneMat4 id = BoneMat4::identity();

    BoneMat4 result1 = a * id;
    BoneMat4 result2 = id * a;

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(result1.m[i], a.m[i], 1e-5f) << "A*I mismatch at " << i;
        EXPECT_NEAR(result2.m[i], a.m[i], 1e-5f) << "I*A mismatch at " << i;
    }
}

// ============================================================================
// BoneMat4 TransformPoint Tests
// ============================================================================

TEST(SimdMath, BoneMat4TransformPoint_MatchesScalar) {
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

    for (int trial = 0; trial < 100; ++trial) {
        BoneMat4 mat = randomBoneMat4();
        RefMat4 refMat = boneToRef(mat);

        float x = dist(rng), y = dist(rng), z = dist(rng);
        float rx = x, ry = y, rz = z;

        mat.transformPoint(x, y, z);
        refMat.transformPoint(rx, ry, rz);

        EXPECT_NEAR(x, rx, 1e-3f) << "X mismatch trial " << trial;
        EXPECT_NEAR(y, ry, 1e-3f) << "Y mismatch trial " << trial;
        EXPECT_NEAR(z, rz, 1e-3f) << "Z mismatch trial " << trial;
    }
}

TEST(SimdMath, BoneMat4TransformPoint_Origin) {
    BoneMat4 mat = BoneMat4::translate(5.0f, 10.0f, -3.0f);
    float x = 0, y = 0, z = 0;
    mat.transformPoint(x, y, z);
    EXPECT_NEAR(x, 5.0f, 1e-5f);
    EXPECT_NEAR(y, 10.0f, 1e-5f);
    EXPECT_NEAR(z, -3.0f, 1e-5f);
}

// ============================================================================
// Matrix Blend Tests
// ============================================================================

static void scalarBlend(float* dst, const float* src, float t, int count) {
    for (int i = 0; i < count; ++i) {
        dst[i] = src[i] + (dst[i] - src[i]) * t;
    }
}

TEST(SimdMath, MatrixBlend_MatchesScalar) {
    float blendFactors[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    for (float bf : blendFactors) {
        BoneMat4 prev = randomBoneMat4();
        BoneMat4 curr = randomBoneMat4();

        // Scalar reference
        float refCurr[16];
        memcpy(refCurr, curr.m, sizeof(float) * 16);
        scalarBlend(refCurr, prev.m, bf, 16);

        // SIMD blend (same logic as in skeletal_animator.cpp)
#ifdef EQT_HAS_NEON
        float32x4_t blend = vdupq_n_f32(bf);
        for (int j = 0; j < 16; j += 4) {
            float32x4_t prev4 = vld1q_f32(&prev.m[j]);
            float32x4_t curr4 = vld1q_f32(&curr.m[j]);
            vst1q_f32(&curr.m[j], vmlaq_f32(prev4, vsubq_f32(curr4, prev4), blend));
        }
#elif defined(EQT_HAS_SSE2)
        __m128 blend = _mm_set1_ps(bf);
        for (int j = 0; j < 16; j += 4) {
            __m128 prev4 = _mm_loadu_ps(&prev.m[j]);
            __m128 curr4 = _mm_loadu_ps(&curr.m[j]);
            __m128 res = _mm_add_ps(prev4, _mm_mul_ps(_mm_sub_ps(curr4, prev4), blend));
            _mm_storeu_ps(&curr.m[j], res);
        }
#else
        for (int j = 0; j < 16; ++j) {
            curr.m[j] = prev.m[j] + (curr.m[j] - prev.m[j]) * bf;
        }
#endif

        for (int i = 0; i < 16; ++i) {
            EXPECT_NEAR(curr.m[i], refCurr[i], 1e-5f)
                << "Blend mismatch at " << i << " blend=" << bf;
        }
    }
}

// ============================================================================
// Frustum Culler Tests
// ============================================================================

TEST(SimdMath, FrustumTestAABB_MatchesExpected) {
    FrustumCuller culler;
    // Camera at origin looking along +X in EQ Z-up coords
    culler.update(0, 0, 0,   // cam pos
                  1, 0, 0,   // forward
                  1.2f,       // ~69 degree vFov
                  1.333f,     // 4:3 aspect
                  1.0f,       // near
                  1000.0f);   // far

    // AABB directly in front should be visible
    EXPECT_TRUE(culler.testAABB(10, -5, -5, 20, 5, 5));

    // AABB behind camera should be invisible
    EXPECT_FALSE(culler.testAABB(-50, -5, -5, -10, 5, 5));

    // AABB far to the side should be invisible
    EXPECT_FALSE(culler.testAABB(10, 500, -5, 20, 510, 5));

    // AABB beyond far plane should be invisible
    EXPECT_FALSE(culler.testAABB(1100, -5, -5, 1200, 5, 5));

    // AABB at camera position (overlapping near plane) should be visible
    EXPECT_TRUE(culler.testAABB(-1, -1, -1, 2, 1, 1));
}

TEST(SimdMath, FrustumTestSphere_MatchesExpected) {
    FrustumCuller culler;
    culler.update(0, 0, 0, 1, 0, 0, 1.2f, 1.333f, 1.0f, 1000.0f);

    // Sphere in front
    EXPECT_TRUE(culler.testSphere(50, 0, 0, 10));

    // Sphere behind
    EXPECT_FALSE(culler.testSphere(-50, 0, 0, 5));

    // Sphere far to side
    EXPECT_FALSE(culler.testSphere(10, 500, 0, 5));

    // Sphere partially intersecting (large radius reaches into frustum)
    EXPECT_TRUE(culler.testSphere(-5, 0, 0, 20));
}

TEST(SimdMath, FrustumTestAABB_DisabledAlwaysTrue) {
    FrustumCuller culler;
    culler.update(0, 0, 0, 1, 0, 0, 1.2f, 1.333f, 1.0f, 1000.0f);
    culler.setEnabled(false);

    // Even behind-camera AABB should return true when disabled
    EXPECT_TRUE(culler.testAABB(-50, -5, -5, -10, 5, 5));
}

// ============================================================================
// Normal Normalize Tests
// ============================================================================

TEST(SimdMath, NormalNormalize_UnitLength) {
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);

    for (int trial = 0; trial < 100; ++trial) {
        float nx = dist(rng), ny = dist(rng), nz = dist(rng);

        // Skip near-zero normals
        float lenSq = nx*nx + ny*ny + nz*nz;
        if (lenSq < 0.001f) continue;

        // Scalar normalize
        float len = std::sqrt(lenSq);
        float snx = nx / len, sny = ny / len, snz = nz / len;

        // Verify scalar result is unit length
        float sLen = std::sqrt(snx*snx + sny*sny + snz*snz);
        EXPECT_NEAR(sLen, 1.0f, 1e-4f) << "Scalar normalize not unit length";

        // SIMD normalize (test the actual intrinsics used in the code)
        float tnx, tny, tnz;
#ifdef EQT_HAS_NEON
        float32x4_t nv = {nx, ny, nz, 0.0f};
        float32x4_t sq = vmulq_f32(nv, nv);
        float32x2_t sum = vadd_f32(vget_low_f32(sq), vget_high_f32(sq));
        float32x2_t ls = vpadd_f32(sum, sum);
        if (vget_lane_f32(ls, 0) > 0.00000001f) {
            float32x2_t invLen = vrsqrte_f32(ls);
            invLen = vmul_f32(invLen, vrsqrts_f32(vmul_f32(ls, invLen), invLen));
            nv = vmulq_f32(nv, vdupq_lane_f32(invLen, 0));
        }
        tnx = vgetq_lane_f32(nv, 0);
        tny = vgetq_lane_f32(nv, 1);
        tnz = vgetq_lane_f32(nv, 2);
#elif defined(EQT_HAS_SSE2)
        __m128 nv = _mm_setr_ps(nx, ny, nz, 0.0f);
        __m128 sqv = _mm_mul_ps(nv, nv);
        __m128 shuf = _mm_shuffle_ps(sqv, sqv, _MM_SHUFFLE(2,3,0,1));
        __m128 sums = _mm_add_ps(sqv, shuf);
        shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0,1,2,3));
        __m128 lenSqV = _mm_add_ps(sums, shuf);
        if (_mm_cvtss_f32(lenSqV) > 0.00000001f) {
            __m128 invLen = _mm_rsqrt_ps(lenSqV);
            __m128 half = _mm_set1_ps(0.5f);
            __m128 three_half = _mm_set1_ps(1.5f);
            __m128 refined = _mm_mul_ps(invLen, _mm_sub_ps(three_half,
                _mm_mul_ps(half, _mm_mul_ps(lenSqV, _mm_mul_ps(invLen, invLen)))));
            nv = _mm_mul_ps(nv, refined);
        }
        EQT_ALIGN(16) float out[4];
        _mm_store_ps(out, nv);
        tnx = out[0]; tny = out[1]; tnz = out[2];
#else
        tnx = snx; tny = sny; tnz = snz;
#endif

        float tLen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
        EXPECT_NEAR(tLen, 1.0f, 2e-3f)
            << "SIMD normalize not unit length, trial " << trial;

        // Direction should match
        EXPECT_NEAR(tnx, snx, 2e-3f) << "X direction mismatch trial " << trial;
        EXPECT_NEAR(tny, sny, 2e-3f) << "Y direction mismatch trial " << trial;
        EXPECT_NEAR(tnz, snz, 2e-3f) << "Z direction mismatch trial " << trial;
    }
}

// ============================================================================
// CRC32 Tests
// ============================================================================

// Reference byte-at-a-time CRC32 for comparison
static int crc32_reference(const void* data, int size) {
    // Standard CRC32 table (same polynomial 0xEDB88320)
    static unsigned int table[256];
    static bool init = false;
    if (!init) {
        for (unsigned int i = 0; i < 256; ++i) {
            unsigned int c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0u);
            }
            table[i] = c;
        }
        init = true;
    }

    unsigned int crc = 0xFFFFFFFFu;
    auto buffer = (const uint8_t*)data;
    for (int i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ buffer[i]) & 0xFF];
    }
    return static_cast<int>(~crc);
}

static int crc32_reference_key(const void* data, int size, int key) {
    static unsigned int table[256];
    static bool init = false;
    if (!init) {
        for (unsigned int i = 0; i < 256; ++i) {
            unsigned int c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0u);
            }
            table[i] = c;
        }
        init = true;
    }

    unsigned int crc = 0xFFFFFFFFu;
    for (int i = 0; i < 4; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ ((key >> (i * 8)) & 0xff)) & 0xFF];
    }
    auto buffer = (const uint8_t*)data;
    for (int i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ buffer[i]) & 0xFF];
    }
    return static_cast<int>(~crc);
}

TEST(SimdMath, CRC32SliceBy4_MatchesByteAtATime) {
    // Test various sizes including edge cases around 4-byte boundaries
    std::vector<int> sizes = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64, 100, 255, 256, 1000, 4096};

    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int sz : sizes) {
        std::vector<uint8_t> data(sz);
        for (int i = 0; i < sz; ++i) {
            data[i] = static_cast<uint8_t>(byteDist(rng));
        }

        int sliceResult = EQ::Crc32(data.data(), sz);
        int refResult = crc32_reference(data.data(), sz);

        EXPECT_EQ(sliceResult, refResult)
            << "CRC32 mismatch for size " << sz;
    }
}

TEST(SimdMath, CRC32SliceBy4_WithKey_MatchesByteAtATime) {
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> keyDist(-2147483647, 2147483647);

    for (int trial = 0; trial < 50; ++trial) {
        int sz = (trial + 1) * 7;  // Various sizes
        std::vector<uint8_t> data(sz);
        for (int i = 0; i < sz; ++i) {
            data[i] = static_cast<uint8_t>(byteDist(rng));
        }

        int key = keyDist(rng);
        int sliceResult = EQ::Crc32(data.data(), sz, key);
        int refResult = crc32_reference_key(data.data(), sz, key);

        EXPECT_EQ(sliceResult, refResult)
            << "CRC32+key mismatch for size " << sz << " key " << key;
    }
}

TEST(SimdMath, CRC32_KnownValues) {
    // "123456789" should give CRC32 = 0xCBF43926
    const char* testStr = "123456789";
    int result = EQ::Crc32(testStr, 9);
    EXPECT_EQ(static_cast<unsigned int>(result), 0xCBF43926u);
}

TEST(SimdMath, CRC32_EmptyData) {
    int result = EQ::Crc32("", 0);
    // CRC32 of empty data = ~0xFFFFFFFF = 0x00000000
    EXPECT_EQ(result, 0);
}
