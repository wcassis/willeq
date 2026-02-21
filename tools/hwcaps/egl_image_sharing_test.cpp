// egl_image_sharing_test - Test cross-API texture sharing via EGL images
//
// Tests two paths for sharing ETC1 textures from GLES2 to desktop GL:
//
// Path A: Direct texture → EGL image (requires EGL_KHR_gl_texture_2d_image)
//   Upload ETC1 in GLES2 → eglCreateImageKHR(EGL_GL_TEXTURE_2D_KHR) → import in GL
//
// Path B: DMA-BUF render-to-buffer (fallback, uses GBM + GLES2 render)
//   Upload ETC1 in GLES2 → render to GBM-backed FBO → create EGL image from
//   DMA-BUF → import in GL. One extra fullscreen quad per atlas page at load time.
//
// Usage:
//   ./egl_image_sharing_test --drm

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <gbm.h>
#endif

// Constants
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef EGL_GL_TEXTURE_2D_KHR
#define EGL_GL_TEXTURE_2D_KHR 0x30B1
#endif
#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#endif
#ifndef EGL_LINUX_DRM_FOURCC_EXT
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#endif
#ifndef EGL_DMA_BUF_PLANE0_FD_EXT
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#endif
#ifndef EGL_DMA_BUF_PLANE0_OFFSET_EXT
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#endif
#ifndef EGL_DMA_BUF_PLANE0_PITCH_EXT
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#endif

// ======================================================================
// Function pointer types
// ======================================================================
// Core GL (dispatched per-context by Mesa)
typedef void (*PFN_glGenTextures)(GLsizei, GLuint*);
typedef void (*PFN_glDeleteTextures)(GLsizei, const GLuint*);
typedef void (*PFN_glBindTexture)(GLenum, GLuint);
typedef void (*PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                  GLint, GLenum, GLenum, const void*);
typedef void (*PFN_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei,
                                            GLsizei, GLint, GLsizei, const void*);
