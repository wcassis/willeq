// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright.txt in the distribution
// or at http://irrlicht.sourceforge.net/license/

// DRM/KMS/GBM/EGL replacement for CIrrDeviceFB
// Provides EDT_OPENGL support via EGL on GBM surfaces with DRM/KMS page flipping.
// No X11 required - renders directly to the display via kernel mode setting.
//
// x86/Xvfb/llvmpipe FORK (H01): this is a copy of docker/irrlicht-drm/CIrrDeviceFB.*
// with one addition — if real DRM/KMS init fails (no /dev/dri access, no connected
// display — the common case for a headless dev VM without a DRM render-node grant),
// the device falls back to a Mesa "surfaceless" EGL platform + a pbuffer surface.
// This gives the real COpenGLES2Driver a genuine, unmodified GLES2 context backed
// by llvmpipe software rendering, with no window system and no /dev/dri permissions
// needed at all. On real hardware (Orange Pi), initDRM() succeeds and this class
// behaves identically to the ARM version — the fallback path never triggers there.
// See docker/irrlicht-drm/CIrrDeviceFB.* for the canonical ARM version (untouched).
//
// P25: second addition — an explicit opt-in X11-windowed EGL backend
// (--egl-x11 CLI flag), for interactive x86 dev use with a real on-screen
// window instead of the invisible surfaceless pbuffer. This is NEVER
// auto-detected from $DISPLAY: surfaceless stays the default so CI, goldens,
// and benchmarks are unaffected. The opt-in is plumbed via the
// WILLEQ_EGL_X11 environment variable (set by irrlicht_renderer.cpp before
// createDeviceEx() when --egl-x11 is passed) rather than a direct call into
// this class, because application code only has the public Irrlicht include
// path available, not this private device header (same reason
// gpu_upload_thread.cpp reads eglGetCurrentDisplay() instead of reaching
// into CIrrDeviceFB). When requested, initEGLX11() opens an X11 window via
// eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR) + eglCreateWindowSurface and
// replaces the evdev input path with X11 event translation
// (pollX11Events()). See CIrrDeviceFB_X11.cpp for the implementation (split
// out from CIrrDeviceFB.cpp to stay under the file-size warning threshold)
// and refactor/P25_x11_egl_backend.md for the design record.

#ifndef __C_IRR_DEVICE_FB_H_INCLUDED__
#define __C_IRR_DEVICE_FB_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_FB_DEVICE_

#include "CIrrDeviceStub.h"
#include "SIrrCreationParameters.h"
#include "IImagePresenter.h"
#include "ICursorControl.h"

#include <EGL/egl.h>
#ifndef __APPLE__
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#else
// B1043 (#363): Darwin has no libdrm/GBM. The DRM/GBM members below stay in
// the class (so the layout and every member access compile unchanged), typed
// by these opaque stand-ins; on Darwin they are never set and the DRM/GBM/
// evdev/X11 member functions are the stubs in CIrrDeviceFB_Darwin.cpp. EGL
// comes from ANGLE (Metal); the device takes the surfaceless/pbuffer path.
struct gbm_device;
struct gbm_surface;
struct gbm_bo;
typedef struct _drmModeCrtc drmModeCrtc;
typedef struct { int unused; } drmModeModeInfo;
#endif

// linux/input.h is NOT included here to avoid KEY_* macro collisions with
// Irrlicht's EKEY_CODE enum (both define KEY_SPACE, KEY_TAB, KEY_MINUS, etc.)
// It's included in the .cpp with careful #undef handling instead.
enum { EVDEV_KEY_MAX = 768 };  // Same as KEY_CNT from linux/input.h

namespace irr
{

class CIrrDeviceFB : public CIrrDeviceStub, public video::IImagePresenter
{
public:
    CIrrDeviceFB(const SIrrlichtCreationParameters& param);
    virtual ~CIrrDeviceFB();

    // IrrlichtDevice interface
    virtual bool run();
    virtual void yield();
    virtual void sleep(u32 timeMs, bool pauseTimer=false);
    virtual void setWindowCaption(const wchar_t* text);
    virtual bool present(video::IImage* surface, void* windowId=0, core::rect<s32>* srcClip=0);
    virtual bool isWindowActive() const;
    virtual bool isWindowFocused() const;
    virtual bool isWindowMinimized() const;
    virtual void closeDevice();

    virtual E_DEVICE_TYPE getType() const { return EIDT_FRAMEBUFFER; }

    virtual void setResizable(bool resize=false) {}
    virtual void minimizeWindow() {}
    virtual void maximizeWindow() {}
    virtual void restoreWindow() {}
    virtual core::position2di getWindowPosition() { return core::position2di(0,0); }

    // EGL accessors for COpenGLDriver / COpenGLES2Driver
    EGLDisplay getEGLDisplay() const { return eglDisplay_; }
    EGLSurface getEGLSurface() const { return eglSurface_; }
    EGLContext getEGLContext() const { return eglContext_; }
    EGLConfig getEGLConfig() const { return eglConfig_; }
    struct gbm_device* getGBMDevice() const { return gbmDevice_; }

