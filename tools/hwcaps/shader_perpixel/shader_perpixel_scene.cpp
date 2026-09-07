// shader_perpixel_scene.cpp — the benchmark's scene: the subdivided alley
// geometry, the checkerboard texture, the FP16 attenuation LUT and the 4x4
// matrix helpers (T447; bodies moved verbatim from
// tools/hwcaps/gles2_shader_perpixel_benchmark.cpp).

#include "hwcaps/shader_perpixel/shader_perpixel.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// ============================================================================
// Scene geometry: an alley (3 walls + floor, ~100 polys with subdivisions)
// ============================================================================


// Build a subdivided quad (for more realistic per-vertex lighting interpolation)
// Returns vertices and indices for a grid of triangles.
static void buildSubdividedQuad(
    float x0, float y0, float z0,   // corner 0
    float x1, float y1, float z1,   // corner 1 (along U)
    float x2, float y2, float z2,   // corner 2 (along V, opposite corner)
    float nx, float ny, float nz,   // face normal
    int divisionsU, int divisionsV,
    std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    // Corner 3 = corner1 + (corner2 - corner0)
    float x3 = x1 + (x2 - x0);
    float y3 = y1 + (y2 - y0);
    float z3 = z1 + (z2 - z0);

    uint16_t baseIdx = (uint16_t)verts.size();

    for (int v = 0; v <= divisionsV; v++) {
        float tv = (float)v / divisionsV;
        for (int u = 0; u <= divisionsU; u++) {
            float tu = (float)u / divisionsU;

            // Bilinear interpolation
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
    // Alley dimensions: 6 wide, 20 deep, 8 tall (Irrlicht Y-up)
    // Player stands at origin looking down +Z
    float w = 3.0f, d = 20.0f, h = 8.0f;
    int div = 4;  // subdivisions per wall

    // Floor (Y=0, normal up)
    buildSubdividedQuad(-w, 0, 0,  w, 0, 0,  -w, 0, d,  0, 1, 0, div, div*2, verts, indices);

    // Left wall (X=-w, normal +X)
    buildSubdividedQuad(-w, 0, 0,  -w, 0, d,  -w, h, 0,  1, 0, 0, div*2, div, verts, indices);

    // Right wall (X=+w, normal -X)
    buildSubdividedQuad(w, 0, d,  w, 0, 0,  w, h, d,  -1, 0, 0, div*2, div, verts, indices);

    // Back wall (Z=d, normal -Z)
    buildSubdividedQuad(w, 0, d,  -w, 0, d,  w, h, d,  0, 0, -1, div, div, verts, indices);
}

// ============================================================================
// Simple checkerboard texture
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
// FP16 conversion and attenuation LUT creation
// ============================================================================

static uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint16_t sign = (x >> 16) & 0x8000;
    int exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t frac = (x >> 13) & 0x03FF;
    if (exp <= 0) return sign;       // underflow to zero
    if (exp >= 31) return sign | 0x7C00;  // overflow to inf
    return sign | (exp << 10) | frac;
}

// Check if GL_OES_texture_half_float and GL_OES_texture_half_float_linear are available
bool hasHalfFloatTextures() {
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (!ext) return false;
    return strstr(ext, "GL_OES_texture_half_float") != nullptr &&
           strstr(ext, "GL_OES_texture_half_float_linear") != nullptr;
}

// Create a 256x1 GL_LUMINANCE_ALPHA FP16 attenuation LUT.
// L channel = inversesqrt(d² + epsilon)
// A channel = 1.0 / (attenConst + attenQuad * d² + epsilon)
// d² is mapped to [0, maxD2] across the 256 texels.
GLuint createAttenLUT(float attenConst, float attenQuad, float maxD2) {
    const int LUT_SIZE = 256;
    // Each texel is 2 x FP16 = 4 bytes (luminance + alpha)
    uint16_t data[LUT_SIZE * 2];

    for (int i = 0; i < LUT_SIZE; i++) {
        float t = (float)i / (float)(LUT_SIZE - 1);
        float d2 = t * maxD2;
        float d2e = d2 + 0.0001f;  // epsilon to match shader

        float invD = 1.0f / sqrtf(d2e);
        float atten = 1.0f / (attenConst + attenQuad * d2e + 0.0001f);

        data[i * 2 + 0] = floatToHalf(invD);    // luminance
        data[i * 2 + 1] = floatToHalf(atten);   // alpha
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, LUT_SIZE, 1, 0,
                 GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "LUT creation failed (GL error 0x%x)\n", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    return tex;
}

// ============================================================================
// Simple 4x4 identity/projection matrices
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