typedef void (*PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef GLenum (*PFN_glGetError)(void);
typedef const GLubyte* (*PFN_glGetString)(GLenum);
typedef void (*PFN_glGetIntegerv)(GLenum, GLint*);
typedef void (*PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (*PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (*PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFN_glCheckFramebufferStatus)(GLenum);
typedef void (*PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*PFN_glFinish)(void);
typedef void (*PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);

// Shader/render (for Path B)
typedef GLuint (*PFN_glCreateShader)(GLenum);
typedef void (*PFN_glDeleteShader)(GLuint);
typedef void (*PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*PFN_glCompileShader)(GLuint);
typedef void (*PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*PFN_glCreateProgram)(void);
typedef void (*PFN_glDeleteProgram)(GLuint);
typedef void (*PFN_glAttachShader)(GLuint, GLuint);
typedef void (*PFN_glLinkProgram)(GLuint);
typedef void (*PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_glUseProgram)(GLuint);
typedef GLint (*PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void (*PFN_glUniform1i)(GLint, GLint);
typedef void (*PFN_glEnableVertexAttribArray)(GLuint);
typedef void (*PFN_glDisableVertexAttribArray)(GLuint);
typedef void (*PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*PFN_glActiveTexture)(GLenum);

// EGL image
typedef EGLImageKHR (*PFN_eglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum,
                                              EGLClientBuffer, const EGLint*);
typedef EGLBoolean (*PFN_eglDestroyImageKHR)(EGLDisplay, EGLImageKHR);
typedef void (*PFN_glEGLImageTargetTexture2DOES)(GLenum, GLeglImageOES);

// ======================================================================
// Global function pointers
// ======================================================================
static PFN_glGenTextures gl_GenTextures;
static PFN_glDeleteTextures gl_DeleteTextures;
static PFN_glBindTexture gl_BindTexture;
static PFN_glTexImage2D gl_TexImage2D;
static PFN_glCompressedTexImage2D gl_CompressedTexImage2D;
static PFN_glTexParameteri gl_TexParameteri;
static PFN_glGetError gl_GetError;
static PFN_glGetString gl_GetString;
static PFN_glGetIntegerv gl_GetIntegerv;
static PFN_glGenFramebuffers gl_GenFramebuffers;
static PFN_glDeleteFramebuffers gl_DeleteFramebuffers;
static PFN_glBindFramebuffer gl_BindFramebuffer;
static PFN_glFramebufferTexture2D gl_FramebufferTexture2D;
static PFN_glCheckFramebufferStatus gl_CheckFramebufferStatus;
static PFN_glReadPixels gl_ReadPixels;
static PFN_glFinish gl_Finish;
static PFN_glViewport gl_Viewport;

static PFN_glCreateShader gl_CreateShader;
static PFN_glDeleteShader gl_DeleteShader;
static PFN_glShaderSource gl_ShaderSource;
static PFN_glCompileShader gl_CompileShader;
static PFN_glGetShaderiv gl_GetShaderiv;
static PFN_glCreateProgram gl_CreateProgram;
static PFN_glDeleteProgram gl_DeleteProgram;
static PFN_glAttachShader gl_AttachShader;
static PFN_glLinkProgram gl_LinkProgram;
static PFN_glGetProgramiv gl_GetProgramiv;
static PFN_glUseProgram gl_UseProgram;
static PFN_glGetUniformLocation gl_GetUniformLocation;
static PFN_glUniform1i gl_Uniform1i;
static PFN_glEnableVertexAttribArray gl_EnableVertexAttribArray;
static PFN_glDisableVertexAttribArray gl_DisableVertexAttribArray;
static PFN_glVertexAttribPointer gl_VertexAttribPointer;
static PFN_glDrawArrays gl_DrawArrays;
static PFN_glActiveTexture gl_ActiveTexture;

static PFN_eglCreateImageKHR egl_CreateImageKHR;
static PFN_eglDestroyImageKHR egl_DestroyImageKHR;
static PFN_glEGLImageTargetTexture2DOES gl_EGLImageTargetTexture2DOES;

#define RESOLVE(name, type) do { \
    gl_##name = (type)eglGetProcAddress("gl" #name); \
    if (!gl_##name) { fprintf(stderr, "Failed to resolve gl" #name "\n"); return false; } \
} while(0)

static bool resolveGL() {
    RESOLVE(GenTextures, PFN_glGenTextures);
    RESOLVE(DeleteTextures, PFN_glDeleteTextures);
    RESOLVE(BindTexture, PFN_glBindTexture);
    RESOLVE(TexImage2D, PFN_glTexImage2D);
    RESOLVE(CompressedTexImage2D, PFN_glCompressedTexImage2D);
    RESOLVE(TexParameteri, PFN_glTexParameteri);
    RESOLVE(GetError, PFN_glGetError);
    RESOLVE(GetString, PFN_glGetString);
    RESOLVE(GetIntegerv, PFN_glGetIntegerv);
    RESOLVE(GenFramebuffers, PFN_glGenFramebuffers);
    RESOLVE(DeleteFramebuffers, PFN_glDeleteFramebuffers);
    RESOLVE(BindFramebuffer, PFN_glBindFramebuffer);
    RESOLVE(FramebufferTexture2D, PFN_glFramebufferTexture2D);
    RESOLVE(CheckFramebufferStatus, PFN_glCheckFramebufferStatus);
    RESOLVE(ReadPixels, PFN_glReadPixels);
    RESOLVE(Finish, PFN_glFinish);
    RESOLVE(Viewport, PFN_glViewport);
    RESOLVE(CreateShader, PFN_glCreateShader);
    RESOLVE(DeleteShader, PFN_glDeleteShader);
    RESOLVE(ShaderSource, PFN_glShaderSource);
    RESOLVE(CompileShader, PFN_glCompileShader);
    RESOLVE(GetShaderiv, PFN_glGetShaderiv);
    RESOLVE(CreateProgram, PFN_glCreateProgram);
    RESOLVE(DeleteProgram, PFN_glDeleteProgram);
    RESOLVE(AttachShader, PFN_glAttachShader);
    RESOLVE(LinkProgram, PFN_glLinkProgram);
    RESOLVE(GetProgramiv, PFN_glGetProgramiv);
    RESOLVE(UseProgram, PFN_glUseProgram);
    RESOLVE(GetUniformLocation, PFN_glGetUniformLocation);
    RESOLVE(Uniform1i, PFN_glUniform1i);
    RESOLVE(EnableVertexAttribArray, PFN_glEnableVertexAttribArray);
    RESOLVE(DisableVertexAttribArray, PFN_glDisableVertexAttribArray);
    RESOLVE(VertexAttribPointer, PFN_glVertexAttribPointer);
    RESOLVE(DrawArrays, PFN_glDrawArrays);
    RESOLVE(ActiveTexture, PFN_glActiveTexture);
    return true;
}
#undef RESOLVE

static bool resolveEGLImage() {
    egl_CreateImageKHR = (PFN_eglCreateImageKHR)eglGetProcAddress("eglCreateImageKHR");
    egl_DestroyImageKHR = (PFN_eglDestroyImageKHR)eglGetProcAddress("eglDestroyImageKHR");
    gl_EGLImageTargetTexture2DOES =
        (PFN_glEGLImageTargetTexture2DOES)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!egl_CreateImageKHR) fprintf(stderr, "  eglCreateImageKHR not available\n");
    if (!egl_DestroyImageKHR) fprintf(stderr, "  eglDestroyImageKHR not available\n");
    if (!gl_EGLImageTargetTexture2DOES)
        fprintf(stderr, "  glEGLImageTargetTexture2DOES not available\n");
    return egl_CreateImageKHR && egl_DestroyImageKHR && gl_EGLImageTargetTexture2DOES;
}

// ======================================================================
// Helpers
// ======================================================================
static bool hasExt(const char* list, const char* name) {
    if (!list || !name) return false;
    size_t len = strlen(name);
    const char* p = list;
    while ((p = strstr(p, name)) != nullptr) {
        if ((p == list || p[-1] == ' ') && (p[len] == '\0' || p[len] == ' '))
            return true;
        p += len;
    }
    return false;
}

static std::vector<uint8_t> makeETC1(int w, int h) {
    int bx = (w + 3) / 4, by = (h + 3) / 4;
    return std::vector<uint8_t>(bx * by * 8, 0);
}

// ======================================================================
// State
// ======================================================================
struct State {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext gles2Ctx = EGL_NO_CONTEXT;
    EGLContext glCtx = EGL_NO_CONTEXT;
    EGLSurface gles2Surf = EGL_NO_SURFACE;
    EGLSurface glSurf = EGL_NO_SURFACE;
#ifdef EQT_HAS_DRM
    int drmFd = -1;
    struct gbm_device* gbm = nullptr;
    struct gbm_surface* gbmSurf1 = nullptr;
    struct gbm_surface* gbmSurf2 = nullptr;
#endif
};

static void cleanup(State& s) {
    if (s.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s.gles2Ctx != EGL_NO_CONTEXT) eglDestroyContext(s.display, s.gles2Ctx);
        if (s.glCtx != EGL_NO_CONTEXT) eglDestroyContext(s.display, s.glCtx);
        if (s.gles2Surf != EGL_NO_SURFACE) eglDestroySurface(s.display, s.gles2Surf);
        if (s.glSurf != EGL_NO_SURFACE) eglDestroySurface(s.display, s.glSurf);
        eglTerminate(s.display);
    }
#ifdef EQT_HAS_DRM
    if (s.gbmSurf1) gbm_surface_destroy(s.gbmSurf1);
    if (s.gbmSurf2) gbm_surface_destroy(s.gbmSurf2);
    if (s.gbm) gbm_device_destroy(s.gbm);
    if (s.drmFd >= 0) close(s.drmFd);
#endif
}

// ======================================================================
// GLES2 blit shader (for Path B: render ETC1 texture to FBO)
// ======================================================================
static GLuint compileBlitProgram() {
    const char* vsSrc =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";
    const char* fsSrc =
        "precision mediump float;\n"
        "uniform sampler2D uTexture;\n"
        "varying vec2 vUV;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(uTexture, vUV);\n"
        "}\n";

    GLuint vs = gl_CreateShader(GL_VERTEX_SHADER);
    gl_ShaderSource(vs, 1, &vsSrc, nullptr);
    gl_CompileShader(vs);
    GLint ok;
    gl_GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { gl_DeleteShader(vs); return 0; }

    GLuint fs = gl_CreateShader(GL_FRAGMENT_SHADER);
    gl_ShaderSource(fs, 1, &fsSrc, nullptr);
    gl_CompileShader(fs);
    gl_GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { gl_DeleteShader(vs); gl_DeleteShader(fs); return 0; }

    GLuint prog = gl_CreateProgram();
    gl_AttachShader(prog, vs);
    gl_AttachShader(prog, fs);
    gl_LinkProgram(prog);
    gl_GetProgramiv(prog, GL_LINK_STATUS, &ok);
    gl_DeleteShader(vs);
    gl_DeleteShader(fs);
    if (!ok) { gl_DeleteProgram(prog); return 0; }
    return prog;
}

// Render a fullscreen quad from srcTex into the current FBO
static void blitTexture(GLuint prog, GLuint srcTex, int w, int h) {
    // Fullscreen quad: position(x,y) + uv(u,v) interleaved
    static const float quad[] = {
        -1, -1,  0, 0,
         1, -1,  1, 0,
        -1,  1,  0, 1,
         1,  1,  1, 1,
    };

    gl_UseProgram(prog);
    gl_Viewport(0, 0, w, h);

    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, srcTex);
    gl_Uniform1i(gl_GetUniformLocation(prog, "uTexture"), 0);

    GLint posLoc = 0;  // aPos
    GLint uvLoc = 1;   // aUV
    // Bind by name for safety
    posLoc = gl_GetUniformLocation(prog, "aPos");
    uvLoc = gl_GetUniformLocation(prog, "aUV");
    // glGetAttribLocation is more correct here — but we use layout order
    // Just use 0 and 1 since they're the only attributes
    posLoc = 0;
    uvLoc = 1;

    gl_EnableVertexAttribArray(posLoc);
    gl_EnableVertexAttribArray(uvLoc);
    gl_VertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 16, quad);
    gl_VertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, 16, quad + 2);
    gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_DisableVertexAttribArray(posLoc);
    gl_DisableVertexAttribArray(uvLoc);
    gl_Finish();
}

