#include "client/graphics/gles2_egl_helper.h"
#include "common/logging.h"

#include <GL/gl.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <unistd.h>

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

// GLES2 constants
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif

// GBM surface size for the GLES2 context (never rendered to, just needed
// so Lima allocates proper backing storage for textures).
static constexpr int GBM_HELPER_SIZE = 64;

namespace EQT {
namespace Graphics {

GLES2EGLHelper::~GLES2EGLHelper() {
    shutdown();
}

bool GLES2EGLHelper::resolveGLES2Functions() {
    // Resolve all GLES2 function pointers via eglGetProcAddress while GLES2 context is current.
    // Mesa dispatches per-context, so these are API-specific.
#define RESOLVE(var, name) var = (decltype(var))eglGetProcAddress(name); \
    if (!var) { LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Failed to resolve {}", name); return false; }

    RESOLVE(gles2_glGenTextures_, "glGenTextures");
    RESOLVE(gles2_glDeleteTextures_, "glDeleteTextures");
    RESOLVE(gles2_glBindTexture_, "glBindTexture");
    RESOLVE(gles2_glCompressedTexImage2D_, "glCompressedTexImage2D");
    RESOLVE(gles2_glTexParameteri_, "glTexParameteri");
    RESOLVE(gles2_glGetError_, "glGetError");
    RESOLVE(gles2_glViewport_, "glViewport");
    RESOLVE(gles2_glActiveTexture_, "glActiveTexture");
    RESOLVE(gles2_glFinish_, "glFinish");

    // Shader functions
    RESOLVE(gles2_glCreateShader_, "glCreateShader");
    RESOLVE(gles2_glDeleteShader_, "glDeleteShader");
    RESOLVE(gles2_glShaderSource_, "glShaderSource");
    RESOLVE(gles2_glCompileShader_, "glCompileShader");
    RESOLVE(gles2_glGetShaderiv_, "glGetShaderiv");
    RESOLVE(gles2_glCreateProgram_, "glCreateProgram");
    RESOLVE(gles2_glDeleteProgram_, "glDeleteProgram");
    RESOLVE(gles2_glAttachShader_, "glAttachShader");
    RESOLVE(gles2_glLinkProgram_, "glLinkProgram");
    RESOLVE(gles2_glGetProgramiv_, "glGetProgramiv");
    RESOLVE(gles2_glUseProgram_, "glUseProgram");
    RESOLVE(gles2_glGetUniformLocation_, "glGetUniformLocation");
    RESOLVE(gles2_glUniform1i_, "glUniform1i");
    RESOLVE(gles2_glEnableVertexAttribArray_, "glEnableVertexAttribArray");
    RESOLVE(gles2_glDisableVertexAttribArray_, "glDisableVertexAttribArray");
    RESOLVE(gles2_glVertexAttribPointer_, "glVertexAttribPointer");
    RESOLVE(gles2_glDrawArrays_, "glDrawArrays");

    // FBO functions
    RESOLVE(gles2_glGenFramebuffers_, "glGenFramebuffers");
    RESOLVE(gles2_glDeleteFramebuffers_, "glDeleteFramebuffers");
    RESOLVE(gles2_glBindFramebuffer_, "glBindFramebuffer");
    RESOLVE(gles2_glFramebufferTexture2D_, "glFramebufferTexture2D");
    RESOLVE(gles2_glCheckFramebufferStatus_, "glCheckFramebufferStatus");

#undef RESOLVE
    return true;
}

bool GLES2EGLHelper::compileBlitShader() {
    // Simple fullscreen quad shader to blit ETC1 texture to an RGBA FBO.
    // This decodes ETC1 on the Mali 400's texture hardware and writes RGBA pixels.
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

    unsigned int vs = gles2_glCreateShader_(GL_VERTEX_SHADER);
    gles2_glShaderSource_(vs, 1, &vsSrc, nullptr);
    gles2_glCompileShader_(vs);
    int ok = 0;
    gles2_glGetShaderiv_(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Blit vertex shader compile failed");
        gles2_glDeleteShader_(vs);
        return false;
    }

    unsigned int fs = gles2_glCreateShader_(GL_FRAGMENT_SHADER);
    gles2_glShaderSource_(fs, 1, &fsSrc, nullptr);
    gles2_glCompileShader_(fs);
    gles2_glGetShaderiv_(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Blit fragment shader compile failed");
        gles2_glDeleteShader_(vs);
        gles2_glDeleteShader_(fs);
        return false;
    }

    blitProgram_ = gles2_glCreateProgram_();
    gles2_glAttachShader_(blitProgram_, vs);
    gles2_glAttachShader_(blitProgram_, fs);
    gles2_glLinkProgram_(blitProgram_);
    gles2_glGetProgramiv_(blitProgram_, GL_LINK_STATUS, &ok);
    gles2_glDeleteShader_(vs);
    gles2_glDeleteShader_(fs);

    if (!ok) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Blit shader link failed");
        gles2_glDeleteProgram_(blitProgram_);
        blitProgram_ = 0;
        return false;
    }

    return true;
}

bool GLES2EGLHelper::init(EGLDisplay display) {
    if (available_) return true;

    display_ = display;
    if (display_ == EGL_NO_DISPLAY) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Invalid EGL display");
        return false;
    }

