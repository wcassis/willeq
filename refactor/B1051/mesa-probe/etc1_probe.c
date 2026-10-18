/* B1051 (GitLab #371): ETC1 capability probe.
 *
 * Answers ruling step (a) for the x86/llvmpipe leg AND the XU4/Midgard leg:
 *   1. Which GL context does the host create (ES 2.0 vs 3.x)?
 *   2. Is ETC1 a CORE teximage format (ES3) or a listed extension (ES2)?
 *   3. Does glCompressedTexImage2D(GL_ETC1_RGB8_OES) get accepted (glError)?
 *   4. Does it DECODE correctly (render a known solid-color ETC1 texture,
 *      read back the pixels)?
 *
 * The probe tests BOTH the ES 2 context the client requests and the ES 3
 * context the Mesa server may actually return, so a "capability confound"
 * can be assigned to the context the client really runs in. Driver ground
 * truth, independent of the willeq client: if a host accepts the upload and
 * reads back the right pixels, ETC1 storage works there and a client-side
 * ETC1 fault is the only remaining explanation for a broken multi-buffer
 * character.
 *
 * Build (any host with EGL + GLESv2 dev headers):
 *   gcc -O2 etc1_probe.c -o etc1_probe -lEGL -lGLESv2
 * Run on the device (on X11 hosts: xvfb-run -a ./etc1_probe).
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

/* Solid-color ETC1 4x4 block, direct mode (T=0,P=0,R=1,Q=0, no diff, all
 * pixels index 00 -> C1):
 *   word0 (little-endian) = C1 | (C2 << 14) | (1 << 30); bytes 4-15 = 0. */
static void pack565_block(uint8_t *out, int r, int g, int b)
{
    int r5 = (r * 31) / 255, g6 = (g * 63) / 255, b5 = (b * 31) / 255;
    uint32_t c = (uint32_t)r5 | ((uint32_t)g6 << 5) | ((uint32_t)b5 << 11);
    uint32_t word = c | (c << 14) | (1u << 30);
    out[0] = (uint8_t)(word & 0xff);
    out[1] = (uint8_t)((word >> 8) & 0xff);
    out[2] = (uint8_t)((word >> 16) & 0xff);
    out[3] = (uint8_t)((word >> 24) & 0xff);
    memset(out + 4, 0, 12);
}

static const char *has_ext(const char *all, const char *name)
{
    if (!all) return NULL;
    size_t n = strlen(name);
    const char *p = all;
    while ((p = strstr(p, name)) != NULL) {
        if ((p == all || p[-1] == ' ') && (p[n] == '\0' || p[n] == ' '))
            return p;
        p += n;
    }
    return NULL;
}

/* Run the full upload+render+readback test in the CURRENT context. */
static int etc1_test(void)
{
    const char *ver = (const char *)glGetString(GL_VERSION);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    int isES3 = ver && strstr(ver, "OpenGL ES 3.");
    const char *etc1ext = (!isES3) ? has_ext(ext, "GL_OES_compressed_ETC1_RGB8_texture") : NULL;
    int hasETC1 = isES3 ? 1 : (etc1ext != NULL);

    printf("  [ctx] GL_VERSION = %s\n", ver ? ver : "?");
    printf("  [ctx] ETC1: %s\n",
           isES3 ? "CORE (ES3.x: GL_ETC1_RGB8_OES is a core teximage format)"
                 : (etc1ext ? "EXT PRESENT (GL_OES_compressed_ETC1_RGB8_texture)"
                            : "ABSENT (ES2 without the OES extension)"));

    /* 16x16 ETC1 texture, 4 solid quadrants: BL red, BR green, TL blue,
     * TR white (texture-space: v grows upward from the first row). */
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
        printf("  [ctx] NO CURRENT CONTEXT -- results invalid\n");
        return 3;
    }
    const char *glver = (const char *)glGetString(GL_VERSION);
    GLint maxTex, maxUnits;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    printf("  [ctx] version=\"%s\" MAX_TEXTURE_SIZE=%d MAX_UNITS=%d\n",
           glver ? glver : "?", maxTex, maxUnits);
    const int W = 16, H = 16;
    uint8_t level0[16 * 16];
    for (int by = 0; by < H / 4; ++by) {
        for (int bx = 0; bx < W / 4; ++bx) {
            uint8_t *blk = level0 + (by * (W / 4) + bx) * 16;
            int px = bx * 4, py = by * 4;
            if (px < 8 && py < 8)       pack565_block(blk, 255, 0, 0);     /* BL red */
            else if (px >= 8 && py < 8) pack565_block(blk, 0, 255, 0);     /* BR green */
            else if (px < 8)            pack565_block(blk, 0, 0, 255);     /* TL blue */
            else                        pack565_block(blk, 255, 255, 255);/* TR white */
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* The CLIENT's exact sequence (createETC1Storage): set the linear
     * filters FIRST, then upload level 0 only. Test that first. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, W, H, 0,
                           sizeof(level0), level0);
    GLenum uperr = glGetError();
    printf("  [ctx] client-seq (filters first, level 0 only) glError = 0x%04x (%s)\n",
           uperr, uperr == GL_NO_ERROR ? "ACCEPTED" : "REJECTED");

    /* Then the plain sequence: no prior filter changes, level 0 only. */
    {
        GLuint t2 = 0;
        glGenTextures(1, &t2);
        glBindTexture(GL_TEXTURE_2D, t2);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, W, H, 0,
                               sizeof(level0), level0);
        GLenum e2 = glGetError();
        printf("  [ctx] bare-seq (level 0 only) glError = 0x%04x (%s)\n",
               e2, e2 == GL_NO_ERROR ? "ACCEPTED" : "REJECTED");
        glDeleteTextures(1, &t2);
    }
    if (uperr != GL_NO_ERROR)
        return 1;

    uint8_t one[16];
    pack565_block(one, 255, 255, 255);
    glCompressedTexImage2D(GL_TEXTURE_2D, 1, GL_ETC1_RGB8_OES, 8, 8, 0, 16, one);
    glCompressedTexImage2D(GL_TEXTURE_2D, 2, GL_ETC1_RGB8_OES, 4, 4, 0, 16, one);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const char *vs =
        "attribute vec2 aPos; varying vec2 vUv; "
         "void main(){ vUv = vec2(aPos.x*0.5+0.5, aPos.y*0.5+0.5); gl_Position = vec4(aPos,0.0,1.0); }";
    const char *fs =
        "precision mediump float; varying vec2 vUv; uniform sampler2D uTex; "
        "void main(){ gl_FragColor = texture2D(uTex, vUv); }";
    GLuint prog = glCreateProgram();
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vs, 0); glCompileShader(v);
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fs, 0); glCompileShader(f);
    glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char logi[256]; glGetProgramInfoLog(prog, 0, 256, logi);
               printf("  [ctx] FAIL link: %s\n", logi); return 2; }

    static const float quad[] = { -1,-1,  1,-1,  -1,1,  1,1 };
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glUseProgram(prog);
    GLint loc = glGetAttribLocation(prog, "aPos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);

    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GLenum drerr = glGetError();
    if (drerr != GL_NO_ERROR)
        printf("  [ctx] draw glError = 0x%04x\n", drerr);

    /* Read back the four quadrant centers. GL y=0 (bottom) = texture v=0
     * (first row) = our py<8 rows. */
    uint8_t px[4][3];
    const float samples[4][2] = { {0.25,0.25},{0.75,0.25},{0.25,0.75},{0.75,0.75} };
    for (int i = 0; i < 4; ++i) {
        int sx = (int)(samples[i][0] * 63), sy = (int)(samples[i][1] * 63);
        GLubyte c[4];
        glReadPixels(sx, sy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
        px[i][0] = c[0]; px[i][1] = c[1]; px[i][2] = c[2];
    }
    const char *names[4] = { "bottom-left(red)", "bottom-right(green)",
                             "top-left(blue)", "top-right(white)" };
    const uint8_t want[4][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255} };
    int good = 0;
    for (int i = 0; i < 4; ++i) {
        int err = abs((int)px[i][0]-want[i][0]) + abs((int)px[i][1]-want[i][1]) +
                  abs((int)px[i][2]-want[i][2]);
        if (err < 40) good++;
        printf("  [ctx] readback %-18s = (%3u,%3u,%3u) %s\n",
               names[i], px[i][0], px[i][1], px[i][2], err < 40 ? "OK" : "MISMATCH");
    }
    printf("  [ctx] VERDICT: %d/4 quadrants decode correctly\n", good);
    if (good >= 3) return 0;
    return 2;
}