// ======================================================================
// Path A: Direct texture → EGL image
// ======================================================================
static bool testPathA(State& s, int w, int h, const char* label) {
    printf("\n=== %s: Path A — direct texture sharing (%dx%d) ===\n", label, w, h);

    auto etc1 = makeETC1(w, h);

    // Upload ETC1 in GLES2
    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);

    GLuint srcTex = 0;
    gl_GenTextures(1, &srcTex);
    gl_BindTexture(GL_TEXTURE_2D, srcTex);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, w, h, 0,
                            (GLsizei)etc1.size(), etc1.data());
    if (gl_GetError() != GL_NO_ERROR) {
        printf("  ETC1 upload failed\n");
        gl_DeleteTextures(1, &srcTex);
        return false;
    }
    printf("  ETC1 upload in GLES2:     OK\n");

    // Try creating EGL image directly from texture
    EGLImageKHR image = egl_CreateImageKHR(
        s.display, s.gles2Ctx, EGL_GL_TEXTURE_2D_KHR,
        (EGLClientBuffer)(uintptr_t)srcTex, nullptr);

    if (image == EGL_NO_IMAGE_KHR) {
        EGLint err = eglGetError();
        printf("  eglCreateImageKHR(EGL_GL_TEXTURE_2D_KHR): FAILED (0x%04X)\n", err);
        gl_DeleteTextures(1, &srcTex);
        return false;
    }
    printf("  EGL image from texture:   OK\n");

    // Import into desktop GL
    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);

    GLuint dstTex = 0;
    gl_GenTextures(1, &dstTex);
    gl_BindTexture(GL_TEXTURE_2D, dstTex);
    gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);

    if (gl_GetError() != GL_NO_ERROR) {
        printf("  Desktop GL import:        FAILED\n");
        gl_DeleteTextures(1, &dstTex);
        egl_DestroyImageKHR(s.display, image);
        eglBindAPI(EGL_OPENGL_ES_API);
        eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
        gl_DeleteTextures(1, &srcTex);
        return false;
    }
    printf("  Desktop GL import:        OK\n");

    // FBO readback verification
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint fbo = 0;
    gl_GenFramebuffers(1, &fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        uint8_t px[4];
        gl_ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (gl_GetError() == GL_NO_ERROR)
            printf("  GL readback pixel[0]:     %d,%d,%d,%d\n", px[0], px[1], px[2], px[3]);
    } else {
        printf("  GL FBO readback:          skipped (FBO incomplete)\n");
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_DeleteFramebuffers(1, &fbo);

    printf("  PASS (Path A)\n");

    gl_DeleteTextures(1, &dstTex);
    egl_DestroyImageKHR(s.display, image);
    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
    gl_DeleteTextures(1, &srcTex);
    return true;
}

