// B1043 (#363): Darwin (macOS) member stubs for CIrrDeviceFB.
//
// On Darwin the device has no DRM/KMS, GBM, evdev, VT or X11: CIrrDeviceFB.cpp
// and CIrrDeviceFB_X11.cpp compile those member functions out under
// `#ifndef __APPLE__`, and this TU supplies the Darwin definitions. initDRM()
// returns false, so the constructor's existing fallback runs
// initEGLSurfaceless(): on ANGLE the Mesa surfaceless platform is refused and
// eglGetDisplay(EGL_DEFAULT_DISPLAY) gives ANGLE's default (Metal) display,
// with a pbuffer surface -- the headless shape gles2_zone_benchmark and the
// visual gate use on x86. The opt-in window + input (--egl-window, the
// parallel of --egl-x11) is CIrrDeviceFB_Cocoa.mm (B1044).
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

bool CIrrDeviceFB::waitForFlipComplete() { return true; }

void CIrrDeviceFB::drmPageFlip()
{
    // Unreachable on Darwin (surfaceless_ is always true after a successful
    // init); swap anyway so a caller never loses a frame.
    eglSwapBuffers(eglDisplay_, eglSurface_);
}

void CIrrDeviceFB::initEvdev() {}
void CIrrDeviceFB::pollEvdev() {}
void CIrrDeviceFB::initKeyMap() {}

// B1044 (#364): initEGLX11() / pollX11Events() / closeX11Window() are the
// Cocoa window backend on Darwin, in CIrrDeviceFB_Cocoa.mm.

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_ && __APPLE__
