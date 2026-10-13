// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright.txt in the distribution
// or at http://irrlicht.sourceforge.net/license/

// DRM/KMS/GBM/EGL replacement for CIrrDeviceFB
// Provides EDT_OPENGL support via EGL on GBM surfaces with DRM/KMS page flipping.
// No X11 required - renders directly to the display via kernel mode setting.

#ifndef __APPLE__ // B1043 (#363): Linux-only (DRM/GBM/evdev/VT/X11); Darwin stubs in CIrrDeviceFB_Darwin.cpp
// linux/input.h MUST be included FIRST, before any Irrlicht headers.
// Both linux/input.h and Irrlicht's Keycodes.h define KEY_SPACE, KEY_TAB,
// KEY_MINUS, KEY_F1, etc. - Linux as preprocessor macros (#define KEY_SPACE 57),
// Irrlicht as enum values (KEY_SPACE = 0x20). The macros would shadow the enum.
// Strategy: include linux/input.h first, save needed Linux values under LK_ prefix,
// then #undef the conflicting macros before Irrlicht headers are included.
#include <linux/input.h>

// Save Linux evdev key codes that conflict with Irrlicht EKEY_CODE enum names
enum LinuxKey {
    LK_TAB      = KEY_TAB,       // 15  (Irrlicht KEY_TAB = 0x09)
    LK_SPACE    = KEY_SPACE,     // 57  (Irrlicht KEY_SPACE = 0x20)
    LK_MINUS    = KEY_MINUS,     // 12  (Irrlicht KEY_MINUS = 0xBD)
    LK_COMMA    = KEY_COMMA,     // 51  (Irrlicht KEY_COMMA = 0xBC)
    LK_HOME     = KEY_HOME,      // 102 (Irrlicht KEY_HOME = 0x24)
    LK_END      = KEY_END,       // 107 (Irrlicht KEY_END = 0x23)
    LK_UP       = KEY_UP,        // 103 (Irrlicht KEY_UP = 0x26)
    LK_DOWN     = KEY_DOWN,      // 108 (Irrlicht KEY_DOWN = 0x28)
    LK_LEFT     = KEY_LEFT,      // 105 (Irrlicht KEY_LEFT = 0x25)
    LK_RIGHT    = KEY_RIGHT,     // 106 (Irrlicht KEY_RIGHT = 0x27)
    LK_INSERT   = KEY_INSERT,    // 110 (Irrlicht KEY_INSERT = 0x2D)
    LK_DELETE   = KEY_DELETE,    // 111 (Irrlicht KEY_DELETE = 0x2E)
    LK_NUMLOCK  = KEY_NUMLOCK,   // 69  (Irrlicht KEY_NUMLOCK = 0x90)
    LK_F1  = KEY_F1,  LK_F2  = KEY_F2,  LK_F3  = KEY_F3,  LK_F4  = KEY_F4,
    LK_F5  = KEY_F5,  LK_F6  = KEY_F6,  LK_F7  = KEY_F7,  LK_F8  = KEY_F8,
    LK_F9  = KEY_F9,  LK_F10 = KEY_F10, LK_F11 = KEY_F11, LK_F12 = KEY_F12,
};

// Undefine ALL Linux KEY_* macros that clash with Irrlicht EKEY_CODE enum names.
// If any are left active when Keycodes.h is parsed, enum member names get
// macro-expanded (e.g., KEY_CANCEL becomes 223), corrupting the enum definition.
#undef KEY_BACK
#undef KEY_TAB
#undef KEY_CANCEL
#undef KEY_CLEAR
#undef KEY_MENU
#undef KEY_PAUSE
#undef KEY_SPACE
#undef KEY_NEXT
#undef KEY_HOME
#undef KEY_END
#undef KEY_LEFT
#undef KEY_UP
#undef KEY_RIGHT
#undef KEY_DOWN
#undef KEY_SELECT
#undef KEY_PRINT
#undef KEY_INSERT
#undef KEY_DELETE
#undef KEY_HELP
#undef KEY_SLEEP
#undef KEY_HANGUEL
#undef KEY_HANJA
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_F13
#undef KEY_F14
#undef KEY_F15
#undef KEY_F16
#undef KEY_F17
#undef KEY_F18
#undef KEY_F19
#undef KEY_F20
#undef KEY_F21
#undef KEY_F22
#undef KEY_F23
#undef KEY_F24
#undef KEY_NUMLOCK
#undef KEY_MINUS
#undef KEY_COMMA
#undef KEY_PLAY
#undef KEY_ZOOM
#endif // !__APPLE__

// NOW safe to include Irrlicht headers (EKEY_CODE enum values won't be shadowed)
#include "CIrrDeviceFB.h"

#ifdef _IRR_COMPILE_WITH_FB_DEVICE_

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>
#ifndef __APPLE__ // B1043: Linux-only
#include <linux/kd.h>
#include <linux/vt.h>
#endif // !__APPLE__

#include "IEventReceiver.h"
#include "os.h"
#include "CTimer.h"
#include "gpu_sync_probe.h"
#include "COpenGLES2Driver.h"
#include <chrono>

// P25: X11 headers for the opt-in windowed EGL backend. Included after all
// Irrlicht headers (same ordering discipline as linux/input.h above) since
// Xlib.h defines plain macros/typedefs (None, Bool, Status, True, False)
// that could otherwise shadow identifiers parsed earlier. Nothing in this
// file uses those names for its own symbols, so no #undef dance is needed
// here — X11 headers just have to come last.
#ifndef __APPLE__ // B1043: Linux-only
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif // !__APPLE__

#ifndef __APPLE__ // B1043: Linux-only
// Signal handler to restore VT keyboard mode on abnormal exit.
// Without this, SIGINT/SIGTERM/SIGSEGV leaves the keyboard stuck in K_OFF mode
// and the console becomes unresponsive to local keyboard input.
static int s_savedTtyFd = -1;
static int s_savedKbMode = -1;

static void restoreKeyboardMode()
{
    if (s_savedTtyFd >= 0 && s_savedKbMode >= 0) {
        ioctl(s_savedTtyFd, KDSKBMODE, s_savedKbMode);
        s_savedTtyFd = -1;
        s_savedKbMode = -1;
    }
}

