// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright.txt in the distribution
// or at http://irrlicht.sourceforge.net/license/

// DRM/KMS/GBM/EGL replacement for CIrrDeviceFB
// Provides EDT_OPENGL support via EGL on GBM surfaces with DRM/KMS page flipping.
// No X11 required - renders directly to the display via kernel mode setting.

#ifndef __C_IRR_DEVICE_FB_H_INCLUDED__
#define __C_IRR_DEVICE_FB_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_FB_DEVICE_

#include "CIrrDeviceStub.h"
#include "SIrrCreationParameters.h"
#include "IImagePresenter.h"
#include "ICursorControl.h"

#include <EGL/egl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

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

    // EGL accessors for COpenGLDriver
    EGLDisplay getEGLDisplay() const { return eglDisplay_; }
    EGLSurface getEGLSurface() const { return eglSurface_; }
    EGLContext getEGLContext() const { return eglContext_; }

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
    void initEvdev();
    void pollEvdev();

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
    uint32_t previousFb_;

    // EGL
    EGLDisplay eglDisplay_;
    EGLSurface eglSurface_;
    EGLContext eglContext_;
    EGLConfig eglConfig_;

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
};

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_
#endif // __C_IRR_DEVICE_FB_H_INCLUDED__
