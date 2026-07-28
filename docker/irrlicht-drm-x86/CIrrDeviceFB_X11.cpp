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

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
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

// Keymap: X11 KeySym -> Irrlicht EKEY_CODE. Separate namespace from the
// evdev Linux KEY_* map above (X11 keysyms are XK_-prefixed, no collision
// with Irrlicht's EKEY_CODE enum names, so no #undef gymnastics needed).
static EKEY_CODE x11KeysymToIrrlicht(KeySym sym)
{
    switch (sym) {
        case XK_a: case XK_A: return KEY_KEY_A;
        case XK_b: case XK_B: return KEY_KEY_B;
        case XK_c: case XK_C: return KEY_KEY_C;
        case XK_d: case XK_D: return KEY_KEY_D;
        case XK_e: case XK_E: return KEY_KEY_E;
        case XK_f: case XK_F: return KEY_KEY_F;
        case XK_g: case XK_G: return KEY_KEY_G;
        case XK_h: case XK_H: return KEY_KEY_H;
        case XK_i: case XK_I: return KEY_KEY_I;
        case XK_j: case XK_J: return KEY_KEY_J;
        case XK_k: case XK_K: return KEY_KEY_K;
        case XK_l: case XK_L: return KEY_KEY_L;
        case XK_m: case XK_M: return KEY_KEY_M;
        case XK_n: case XK_N: return KEY_KEY_N;
        case XK_o: case XK_O: return KEY_KEY_O;
        case XK_p: case XK_P: return KEY_KEY_P;
        case XK_q: case XK_Q: return KEY_KEY_Q;
        case XK_r: case XK_R: return KEY_KEY_R;
        case XK_s: case XK_S: return KEY_KEY_S;
        case XK_t: case XK_T: return KEY_KEY_T;
        case XK_u: case XK_U: return KEY_KEY_U;
        case XK_v: case XK_V: return KEY_KEY_V;
        case XK_w: case XK_W: return KEY_KEY_W;
        case XK_x: case XK_X: return KEY_KEY_X;
        case XK_y: case XK_Y: return KEY_KEY_Y;
        case XK_z: case XK_Z: return KEY_KEY_Z;

        case XK_0: return KEY_KEY_0;
        case XK_1: return KEY_KEY_1;
        case XK_2: return KEY_KEY_2;
        case XK_3: return KEY_KEY_3;
        case XK_4: return KEY_KEY_4;
        case XK_5: return KEY_KEY_5;
        case XK_6: return KEY_KEY_6;
        case XK_7: return KEY_KEY_7;
        case XK_8: return KEY_KEY_8;
        case XK_9: return KEY_KEY_9;

        case XK_F1:  return KEY_F1;
        case XK_F2:  return KEY_F2;
        case XK_F3:  return KEY_F3;
        case XK_F4:  return KEY_F4;
        case XK_F5:  return KEY_F5;
        case XK_F6:  return KEY_F6;
        case XK_F7:  return KEY_F7;
        case XK_F8:  return KEY_F8;
        case XK_F9:  return KEY_F9;
        case XK_F10: return KEY_F10;
        case XK_F11: return KEY_F11;
        case XK_F12: return KEY_F12;

        case XK_Escape:      return KEY_ESCAPE;
        case XK_Tab:         return KEY_TAB;
        case XK_BackSpace:   return KEY_BACK;
        case XK_Return:      return KEY_RETURN;
        case XK_space:       return KEY_SPACE;
        case XK_Delete:      return KEY_DELETE;
        case XK_Insert:      return KEY_INSERT;
        case XK_Home:        return KEY_HOME;
        case XK_End:         return KEY_END;
        case XK_Prior:       return KEY_PRIOR;
        case XK_Next:        return KEY_NEXT;
        case XK_Caps_Lock:   return KEY_CAPITAL;
        case XK_Num_Lock:    return KEY_NUMLOCK;
        case XK_Scroll_Lock: return KEY_SCROLL;

        case XK_Up:    return KEY_UP;
        case XK_Down:  return KEY_DOWN;
        case XK_Left:  return KEY_LEFT;
        case XK_Right: return KEY_RIGHT;

        case XK_Shift_L:   return KEY_LSHIFT;
        case XK_Shift_R:   return KEY_RSHIFT;
        case XK_Control_L: return KEY_LCONTROL;
        case XK_Control_R: return KEY_RCONTROL;
        case XK_Alt_L:     return KEY_LMENU;
        case XK_Alt_R:     return KEY_RMENU;

        case XK_minus:         return KEY_MINUS;
        case XK_equal:         return KEY_PLUS;
        case XK_bracketleft:   return KEY_OEM_4;
        case XK_bracketright:  return KEY_OEM_6;
        case XK_semicolon:     return KEY_OEM_1;
        case XK_apostrophe:    return KEY_OEM_7;
        case XK_grave:         return KEY_OEM_3;
        case XK_backslash:     return KEY_OEM_5;
        case XK_comma:         return KEY_COMMA;
        case XK_period:        return KEY_PERIOD;
        case XK_slash:         return KEY_OEM_2;

        case XK_KP_0: return KEY_NUMPAD0;
        case XK_KP_1: return KEY_NUMPAD1;
        case XK_KP_2: return KEY_NUMPAD2;
        case XK_KP_3: return KEY_NUMPAD3;
        case XK_KP_4: return KEY_NUMPAD4;
        case XK_KP_5: return KEY_NUMPAD5;
        case XK_KP_6: return KEY_NUMPAD6;
        case XK_KP_7: return KEY_NUMPAD7;
        case XK_KP_8: return KEY_NUMPAD8;
        case XK_KP_9: return KEY_NUMPAD9;
        case XK_KP_Decimal:  return KEY_DECIMAL;
        case XK_KP_Enter:    return KEY_RETURN;
        case XK_KP_Add:      return KEY_ADD;
        case XK_KP_Subtract: return KEY_SUBTRACT;
        case XK_KP_Multiply: return KEY_MULTIPLY;
        case XK_KP_Divide:   return KEY_DIVIDE;

        default: return (EKEY_CODE)0;
    }
}

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
