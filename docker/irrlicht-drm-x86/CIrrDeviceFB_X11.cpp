// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright.txt in the distribution
// or at http://irrlicht.sourceforge.net/license/

// P25: X11-windowed EGL backend for CIrrDeviceFB (docker/irrlicht-drm-x86
// x86/Xvfb/llvmpipe fork only - see CIrrDeviceFB.h/.cpp for the surfaceless-
// EGL and DRM/KMS paths this sits alongside). Split into its own TU to keep
// CIrrDeviceFB.cpp under the project's file-size warning threshold
// (docs/decisions.md Batch F ceilings) - this is member-function-definition
// overflow for irr::CIrrDeviceFB, not a separate class.
//
// Explicit opt-in only (--egl-x11 CLI flag, plumbed via the WILLEQ_EGL_X11
// env var set by irrlicht_renderer.cpp — see the file-header comment in
// CIrrDeviceFB.h for why an env var rather than a direct call) — never
// triggered by $DISPLAY alone, so surfaceless stays the default and CI/
// goldens/benchmarks are unaffected. Opens a real X11 window, binds an EGL
// display to it via eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR) (falling
// back to the EXT proc / plain eglGetDisplay if the KHR entry point isn't
// exposed — same defensive-lookup style as initEGLSurfaceless() in
// CIrrDeviceFB.cpp), and creates a window surface with
// eglCreateWindowSurface. Input arrives via XNextEvent, translated in
// pollX11Events() (replaces evdev, which needs /dev/input access X11/Xvfb
// sessions don't grant).
//
// Resize: declined for round 1. The window is created with WM_NORMAL_HINTS
// min==max size, so well-behaved window managers won't offer a resize
// handle; ConfigureNotify/Expose are still handled (as no-ops) so an
// unexpected resize attempt or expose event doesn't crash. See
// refactor/P25_x11_egl_backend.md Review for the rationale.

#include "CIrrDeviceFB.h"

#ifdef _IRR_COMPILE_WITH_FB_DEVICE_

#include <cstdio>
#include <cstring>

#include "IEventReceiver.h"
#include "os.h"

// X11 headers: no #undef dance needed (X11 keysyms/macros don't collide
// with Irrlicht's EKEY_CODE enum names - only linux/input.h's KEY_* macros
// do, and that header isn't included here).
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

// B654 (issue #56): the X11-keysym -> EKEY_CODE decision lives in a pure,
// display-free header shared with the display-less unit test (see that file's
// header comment). It is copied into the build area next to this TU by
// scripts/build-gles2-x86.sh (same as the other driver headers, e.g.
// COpenGLES2Driver.h), so a plain relative include resolves it.
#include "x11_keysym_map.h"

namespace irr
{

bool CIrrDeviceFB::initEGLX11()
{
#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        os::Printer::log("EGL (X11): XOpenDisplay failed (no DISPLAY?)", ELL_ERROR);
        return false;
    }
    x11Display_ = dpy;

    typedef EGLDisplay (*GetPlatformDisplayProc)(EGLenum, void*, const EGLint*);
    GetPlatformDisplayProc getPlatformDisplay =
        (GetPlatformDisplayProc)eglGetProcAddress("eglGetPlatformDisplay");
    if (!getPlatformDisplay)
        getPlatformDisplay = (GetPlatformDisplayProc)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (getPlatformDisplay)
        eglDisplay_ = getPlatformDisplay(EGL_PLATFORM_X11_KHR, dpy, nullptr);
    if (eglDisplay_ == EGL_NO_DISPLAY)
        eglDisplay_ = eglGetDisplay((EGLNativeDisplayType)dpy);

    if (eglDisplay_ == EGL_NO_DISPLAY) {
        os::Printer::log("EGL (X11): eglGetDisplay failed", ELL_ERROR);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(eglDisplay_, &major, &minor)) {
        os::Printer::log("EGL (X11): eglInitialize failed", ELL_ERROR);
        return false;
    }