    // Resolve EGL image function pointers
    eglCreateImageKHR_ = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_ = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    glEGLImageTargetTexture2DOES_ =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if (!eglCreateImageKHR_ || !eglDestroyImageKHR_ || !glEGLImageTargetTexture2DOES_) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: EGL image functions not available");
        return false;
    }

    // Check for DMA-BUF import extension
    const char* eglExts = eglQueryString(display_, EGL_EXTENSIONS);
    if (!eglExts || !strstr(eglExts, "EGL_EXT_image_dma_buf_import")) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: EGL_EXT_image_dma_buf_import not available");
        return false;
    }

    // Save caller's current context
    EGLContext savedCtx = eglGetCurrentContext();
    EGLSurface savedDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface savedRead = eglGetCurrentSurface(EGL_READ);

    // Open DRM device and create GBM device + surface.
    drmFd_ = open("/dev/dri/card0", O_RDWR);
    if (drmFd_ < 0) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot open /dev/dri/card0");
        return false;
    }

    gbmDevice_ = gbm_create_device(drmFd_);
    if (!gbmDevice_) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot create GBM device");
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    gbmSurface_ = gbm_surface_create(gbmDevice_, GBM_HELPER_SIZE, GBM_HELPER_SIZE,
                                       GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!gbmSurface_) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot create GBM surface");
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    // Create a GLES2 config with window surface support
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display_, configAttribs, &gles2Config_, 1, &numConfigs) || numConfigs == 0) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: No suitable GLES2 EGL config found");
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    // Create EGL window surface from GBM surface
    gles2Surface_ = eglCreateWindowSurface(display_, gles2Config_,
                                             (EGLNativeWindowType)gbmSurface_, nullptr);
    if (gles2Surface_ == EGL_NO_SURFACE) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot create EGL surface (0x{:04X})",
                  eglGetError());
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    // Create GLES2 context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    gles2Context_ = eglCreateContext(display_, gles2Config_, EGL_NO_CONTEXT, contextAttribs);
    if (gles2Context_ == EGL_NO_CONTEXT) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Failed to create GLES2 context (0x{:04X})",
                  eglGetError());
        eglDestroySurface(display_, gles2Surface_);
        gles2Surface_ = EGL_NO_SURFACE;
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    // Make GLES2 context current to resolve GL functions and compile shader
    if (!eglMakeCurrent(display_, gles2Surface_, gles2Surface_, gles2Context_)) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Failed to make GLES2 context current");
        eglDestroyContext(display_, gles2Context_);
        gles2Context_ = EGL_NO_CONTEXT;
        eglDestroySurface(display_, gles2Surface_);
        gles2Surface_ = EGL_NO_SURFACE;
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    if (!resolveGLES2Functions()) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display_, gles2Context_);
        gles2Context_ = EGL_NO_CONTEXT;
        eglDestroySurface(display_, gles2Surface_);
        gles2Surface_ = EGL_NO_SURFACE;
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    if (!compileBlitShader()) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display_, gles2Context_);
        gles2Context_ = EGL_NO_CONTEXT;
        eglDestroySurface(display_, gles2Surface_);
        gles2Surface_ = EGL_NO_SURFACE;
        eglBindAPI(EGL_OPENGL_API);
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
        close(drmFd_);
        drmFd_ = -1;
        return false;
    }

    // Restore caller's context
    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(display_, savedDraw, savedRead, savedCtx);

    available_ = true;
    LOG_INFO(MOD_GRAPHICS, "GLES2EGLHelper: Initialized (DMA-BUF blit pipeline, GLES2 context {}x{})",
             GBM_HELPER_SIZE, GBM_HELPER_SIZE);
    return true;
}