static void restoreKeyboardSignalHandler(int sig)
{
    restoreKeyboardMode();
    // Re-raise with default handler so the process actually exits
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif // !__APPLE__

// Software driver factory functions are declared in CIrrDeviceStub.h (base class)
// OpenGL driver factory function is declared locally below

namespace irr
{

#ifdef _IRR_COMPILE_WITH_OPENGL_
// Forward declaration of factory function (implemented via patch in COpenGLDriver.cpp)
namespace video
{
    IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
        io::IFileSystem* io, CIrrDeviceFB* device);
}
#endif

#ifdef _IRR_COMPILE_WITH_OGLES2_
namespace video
{
    IVideoDriver* createOpenGLES2Driver(const SIrrlichtCreationParameters& params,
        io::IFileSystem* io, CIrrDeviceFB* device);
}
#endif

// ============================================================
// Constructor / Destructor
// ============================================================

CIrrDeviceFB::CIrrDeviceFB(const SIrrlichtCreationParameters& param)
    : CIrrDeviceStub(param),
      drmFd_(-1), crtcId_(0), connectorId_(0), savedCrtc_(nullptr),
      gbmDevice_(nullptr), gbmSurface_(nullptr),
      previousBo_(nullptr),
      eglDisplay_(EGL_NO_DISPLAY), eglSurface_(EGL_NO_SURFACE),
      eglContext_(EGL_NO_CONTEXT), eglConfig_(nullptr),
      ttyFd_(-1), savedKbMode_(-1),
      keyboardFd_(-1), mouseFd_(-1),
      shiftDown_(false), ctrlDown_(false), altDown_(false),
      Close(false), firstFrame_(true), flipPending_(false), useGLES2_(false),
      surfaceless_(false),
      x11Display_(nullptr), x11Window_(0), x11WmDeleteMsg_(0), x11Active_(false)
{
    Width = param.WindowSize.Width;
    Height = param.WindowSize.Height;

    memset(keyIsDown_, 0, sizeof(keyIsDown_));
    memset(mouseButtonState_, 0, sizeof(mouseButtonState_));
    memset(keyMap_, 0, sizeof(keyMap_));

    mousePos_.X = Width / 2;
    mousePos_.Y = Height / 2;

    initKeyMap();

    // P27: EDT_NULL (headless/no-GPU tests, e.g. RaceModelAnimationHeadlessTest)
    // must skip DRM/GBM/EGL/evdev probing entirely, exactly like stock Irrlicht's
    // CIrrDeviceLinux/CIrrDeviceConsole guard every hardware-init step on
    // `DriverType != EDT_NULL`. This device previously ran the full DRM/EGL
    // pipeline unconditionally (hence spurious "/dev/dri/... Permission denied"
    // warnings even under EDT_NULL) and createDriver()'s switch had no EDT_NULL
    // case, so VideoDriver stayed null and createGUIAndScene() below was
    // skipped — leaving getSceneManager()/getVideoDriver() null on a device
    // the caller was told succeeded (Irrlicht.cpp's createDeviceEx() never
    // closes an EDT_NULL device even if getVideoDriver() is null). See
    // docker/irrlicht-drm-patch.py / build-gles2-x86.sh --irrlicht-only for
    // how this file lands in the built tree.
    if (CreationParams.DriverType != video::EDT_NULL) {
        bool eglOk = false;

        // P25: explicit opt-in X11-windowed EGL backend. WILLEQ_EGL_X11 is only
        // ever set by irrlicht_renderer.cpp, only when the --egl-x11 CLI flag
        // was passed — never derived from $DISPLAY, so surfaceless remains the
        // default and this branch is inert for CI/goldens/benchmarks. Skips the
        // DRM/GBM path entirely when requested. (An env var, not a direct call,
        // because application code doesn't have this private device header on
        // its include path — see the file-header comment above.)
        bool x11Requested = (getenv("WILLEQ_EGL_X11") != nullptr);
        if (x11Requested) {
            eglOk = initEGLX11();
            if (eglOk) {
                x11Active_ = true;
            } else {
                os::Printer::log("--egl-x11 requested but X11/EGL init failed - falling back to DRM/surfaceless path", ELL_ERROR);
            }
        }

        if (!eglOk) {
            // Setup DRM/GBM/EGL pipeline
            bool drmOk = initDRM();
            bool gbmOk = drmOk ? initGBM() : false;
            eglOk = gbmOk ? initEGL() : false;

            if (!eglOk) {
                // x86/Xvfb fallback: no real DRM/KMS display available (headless dev VM,
                // no /dev/dri permission, or no connected connector). Fall back to a
                // Mesa surfaceless EGL platform + pbuffer surface so the real
                // COpenGLES2Driver still gets a genuine llvmpipe GLES2 context.
#ifdef __APPLE__
                os::Printer::log("DRM unavailable - using ANGLE/Metal pbuffer EGL", ELL_INFORMATION);
#else
                os::Printer::log("DRM/GBM/EGL initialization failed - falling back to surfaceless EGL (x86/Xvfb)", ELL_WARNING);
#endif
                eglOk = initEGLSurfaceless();
                if (eglOk) {
                    surfaceless_ = true;
                } else {
                    os::Printer::log("Surfaceless EGL fallback also failed", ELL_ERROR);
                }
            }
        }

        // Open evdev input devices. Not used in X11-windowed mode: input comes
        // from X11 events instead (pollX11Events()) - see run(). evdev also
        // requires /dev/input access, which X11/Xvfb sessions don't grant.
        if (!x11Active_) {
            initEvdev();
        }
    }

    // Create cursor control
    CursorControl = new CCursorControl(this);

    // Create video driver
    createDriver();

    if (VideoDriver)
        createGUIAndScene();
}

CIrrDeviceFB::~CIrrDeviceFB()
{
    // T61: flush any deferred present() before EGL/DRM teardown below --
    // see the ARM CIrrDeviceFB.cpp's identical block (this file is a full
    // fork, not shared) and COpenGLES2Driver::endScene()'s comment for why.
    // A no-op on this fork's own present() (already a harmless swap), but
    // keeps pendingPresent_ from being left permanently true, and matches
    // the DRM/GBM path exactly in case this fork's DRM branch (non-X11,
    // non-surfaceless) is ever exercised.
    if (VideoDriver && CreationParams.DriverType == video::EDT_OGLES2) {
        static_cast<video::COpenGLES2Driver*>(VideoDriver)->flushPendingPresent();
    }

    // Destroy EGL
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT)
            eglDestroyContext(eglDisplay_, eglContext_);
        if (eglSurface_ != EGL_NO_SURFACE)
            eglDestroySurface(eglDisplay_, eglSurface_);
        eglTerminate(eglDisplay_);
    }

#ifndef __APPLE__ // B1043: Linux-only
    // Release GBM (cached DRM FBs are cleaned up by gbm_bo user_data destructors)
    if (previousBo_) {
        gbm_surface_release_buffer(gbmSurface_, previousBo_);
    }
    if (gbmSurface_)
        gbm_surface_destroy(gbmSurface_);
    if (gbmDevice_)
        gbm_device_destroy(gbmDevice_);

    // Restore original CRTC
    if (savedCrtc_) {
        drmModeSetCrtc(drmFd_, savedCrtc_->crtc_id, savedCrtc_->buffer_id,
                       savedCrtc_->x, savedCrtc_->y, &connectorId_, 1, &savedCrtc_->mode);
        drmModeFreeCrtc(savedCrtc_);
    }

    // Close DRM
    if (drmFd_ >= 0)
        close(drmFd_);
#endif // !__APPLE__

    // P25: close the X11 window/display if the windowed EGL backend was used
    if (x11Active_) {
        closeX11Window();
    }

#ifndef __APPLE__ // B1043: Linux-only
    // Restore VT keyboard mode before closing evdev
    if (ttyFd_ >= 0) {
        if (savedKbMode_ >= 0)
            ioctl(ttyFd_, KDSKBMODE, savedKbMode_);
        // Clear signal handler state so it becomes a no-op
        s_savedTtyFd = -1;
        s_savedKbMode = -1;
        close(ttyFd_);
    }
#endif // !__APPLE__

    // Close evdev
    if (keyboardFd_ >= 0)
        close(keyboardFd_);
    if (mouseFd_ >= 0)
        close(mouseFd_);
}

#ifndef __APPLE__ // B1043: Linux-only
// ============================================================
// DRM Initialization
// ============================================================

