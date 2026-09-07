// fog_volume_scene.cpp -- the fog volume benchmark's scene inputs: the FP16
// sqrt/exp lookup textures, the subdivided alley geometry, the fog billboard
// quad, the checker texture, the 4x4 matrix helpers and the eight fog-sphere
// positions.
//
// Split out of tools/hwcaps/gles2_fog_volume_benchmark.cpp by T453
// (contiguous ranges 337..397, 408..468, 473..566 and 575..595 of the
// 1,220-line original, moved verbatim; structs Vertex, FogVertex and FogSphere
// went to fog_volume_bench.h so they may be defined once). floatToHalf and
// buildSubdividedQuad stayed file-static: their only callers are in this TU.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#endif

#include "fog_volume/fog_volume_bench.h"

// ============================================================================
// FP16 LUT helpers
// ============================================================================

static uint16_t floatToHalf(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t mant = bits & 0x7FFFFF;
    if (exp > 15) return sign | 0x7C00;
    if (exp < -14) return sign;
    return sign | ((exp + 15) << 10) | (mant >> 13);
}

bool hasHalfFloatTextures() {
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    return ext && strstr(ext, "OES_texture_half_float") && strstr(ext, "OES_texture_half_float_linear");
}

// Create a 256x1 FP16 LUMINANCE LUT for sqrt(x), x ∈ [0, maxVal]
GLuint createSqrtLUT(float maxVal) {
    uint16_t data[256];
    for (int i = 0; i < 256; ++i) {
        float x = maxVal * (static_cast<float>(i) + 0.5f) / 256.0f;
        data[i] = floatToHalf(std::sqrt(x));
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0,
                 GL_LUMINANCE, GL_HALF_FLOAT_OES, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Create a 256x1 FP16 LUMINANCE LUT for exp(-x), x ∈ [0, maxVal]
GLuint createExpLUT(float maxVal) {
    uint16_t data[256];
    for (int i = 0; i < 256; ++i) {
        float x = maxVal * (static_cast<float>(i) + 0.5f) / 256.0f;
        data[i] = floatToHalf(std::exp(-x));
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0,
                 GL_LUMINANCE, GL_HALF_FLOAT_OES, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}




static void buildSubdividedQuad(
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    float x2, float y2, float z2,
    float nx, float ny, float nz,
    int divisionsU, int divisionsV,
    std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    float x3 = x1 + (x2 - x0);
    float y3 = y1 + (y2 - y0);
    float z3 = z1 + (z2 - z0);

    uint16_t baseIdx = (uint16_t)verts.size();

    for (int v = 0; v <= divisionsV; v++) {
        float tv = (float)v / divisionsV;
        for (int u = 0; u <= divisionsU; u++) {
            float tu = (float)u / divisionsU;

            float px = (1-tu)*(1-tv)*x0 + tu*(1-tv)*x1 + (1-tu)*tv*x2 + tu*tv*x3;
            float py = (1-tu)*(1-tv)*y0 + tu*(1-tv)*y1 + (1-tu)*tv*y2 + tu*tv*y3;
            float pz = (1-tu)*(1-tv)*z0 + tu*(1-tv)*z1 + (1-tu)*tv*z2 + tu*tv*z3;

            Vertex vert;
            vert.pos[0] = px; vert.pos[1] = py; vert.pos[2] = pz;
            vert.normal[0] = nx; vert.normal[1] = ny; vert.normal[2] = nz;
            vert.color[0] = 0.8f; vert.color[1] = 0.8f; vert.color[2] = 0.8f; vert.color[3] = 1.0f;
            vert.uv[0] = tu; vert.uv[1] = tv;
            verts.push_back(vert);
        }
    }

    for (int v = 0; v < divisionsV; v++) {
        for (int u = 0; u < divisionsU; u++) {
            uint16_t i0 = baseIdx + v * (divisionsU + 1) + u;
            uint16_t i1 = i0 + 1;
            uint16_t i2 = i0 + (divisionsU + 1);
            uint16_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }
}

void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    float w = 3.0f, d = 20.0f, h = 8.0f;
    int div = 4;

    // Floor
    buildSubdividedQuad(-w, 0, 0,  w, 0, 0,  -w, 0, d,  0, 1, 0, div, div*2, verts, indices);
    // Left wall
    buildSubdividedQuad(-w, 0, 0,  -w, 0, d,  -w, h, 0,  1, 0, 0, div*2, div, verts, indices);
    // Right wall
    buildSubdividedQuad(w, 0, d,  w, 0, 0,  w, h, d,  -1, 0, 0, div*2, div, verts, indices);
    // Back wall
    buildSubdividedQuad(w, 0, d,  -w, 0, d,  w, h, d,  0, 0, -1, div, div, verts, indices);
    // Ceiling
    buildSubdividedQuad(-w, h, d,  w, h, d,  -w, h, 0,  0, -1, 0, div, div*2, verts, indices);
}



void buildFogQuad(GLuint& vbo, GLuint& ibo) {
    FogVertex verts[4] = {
        {{-1.0f, -1.0f, 0.0f}},
        {{ 1.0f, -1.0f, 0.0f}},
        {{-1.0f,  1.0f, 0.0f}},
        {{ 1.0f,  1.0f, 0.0f}},
    };
    uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
}

// ============================================================================
// Checkerboard texture
// ============================================================================

GLuint createCheckerTexture(int size) {
    std::vector<uint8_t> pixels(size * size * 3);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool check = ((x / (size/8)) + (y / (size/8))) % 2 == 0;
            uint8_t val = check ? 180 : 100;
            int i = (y * size + x) * 3;
            pixels[i] = val; pixels[i+1] = val; pixels[i+2] = val;
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

// ============================================================================
// Matrix math (same as perpixel benchmark)
// ============================================================================

void mat4Identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far) {
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fovDeg * 3.14159f / 360.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

void mat4LookAt(float m[16], float eyeX, float eyeY, float eyeZ,
                        float atX, float atY, float atZ,
                        float upX, float upY, float upZ) {
    float fx = atX - eyeX, fy = atY - eyeY, fz = atZ - eyeZ;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fl; fy /= fl; fz /= fl;

    float sx = fy*upZ - fz*upY, sy = fz*upX - fx*upZ, sz = fx*upY - fy*upX;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sl; sy /= sl; sz /= sl;

    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;

    mat4Identity(m);
    m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[14] = (fx*eyeX + fy*eyeY + fz*eyeZ);
}

void mat4Multiply(float out[16], const float a[16], const float b[16]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            out[i + j*4] = 0;
            for (int k = 0; k < 4; k++)
                out[i + j*4] += a[i + k*4] * b[k + j*4];
        }
}



// 8 fog spheres at light/fire positions along the alley
const FogSphere fogSpheres[] = {
    // Campfire on the ground near player
    { 0.0f, 1.5f,  3.0f,   1.0f, 0.6f, 0.2f},
    // Wall torch left
    {-2.5f, 3.5f,  6.0f,   1.0f, 0.7f, 0.3f},
    // Wall torch right
    { 2.5f, 3.5f,  9.0f,   1.0f, 0.7f, 0.3f},
    // Brazier center
    { 0.0f, 2.0f, 12.0f,   1.0f, 0.5f, 0.15f},
    // Wall torch left
    {-2.5f, 3.5f, 14.0f,   1.0f, 0.7f, 0.3f},
    // Small fire right
    { 1.5f, 1.0f, 16.0f,   0.9f, 0.4f, 0.1f},
    // Ceiling lantern
    { 0.0f, 6.5f, 18.0f,   0.8f, 0.8f, 0.6f},
    // Deep brazier
    { 0.0f, 2.0f, 19.0f,   1.0f, 0.5f, 0.15f},
};
const int NUM_FOG_SPHERES = sizeof(fogSpheres) / sizeof(fogSpheres[0]);