uint32_t GLES2EGLHelper::uploadETC1AsSharedTexture(
    int width, int height,
    const uint8_t* etc1Data, size_t dataSize,
    EGLContext callerContext, EGLSurface callerSurface)
{
    if (!available_) return 0;

    // --- Step 1: Create GBM buffer object and EGL image from DMA-BUF ---
    // The GBM BO is XRGB8888 (standard format that desktop GL can sample).
    struct gbm_bo* bo = gbm_bo_create(gbmDevice_, width, height,
                                       GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!bo) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot create GBM BO {}x{}", width, height);
        return 0;
    }

    int dmaFd = gbm_bo_get_fd(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    if (dmaFd < 0) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Cannot get DMA-BUF fd");
        gbm_bo_destroy(bo);
        return 0;
    }

    EGLint imgAttrs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)DRM_FORMAT_XRGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT, dmaFd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride,
        EGL_NONE
    };
    EGLImageKHR eglImage = eglCreateImageKHR_(
        display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, imgAttrs);

    if (eglImage == EGL_NO_IMAGE_KHR) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: eglCreateImageKHR(DMA-BUF) failed (0x{:04X})",
                  eglGetError());
        close(dmaFd);
        gbm_bo_destroy(bo);
        return 0;
    }

    // --- Step 2: Switch to GLES2 context, upload ETC1, blit to DMA-BUF FBO ---
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglMakeCurrent(display_, gles2Surface_, gles2Surface_, gles2Context_)) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Failed to switch to GLES2 context");
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(display_, callerSurface, callerSurface, callerContext);
        return 0;
    }

    // Upload ETC1 source texture
    unsigned int srcTex = 0;
    gles2_glGenTextures_(1, &srcTex);
    gles2_glBindTexture_(GL_TEXTURE_2D, srcTex);
    gles2_glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gles2_glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gles2_glCompressedTexImage2D_(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                                   width, height, 0,
                                   static_cast<int>(dataSize), etc1Data);

    unsigned int err = gles2_glGetError_();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: ETC1 upload failed (GL error 0x{:04X})", err);
        gles2_glDeleteTextures_(1, &srcTex);
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(display_, callerSurface, callerSurface, callerContext);
        return 0;
    }

    // Create render target texture from EGL image (backed by GBM BO)
    unsigned int rtTex = 0;
    gles2_glGenTextures_(1, &rtTex);
    gles2_glBindTexture_(GL_TEXTURE_2D, rtTex);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, (void*)eglImage);

    err = gles2_glGetError_();
    if (err != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: EGL image bind to GLES2 render target failed (0x{:04X})", err);
        gles2_glDeleteTextures_(1, &srcTex);
        gles2_glDeleteTextures_(1, &rtTex);
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(display_, callerSurface, callerSurface, callerContext);
        return 0;
    }

    // Create FBO with render target
    unsigned int fbo = 0;
    gles2_glGenFramebuffers_(1, &fbo);
    gles2_glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    gles2_glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtTex, 0);

    unsigned int fbStatus = gles2_glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: FBO incomplete (0x{:04X})", fbStatus);
        gles2_glBindFramebuffer_(GL_FRAMEBUFFER, 0);
        gles2_glDeleteFramebuffers_(1, &fbo);
        gles2_glDeleteTextures_(1, &srcTex);
        gles2_glDeleteTextures_(1, &rtTex);
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(display_, callerSurface, callerSurface, callerContext);
        return 0;
    }

    // Blit ETC1 texture to the RGBA FBO (hardware ETC1 decode)
    // Fullscreen quad: position(x,y) + uv(u,v) interleaved
    static const float quad[] = {
        -1, -1,  0, 0,
         1, -1,  1, 0,
        -1,  1,  0, 1,
         1,  1,  1, 1,
    };

    gles2_glUseProgram_(blitProgram_);
    gles2_glViewport_(0, 0, width, height);

    gles2_glActiveTexture_(GL_TEXTURE0);
    gles2_glBindTexture_(GL_TEXTURE_2D, srcTex);
    gles2_glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gles2_glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gles2_glUniform1i_(gles2_glGetUniformLocation_(blitProgram_, "uTexture"), 0);

    gles2_glEnableVertexAttribArray_(0);
    gles2_glEnableVertexAttribArray_(1);
    gles2_glVertexAttribPointer_(0, 2, GL_FLOAT, 0, 16, quad);
    gles2_glVertexAttribPointer_(1, 2, GL_FLOAT, 0, 16, quad + 2);
    gles2_glDrawArrays_(GL_TRIANGLE_STRIP, 0, 4);
    gles2_glDisableVertexAttribArray_(0);
    gles2_glDisableVertexAttribArray_(1);
    gles2_glFinish_();

    // Clean up GLES2 resources (FBO, textures)
    gles2_glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    gles2_glDeleteFramebuffers_(1, &fbo);
    gles2_glDeleteTextures_(1, &srcTex);
    gles2_glDeleteTextures_(1, &rtTex);

    // --- Step 3: Switch to desktop GL and import the same EGL image ---
    // The EGL image is backed by the GBM BO which now contains decoded RGBA data.
    eglBindAPI(EGL_OPENGL_API);
    if (!eglMakeCurrent(display_, callerSurface, callerSurface, callerContext)) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Failed to restore desktop GL context");
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return 0;
    }

    GLuint glTex = 0;
    glGenTextures(1, &glTex);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, (void*)eglImage);

    GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "GLES2EGLHelper: Desktop GL import failed (GL error 0x{:04X})", glErr);
        glDeleteTextures(1, &glTex);
        eglDestroyImageKHR_(display_, eglImage);
        close(dmaFd);
        gbm_bo_destroy(bo);
        return 0;
    }

    // Set filtering on the desktop GL texture
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // --- Step 4: Retain GBM BO, DMA-BUF fd, and EGL image ---
    // Lima does not properly retain backing storage, so keep these alive.
    RetainedPage page;
    page.bo = bo;
    page.dmaFd = dmaFd;
    page.image = eglImage;
    retainedPages_.push_back(page);

    LOG_INFO(MOD_GRAPHICS, "GLES2EGLHelper: ETC1 {}x{} -> DMA-BUF blit -> GL tex {} (stride={})",
             width, height, glTex, stride);

    return static_cast<uint32_t>(glTex);
}