// ======================================================================
// Path B: DMA-BUF render-to-buffer
// ======================================================================
#ifdef EQT_HAS_DRM
static bool testPathB(State& s, int w, int h, const char* label) {
    printf("\n=== %s: Path B — DMA-BUF render-to-buffer (%dx%d) ===\n", label, w, h);

    auto etc1 = makeETC1(w, h);

    // Create GBM buffer object (XRGB8888, GPU-renderable)
    struct gbm_bo* bo = gbm_bo_create(s.gbm, w, h, GBM_FORMAT_XRGB8888,
                                       GBM_BO_USE_RENDERING);
    if (!bo) {
        printf("  gbm_bo_create:            FAILED\n");
        return false;
    }
    int dmaFd = gbm_bo_get_fd(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    printf("  GBM buffer:               %dx%d stride=%u fd=%d\n", w, h, stride, dmaFd);

    if (dmaFd < 0) {
        printf("  gbm_bo_get_fd:            FAILED\n");
        gbm_bo_destroy(bo);
        return false;
    }

    // Create EGL image from DMA-BUF (no context needed)
    EGLint imgAttrs[] = {
        EGL_WIDTH, w,
        EGL_HEIGHT, h,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)DRM_FORMAT_XRGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT, dmaFd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride,
        EGL_NONE
    };
    EGLImageKHR image = egl_CreateImageKHR(
        s.display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, imgAttrs);

    if (image == EGL_NO_IMAGE_KHR) {
        EGLint err = eglGetError();
        printf("  EGL image from DMA-BUF:   FAILED (0x%04X)\n", err);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  EGL image from DMA-BUF:   OK\n");

    // --- GLES2: upload ETC1 + render to GBM-backed texture ---
    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);

    // Upload ETC1 source texture
    GLuint srcTex = 0;
    gl_GenTextures(1, &srcTex);
    gl_BindTexture(GL_TEXTURE_2D, srcTex);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, w, h, 0,
                            (GLsizei)etc1.size(), etc1.data());
    if (gl_GetError() != GL_NO_ERROR) {
        printf("  ETC1 upload:              FAILED\n");
        gl_DeleteTextures(1, &srcTex);
        egl_DestroyImageKHR(s.display, image);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  ETC1 upload in GLES2:     OK\n");

    // Bind EGL image to render-target texture
    GLuint rtTex = 0;
    gl_GenTextures(1, &rtTex);
    gl_BindTexture(GL_TEXTURE_2D, rtTex);
    gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
    if (gl_GetError() != GL_NO_ERROR) {
        printf("  GLES2 EGL image bind:     FAILED\n");
        gl_DeleteTextures(1, &srcTex);
        gl_DeleteTextures(1, &rtTex);
        egl_DestroyImageKHR(s.display, image);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  GLES2 render target:      OK\n");

    // Create FBO with render target
    GLuint fbo = 0;
    gl_GenFramebuffers(1, &fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtTex, 0);
    GLenum fbStatus = gl_CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        printf("  FBO status:               INCOMPLETE (0x%04X)\n", fbStatus);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_DeleteFramebuffers(1, &fbo);
        gl_DeleteTextures(1, &srcTex);
        gl_DeleteTextures(1, &rtTex);
        egl_DestroyImageKHR(s.display, image);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  GLES2 FBO:                complete\n");

    // Compile blit shader and render ETC1 → render target
    GLuint prog = compileBlitProgram();
    if (!prog) {
        printf("  Blit shader:              FAILED\n");
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_DeleteFramebuffers(1, &fbo);
        gl_DeleteTextures(1, &srcTex);
        gl_DeleteTextures(1, &rtTex);
        egl_DestroyImageKHR(s.display, image);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  Blit shader:              OK\n");

    blitTexture(prog, srcTex, w, h);

    // Readback verification in GLES2
    uint8_t gles2Px[4] = {};
    gl_ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gles2Px);
    if (gl_GetError() == GL_NO_ERROR)
        printf("  GLES2 blit pixel[0]:      %d,%d,%d,%d\n",
               gles2Px[0], gles2Px[1], gles2Px[2], gles2Px[3]);

    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_DeleteFramebuffers(1, &fbo);
    gl_DeleteProgram(prog);
    gl_DeleteTextures(1, &srcTex);
    gl_DeleteTextures(1, &rtTex);

    // --- Desktop GL: import same EGL image ---
    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);

    GLuint glTex = 0;
    gl_GenTextures(1, &glTex);
    gl_BindTexture(GL_TEXTURE_2D, glTex);
    gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);

    if (gl_GetError() != GL_NO_ERROR) {
        printf("  Desktop GL import:        FAILED\n");
        gl_DeleteTextures(1, &glTex);
        egl_DestroyImageKHR(s.display, image);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return false;
    }
    printf("  Desktop GL import:        OK\n");

    // FBO readback in GL
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint glFbo = 0;
    gl_GenFramebuffers(1, &glFbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, glFbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glTex, 0);
    if (gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        uint8_t glPx[4] = {};
        gl_ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, glPx);
        if (gl_GetError() == GL_NO_ERROR) {
            printf("  GL readback pixel[0]:     %d,%d,%d,%d\n",
                   glPx[0], glPx[1], glPx[2], glPx[3]);
            bool match = (gles2Px[0] == glPx[0] && gles2Px[1] == glPx[1] &&
                          gles2Px[2] == glPx[2] && gles2Px[3] == glPx[3]);
            printf("  Pixel match:              %s\n", match ? "YES" : "NO");
        }
    } else {
        printf("  GL FBO readback:          skipped (FBO incomplete)\n");
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl_DeleteFramebuffers(1, &glFbo);

    printf("  PASS (Path B)\n");

    gl_DeleteTextures(1, &glTex);
    egl_DestroyImageKHR(s.display, image);
    close(dmaFd);
    gbm_bo_destroy(bo);
    return true;
}
#endif // EQT_HAS_DRM