static EGLContext make_context(EGLDisplay dpy, EGLConfig cfg, EGLSurface surf,
                               EGLint clientVersion)
{
    const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, clientVersion, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_SURFACE, ca);
    if (ctx == EGL_NO_CONTEXT) return EGL_NO_CONTEXT;
    if (surf != EGL_NO_SURFACE) {
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
            eglDestroyContext(dpy, ctx);
            return EGL_NO_CONTEXT;
        }
        return ctx;
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        eglDestroyContext(dpy, ctx);
        return EGL_NO_CONTEXT;
    }
    return ctx;
}

int main(void)
{
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { printf("FAIL eglGetDisplay\n"); return 2; }
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) {
        printf("eglInitialize failed on default display\n");
#ifdef EGL_PLATFORM_SURFACELESS_MESA
        dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
        if (dpy != EGL_NO_DISPLAY && eglInitialize(dpy, &maj, &min)) {
            printf("[probe] using surfaceless platform\n");
        } else
#endif
        {
            printf("FAIL eglInitialize\n");
            return 2;
        }
    }
    printf("[probe] EGL %d.%d vendor=%s\n", maj, min, eglQueryString(dpy, EGL_VENDOR));

    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE };
    EGLConfig cfg; EGLint ncfg;
    if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &ncfg) || ncfg == 0) {
        printf("FAIL eglChooseConfig\n"); return 2;
    }
    const EGLint pbufAttribs[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbufAttribs);
    if (surf == EGL_NO_SURFACE)
        printf("[probe] warning: no pbuffer surface; using windowless context\n");

    printf("\n=== ES 2 context (what the willeq client requests) ===\n");
    EGLContext c2 = make_context(dpy, cfg, surf, 2);
    if (c2 == EGL_NO_CONTEXT) {
        printf("  [ctx] no ES2 context could be created/current\n");
    } else {
        const char *rend = (const char *)glGetString(GL_RENDERER);
        printf("  [ctx] GL_RENDERER = %s\n", rend ? rend : "?");
        etc1_test();
    }

    printf("\n=== ES 3 context request (what Mesa may actually serve) ===\n");
    EGLContext c3 = make_context(dpy, cfg, surf, 3);
    if (c3 == EGL_NO_CONTEXT) {
        printf("  [ctx] no ES3 context (server is ES2-only) -- the ES2 result above is the client's reality\n");
    } else if (c2 != EGL_NO_CONTEXT && c3 == c2) {
        printf("  [ctx] server returned the SAME context for version 3 (ES2-only server)\n");
    } else {
        etc1_test();
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (c2 != EGL_NO_CONTEXT) eglDestroyContext(dpy, c2);
    if (c3 != EGL_NO_CONTEXT && c3 != c2) eglDestroyContext(dpy, c3);
    if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    return 0;
}
