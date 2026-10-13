// B1043 (#363): Darwin (macOS) member stubs for CIrrDeviceFB.
//
// On Darwin the device has no DRM/KMS, GBM, evdev, VT or X11: CIrrDeviceFB.cpp
// and CIrrDeviceFB_X11.cpp compile those member functions out under
// `#ifndef __APPLE__`, and this TU supplies the Darwin definitions. initDRM()
// returns false, so the constructor's existing fallback runs
// initEGLSurfaceless(): on ANGLE the Mesa surfaceless platform is refused and
// eglGetDisplay(EGL_DEFAULT_DISPLAY) gives ANGLE's default (Metal) display,
// with a pbuffer surface -- the headless shape gles2_zone_benchmark and the
// visual gate use on x86. No input and no window: a Cocoa window path (the
// parallel of --egl-x11) and Cocoa input are the FIX card's.
//
// Compiled only on Darwin (scripts/build-gles2-darwin.sh adds the object);
// the whole body is guarded so a stray Linux compile is empty.

#include "CIrrDeviceFB.h"

#if defined(_IRR_COMPILE_WITH_FB_DEVICE_) && defined(__APPLE__)

#include "os.h"

namespace irr
{

bool CIrrDeviceFB::initDRM()
{
    os::Printer::log("DRM: not available on Darwin (B1043) - using the pbuffer EGL path", ELL_INFORMATION);
    return false;
}

bool CIrrDeviceFB::initGBM() { return false; }
bool CIrrDeviceFB::initEGL() { return false; }

void CIrrDeviceFB::waitForFlipComplete() {}

void CIrrDeviceFB::drmPageFlip()
{
    // Unreachable on Darwin (surfaceless_ is always true after a successful
    // init); swap anyway so a caller never loses a frame.
    eglSwapBuffers(eglDisplay_, eglSurface_);
}

void CIrrDeviceFB::initEvdev() {}
void CIrrDeviceFB::pollEvdev() {}
void CIrrDeviceFB::initKeyMap() {}

bool CIrrDeviceFB::initEGLX11()
{
    os::Printer::log("--egl-x11: no X11 on Darwin (B1043)", ELL_ERROR);
    return false;
}

void CIrrDeviceFB::closeX11Window() {}
void CIrrDeviceFB::pollX11Events() {}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_ && __APPLE__