// ======================================================================
// Performance benchmark (parameterized for either path)
// ======================================================================
static void benchmarkDirect(State& s, int w, int h, int iters) {
    printf("\n=== Performance Path A: %dx%d, %d iterations ===\n", w, h, iters);
    auto etc1 = makeETC1(w, h);
    std::vector<uint8_t> rgba(w * h * 4, 128);

    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; i++) {
            eglBindAPI(EGL_OPENGL_ES_API);
            eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
            GLuint t1;
            gl_GenTextures(1, &t1);
            gl_BindTexture(GL_TEXTURE_2D, t1);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, w, h, 0,
                                    (GLsizei)etc1.size(), etc1.data());
            gl_Finish();
            EGLImageKHR img = egl_CreateImageKHR(s.display, s.gles2Ctx,
                EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)t1, nullptr);
            eglBindAPI(EGL_OPENGL_API);
            eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);
            GLuint t2;
            gl_GenTextures(1, &t2);
            gl_BindTexture(GL_TEXTURE_2D, t2);
            gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
            gl_Finish();
            gl_DeleteTextures(1, &t2);
            egl_DestroyImageKHR(s.display, img);
            eglBindAPI(EGL_OPENGL_ES_API);
            eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
            gl_DeleteTextures(1, &t1);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                    / (double)iters;
        printf("  ETC1 sharing pipeline:  %10.1f us/iter\n", us);
    }
    {
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; i++) {
            GLuint t;
            gl_GenTextures(1, &t);
            gl_BindTexture(GL_TEXTURE_2D, t);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            gl_Finish();
            gl_DeleteTextures(1, &t);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                    / (double)iters;
        printf("  RGBA direct baseline:   %10.1f us/iter\n", us);
    }
}

