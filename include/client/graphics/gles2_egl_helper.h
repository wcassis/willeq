#ifndef EQT_GRAPHICS_GLES2_EGL_HELPER_H
#define EQT_GRAPHICS_GLES2_EGL_HELPER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <EGL/egl.h>
#include <EGL/eglext.h>

// Forward declarations for GBM (avoid including gbm.h in header)
struct gbm_device;
struct gbm_surface;
struct gbm_bo;

namespace EQT {
namespace Graphics {

// Manages a GLES2 context on the same EGL display as the main desktop GL
// context. Provides ETC1 upload + DMA-BUF blit pipeline for hardware-
// decoded compressed textures on Mali 400 (Lima driver).
//
// Desktop GL 2.1 on Lima rejects glCompressedTexImage2D(GL_ETC1_RGB8_OES)
// with GL_INVALID_ENUM, but GLES2 can upload ETC1 natively. Direct EGL image
// sharing of ETC1 textures produces textures with invalid texBaseFormat (0xffff)
// in desktop GL, so we use a DMA-BUF render-to-buffer approach:
//
//   1. Upload ETC1 in GLES2 (hardware decode on Mali 400)
//   2. Create a GBM buffer (XRGB8888) and EGL image from its DMA-BUF
//   3. Render ETC1 to the GBM-backed FBO via a fullscreen quad (GLES2 blit)
//   4. Import the same EGL image in desktop GL → standard RGBA format texture
//   5. Irrlicht renders normally using the desktop GL texture
//
// This adds one fullscreen blit per atlas page at zone load time (not per frame).
class GLES2EGLHelper {
public:
    GLES2EGLHelper() = default;
    ~GLES2EGLHelper();

    // Initialize with the EGL display from CIrrDeviceFB.
    // Creates a surface-backed GLES2 context and compiles the blit shader.
    bool init(EGLDisplay display);

    // Upload ETC1 data via GLES2 hardware decode, blit to RGBA DMA-BUF,
    // and return a desktop GL texture handle. Returns 0 on failure.
    // The caller's GL context is restored after this call.
    uint32_t uploadETC1AsSharedTexture(
        int width, int height,
        const uint8_t* etc1Data, size_t dataSize,
        EGLContext callerContext, EGLSurface callerSurface);

    bool isAvailable() const { return available_; }

    // Release all retained DMA-BUF resources from previous uploads.
    // Call this when the atlas is unloaded (zone change).
    void releaseSharedResources();

    void shutdown();

private:
    bool available_ = false;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext gles2Context_ = EGL_NO_CONTEXT;
    EGLConfig gles2Config_ = nullptr;
    EGLSurface gles2Surface_ = EGL_NO_SURFACE;

    // GBM resources for the GLES2 surface (Lima requires surface-backed context)
    int drmFd_ = -1;
    struct gbm_device* gbmDevice_ = nullptr;
    struct gbm_surface* gbmSurface_ = nullptr;

    // GLES2 blit shader (compile once, used for all atlas page uploads)
    unsigned int blitProgram_ = 0;

    // EGL image function pointers
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ = nullptr;

    // glEGLImageTargetTexture2DOES (works in both GLES2 and desktop GL contexts)
    typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(unsigned int, void*);
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = nullptr;

    // GLES2 GL function pointers (resolved while GLES2 context is current)
    typedef void (*PFN_glGenTextures)(int, unsigned int*);
    typedef void (*PFN_glDeleteTextures)(int, const unsigned int*);
    typedef void (*PFN_glBindTexture)(unsigned int, unsigned int);
    typedef void (*PFN_glCompressedTexImage2D)(unsigned int, int, unsigned int,
                                                int, int, int, int, const void*);
    typedef void (*PFN_glTexParameteri)(unsigned int, unsigned int, int);
    typedef unsigned int (*PFN_glGetError)();
    typedef void (*PFN_glViewport)(int, int, int, int);
    typedef void (*PFN_glActiveTexture)(unsigned int);
    typedef void (*PFN_glFinish)();