bool CIrrDeviceFB::initDRM()
{
    // Try /dev/dri/card0 first, then card1
    const char* cards[] = { "/dev/dri/card0", "/dev/dri/card1", nullptr };
    for (int i = 0; cards[i]; i++) {
        drmFd_ = open(cards[i], O_RDWR | O_CLOEXEC);
        if (drmFd_ >= 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "DRM: Opened %s (fd=%d)", cards[i], drmFd_);
            os::Printer::log(msg, ELL_INFORMATION);
            break;
        }
    }

    if (drmFd_ < 0) {
        os::Printer::log("DRM: Failed to open /dev/dri/card*", ELL_ERROR);
        return false;
    }

    drmModeRes* resources = drmModeGetResources(drmFd_);
    if (!resources) {
        os::Printer::log("DRM: drmModeGetResources failed", ELL_ERROR);
        return false;
    }

    // Find a connected connector
    drmModeConnector* connector = nullptr;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drmFd_, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
            break;
        if (connector)
            drmModeFreeConnector(connector);
        connector = nullptr;
    }

    if (!connector) {
        os::Printer::log("DRM: No connected connector found", ELL_ERROR);
        drmModeFreeResources(resources);
        return false;
    }

    connectorId_ = connector->connector_id;

    // Log available modes
    for (int i = 0; i < connector->count_modes; i++) {
        char modeInfo[128];
        snprintf(modeInfo, sizeof(modeInfo), "DRM: Available mode [%d]: %ux%u@%uHz%s",
                 i, connector->modes[i].hdisplay, connector->modes[i].vdisplay,
                 connector->modes[i].vrefresh,
                 (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) ? " (preferred)" : "");
        os::Printer::log(modeInfo, ELL_INFORMATION);
    }

    // Try to find a mode matching the requested resolution
    drmModeModeInfo* selectedMode = nullptr;
    u32 requestedW = CreationParams.WindowSize.Width;
    u32 requestedH = CreationParams.WindowSize.Height;

    if (requestedW > 0 && requestedH > 0) {
        // Exact match with highest refresh rate
        for (int i = 0; i < connector->count_modes; i++) {
            if (connector->modes[i].hdisplay == requestedW &&
                connector->modes[i].vdisplay == requestedH) {
                if (!selectedMode || connector->modes[i].vrefresh > selectedMode->vrefresh)
                    selectedMode = &connector->modes[i];
            }
        }
        if (selectedMode) {
            char msg[128];
            snprintf(msg, sizeof(msg), "DRM: Found matching mode %ux%u@%uHz for requested %ux%u",
                     selectedMode->hdisplay, selectedMode->vdisplay, selectedMode->vrefresh,
                     requestedW, requestedH);
            os::Printer::log(msg, ELL_INFORMATION);
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "DRM: No mode matching requested %ux%u, using preferred",
                     requestedW, requestedH);
            os::Printer::log(msg, ELL_WARNING);
        }
    }

    // Fall back to preferred mode, then first mode
    if (!selectedMode) {
        for (int i = 0; i < connector->count_modes; i++) {
            if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
                selectedMode = &connector->modes[i];
                break;
            }
        }
    }
    if (!selectedMode)
        selectedMode = &connector->modes[0];

    mode_ = *selectedMode;
    Width = mode_.hdisplay;
    Height = mode_.vdisplay;
    CreationParams.WindowSize.Width = Width;
    CreationParams.WindowSize.Height = Height;

    char modeMsg[128];
    snprintf(modeMsg, sizeof(modeMsg), "DRM: Using mode %ux%u@%uHz",
             mode_.hdisplay, mode_.vdisplay, mode_.vrefresh);
    os::Printer::log(modeMsg, ELL_INFORMATION);

    // Find CRTC
    drmModeEncoder* encoder = nullptr;
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(drmFd_, connector->encoder_id);
    }

    if (encoder) {
        crtcId_ = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    } else {
        // Find a CRTC that can drive this connector
        for (int i = 0; i < resources->count_encoders; i++) {
            encoder = drmModeGetEncoder(drmFd_, resources->encoders[i]);
            if (!encoder) continue;
            for (int j = 0; j < resources->count_crtcs; j++) {
                if (encoder->possible_crtcs & (1 << j)) {
                    crtcId_ = resources->crtcs[j];
                    break;
                }
            }
            drmModeFreeEncoder(encoder);
            if (crtcId_) break;
        }
    }

    if (!crtcId_) {
        os::Printer::log("DRM: No CRTC found", ELL_ERROR);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        return false;
    }

    // Save current CRTC for restoration on exit
    savedCrtc_ = drmModeGetCrtc(drmFd_, crtcId_);

    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    os::Printer::log("DRM: Initialization successful", ELL_INFORMATION);
    return true;
}

// ============================================================
// GBM Initialization
// ============================================================

bool CIrrDeviceFB::initGBM()
{
    gbmDevice_ = gbm_create_device(drmFd_);
    if (!gbmDevice_) {
        os::Printer::log("GBM: Failed to create device", ELL_ERROR);
        return false;
    }

    gbmSurface_ = gbm_surface_create(gbmDevice_,
        Width, Height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    if (!gbmSurface_) {
        os::Printer::log("GBM: Failed to create surface", ELL_ERROR);
        return false;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "GBM: Surface created %ux%u XRGB8888", Width, Height);
    os::Printer::log(msg, ELL_INFORMATION);
    return true;
}

// ============================================================
// EGL Initialization
// ============================================================

// B1030 (#350): pick the EGL config whose native visual IS the GBM surface's
// format. eglChooseConfig sorts deeper colour buffers first, so on a driver
// that exposes 10-bit configs (Panfrost/Mesa 26 on the ROCK 4D lists AR30 and
// XR30 before AR24/XR24) the first match for RED/GREEN/BLUE >= 8 is a 10-bit
// visual, and eglCreateWindowSurface on the XRGB8888 gbm_surface then fails
// (Mesa refuses a config/format mismatch). Lima never listed a 10-bit config,
// which is why the single-config call worked on every lane until now. Same
// rule kmscube applies: EGL_NATIVE_VISUAL_ID == the GBM format. Within the
// matching visual eglChooseConfig's own order still holds (smallest depth and
// stencil that satisfy the request first), so lanes 1-2 pick the config they
// always picked.
static bool chooseConfigForGbmFormat(EGLDisplay dpy, const EGLint* attribs,
                                     uint32_t gbmFormat, EGLConfig* out)
{
    EGLConfig configs[64];
    EGLint numConfigs = 0;
    if (!eglChooseConfig(dpy, attribs, configs, 64, &numConfigs) || numConfigs < 1)
        return false;
    for (EGLint i = 0; i < numConfigs; ++i) {
        EGLint visual = 0;
        if (eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &visual)
            && (uint32_t)visual == gbmFormat) {
            *out = configs[i];
            return true;
        }
    }
    os::Printer::log("EGL: no config matches the GBM surface format; using the first match", ELL_WARNING);
    *out = configs[0];
    return true;
}