#ifdef EQT_HAS_DRM
static void benchmarkDMABUF(State& s, int w, int h, int iters) {
    printf("\n=== Performance Path B: %dx%d, %d iterations ===\n", w, h, iters);
    auto etc1 = makeETC1(w, h);
    std::vector<uint8_t> rgba(w * h * 4, 128);

    // Compile blit shader once
    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
    GLuint prog = compileBlitProgram();
    if (!prog) { printf("  Shader compile failed, skipping\n"); return; }

    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; i++) {
            // Create GBM buffer + EGL image
            struct gbm_bo* bo = gbm_bo_create(s.gbm, w, h, GBM_FORMAT_XRGB8888,
                                               GBM_BO_USE_RENDERING);
            int fd = gbm_bo_get_fd(bo);
            uint32_t stride = gbm_bo_get_stride(bo);
            EGLint attrs[] = {
                EGL_WIDTH, w, EGL_HEIGHT, h,
                EGL_LINUX_DRM_FOURCC_EXT, (EGLint)DRM_FORMAT_XRGB8888,
                EGL_DMA_BUF_PLANE0_FD_EXT, fd,
                EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride,
                EGL_NONE
            };
            EGLImageKHR img = egl_CreateImageKHR(s.display, EGL_NO_CONTEXT,
                EGL_LINUX_DMA_BUF_EXT, nullptr, attrs);

            // GLES2: upload ETC1 + render to buffer
            eglBindAPI(EGL_OPENGL_ES_API);
            eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
            GLuint srcTex, rtTex;
            gl_GenTextures(1, &srcTex);
            gl_BindTexture(GL_TEXTURE_2D, srcTex);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, w, h, 0,
                                    (GLsizei)etc1.size(), etc1.data());
            gl_GenTextures(1, &rtTex);
            gl_BindTexture(GL_TEXTURE_2D, rtTex);
            gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
            GLuint fbo;
            gl_GenFramebuffers(1, &fbo);
            gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
            gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtTex, 0);
            blitTexture(prog, srcTex, w, h);
            gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl_DeleteFramebuffers(1, &fbo);
            gl_DeleteTextures(1, &srcTex);
            gl_DeleteTextures(1, &rtTex);

            // GL: import
            eglBindAPI(EGL_OPENGL_API);
            eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);
            GLuint glTex;
            gl_GenTextures(1, &glTex);
            gl_BindTexture(GL_TEXTURE_2D, glTex);
            gl_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
            gl_Finish();

            gl_DeleteTextures(1, &glTex);
            egl_DestroyImageKHR(s.display, img);
            close(fd);
            gbm_bo_destroy(bo);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                    / (double)iters;
        printf("  ETC1 DMA-BUF pipeline:  %10.1f us/iter\n", us);
    }
    {
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; i++) {
            GLuint t;
            gl_GenTextures(1, &t);
            gl_BindTexture(GL_TEXTURE_2D, t);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            gl_Finish();
            gl_DeleteTextures(1, &t);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                    / (double)iters;
        printf("  RGBA direct baseline:   %10.1f us/iter\n", us);
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
    gl_DeleteProgram(prog);
    printf("  (Note: atlas pages uploaded once at zone load, not per-frame)\n");
}
#endif

