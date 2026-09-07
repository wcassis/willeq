// shader_perpixel_egl.cpp — EGL context creation (DRM/GBM or X11) and GLSL
// compile/link helpers for gles2_shader_perpixel_benchmark (T447; bodies
// moved verbatim from tools/hwcaps/gles2_shader_perpixel_benchmark.cpp).

#include "hwcaps/shader_perpixel/shader_perpixel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#endif

// ============================================================================
// EGL/DRM setup (reused from gles2_etc1_benchmark pattern)
// ============================================================================


#ifdef EQT_HAS_DRM
bool initDRM(EGLState& state) {
    const char* devices[] = { "/dev/dri/card0", "/dev/dri/card1", nullptr };
    for (int i = 0; devices[i]; i++) {
        state.drmFd = open(devices[i], O_RDWR);
        if (state.drmFd >= 0) break;
    }
    if (state.drmFd < 0) {
        fprintf(stderr, "DRM: could not open any /dev/dri/card* device\n");
        return false;
    }

    // Find connector and mode
    drmModeRes* res = drmModeGetResources(state.drmFd);
    if (!res) {
        fprintf(stderr, "DRM: drmModeGetResources failed\n");
        close(state.drmFd);
        return false;
    }

    for (int i = 0; i < res->count_connectors && !state.modeFound; i++) {
        drmModeConnector* conn = drmModeGetConnector(state.drmFd, res->connectors[i]);
        if (!conn || conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            if (conn) drmModeFreeConnector(conn);
            continue;
        }
        state.connectorId = conn->connector_id;

        // Find matching mode
        for (int m = 0; m < conn->count_modes; m++) {
            if ((int)conn->modes[m].hdisplay == state.width &&
                (int)conn->modes[m].vdisplay == state.height) {
                state.mode = conn->modes[m];
                state.modeFound = true;
                break;
            }
        }
        if (!state.modeFound && conn->count_modes > 0) {
            // Fallback to first mode
            state.mode = conn->modes[0];
            state.width = state.mode.hdisplay;
            state.height = state.mode.vdisplay;
            state.modeFound = true;
        }

        // Find CRTC
        drmModeEncoder* enc = drmModeGetEncoder(state.drmFd, conn->encoder_id);
        if (enc) {
            state.crtcId = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);

    if (!state.modeFound) {
        fprintf(stderr, "DRM: no suitable mode found\n");
        close(state.drmFd);
        return false;
    }

    printf("DRM: Using mode %dx%d@%dHz\n", state.mode.hdisplay, state.mode.vdisplay, state.mode.vrefresh);

    state.gbmDevice = gbm_create_device(state.drmFd);
    if (!state.gbmDevice) {
        fprintf(stderr, "DRM: gbm_create_device failed\n");
        close(state.drmFd);
        return false;
    }

    state.display = eglGetDisplay((EGLNativeDisplayType)state.gbmDevice);
    if (state.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "DRM: eglGetDisplay failed\n");
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        fprintf(stderr, "DRM: eglInitialize failed\n");
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    printf("EGL %d.%d (DRM/GBM, GLES2)\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "DRM: eglBindAPI failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "DRM: eglChooseConfig failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.gbmSurface = gbm_surface_create(state.gbmDevice, state.width, state.height,
                                            GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!state.gbmSurface) {
        fprintf(stderr, "DRM: gbm_surface_create failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.surface = eglCreateWindowSurface(state.display, state.config,
                                            (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "DRM: eglCreateContext failed (0x%x)\n", eglGetError());
        eglTerminate(state.display);
        gbm_surface_destroy(state.gbmSurface);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    state.isDRM = true;
    return true;
}

// Page flip for DRM (presents to display)
void drmPresent(EGLState& state) {
    eglSwapBuffers(state.display, state.surface);
    struct gbm_bo* bo = gbm_surface_lock_front_buffer(state.gbmSurface);
    if (!bo) return;

    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t fb = 0;
    drmModeAddFB(state.drmFd, state.width, state.height, 24, 32, stride, handle, &fb);

    static bool first = true;
    if (first) {
        drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode);
        first = false;
    } else {
        int ret = drmModePageFlip(state.drmFd, state.crtcId, fb, 0, nullptr);
        if (ret) drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode);
    }

    static struct gbm_bo* prevBo = nullptr;
    static uint32_t prevFb = 0;
    if (prevBo) {
        drmModeRmFB(state.drmFd, prevFb);
        gbm_surface_release_buffer(state.gbmSurface, prevBo);
    }
    prevBo = bo;
    prevFb = fb;
}
#endif

bool initX11(EGLState& state) {
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) return false;

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) return false;

    printf("EGL %d.%d (X11/default, GLES2)\n", major, minor);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs) || numConfigs == 0) {
        eglTerminate(state.display);
        return false;
    }

    EGLint pbufferAttribs[] = { EGL_WIDTH, state.width, EGL_HEIGHT, state.height, EGL_NONE };
    state.surface = eglCreatePbufferSurface(state.display, state.config, pbufferAttribs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        eglTerminate(state.display);
        return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    return true;
}

void cleanup(EGLState& state) {
    eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(state.display, state.context);
    eglDestroySurface(state.display, state.surface);
    eglTerminate(state.display);
#ifdef EQT_HAS_DRM
    if (state.isDRM) {
        if (state.gbmSurface) gbm_surface_destroy(state.gbmSurface);
        if (state.gbmDevice) gbm_device_destroy(state.gbmDevice);
        if (state.drmFd >= 0) close(state.drmFd);
    }
#endif
}

// ============================================================================
// Shader compilation
// ============================================================================

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glBindAttribLocation(prog, 1, "aNormal");
    glBindAttribLocation(prog, 2, "aColor");
    glBindAttribLocation(prog, 3, "aTexCoord0");
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