    // Shader function pointers
    typedef unsigned int (*PFN_glCreateShader)(unsigned int);
    typedef void (*PFN_glDeleteShader)(unsigned int);
    typedef void (*PFN_glShaderSource)(unsigned int, int, const char* const*, const int*);
    typedef void (*PFN_glCompileShader)(unsigned int);
    typedef void (*PFN_glGetShaderiv)(unsigned int, unsigned int, int*);
    typedef unsigned int (*PFN_glCreateProgram)();
    typedef void (*PFN_glDeleteProgram)(unsigned int);
    typedef void (*PFN_glAttachShader)(unsigned int, unsigned int);
    typedef void (*PFN_glLinkProgram)(unsigned int);
    typedef void (*PFN_glGetProgramiv)(unsigned int, unsigned int, int*);
    typedef void (*PFN_glUseProgram)(unsigned int);
    typedef int (*PFN_glGetUniformLocation)(unsigned int, const char*);
    typedef void (*PFN_glUniform1i)(int, int);
    typedef void (*PFN_glEnableVertexAttribArray)(unsigned int);
    typedef void (*PFN_glDisableVertexAttribArray)(unsigned int);
    typedef void (*PFN_glVertexAttribPointer)(unsigned int, int, unsigned int,
                                               unsigned char, int, const void*);
    typedef void (*PFN_glDrawArrays)(unsigned int, int, int);

    // FBO function pointers
    typedef void (*PFN_glGenFramebuffers)(int, unsigned int*);
    typedef void (*PFN_glDeleteFramebuffers)(int, const unsigned int*);
    typedef void (*PFN_glBindFramebuffer)(unsigned int, unsigned int);
    typedef void (*PFN_glFramebufferTexture2D)(unsigned int, unsigned int,
                                                unsigned int, unsigned int, int);
    typedef unsigned int (*PFN_glCheckFramebufferStatus)(unsigned int);

    // Core GL
    PFN_glGenTextures gles2_glGenTextures_ = nullptr;
    PFN_glDeleteTextures gles2_glDeleteTextures_ = nullptr;
    PFN_glBindTexture gles2_glBindTexture_ = nullptr;
    PFN_glCompressedTexImage2D gles2_glCompressedTexImage2D_ = nullptr;
    PFN_glTexParameteri gles2_glTexParameteri_ = nullptr;
    PFN_glGetError gles2_glGetError_ = nullptr;
    PFN_glViewport gles2_glViewport_ = nullptr;
    PFN_glActiveTexture gles2_glActiveTexture_ = nullptr;
    PFN_glFinish gles2_glFinish_ = nullptr;

    // Shader
    PFN_glCreateShader gles2_glCreateShader_ = nullptr;
    PFN_glDeleteShader gles2_glDeleteShader_ = nullptr;
    PFN_glShaderSource gles2_glShaderSource_ = nullptr;
    PFN_glCompileShader gles2_glCompileShader_ = nullptr;
    PFN_glGetShaderiv gles2_glGetShaderiv_ = nullptr;
    PFN_glCreateProgram gles2_glCreateProgram_ = nullptr;
    PFN_glDeleteProgram gles2_glDeleteProgram_ = nullptr;
    PFN_glAttachShader gles2_glAttachShader_ = nullptr;
    PFN_glLinkProgram gles2_glLinkProgram_ = nullptr;
    PFN_glGetProgramiv gles2_glGetProgramiv_ = nullptr;
    PFN_glUseProgram gles2_glUseProgram_ = nullptr;
    PFN_glGetUniformLocation gles2_glGetUniformLocation_ = nullptr;
    PFN_glUniform1i gles2_glUniform1i_ = nullptr;
    PFN_glEnableVertexAttribArray gles2_glEnableVertexAttribArray_ = nullptr;
    PFN_glDisableVertexAttribArray gles2_glDisableVertexAttribArray_ = nullptr;
    PFN_glVertexAttribPointer gles2_glVertexAttribPointer_ = nullptr;
    PFN_glDrawArrays gles2_glDrawArrays_ = nullptr;

    // FBO
    PFN_glGenFramebuffers gles2_glGenFramebuffers_ = nullptr;
    PFN_glDeleteFramebuffers gles2_glDeleteFramebuffers_ = nullptr;
    PFN_glBindFramebuffer gles2_glBindFramebuffer_ = nullptr;
    PFN_glFramebufferTexture2D gles2_glFramebufferTexture2D_ = nullptr;
    PFN_glCheckFramebufferStatus gles2_glCheckFramebufferStatus_ = nullptr;

    // Retained resources (Lima requires these to stay alive while desktop GL textures are in use)
    struct RetainedPage {
        struct gbm_bo* bo = nullptr;
        int dmaFd = -1;
        EGLImageKHR image = nullptr;
    };
    std::vector<RetainedPage> retainedPages_;

    bool resolveGLES2Functions();
    bool compileBlitShader();
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_GLES2_EGL_HELPER_H