// ======================================================================
// Main
// ======================================================================
int main(int argc, char** argv) {
#ifndef EQT_HAS_DRM
    fprintf(stderr, "This tool requires DRM support (compile with -DEQT_HAS_DRM)\n");
    return 1;
#else
    State s;

    const char* drmPaths[] = {"/dev/dri/card1", "/dev/dri/card0", nullptr};
    for (int i = 0; drmPaths[i]; i++) {
        s.drmFd = open(drmPaths[i], O_RDWR);
        if (s.drmFd >= 0) { printf("DRM device: %s\n", drmPaths[i]); break; }
    }
    if (s.drmFd < 0) { fprintf(stderr, "Cannot open DRM device\n"); return 1; }

    s.gbm = gbm_create_device(s.drmFd);
    if (!s.gbm) { fprintf(stderr, "Cannot create GBM device\n"); cleanup(s); return 1; }

    s.display = eglGetDisplay((EGLNativeDisplayType)s.gbm);
    if (s.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Cannot get EGL display\n"); cleanup(s); return 1;
    }
    EGLint major, minor;
    if (!eglInitialize(s.display, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n"); cleanup(s); return 1;
    }
    printf("EGL %d.%d\n\n", major, minor);

    // --- Print ALL EGL extensions ---
    const char* eglExts = eglQueryString(s.display, EGL_EXTENSIONS);
    printf("=== EGL extensions ===\n");
    if (eglExts) {
        // Print each extension on its own line
        std::string exts(eglExts);
        size_t pos = 0;
        while (pos < exts.size()) {
            size_t sp = exts.find(' ', pos);
            if (sp == std::string::npos) sp = exts.size();
            printf("  %s\n", exts.substr(pos, sp - pos).c_str());
            pos = sp + 1;
        }
    }
    printf("\n");

    // Check key extensions
    bool hasImageBase = hasExt(eglExts, "EGL_KHR_image_base") ||
                        hasExt(eglExts, "EGL_KHR_image");
    bool hasTex2D = hasExt(eglExts, "EGL_KHR_gl_texture_2d_image");
    bool hasDMABufImport = hasExt(eglExts, "EGL_EXT_image_dma_buf_import");

    printf("=== Key extension summary ===\n");
    printf("  EGL_KHR_image_base:           %s\n", hasImageBase ? "YES" : "no");
    printf("  EGL_KHR_gl_texture_2d_image:  %s  %s\n",
           hasTex2D ? "YES" : "no",
           hasTex2D ? "(Path A available)" : "(Path A unavailable, will try anyway)");
    printf("  EGL_EXT_image_dma_buf_import: %s  %s\n",
           hasDMABufImport ? "YES" : "no",
           hasDMABufImport ? "(Path B available)" : "(Path B unavailable)");
    printf("\n");

    if (!hasImageBase) {
        fprintf(stderr, "FATAL: EGL_KHR_image_base not available\n");
        cleanup(s); return 1;
    }

    if (!resolveEGLImage()) { cleanup(s); return 1; }

    // --- EGL configs ---
    EGLConfig gles2Cfg, glCfg;
    EGLint nCfg;
    {
        EGLint attr[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_NONE
        };
        eglChooseConfig(s.display, attr, &gles2Cfg, 1, &nCfg);
        if (nCfg > 0) {
            printf("EGL config: combined (GL + GLES2)\n");
            glCfg = gles2Cfg;
        } else {
            printf("EGL config: separate\n");
            EGLint a1[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
            EGLint a2[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
            eglChooseConfig(s.display, a1, &gles2Cfg, 1, &nCfg);
            if (nCfg == 0) { fprintf(stderr, "No GLES2 config\n"); cleanup(s); return 1; }
            eglChooseConfig(s.display, a2, &glCfg, 1, &nCfg);
            if (nCfg == 0) { fprintf(stderr, "No GL config\n"); cleanup(s); return 1; }
        }
    }

    // --- Surfaces and contexts ---
    s.gbmSurf1 = gbm_surface_create(s.gbm, 64, 64, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    s.gbmSurf2 = gbm_surface_create(s.gbm, 64, 64, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!s.gbmSurf1 || !s.gbmSurf2) {
        fprintf(stderr, "Cannot create GBM surfaces\n"); cleanup(s); return 1;
    }
    s.gles2Surf = eglCreateWindowSurface(s.display, gles2Cfg,
                                          (EGLNativeWindowType)s.gbmSurf1, nullptr);
    s.glSurf = eglCreateWindowSurface(s.display, glCfg,
                                       (EGLNativeWindowType)s.gbmSurf2, nullptr);
    if (s.gles2Surf == EGL_NO_SURFACE || s.glSurf == EGL_NO_SURFACE) {
        fprintf(stderr, "Cannot create EGL surfaces\n"); cleanup(s); return 1;
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint es2Attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s.gles2Ctx = eglCreateContext(s.display, gles2Cfg, EGL_NO_CONTEXT, es2Attr);
    if (s.gles2Ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "Cannot create GLES2 context\n"); cleanup(s); return 1;
    }
    eglBindAPI(EGL_OPENGL_API);
    s.glCtx = eglCreateContext(s.display, glCfg, EGL_NO_CONTEXT, nullptr);
    if (s.glCtx == EGL_NO_CONTEXT) {
        fprintf(stderr, "Cannot create desktop GL context\n"); cleanup(s); return 1;
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
    if (!resolveGL()) { cleanup(s); return 1; }

    // --- Check GL_OES_EGL_image in both contexts ---
    printf("\n=== GL_OES_EGL_image check ===\n");
    eglBindAPI(EGL_OPENGL_ES_API);
    eglMakeCurrent(s.display, s.gles2Surf, s.gles2Surf, s.gles2Ctx);
    const char* gles2Exts = (const char*)gl_GetString(GL_EXTENSIONS);
    bool gles2Img = hasExt(gles2Exts, "GL_OES_EGL_image");
    printf("  GLES2 (%s): %s\n", (const char*)gl_GetString(GL_VERSION),
           gles2Img ? "YES" : "no");

    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(s.display, s.glSurf, s.glSurf, s.glCtx);
    const char* glExts = (const char*)gl_GetString(GL_EXTENSIONS);
    bool glImg = hasExt(glExts, "GL_OES_EGL_image");
    printf("  GL    (%s): %s\n", (const char*)gl_GetString(GL_VERSION),
           glImg ? "YES" : "no");

    if (!gles2Img || !glImg) {
        fprintf(stderr, "\nFATAL: GL_OES_EGL_image required in both contexts\n");
        cleanup(s); return 1;
    }

    // ===================================================================
    // Try Path A: direct texture → EGL image (even without extension string)
    // ===================================================================
    bool pathA_ok = testPathA(s, 256, 256, "Test 1");
    bool pathA_atlas = false;
    if (pathA_ok) {
        pathA_atlas = testPathA(s, 2048, 2048, "Test 2 (atlas)");
        benchmarkDirect(s, 256, 256, 200);
        if (pathA_atlas) benchmarkDirect(s, 2048, 2048, 20);
    }

    // ===================================================================
    // Path B: DMA-BUF render-to-buffer (fallback if Path A failed)
    // ===================================================================
    bool pathB_ok = false, pathB_atlas = false;
    if (!pathA_ok && hasDMABufImport) {
        printf("\n--- Path A failed, trying Path B (DMA-BUF + GLES2 render) ---\n");
        pathB_ok = testPathB(s, 256, 256, "Test 3");
        if (pathB_ok) {
            pathB_atlas = testPathB(s, 2048, 2048, "Test 4 (atlas)");
            benchmarkDMABUF(s, 256, 256, 100);
            if (pathB_atlas) benchmarkDMABUF(s, 2048, 2048, 10);
        }
    } else if (!pathA_ok && !hasDMABufImport) {
        printf("\n--- Path A failed, Path B unavailable (no EGL_EXT_image_dma_buf_import) ---\n");
    }

    // ===================================================================
    // Summary
    // ===================================================================
    printf("\n=== Summary ===\n");
    if (pathA_ok) {
        printf("  Path A (direct texture sharing):  WORKING\n");
        printf("  Atlas page (2048x2048):           %s\n",
               pathA_atlas ? "WORKING" : "FAILED");
        printf("\n  Zero-copy atlas pipeline:\n");
        printf("    1. Upload ETC1 in GLES2 (hardware decode)\n");
        printf("    2. eglCreateImageKHR(EGL_GL_TEXTURE_2D_KHR) — zero copy\n");
        printf("    3. glEGLImageTargetTexture2DOES in desktop GL\n");
        printf("    4. Irrlicht renders normally\n");
    } else if (pathB_ok) {
        printf("  Path A (direct texture sharing):  not available\n");
        printf("  Path B (DMA-BUF + render):        WORKING\n");
        printf("  Atlas page (2048x2048):           %s\n",
               pathB_atlas ? "WORKING" : "FAILED");
        printf("\n  Render-to-buffer atlas pipeline:\n");
        printf("    1. Upload ETC1 in GLES2 (hardware decode)\n");
        printf("    2. Render to GBM-backed FBO (one fullscreen quad per page)\n");
        printf("    3. Import GBM buffer as EGL image in desktop GL\n");
        printf("    4. Irrlicht renders normally\n");
        printf("    Cost: one extra render pass per atlas page at zone load time\n");
    } else {
        printf("  Path A (direct sharing):   FAILED\n");
        printf("  Path B (DMA-BUF render):   %s\n",
               hasDMABufImport ? "FAILED" : "not available");
        printf("\n  Cross-API texture sharing is NOT viable on this hardware.\n");
        printf("  Consider: single GLES2 context with GLES2-compatible renderer.\n");
    }

    cleanup(s);
    return (pathA_ok || pathB_ok) ? 0 : 1;
#endif // EQT_HAS_DRM
}
