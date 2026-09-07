// gl_points_fbo.cpp -- the GL_POINTS diagnostic's offscreen target and its
// verdict machinery: FBO creation and teardown, the pixel readback analyser,
// the four GLSL ES sources the tests compile, and the pass/fail tally.
//
// Split out of tools/test_gl_points.cpp by T453 (contiguous ranges 308..355,
// 364..452 and 460..477 of the 1,206-line original, moved verbatim; structs
// FBO, PixelStats and TestResult went to gl_points.h so they may be defined
// once, and analyzeRegion's default tolerance is stated on its declaration
// there -- a default may be given once).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <gbm.h>
#endif

#include "gl_points/gl_points.h"


bool createFBO(FBO& fbo, int width, int height) {
    fbo.width = width;
    fbo.height = height;

    // Create color texture
    glGenTextures(1, &fbo.colorTex);
    glBindTexture(GL_TEXTURE_2D, fbo.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &fbo.depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo.depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

    // Create FBO
    glGenFramebuffers(1, &fbo.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fbo.colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, fbo.depthRb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("  FBO incomplete: 0x%04X\n", status);
        return false;
    }

    if (!checkGLError("createFBO")) return false;

    return true;
}

void destroyFBO(FBO& fbo) {
    if (fbo.fbo) glDeleteFramebuffers(1, &fbo.fbo);
    if (fbo.colorTex) glDeleteTextures(1, &fbo.colorTex);
    if (fbo.depthRb) glDeleteRenderbuffers(1, &fbo.depthRb);
    fbo = {};
}

// ============================================================
// Pixel readback and analysis
// ============================================================



PixelStats analyzeRegion(const std::vector<uint8_t>& pixels, int fbWidth, int fbHeight,
                                 int rx, int ry, int rw, int rh,
                                 uint8_t expectR, uint8_t expectG, uint8_t expectB,
                                 int tolerance) {
    PixelStats stats = {};
    stats.totalPixels = rw * rh;
    stats.sampleX = -1;
    stats.sampleY = -1;

    for (int y = ry; y < ry + rh && y < fbHeight; y++) {
        for (int x = rx; x < rx + rw && x < fbWidth; x++) {
            int idx = (y * fbWidth + x) * 4;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            uint8_t a = pixels[idx + 3];

            if (r > stats.maxR) stats.maxR = r;
            if (g > stats.maxG) stats.maxG = g;
            if (b > stats.maxB) stats.maxB = b;
            if (a > stats.maxA) stats.maxA = a;

            if (r > 0 || g > 0 || b > 0) {
                stats.nonBlackPixels++;
                if (stats.sampleX < 0) {
                    stats.sampleR = r;
                    stats.sampleG = g;
                    stats.sampleB = b;
                    stats.sampleA = a;
                    stats.sampleX = x;
                    stats.sampleY = y;
                }

                // Check color match
                if (abs((int)r - (int)expectR) <= tolerance &&
                    abs((int)g - (int)expectG) <= tolerance &&
                    abs((int)b - (int)expectB) <= tolerance) {
                    stats.matchingPixels++;
                }
            }
        }
    }

    return stats;
}

// ============================================================
// Test cases
// ============================================================

// Shader for basic colored geometry (triangles and points)
const char* basicVS =
    "precision highp float;\n"
    "attribute vec2 aPosition;\n"
    "attribute vec4 aColor;\n"
    "varying vec4 vColor;\n"
    "uniform float uPointSize;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "    gl_PointSize = uPointSize;\n"
    "    vColor = aColor;\n"
    "}\n";

const char* basicFS =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

// Shader that uses gl_PointCoord to test point sprite functionality
const char* pointCoordVS =
    "precision highp float;\n"
    "attribute vec2 aPosition;\n"
    "uniform float uPointSize;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "    gl_PointSize = uPointSize;\n"
    "}\n";

const char* pointCoordFS =
    "precision mediump float;\n"
    "void main() {\n"
    "    // gl_PointCoord: (0,0) at top-left of point, (1,1) at bottom-right\n"
    "    // Encode point coord as color: R=s, G=t, B=0.5\n"
    "    gl_FragColor = vec4(gl_PointCoord.x, gl_PointCoord.y, 0.5, 1.0);\n"
    "}\n";



int g_numPassed = 0;
int g_numFailed = 0;
int g_numTotal = 0;

void reportTest(const TestResult& t) {
    g_numTotal++;
    if (t.passed) {
        g_numPassed++;
        printf("  [PASS] %s\n", t.name);
    } else {
        g_numFailed++;
        printf("  [FAIL] %s\n", t.name);
    }
    if (t.detail[0]) {
        printf("         %s\n", t.detail);
    }
}