void GLES2EGLHelper::releaseSharedResources() {
    if (!available_ || retainedPages_.empty()) return;

    LOG_INFO(MOD_GRAPHICS, "GLES2EGLHelper: Releasing {} retained DMA-BUF pages",
             retainedPages_.size());

    for (auto& page : retainedPages_) {
        if (page.image) {
            eglDestroyImageKHR_(display_, page.image);
        }
        if (page.dmaFd >= 0) {
            close(page.dmaFd);
        }
        if (page.bo) {
            gbm_bo_destroy(page.bo);
        }
    }
    retainedPages_.clear();
}

void GLES2EGLHelper::shutdown() {
    if (!available_) return;

    // Release any retained shared resources first
    releaseSharedResources();

    // Delete blit shader (requires GLES2 context)
    if (blitProgram_ && gles2Context_ != EGL_NO_CONTEXT) {
        EGLContext savedCtx = eglGetCurrentContext();
        EGLSurface savedDraw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface savedRead = eglGetCurrentSurface(EGL_READ);

        eglBindAPI(EGL_OPENGL_ES_API);
        eglMakeCurrent(display_, gles2Surface_, gles2Surface_, gles2Context_);
        gles2_glDeleteProgram_(blitProgram_);
        blitProgram_ = 0;

        eglBindAPI(EGL_OPENGL_API);
        eglMakeCurrent(display_, savedDraw, savedRead, savedCtx);
    }

    if (gles2Context_ != EGL_NO_CONTEXT && display_ != EGL_NO_DISPLAY) {
        EGLContext savedCtx = eglGetCurrentContext();
        EGLSurface savedDraw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface savedRead = eglGetCurrentSurface(EGL_READ);

        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display_, gles2Context_);
        gles2Context_ = EGL_NO_CONTEXT;

        if (gles2Surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, gles2Surface_);
            gles2Surface_ = EGL_NO_SURFACE;
        }

        // Restore previous context if it wasn't the one we just destroyed
        if (savedCtx != EGL_NO_CONTEXT) {
            eglMakeCurrent(display_, savedDraw, savedRead, savedCtx);
        }
    }

    // Clean up GBM resources
    if (gbmSurface_) {
        gbm_surface_destroy(gbmSurface_);
        gbmSurface_ = nullptr;
    }
    if (gbmDevice_) {
        gbm_device_destroy(gbmDevice_);
        gbmDevice_ = nullptr;
    }
    if (drmFd_ >= 0) {
        close(drmFd_);
        drmFd_ = -1;
    }

    available_ = false;
    LOG_INFO(MOD_GRAPHICS, "GLES2EGLHelper: Shut down");
}

} // namespace Graphics
} // namespace EQT