    // GLES2 mode flag
    bool isGLES2() const { return useGLES2_; }
    void setGLES2(bool v) { useGLES2_ = v; }

    // DRM page flip for endScene
    void drmPageFlip();

    //! Cursor control stub for FB device
    class CCursorControl : public gui::ICursorControl
    {
    public:
        CCursorControl(CIrrDeviceFB* dev) : device_(dev) {}
        virtual void setVisible(bool visible) { isVisible_ = visible; }
        virtual bool isVisible() const { return isVisible_; }
        virtual void setPosition(const core::position2d<f32>& pos)
        {
            setPosition((s32)(pos.X * device_->Width), (s32)(pos.Y * device_->Height));
        }
        virtual void setPosition(f32 x, f32 y) { setPosition((s32)(x * device_->Width), (s32)(y * device_->Height)); }
        virtual void setPosition(const core::position2d<s32>& pos) { setPosition(pos.X, pos.Y); }
        virtual void setPosition(s32 x, s32 y)
        {
            if (x < 0) x = 0;
            if (x >= (s32)device_->Width) x = device_->Width - 1;
            if (y < 0) y = 0;
            if (y >= (s32)device_->Height) y = device_->Height - 1;
            device_->mousePos_.X = x;
            device_->mousePos_.Y = y;
        }
        virtual const core::position2d<s32>& getPosition() { return device_->mousePos_; }
        virtual core::position2d<f32> getRelativePosition()
        {
            return core::position2d<f32>(
                device_->mousePos_.X / (f32)device_->Width,
                device_->mousePos_.Y / (f32)device_->Height);
        }
        virtual void setReferenceRect(core::rect<s32>* rect=0) {}
    private:
        CIrrDeviceFB* device_;
        bool isVisible_ = false;
    };

private:
    void createDriver();
    bool initDRM();
    bool initGBM();
    bool initEGL();
    bool initEGLSurfaceless(); // x86/Xvfb fallback: no DRM/GBM, pbuffer + llvmpipe
    void initEvdev();
    void pollEvdev();

    // P25: X11-windowed EGL backend (opt-in via requestX11Window()/--egl-x11)
    bool initEGLX11();
    void pollX11Events();
    void closeX11Window();

    // Keymap: Linux KEY_* -> Irrlicht EKEY_CODE
    void initKeyMap();
    EKEY_CODE linuxKeyToIrrlicht(int linuxKey) const;

    // DRM
    int drmFd_;
    uint32_t crtcId_;
    uint32_t connectorId_;
    drmModeModeInfo mode_;
    drmModeCrtc* savedCrtc_;

    // GBM
    struct gbm_device* gbmDevice_;
    struct gbm_surface* gbmSurface_;
    struct gbm_bo* previousBo_;

    // EGL
    EGLDisplay eglDisplay_;
    EGLSurface eglSurface_;
    EGLContext eglContext_;
    EGLConfig eglConfig_;

    // TTY keyboard mode (for disabling VT key processing)
    int ttyFd_;
    int savedKbMode_;

    // evdev input
    int keyboardFd_;
    int mouseFd_;
    core::position2d<s32> mousePos_;
    bool keyIsDown_[EVDEV_KEY_MAX];
    bool mouseButtonState_[3];  // Left, Right, Middle

    // Modifier tracking
    bool shiftDown_;
    bool ctrlDown_;
    bool altDown_;

    // Irrlicht keymap
    EKEY_CODE keyMap_[EVDEV_KEY_MAX];

    // Device state
    u32 Width, Height;
    bool Close;

    // First frame flag for DRM
    bool firstFrame_;

    // Page flip synchronization
    bool flipPending_;          // True if a page flip event hasn't been received yet
    void waitForFlipComplete(); // Poll DRM fd for page flip completion event

    // GLES2 mode (set before construction, affects EGL init and driver creation)
    bool useGLES2_;

    // True when running on the x86/Xvfb surfaceless-EGL fallback (no DRM/GBM).
    // present() and the destructor skip all GBM/KMS page-flip work in this mode.
    bool surfaceless_;

    // P25: X11-windowed EGL backend state. x11Display_/x11Window_/
    // x11WmDeleteMsg_ are stored as void*/unsigned long (rather than
    // Display*/Window/Atom) so this header doesn't need to include
    // <X11/Xlib.h> — only CIrrDeviceFB.cpp does, keeping X11 types out of
    // every other translation unit that includes this header for the EGL
    // accessors (COpenGLES2Driver.cpp, GPUUploadThread, etc.).
    void* x11Display_;            // Display*
    unsigned long x11Window_;     // Window (XID)
    unsigned long x11WmDeleteMsg_; // Atom (WM_DELETE_WINDOW)
    bool x11Active_;              // true when the X11-windowed backend is in use
};

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_
#endif // __C_IRR_DEVICE_FB_H_INCLUDED__