    char verMsg[160];
    snprintf(verMsg, sizeof(verMsg), "EGL (X11): Version %d.%d, vendor=%s", major, minor,
             eglQueryString(eglDisplay_, EGL_VENDOR));
    os::Printer::log(verMsg, ELL_INFORMATION);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        os::Printer::log("EGL (X11): EGL_OPENGL_ES_API not available", ELL_ERROR);
        return false;
    }
    useGLES2_ = true;

    // B971: follow the client's stencil request (see initEGL() in
    // CIrrDeviceFB.cpp for the full note).
    const bool wantStencil = CreationParams.Stencilbuffer;
    const int depthBits = wantStencil ? 24 : 16;
    const int stencilBits = wantStencil ? 8 : 0;
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, depthBits,
        EGL_STENCIL_SIZE, stencilBits,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
        os::Printer::log("EGL (X11): No config with depth+stencil, retrying without stencil", ELL_WARNING);
        EGLint fallbackAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(eglDisplay_, fallbackAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
            os::Printer::log("EGL (X11): eglChooseConfig failed", ELL_ERROR);
            return false;
        }
    }

    EGLint visualId = 0;
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_NATIVE_VISUAL_ID, &visualId);

    Window root = DefaultRootWindow(dpy);
    Visual* visual = nullptr;
    int depth = 0;
    XVisualInfo visTemplate;
    visTemplate.visualid = (VisualID)visualId;
    int numVisuals = 0;
    XVisualInfo* visInfo = (visualId != 0)
        ? XGetVisualInfo(dpy, VisualIDMask, &visTemplate, &numVisuals)
        : nullptr;
    if (visInfo && numVisuals > 0) {
        visual = visInfo->visual;
        depth = visInfo->depth;
    } else {
        // Fallback: default visual (works fine for llvmpipe/Mesa in practice)
        visual = DefaultVisual(dpy, DefaultScreen(dpy));
        depth = DefaultDepth(dpy, DefaultScreen(dpy));
    }

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = XCreateColormap(dpy, root, visual, AllocNone);
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | ExposureMask;
    swa.background_pixel = 0;
    swa.border_pixel = 0;

    Window win = XCreateWindow(dpy, root, 0, 0, Width, Height, 0, depth, InputOutput,
                                visual, CWColormap | CWEventMask | CWBackPixel | CWBorderPixel, &swa);
    if (visInfo)
        XFree(visInfo);

    if (!win) {
        os::Printer::log("EGL (X11): XCreateWindow failed", ELL_ERROR);
        return false;
    }
    x11Window_ = win;

    XStoreName(dpy, win, "WillEQ");

    // Decline resize for round 1: min == max via WM_NORMAL_HINTS so
    // well-behaved window managers won't offer a resize handle.
    XSizeHints* hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = (int)Width;
        hints->min_height = hints->max_height = (int)Height;
        XSetWMNormalHints(dpy, win, hints);
        XFree(hints);
    }

    // WM_DELETE_WINDOW: handle window-close via a client message rather than
    // letting the WM sever the X connection, so run() can shut down cleanly.
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    x11WmDeleteMsg_ = wmDelete;

    XMapWindow(dpy, win);
    XFlush(dpy);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        os::Printer::log("EGL (X11): eglCreateContext failed", ELL_ERROR);
        return false;
    }

    eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_, (EGLNativeWindowType)win, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        os::Printer::log("EGL (X11): eglCreateWindowSurface failed", ELL_ERROR);
        return false;
    }

    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        os::Printer::log("EGL (X11): eglMakeCurrent failed", ELL_ERROR);
        return false;
    }

    os::Printer::log("EGL (X11): Context created and window mapped (--egl-x11)", ELL_INFORMATION);
    return true;
}

void CIrrDeviceFB::closeX11Window()
{
    if (!x11Display_) return;
    Display* dpy = (Display*)x11Display_;
    if (x11Window_)
        XDestroyWindow(dpy, x11Window_);
    XCloseDisplay(dpy);
    x11Display_ = nullptr;
    x11Window_ = 0;
}

// Keymap: X11 KeySym -> Irrlicht EKEY_CODE. B654 (issue #56): the map itself
// moved to the pure, display-free header included at the top of this file
// (x11_keysym_map.h, written into the build area by scripts/build-gles2-x86.sh)
// so the normal test build can pin it. Separate namespace from the evdev Linux
// KEY_* map above (X11 keysyms are XK_-prefixed, no collision with Irrlicht's
// EKEY_CODE enum names, so no #undef gymnastics needed).