bool CIrrDeviceFB::initEGL()
{
    // Try EGL_OPENGL_API first (desktop GL 2.1 via Lima), fallback to GLES2
    eglDisplay_ = eglGetDisplay((EGLNativeDisplayType)gbmDevice_);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        os::Printer::log("EGL: eglGetDisplay failed", ELL_ERROR);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(eglDisplay_, &major, &minor)) {
        os::Printer::log("EGL: eglInitialize failed", ELL_ERROR);
        return false;
    }

    char verMsg[128];
    snprintf(verMsg, sizeof(verMsg), "EGL: Version %d.%d", major, minor);
    os::Printer::log(verMsg, ELL_INFORMATION);

    // If GLES2 mode is requested (EDT_OGLES2), go straight to ES API.
    // Otherwise try desktop OpenGL first (Lima provides GL 2.1).
    bool useDesktopGL = true;
    if (useGLES2_ || CreationParams.DriverType == video::EDT_OGLES2) {
        useGLES2_ = true;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            os::Printer::log("EGL: EGL_OPENGL_ES_API not available", ELL_ERROR);
            return false;
        }
        useDesktopGL = false;
    } else if (!eglBindAPI(EGL_OPENGL_API)) {
        os::Printer::log("EGL: EGL_OPENGL_API not available, trying GLES2", ELL_WARNING);
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            os::Printer::log("EGL: No GL API available", ELL_ERROR);
            return false;
        }
        useDesktopGL = false;
    }

    os::Printer::log(useDesktopGL ? "EGL: Using desktop OpenGL API" : "EGL: Using OpenGL ES API",
                     ELL_INFORMATION);

    // Choose EGL config
    // B971 (Will, 2026-10-01: "orangepi uses a 16-bit depth buffer (no
    // stencil user there)"): the surface follows the client's request,
    // carried on SIrrlichtCreationParameters.Stencilbuffer. See
    // docker/irrlicht-drm/CIrrDeviceFB.cpp initEGL() for the full note.
    const bool wantStencil = CreationParams.Stencilbuffer;
    const int depthBits = wantStencil ? 24 : 16;
    const int stencilBits = wantStencil ? 8 : 0;
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, depthBits,
        EGL_STENCIL_SIZE, stencilBits,
        EGL_RENDERABLE_TYPE, useDesktopGL ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    if (!chooseConfigForGbmFormat(eglDisplay_, configAttribs, GBM_FORMAT_XRGB8888, &eglConfig_)) {
        // Fallback: try without depth buffer
        os::Printer::log("EGL: No config with depth buffer, retrying without", ELL_WARNING);
        EGLint fallbackAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_RENDERABLE_TYPE, useDesktopGL ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        if (!chooseConfigForGbmFormat(eglDisplay_, fallbackAttribs, GBM_FORMAT_XRGB8888, &eglConfig_)) {
            os::Printer::log("EGL: eglChooseConfig failed", ELL_ERROR);
            return false;
        }
    }

    // Log actual EGL config depth/stencil bits
    {
        EGLint depthBits = 0, stencilBits = 0;
        eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_DEPTH_SIZE, &depthBits);
        eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_STENCIL_SIZE, &stencilBits);
        char buf[128];
        snprintf(buf, sizeof(buf), "EGL: Config depth=%d stencil=%d", depthBits, stencilBits);
        os::Printer::log(buf, ELL_INFORMATION);
    }

    // Create context
    // For desktop GL, no context attributes needed (default gives highest available)
    // For GLES2, request ES 2.0
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT,
                                   useDesktopGL ? nullptr : contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        os::Printer::log("EGL: eglCreateContext failed", ELL_ERROR);
        return false;
    }

    // Create window surface from GBM surface
    eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_,
                                          (EGLNativeWindowType)gbmSurface_, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        os::Printer::log("EGL: eglCreateWindowSurface failed", ELL_ERROR);
        return false;
    }

    // Make current
    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        os::Printer::log("EGL: eglMakeCurrent failed", ELL_ERROR);
        return false;
    }

    os::Printer::log("EGL: Context created and made current", ELL_INFORMATION);
    return true;
}
#endif // !__APPLE__

// ============================================================
// x86/Xvfb Surfaceless EGL Fallback (H01)
// ============================================================
//
// Used when initDRM()/initGBM()/initEGL() (the real KMS/GBM path, unchanged
// above) fail to find an accessible, connected DRM display — the normal case
// for a headless x86 dev VM under Xvfb with no /dev/dri render-node grant.
// Mesa's "surfaceless" EGL platform provides a genuine EGL display backed by
// llvmpipe with no window, no GBM buffer, and no /dev/dri access required at
// all; we pair it with a small pbuffer surface so eglMakeCurrent has a
// drawable to bind. COpenGLES2Driver is unmodified — it only requires "an EGL
// context is current," which this satisfies exactly as initEGL() does on ARM.
bool CIrrDeviceFB::initEGLSurfaceless()
{
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

    typedef EGLDisplay (*GetPlatformDisplayEXT)(EGLenum, void*, const EGLint*);
    GetPlatformDisplayEXT getPlatformDisplay =
        (GetPlatformDisplayEXT)eglGetProcAddress("eglGetPlatformDisplayEXT");

#ifdef __APPLE__
    // B1043 (#363): ANGLE on macOS. Ask for its Metal backend explicitly
    // (Mesa's surfaceless platform does not exist there); the eglGetDisplay
    // fallback below gives ANGLE's default display if this is refused.
    static const EGLint angleMetal[] = {
        0x3203 /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */, 0x3489 /* EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE */,
        EGL_NONE
    };
    if (getPlatformDisplay)
        eglDisplay_ = getPlatformDisplay(0x3202 /* EGL_PLATFORM_ANGLE_ANGLE */, nullptr, angleMetal);
#else
    if (getPlatformDisplay)
        eglDisplay_ = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#endif

    if (eglDisplay_ == EGL_NO_DISPLAY)
        eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (eglDisplay_ == EGL_NO_DISPLAY) {
        os::Printer::log("EGL (surfaceless): eglGetDisplay failed", ELL_ERROR);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(eglDisplay_, &major, &minor)) {
        os::Printer::log("EGL (surfaceless): eglInitialize failed", ELL_ERROR);
        return false;
    }

    char verMsg[128];
    snprintf(verMsg, sizeof(verMsg), "EGL (surfaceless): Version %d.%d, vendor=%s", major, minor,
             eglQueryString(eglDisplay_, EGL_VENDOR));
    os::Printer::log(verMsg, ELL_INFORMATION);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        os::Printer::log("EGL (surfaceless): EGL_OPENGL_ES_API not available", ELL_ERROR);
        return false;
    }
    useGLES2_ = true;

    // B971: follow the client's stencil request (see initEGL() above).
    const bool wantStencil = CreationParams.Stencilbuffer;
    const int depthBits = wantStencil ? 24 : 16;
    const int stencilBits = wantStencil ? 8 : 0;
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, depthBits,
        EGL_STENCIL_SIZE, stencilBits,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
        os::Printer::log("EGL (surfaceless): No config with depth+stencil, retrying without stencil", ELL_WARNING);
        EGLint fallbackAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(eglDisplay_, fallbackAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
            os::Printer::log("EGL (surfaceless): eglChooseConfig failed", ELL_ERROR);
            return false;
        }
    }

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        os::Printer::log("EGL (surfaceless): eglCreateContext failed", ELL_ERROR);
        return false;
    }

    EGLint pbufAttribs[] = {
        EGL_WIDTH, (EGLint)(Width > 0 ? Width : 800),
        EGL_HEIGHT, (EGLint)(Height > 0 ? Height : 600),
        EGL_NONE
    };
    eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufAttribs);
    if (eglSurface_ == EGL_NO_SURFACE) {
        os::Printer::log("EGL (surfaceless): eglCreatePbufferSurface failed", ELL_ERROR);
        return false;
    }

    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        os::Printer::log("EGL (surfaceless): eglMakeCurrent failed", ELL_ERROR);
        return false;
    }

#ifdef __APPLE__
    os::Printer::log("EGL (pbuffer): Context created and made current (ANGLE/Metal)",
#else
    os::Printer::log("EGL (surfaceless): Context created and made current (llvmpipe, x86/Xvfb fallback)",
#endif
                     ELL_INFORMATION);
    return true;
}

// P25: initEGLX11()/pollX11Events()/closeX11Window() and the X11 keysym
// map moved to CIrrDeviceFB_X11.cpp (same class, split out to stay under
// the file-size warning threshold - docs/decisions.md Batch F ceilings).

#ifndef __APPLE__ // B1043: Linux-only
// ============================================================
// DRM Page Flip
// ============================================================

