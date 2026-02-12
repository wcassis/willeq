// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright.txt in the distribution
// or at http://irrlicht.sourceforge.net/license/

// DRM/KMS/GBM/EGL replacement for CIrrDeviceFB
// Provides EDT_OPENGL support via EGL on GBM surfaces with DRM/KMS page flipping.
// No X11 required - renders directly to the display via kernel mode setting.

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
#include <sys/ioctl.h>
#include <sys/time.h>

#include "IEventReceiver.h"
#include "os.h"
#include "CTimer.h"

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

// ============================================================
// Constructor / Destructor
// ============================================================

CIrrDeviceFB::CIrrDeviceFB(const SIrrlichtCreationParameters& param)
    : CIrrDeviceStub(param),
      drmFd_(-1), crtcId_(0), connectorId_(0), savedCrtc_(nullptr),
      gbmDevice_(nullptr), gbmSurface_(nullptr),
      previousBo_(nullptr), previousFb_(0),
      eglDisplay_(EGL_NO_DISPLAY), eglSurface_(EGL_NO_SURFACE),
      eglContext_(EGL_NO_CONTEXT), eglConfig_(nullptr),
      keyboardFd_(-1), mouseFd_(-1),
      shiftDown_(false), ctrlDown_(false), altDown_(false),
      Close(false), firstFrame_(true)
{
    Width = param.WindowSize.Width;
    Height = param.WindowSize.Height;

    memset(keyIsDown_, 0, sizeof(keyIsDown_));
    memset(mouseButtonState_, 0, sizeof(mouseButtonState_));
    memset(keyMap_, 0, sizeof(keyMap_));

    mousePos_.X = Width / 2;
    mousePos_.Y = Height / 2;

    initKeyMap();

    // Setup DRM/GBM/EGL pipeline
    bool drmOk = initDRM();
    bool gbmOk = drmOk ? initGBM() : false;
    bool eglOk = gbmOk ? initEGL() : false;

    if (!eglOk) {
        os::Printer::log("DRM/GBM/EGL initialization failed", ELL_ERROR);
    }

    // Open evdev input devices
    initEvdev();

    // Create cursor control
    CursorControl = new CCursorControl(this);

    // Create video driver
    createDriver();

    if (VideoDriver)
        createGUIAndScene();
}

CIrrDeviceFB::~CIrrDeviceFB()
{
    // Destroy EGL
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT)
            eglDestroyContext(eglDisplay_, eglContext_);
        if (eglSurface_ != EGL_NO_SURFACE)
            eglDestroySurface(eglDisplay_, eglSurface_);
        eglTerminate(eglDisplay_);
    }

    // Release GBM
    if (previousBo_) {
        drmModeRmFB(drmFd_, previousFb_);
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

    // Close evdev
    if (keyboardFd_ >= 0)
        close(keyboardFd_);
    if (mouseFd_ >= 0)
        close(mouseFd_);
}

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

    // Try desktop OpenGL first (Lima provides GL 2.1)
    bool useDesktopGL = true;
    if (!eglBindAPI(EGL_OPENGL_API)) {
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
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, useDesktopGL ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
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
        if (!eglChooseConfig(eglDisplay_, fallbackAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
            os::Printer::log("EGL: eglChooseConfig failed", ELL_ERROR);
            return false;
        }
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

// ============================================================
// DRM Page Flip
// ============================================================

void CIrrDeviceFB::drmPageFlip()
{
    eglSwapBuffers(eglDisplay_, eglSurface_);

    struct gbm_bo* bo = gbm_surface_lock_front_buffer(gbmSurface_);
    if (!bo) {
        os::Printer::log("DRM: gbm_surface_lock_front_buffer failed", ELL_WARNING);
        return;
    }

    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t fb = 0;

    int ret = drmModeAddFB(drmFd_, Width, Height, 24, 32, stride, handle, &fb);
    if (ret) {
        os::Printer::log("DRM: drmModeAddFB failed", ELL_WARNING);
        gbm_surface_release_buffer(gbmSurface_, bo);
        return;
    }

    if (firstFrame_) {
        // First frame: set the CRTC
        ret = drmModeSetCrtc(drmFd_, crtcId_, fb, 0, 0, &connectorId_, 1, &mode_);
        if (ret) {
            os::Printer::log("DRM: drmModeSetCrtc failed", ELL_WARNING);
        }
        firstFrame_ = false;
    } else {
        // Subsequent frames: page flip (non-blocking)
        ret = drmModePageFlip(drmFd_, crtcId_, fb, 0, nullptr);
        if (ret) {
            // Page flip failed (e.g., previous flip still pending) - use SetCrtc as fallback
            drmModeSetCrtc(drmFd_, crtcId_, fb, 0, 0, &connectorId_, 1, &mode_);
        }
    }

    // Release previous buffer
    if (previousBo_) {
        drmModeRmFB(drmFd_, previousFb_);
        gbm_surface_release_buffer(gbmSurface_, previousBo_);
    }
    previousBo_ = bo;
    previousFb_ = fb;
}

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
        case video::EDT_BURNINGSVIDEO:
            VideoDriver = video::createBurningVideoDriver(CreationParams, FileSystem, this);
            break;
        case video::EDT_SOFTWARE:
            VideoDriver = video::createSoftwareDriver(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
            break;
        default:
            os::Printer::log("Unable to create driver for requested type.", ELL_ERROR);
            break;
    }
}

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
    // No window caption in DRM/framebuffer mode
}

bool CIrrDeviceFB::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
    // For software rendering: would need to blit to DRM framebuffer
    // For OpenGL: handled by drmPageFlip() in COpenGLDriver::endScene()
    if (CreationParams.DriverType == video::EDT_OPENGL) {
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