void CIrrDeviceFB::pollX11Events()
{
    if (!x11Display_) return;
    Display* dpy = (Display*)x11Display_;

    while (XPending(dpy) > 0) {
        XEvent xev;
        XNextEvent(dpy, &xev);

        switch (xev.type) {
            case ClientMessage:
                if ((Atom)xev.xclient.data.l[0] == (Atom)x11WmDeleteMsg_)
                    Close = true;
                break;

            case KeyPress:
            case KeyRelease: {
                bool pressed = (xev.type == KeyPress);
                KeySym sym = XLookupKeysym(&xev.xkey, 0);
                unsigned int state = xev.xkey.state;
                shiftDown_ = (state & ShiftMask) != 0;
                ctrlDown_  = (state & ControlMask) != 0;
                altDown_   = (state & Mod1Mask) != 0;

                EKEY_CODE irrKey = x11KeysymToIrrlicht(sym);

                SEvent event;
                event.EventType = EET_KEY_INPUT_EVENT;
                event.KeyInput.Key = irrKey;
                event.KeyInput.PressedDown = pressed;
                event.KeyInput.Shift = shiftDown_;
                event.KeyInput.Control = ctrlDown_;
                event.KeyInput.Char = 0;

                if (pressed) {
                    char buf[8] = {0};
                    XLookupString(&xev.xkey, buf, sizeof(buf) - 1, nullptr, nullptr);
                    if (buf[0])
                        event.KeyInput.Char = (wchar_t)(unsigned char)buf[0];
                }

                postEventFromUser(event);
                break;
            }

            case ButtonPress:
            case ButtonRelease: {
                bool pressed = (xev.type == ButtonPress);
                mousePos_.X = xev.xbutton.x;
                mousePos_.Y = xev.xbutton.y;

                SEvent event;
                event.EventType = EET_MOUSE_INPUT_EVENT;
                event.MouseInput.X = mousePos_.X;
                event.MouseInput.Y = mousePos_.Y;
                event.MouseInput.Wheel = 0.0f;

                switch (xev.xbutton.button) {
                    case Button1:
                        mouseButtonState_[0] = pressed;
                        event.MouseInput.Event = pressed ? EMIE_LMOUSE_PRESSED_DOWN : EMIE_LMOUSE_LEFT_UP;
                        break;
                    case Button3:
                        mouseButtonState_[1] = pressed;
                        event.MouseInput.Event = pressed ? EMIE_RMOUSE_PRESSED_DOWN : EMIE_RMOUSE_LEFT_UP;
                        break;
                    case Button2:
                        mouseButtonState_[2] = pressed;
                        event.MouseInput.Event = pressed ? EMIE_MMOUSE_PRESSED_DOWN : EMIE_MMOUSE_LEFT_UP;
                        break;
                    case Button4: // wheel up
                    case Button5: // wheel down
                        if (!pressed) { break; }
                        event.MouseInput.Event = EMIE_MOUSE_WHEEL;
                        event.MouseInput.Wheel = (xev.xbutton.button == Button4) ? 1.0f : -1.0f;
                        break;
                    default:
                        break;
                }

                event.MouseInput.ButtonStates = 0;
                if (mouseButtonState_[0]) event.MouseInput.ButtonStates |= EMBSM_LEFT;
                if (mouseButtonState_[1]) event.MouseInput.ButtonStates |= EMBSM_RIGHT;
                if (mouseButtonState_[2]) event.MouseInput.ButtonStates |= EMBSM_MIDDLE;

                postEventFromUser(event);
                break;
            }

            case MotionNotify: {
                mousePos_.X = xev.xmotion.x;
                mousePos_.Y = xev.xmotion.y;
                SEvent event;
                event.EventType = EET_MOUSE_INPUT_EVENT;
                event.MouseInput.Event = EMIE_MOUSE_MOVED;
                event.MouseInput.X = mousePos_.X;
                event.MouseInput.Y = mousePos_.Y;
                event.MouseInput.ButtonStates = 0;
                if (mouseButtonState_[0]) event.MouseInput.ButtonStates |= EMBSM_LEFT;
                if (mouseButtonState_[1]) event.MouseInput.ButtonStates |= EMBSM_RIGHT;
                if (mouseButtonState_[2]) event.MouseInput.ButtonStates |= EMBSM_MIDDLE;
                postEventFromUser(event);
                break;
            }

            case ConfigureNotify:
                // Round 1: resize declined (fixed-size window via WM_NORMAL_HINTS
                // in initEGLX11()); handled here only so an unexpected
                // ConfigureNotify (e.g. a WM ignoring the hints) doesn't fall
                // through to `default` unnoticed. No actual resize logic.
                break;

            case Expose:
                // No dirty-rect tracking needed - the render loop redraws
                // every frame regardless.
                break;

            default:
                break;
        }
    }
}


} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_