// Per-bo cached framebuffer data, stored via gbm_bo_set_user_data().
// GBM recycles a small pool of buffer objects (typically 2-3), so each bo
// gets a DRM framebuffer created once and reused for thousands of frames.
// The destructor callback runs when GBM actually frees the bo.
struct DrmFbData {
    int drmFd;
    uint32_t fbId;
};

static void drmFbDestroyCallback(struct gbm_bo* bo, void* data) {
    auto* fbData = static_cast<DrmFbData*>(data);
    if (fbData) {
        if (fbData->fbId)
            drmModeRmFB(fbData->drmFd, fbData->fbId);
        delete fbData;
    }
}

static uint32_t getOrCreateFb(struct gbm_bo* bo, int drmFd, uint32_t width, uint32_t height) {
    // Check for cached FB on this bo
    auto* existing = static_cast<DrmFbData*>(gbm_bo_get_user_data(bo));
    if (existing)
        return existing->fbId;

    // First time seeing this bo — create a DRM framebuffer
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t fb = 0;

    int ret = drmModeAddFB(drmFd, width, height, 24, 32, stride, handle, &fb);
    if (ret) {
        os::Printer::log("DRM: drmModeAddFB failed", ELL_WARNING);
        return 0;
    }

    auto* fbData = new DrmFbData{drmFd, fb};
    gbm_bo_set_user_data(bo, fbData, drmFbDestroyCallback);
    return fb;
}

// Page flip event handler — called by drmHandleEvent when flip completes
static void pageFlipHandler(int fd, unsigned int frame,
                            unsigned int sec, unsigned int usec, void* data)
{
    bool* flipPending = static_cast<bool*>(data);
    *flipPending = false;
}

void CIrrDeviceFB::waitForFlipComplete()
{
    if (!flipPending_) return;

    drmEventContext evctx = {};
    evctx.version = 2;
    evctx.page_flip_handler = pageFlipHandler;

    // Poll DRM fd until the flip event arrives
    struct pollfd pfd = {};
    pfd.fd = drmFd_;
    pfd.events = POLLIN;

    while (flipPending_) {
        int ret = poll(&pfd, 1, 100);  // 100ms timeout (safety)
        if (ret > 0 && (pfd.revents & POLLIN)) {
            drmHandleEvent(drmFd_, &evctx);
        } else if (ret == 0) {
            // Timeout — flip event lost, force clear
            flipPending_ = false;
        }
    }
}

void CIrrDeviceFB::drmPageFlip()
{
    // Wait for previous flip to complete before submitting a new one.
    // This prevents drmModePageFlip from returning -EBUSY and falling
    // back to the synchronous drmModeSetCrtc (which blocks ~35ms).
    waitForFlipComplete();

    eglSwapBuffers(eglDisplay_, eglSurface_);

    struct gbm_bo* bo = gbm_surface_lock_front_buffer(gbmSurface_);
    if (!bo) {
        os::Printer::log("DRM: gbm_surface_lock_front_buffer failed", ELL_WARNING);
        return;
    }

    uint32_t fb = getOrCreateFb(bo, drmFd_, Width, Height);
    if (!fb) {
        gbm_surface_release_buffer(gbmSurface_, bo);
        return;
    }

    if (firstFrame_) {
        // First frame: set the CRTC
        int ret = drmModeSetCrtc(drmFd_, crtcId_, fb, 0, 0, &connectorId_, 1, &mode_);
        if (ret) {
            os::Printer::log("DRM: drmModeSetCrtc failed", ELL_WARNING);
        }
        firstFrame_ = false;
    } else {
        // Async page flip with event notification
        int ret = drmModePageFlip(drmFd_, crtcId_, fb,
                                  DRM_MODE_PAGE_FLIP_EVENT, &flipPending_);
        if (ret) {
            // Fallback: synchronous CRTC set (should rarely happen now)
            drmModeSetCrtc(drmFd_, crtcId_, fb, 0, 0, &connectorId_, 1, &mode_);
        } else {
            flipPending_ = true;
        }
    }

    // Release previous buffer (FB stays cached via gbm_bo user_data)
    if (previousBo_) {
        gbm_surface_release_buffer(gbmSurface_, previousBo_);
    }
    previousBo_ = bo;
}
#endif // !__APPLE__

// ============================================================
// Driver Creation
// ============================================================

void CIrrDeviceFB::createDriver()
{
    switch (CreationParams.DriverType) {
#ifdef _IRR_COMPILE_WITH_OPENGL_
        case video::EDT_OPENGL:
            VideoDriver = video::createOpenGLDriver(CreationParams, FileSystem, this);
            break;
#endif
#ifdef _IRR_COMPILE_WITH_OGLES2_
        case video::EDT_OGLES2:
            VideoDriver = video::createOpenGLES2Driver(CreationParams, FileSystem, this);
            break;
#endif
        case video::EDT_BURNINGSVIDEO:
            VideoDriver = video::createBurningVideoDriver(CreationParams, FileSystem, this);
            break;
        case video::EDT_SOFTWARE:
            VideoDriver = video::createSoftwareDriver(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
            break;
        case video::EDT_NULL:
            // P27: stock Irrlicht devices (CIrrDeviceLinux, CIrrDeviceConsole)
            // all handle EDT_NULL here via createNullDriver() — this forked
            // device was missing the case, silently leaving VideoDriver null
            // (and, transitively, the scene manager never created below).
            VideoDriver = video::createNullDriver(FileSystem, CreationParams.WindowSize);
            break;
        default:
            os::Printer::log("Unable to create driver for requested type.", ELL_ERROR);
            break;
    }
}

#ifndef __APPLE__ // B1043: Linux-only
// ============================================================
// evdev Input
// ============================================================

void CIrrDeviceFB::initEvdev()
{
    // Scan /dev/input/event* for keyboard and mouse
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        os::Printer::log("evdev: Cannot open /dev/input", ELL_WARNING);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        // Check device capabilities
        // Note: EV_CNT, KEY_CNT, REL_CNT are from linux/input.h (no Irrlicht conflict)
        unsigned long evbits[((EV_CNT - 1) / (sizeof(long) * 8)) + 1] = {};
        unsigned long keybits[((KEY_CNT - 1) / (sizeof(long) * 8)) + 1] = {};

        ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);

        // Check for keyboard (has EV_KEY and KEY_A)
        // Note: EV_KEY, KEY_A are from linux/input.h (no Irrlicht conflict)
        if (keyboardFd_ < 0 && (evbits[0] & (1 << EV_KEY))) {
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
            // Check for letter keys (indicates keyboard, not just power button)
            if (keybits[KEY_A / (sizeof(long) * 8)] & (1UL << (KEY_A % (sizeof(long) * 8)))) {
                keyboardFd_ = fd;
                char msg[128];
                snprintf(msg, sizeof(msg), "evdev: Keyboard at %s", path);
                os::Printer::log(msg, ELL_INFORMATION);
                continue;
            }
        }

        // Check for mouse (has EV_REL with REL_X)
        if (mouseFd_ < 0 && (evbits[0] & (1 << EV_REL))) {
            unsigned long relbits[((REL_CNT - 1) / (sizeof(long) * 8)) + 1] = {};
            ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits);
            if (relbits[REL_X / (sizeof(long) * 8)] & (1UL << (REL_X % (sizeof(long) * 8)))) {
                mouseFd_ = fd;
                char msg[128];
                snprintf(msg, sizeof(msg), "evdev: Mouse at %s", path);
                os::Printer::log(msg, ELL_INFORMATION);
                continue;
            }
        }

        close(fd);

        if (keyboardFd_ >= 0 && mouseFd_ >= 0)
            break;
    }
    closedir(dir);

    if (keyboardFd_ < 0)
        os::Printer::log("evdev: No keyboard found", ELL_WARNING);
    if (mouseFd_ < 0)
        os::Printer::log("evdev: No mouse found", ELL_WARNING);

    // Disable VT keyboard processing so the console layer doesn't
    // intercept Ctrl+key combinations (Ctrl+C = SIGINT, Ctrl+V = lnext, etc.)
    if (keyboardFd_ >= 0) {
        ttyFd_ = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (ttyFd_ < 0)
            ttyFd_ = open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (ttyFd_ >= 0) {
            if (ioctl(ttyFd_, KDGKBMODE, &savedKbMode_) == 0) {
                if (ioctl(ttyFd_, KDSKBMODE, K_OFF) == 0) {
                    os::Printer::log("evdev: VT keyboard processing disabled (K_OFF)", ELL_INFORMATION);
                    // Install handlers so keyboard is restored on any exit path:
                    // - atexit: covers exit(0), return from main, normal termination
                    // - signals: covers Ctrl+C, kill, crash
                    s_savedTtyFd = ttyFd_;
                    s_savedKbMode = savedKbMode_;
                    atexit(restoreKeyboardMode);
                    signal(SIGINT, restoreKeyboardSignalHandler);
                    signal(SIGTERM, restoreKeyboardSignalHandler);
                    signal(SIGSEGV, restoreKeyboardSignalHandler);
                    signal(SIGABRT, restoreKeyboardSignalHandler);
                } else {
                    os::Printer::log("evdev: Failed to set K_OFF on tty", ELL_WARNING);
                    savedKbMode_ = -1;
                }
            } else {
                os::Printer::log("evdev: Failed to get keyboard mode from tty", ELL_WARNING);
                savedKbMode_ = -1;
            }
        } else {
            os::Printer::log("evdev: Cannot open tty for keyboard mode control", ELL_WARNING);
        }
    }
}

void CIrrDeviceFB::pollEvdev()
{
    struct input_event ev;

    // Read keyboard events
    if (keyboardFd_ >= 0) {
        while (read(keyboardFd_, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                bool pressed = (ev.value == 1);
                bool released = (ev.value == 0);
                // value == 2 is repeat

                if (ev.code < EVDEV_KEY_MAX)
                    keyIsDown_[ev.code] = pressed || (ev.value == 2);

                // Track modifiers (KEY_LEFTSHIFT etc. don't conflict with Irrlicht)
                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT)
                    shiftDown_ = pressed || (ev.value == 2);
                else if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL)
                    ctrlDown_ = pressed || (ev.value == 2);
                else if (ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT)
                    altDown_ = pressed || (ev.value == 2);

                if (pressed || released) {
                    EKEY_CODE irrKey = linuxKeyToIrrlicht(ev.code);

                    SEvent event;
                    event.EventType = EET_KEY_INPUT_EVENT;
                    event.KeyInput.Key = irrKey;
                    event.KeyInput.PressedDown = pressed;
                    event.KeyInput.Shift = shiftDown_;
                    event.KeyInput.Control = ctrlDown_;
                    event.KeyInput.Char = 0;

                    // For keys where shift produces a different logical key,
                    // clear the shift flag since it was "consumed" by the
                    // keymap translation (e.g. Shift+"=" → "+", not Shift+"+")
                    if (shiftDown_ && ev.code == KEY_EQUAL) {
                        event.KeyInput.Shift = false;
                    }

                    // Generate character for printable keys
                    // (KEY_KEY_A etc. are Irrlicht-only names, no conflict)
                    if (pressed && irrKey >= KEY_KEY_A && irrKey <= KEY_KEY_Z) {
                        event.KeyInput.Char = shiftDown_ ?
                            (wchar_t)('A' + (irrKey - KEY_KEY_A)) :
                            (wchar_t)('a' + (irrKey - KEY_KEY_A));
                    } else if (pressed && irrKey >= KEY_KEY_0 && irrKey <= KEY_KEY_9) {
                        if (!shiftDown_) {
                            event.KeyInput.Char = (wchar_t)('0' + (irrKey - KEY_KEY_0));
                        } else {
                            // Shifted number keys
                            const char shifted[] = ")!@#$%^&*(";
                            int idx = irrKey - KEY_KEY_0;
                            if (idx >= 0 && idx < 10)
                                event.KeyInput.Char = (wchar_t)shifted[idx];
                        }
                    } else if (pressed) {
                        // These are all Irrlicht EKEY_CODE values (Linux macros were #undef'd)
                        switch (irrKey) {
                            case KEY_SPACE: event.KeyInput.Char = L' '; break;
                            case KEY_RETURN: event.KeyInput.Char = L'\r'; break;
                            case KEY_TAB: event.KeyInput.Char = L'\t'; break;
                            case KEY_BACK: event.KeyInput.Char = L'\b'; break;
                            case KEY_MINUS: event.KeyInput.Char = shiftDown_ ? L'_' : L'-'; break;
                            case KEY_PLUS: event.KeyInput.Char = shiftDown_ ? L'+' : L'='; break;
                            case KEY_COMMA: event.KeyInput.Char = shiftDown_ ? L'<' : L','; break;
                            case KEY_PERIOD: event.KeyInput.Char = shiftDown_ ? L'>' : L'.'; break;
                            case KEY_OEM_2: event.KeyInput.Char = shiftDown_ ? L'?' : L'/'; break;
                            case KEY_OEM_1: event.KeyInput.Char = shiftDown_ ? L':' : L';'; break;
                            case KEY_OEM_7: event.KeyInput.Char = shiftDown_ ? L'"' : L'\''; break;
                            case KEY_OEM_4: event.KeyInput.Char = shiftDown_ ? L'{' : L'['; break;
                            case KEY_OEM_6: event.KeyInput.Char = shiftDown_ ? L'}' : L']'; break;
                            case KEY_OEM_5: event.KeyInput.Char = shiftDown_ ? L'|' : L'\\'; break;
                            case KEY_OEM_3: event.KeyInput.Char = shiftDown_ ? L'~' : L'`'; break;
                            default: break;
                        }
                    }

                    postEventFromUser(event);
                }
            }
        }
    }

    // Read mouse events
    if (mouseFd_ >= 0) {
        while (read(mouseFd_, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_REL) {
                SEvent event;
                event.EventType = EET_MOUSE_INPUT_EVENT;
                event.MouseInput.ButtonStates = 0;
                if (mouseButtonState_[0]) event.MouseInput.ButtonStates |= EMBSM_LEFT;
                if (mouseButtonState_[1]) event.MouseInput.ButtonStates |= EMBSM_RIGHT;
                if (mouseButtonState_[2]) event.MouseInput.ButtonStates |= EMBSM_MIDDLE;

                if (ev.code == REL_X) {
                    mousePos_.X += ev.value;
                    if (mousePos_.X < 0) mousePos_.X = 0;
                    if (mousePos_.X >= (s32)Width) mousePos_.X = Width - 1;
                    event.MouseInput.Event = EMIE_MOUSE_MOVED;
                    event.MouseInput.X = mousePos_.X;
                    event.MouseInput.Y = mousePos_.Y;
                    postEventFromUser(event);
                } else if (ev.code == REL_Y) {
                    mousePos_.Y += ev.value;
                    if (mousePos_.Y < 0) mousePos_.Y = 0;
                    if (mousePos_.Y >= (s32)Height) mousePos_.Y = Height - 1;
                    event.MouseInput.Event = EMIE_MOUSE_MOVED;
                    event.MouseInput.X = mousePos_.X;
                    event.MouseInput.Y = mousePos_.Y;
                    postEventFromUser(event);
                } else if (ev.code == REL_WHEEL) {
                    event.MouseInput.Event = EMIE_MOUSE_WHEEL;
                    event.MouseInput.Wheel = (f32)ev.value;
                    event.MouseInput.X = mousePos_.X;
                    event.MouseInput.Y = mousePos_.Y;
                    postEventFromUser(event);
                }
            } else if (ev.type == EV_KEY) {
                SEvent event;
                event.EventType = EET_MOUSE_INPUT_EVENT;
                event.MouseInput.X = mousePos_.X;
                event.MouseInput.Y = mousePos_.Y;

                event.MouseInput.ButtonStates = 0;
                if (mouseButtonState_[0]) event.MouseInput.ButtonStates |= EMBSM_LEFT;
                if (mouseButtonState_[1]) event.MouseInput.ButtonStates |= EMBSM_RIGHT;
                if (mouseButtonState_[2]) event.MouseInput.ButtonStates |= EMBSM_MIDDLE;

                if (ev.code == BTN_LEFT) {
                    mouseButtonState_[0] = (ev.value == 1);
                    event.MouseInput.Event = (ev.value == 1) ?
                        EMIE_LMOUSE_PRESSED_DOWN : EMIE_LMOUSE_LEFT_UP;
                    if (mouseButtonState_[0])
                        event.MouseInput.ButtonStates |= EMBSM_LEFT;
                    else
                        event.MouseInput.ButtonStates &= ~EMBSM_LEFT;
                    postEventFromUser(event);
                } else if (ev.code == BTN_RIGHT) {
                    mouseButtonState_[1] = (ev.value == 1);
                    event.MouseInput.Event = (ev.value == 1) ?
                        EMIE_RMOUSE_PRESSED_DOWN : EMIE_RMOUSE_LEFT_UP;
                    if (mouseButtonState_[1])
                        event.MouseInput.ButtonStates |= EMBSM_RIGHT;
                    else
                        event.MouseInput.ButtonStates &= ~EMBSM_RIGHT;
                    postEventFromUser(event);
                } else if (ev.code == BTN_MIDDLE) {
                    mouseButtonState_[2] = (ev.value == 1);
                    event.MouseInput.Event = (ev.value == 1) ?
                        EMIE_MMOUSE_PRESSED_DOWN : EMIE_MMOUSE_LEFT_UP;
                    if (mouseButtonState_[2])
                        event.MouseInput.ButtonStates |= EMBSM_MIDDLE;
                    else
                        event.MouseInput.ButtonStates &= ~EMBSM_MIDDLE;
                    postEventFromUser(event);
                }
            }
        }
    }
}

// ============================================================
// Keymap: Linux KEY_* -> Irrlicht EKEY_CODE
// ============================================================

void CIrrDeviceFB::initKeyMap()
{
    // Initialize all to unknown
    for (int i = 0; i < EVDEV_KEY_MAX; i++)
        keyMap_[i] = (EKEY_CODE)0;

    // Letters (KEY_A..KEY_Z are Linux-only names, no conflict)
    keyMap_[KEY_A] = KEY_KEY_A;
    keyMap_[KEY_B] = KEY_KEY_B;
    keyMap_[KEY_C] = KEY_KEY_C;
    keyMap_[KEY_D] = KEY_KEY_D;
    keyMap_[KEY_E] = KEY_KEY_E;
    keyMap_[KEY_F] = KEY_KEY_F;
    keyMap_[KEY_G] = KEY_KEY_G;
    keyMap_[KEY_H] = KEY_KEY_H;
    keyMap_[KEY_I] = KEY_KEY_I;
    keyMap_[KEY_J] = KEY_KEY_J;
    keyMap_[KEY_K] = KEY_KEY_K;
    keyMap_[KEY_L] = KEY_KEY_L;
    keyMap_[KEY_M] = KEY_KEY_M;
    keyMap_[KEY_N] = KEY_KEY_N;
    keyMap_[KEY_O] = KEY_KEY_O;
    keyMap_[KEY_P] = KEY_KEY_P;
    keyMap_[KEY_Q] = KEY_KEY_Q;
    keyMap_[KEY_R] = KEY_KEY_R;
    keyMap_[KEY_S] = KEY_KEY_S;
    keyMap_[KEY_T] = KEY_KEY_T;
    keyMap_[KEY_U] = KEY_KEY_U;
    keyMap_[KEY_V] = KEY_KEY_V;
    keyMap_[KEY_W] = KEY_KEY_W;
    keyMap_[KEY_X] = KEY_KEY_X;
    keyMap_[KEY_Y] = KEY_KEY_Y;
    keyMap_[KEY_Z] = KEY_KEY_Z;

    // Numbers (KEY_0..KEY_9 are Linux-only names, no conflict)
    keyMap_[KEY_0] = KEY_KEY_0;
    keyMap_[KEY_1] = KEY_KEY_1;
    keyMap_[KEY_2] = KEY_KEY_2;
    keyMap_[KEY_3] = KEY_KEY_3;
    keyMap_[KEY_4] = KEY_KEY_4;
    keyMap_[KEY_5] = KEY_KEY_5;
    keyMap_[KEY_6] = KEY_KEY_6;
    keyMap_[KEY_7] = KEY_KEY_7;
    keyMap_[KEY_8] = KEY_KEY_8;
    keyMap_[KEY_9] = KEY_KEY_9;

    // Function keys (use LK_ prefix - Linux KEY_F1..F12 were #undef'd)
    keyMap_[LK_F1]  = KEY_F1;
    keyMap_[LK_F2]  = KEY_F2;
    keyMap_[LK_F3]  = KEY_F3;
    keyMap_[LK_F4]  = KEY_F4;
    keyMap_[LK_F5]  = KEY_F5;
    keyMap_[LK_F6]  = KEY_F6;
    keyMap_[LK_F7]  = KEY_F7;
    keyMap_[LK_F8]  = KEY_F8;
    keyMap_[LK_F9]  = KEY_F9;
    keyMap_[LK_F10] = KEY_F10;
    keyMap_[LK_F11] = KEY_F11;
    keyMap_[LK_F12] = KEY_F12;

    // Control keys (use LK_ prefix for conflicting names)
    keyMap_[KEY_ESC]       = KEY_ESCAPE;   // KEY_ESC is Linux-only, no conflict
    keyMap_[LK_TAB]        = KEY_TAB;      // Both define KEY_TAB
    keyMap_[KEY_BACKSPACE]  = KEY_BACK;    // KEY_BACKSPACE is Linux-only
    keyMap_[KEY_ENTER]      = KEY_RETURN;  // KEY_ENTER is Linux-only
    keyMap_[LK_SPACE]      = KEY_SPACE;    // Both define KEY_SPACE
    keyMap_[LK_DELETE]     = KEY_DELETE;    // Both define KEY_DELETE
    keyMap_[LK_INSERT]     = KEY_INSERT;   // Both define KEY_INSERT
    keyMap_[LK_HOME]       = KEY_HOME;     // Both define KEY_HOME
    keyMap_[LK_END]        = KEY_END;      // Both define KEY_END
    keyMap_[KEY_PAGEUP]    = KEY_PRIOR;    // KEY_PAGEUP is Linux-only
    keyMap_[KEY_PAGEDOWN]  = KEY_NEXT;     // KEY_PAGEDOWN is Linux-only
    keyMap_[KEY_CAPSLOCK]  = KEY_CAPITAL;   // KEY_CAPSLOCK is Linux-only
    keyMap_[LK_NUMLOCK]    = KEY_NUMLOCK;  // Both define KEY_NUMLOCK
    keyMap_[KEY_SCROLLLOCK] = KEY_SCROLL;  // KEY_SCROLLLOCK is Linux-only

    // Arrow keys (use LK_ prefix - conflicting names)
    keyMap_[LK_UP]    = KEY_UP;
    keyMap_[LK_DOWN]  = KEY_DOWN;
    keyMap_[LK_LEFT]  = KEY_LEFT;
    keyMap_[LK_RIGHT] = KEY_RIGHT;

    // Modifiers (KEY_LEFTSHIFT etc. are Linux-only names, no conflict)
    keyMap_[KEY_LEFTSHIFT]  = KEY_LSHIFT;
    keyMap_[KEY_RIGHTSHIFT] = KEY_RSHIFT;
    keyMap_[KEY_LEFTCTRL]   = KEY_LCONTROL;
    keyMap_[KEY_RIGHTCTRL]  = KEY_RCONTROL;
    keyMap_[KEY_LEFTALT]    = KEY_LMENU;
    keyMap_[KEY_RIGHTALT]   = KEY_RMENU;

    // Punctuation / OEM keys (use LK_ for conflicting names)
    keyMap_[LK_MINUS]      = KEY_MINUS;    // Both define KEY_MINUS
    keyMap_[KEY_EQUAL]     = KEY_PLUS;     // KEY_EQUAL is Linux-only
    keyMap_[KEY_LEFTBRACE] = KEY_OEM_4;    // KEY_LEFTBRACE is Linux-only
    keyMap_[KEY_RIGHTBRACE] = KEY_OEM_6;   // KEY_RIGHTBRACE is Linux-only
    keyMap_[KEY_SEMICOLON] = KEY_OEM_1;    // KEY_SEMICOLON is Linux-only
    keyMap_[KEY_APOSTROPHE] = KEY_OEM_7;   // KEY_APOSTROPHE is Linux-only
    keyMap_[KEY_GRAVE]     = KEY_OEM_3;    // KEY_GRAVE is Linux-only
    keyMap_[KEY_BACKSLASH] = KEY_OEM_5;    // KEY_BACKSLASH is Linux-only
    keyMap_[LK_COMMA]      = KEY_COMMA;    // Both define KEY_COMMA
    keyMap_[KEY_DOT]       = KEY_PERIOD;   // KEY_DOT is Linux-only
    keyMap_[KEY_SLASH]     = KEY_OEM_2;    // KEY_SLASH is Linux-only

    // Numpad (KEY_KP* are Linux-only names, no conflict)
    keyMap_[KEY_KP0] = KEY_NUMPAD0;
    keyMap_[KEY_KP1] = KEY_NUMPAD1;
    keyMap_[KEY_KP2] = KEY_NUMPAD2;
    keyMap_[KEY_KP3] = KEY_NUMPAD3;
    keyMap_[KEY_KP4] = KEY_NUMPAD4;
    keyMap_[KEY_KP5] = KEY_NUMPAD5;
    keyMap_[KEY_KP6] = KEY_NUMPAD6;
    keyMap_[KEY_KP7] = KEY_NUMPAD7;
    keyMap_[KEY_KP8] = KEY_NUMPAD8;
    keyMap_[KEY_KP9] = KEY_NUMPAD9;
    keyMap_[KEY_KPDOT]      = KEY_DECIMAL;
    keyMap_[KEY_KPENTER]    = KEY_RETURN;
    keyMap_[KEY_KPPLUS]     = KEY_ADD;
    keyMap_[KEY_KPMINUS]    = KEY_SUBTRACT;
    keyMap_[KEY_KPASTERISK] = KEY_MULTIPLY;
    keyMap_[KEY_KPSLASH]    = KEY_DIVIDE;
}
#endif // !__APPLE__

EKEY_CODE CIrrDeviceFB::linuxKeyToIrrlicht(int linuxKey) const
{
    if (linuxKey >= 0 && linuxKey < EVDEV_KEY_MAX)
        return keyMap_[linuxKey];
    return (EKEY_CODE)0;
}

// ============================================================
// IrrlichtDevice Interface
// ============================================================

bool CIrrDeviceFB::run()
{
    os::Timer::tick();

    if (x11Active_)
        pollX11Events();
    else
        pollEvdev();

    return !Close;
}

void CIrrDeviceFB::yield()
{
    usleep(0);
}

void CIrrDeviceFB::sleep(u32 timeMs, bool pauseTimer)
{
    const bool wasStopped = Timer ? Timer->isStopped() : true;
    if (pauseTimer && !wasStopped)
        Timer->stop();

    usleep(timeMs * 1000);

    if (pauseTimer && !wasStopped)
        Timer->start();
}

void CIrrDeviceFB::setWindowCaption(const wchar_t* text)
{
    // P25: set the real X11 window title in windowed mode. No window
    // caption otherwise (DRM/framebuffer/surfaceless modes have no window).
    if (x11Active_ && x11Display_ && x11Window_) {
        char buf[256];
        size_t n = 0;
        while (text[n] && n < sizeof(buf) - 1) {
            buf[n] = (text[n] < 128) ? (char)text[n] : '?';
            n++;
        }
        buf[n] = '\0';
#ifndef __APPLE__ // B1043: Linux-only
        XStoreName((Display*)x11Display_, x11Window_, buf);
#endif // !__APPLE__
    }
}

bool CIrrDeviceFB::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
    // P52: CPU-side sync-point attribution (flag-gated, see
    // gpu_sync_probe.h). No DRM page-flip path exists on x86 (no DRM/KMS
    // scanout — see docker/CLAUDE.md), so lastFlipWaitMs stays 0 here by
    // construction; only eglSwapBuffers() wall time is meaningful on this
    // fork. That asymmetry with the ARM fork's drmPageFlip() is itself part
    // of the characterization (x86 sanity checks can never reproduce the
    // Pi's flip-wait cost, only exercise the GPU-query/CPU-fallback plumbing).
    const bool probeOn = eqtGPUSyncProbeEnabled();

    // P25: X11-windowed backend - swap the real window surface.
    if (x11Active_) {
        if (probeOn) {
            const auto t0 = std::chrono::steady_clock::now();
            eglSwapBuffers(eglDisplay_, eglSurface_);
            const auto t1 = std::chrono::steady_clock::now();
            eqtRecordSwapBuffersMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
        } else {
            eglSwapBuffers(eglDisplay_, eglSurface_);
        }
        return true;
    }

    // Surfaceless fallback (x86/Xvfb): no DRM/KMS scanout to flip to. Swapping
    // the pbuffer surface is a harmless no-op on Mesa but keeps EGL state
    // consistent; callers that need the pixels use glReadPixels directly.
    if (surfaceless_) {
        if (probeOn) {
            const auto t0 = std::chrono::steady_clock::now();
            eglSwapBuffers(eglDisplay_, eglSurface_);
            const auto t1 = std::chrono::steady_clock::now();
            eqtRecordSwapBuffersMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
        } else {
            eglSwapBuffers(eglDisplay_, eglSurface_);
        }
        return true;
    }

    // For software rendering: would need to blit to DRM framebuffer
    // For OpenGL/GLES2: handled by drmPageFlip()
    if (CreationParams.DriverType == video::EDT_OPENGL ||
        CreationParams.DriverType == video::EDT_OGLES2) {
        drmPageFlip();
    }
    return true;
}

bool CIrrDeviceFB::isWindowActive() const
{
    return !Close;
}

bool CIrrDeviceFB::isWindowFocused() const
{
    return !Close;
}

bool CIrrDeviceFB::isWindowMinimized() const
{
    return false;
}

void CIrrDeviceFB::closeDevice()
{
    Close = true;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_
