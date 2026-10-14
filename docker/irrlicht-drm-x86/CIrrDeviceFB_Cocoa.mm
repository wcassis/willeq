// B1044 (#364): the Cocoa windowed EGL backend for CIrrDeviceFB on macOS --
// the Darwin counterpart of CIrrDeviceFB_X11.cpp (P25's --egl-x11).
//
// Explicit opt-in only, exactly like X11: `--egl-window` (or `--egl-x11`,
// which on Darwin prints a hint and means the same) makes the client set
// WILLEQ_EGL_X11=1 before createDeviceEx(); the device constructor then calls
// initEGLX11(), which on Darwin is the body below. Without the flag the device
// takes the ANGLE/Metal pbuffer path (CIrrDeviceFB.cpp initEGLSurfaceless()),
// so gles2_zone_benchmark, the visual gate and CI are unchanged.
//
// What it does:
//   - NSApplication (regular activation policy) + one titled, closable,
//     resizable NSWindow whose content view HOSTS a CAMetalLayer with
//     contentsScale 1, so the EGL surface is Width x Height PIXELS -- the -r
//     resolution, the same render size the X11 window and the pbuffer give --
//     and the compositor scales it on a Retina display.
//   - ANGLE's Metal EGL display (EGL_PLATFORM_ANGLE_ANGLE + TYPE_METAL) and
//     eglCreateWindowSurface on that CAMetalLayer (ANGLE takes a CALayer* as
//     its native window; B1043 proved the call, review/B1043/egl_probe/).
//   - NSEvent -> Irrlicht events in pollX11Events() (same member name as the
//     X11 TU, so CIrrDeviceFB.cpp's run() is unchanged): keys through the
//     pure map cocoa_keycode_map.h, modifiers from flagsChanged with the
//     per-side device bits, mouse move/drag/buttons/wheel with the X11
//     device's coordinate convention (origin top-left, render pixels), window
//     close -> run() returns false, Command-Q -> close.
//   - Resize: the window is resizable; on windowDidResize the device adopts
//     the content size, re-frames the layer and calls VideoDriver->OnResize().
//     (X11 declined resize in round 1; here the OS offers the handle anyway.)
//   - present() pumps events itself when nothing has called run() since the
//     last frame (gles2_zone_benchmark never calls run()), so a windowed tool
//     stays responsive and its window paints.
//   - WILLEQ_INPUT_TRACE=1 prints one `[cocoa-input]` line per delivered
//     event to stderr; teardown always prints one `[cocoa-input] summary`
//     line with the per-kind counts (the B1044 window-proof script reads it).
//
// A process with no WindowServer session (ssh, launchd background) cannot show
// a window: initEGLX11() checks CGSessionCopyCurrentDictionary() first and
// returns false with "no WindowServer session: run this at the console", and
// the constructor falls back to the pbuffer path (the X11 fallback's shape).
//
// Compiled only on Darwin (scripts/build-gles2-darwin.sh adds the object and
// compiles it with -fobjc-arc); the body is guarded so a stray Linux compile
// is empty. Linux never sees this file in a Makefile.

#include "CIrrDeviceFB.h"

#if defined(_IRR_COMPILE_WITH_FB_DEVICE_) && defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <CoreGraphics/CoreGraphics.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "IEventReceiver.h"
#include "IVideoDriver.h"
#include "os.h"

#include "cocoa_keycode_map.h"

// Per-side modifier bits in -[NSEvent modifierFlags] (IOKit's
// IOLLEvent.h NX_DEVICE*KEYMASK; not exported by AppKit).
static const NSUInteger kDevLCtl = 0x00000001, kDevLShift = 0x00000002, kDevRShift = 0x00000004,
                        kDevLAlt = 0x00000020, kDevRAlt = 0x00000040, kDevRCtl = 0x00002000;

@interface WillEQCocoaView : NSView
@end

@implementation WillEQCocoaView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
// Keys are translated in pollX11Events() and never forwarded to AppKit; these
// are only here so a stray forward cannot beep.
- (void)keyDown:(NSEvent*)event {}
- (void)keyUp:(NSEvent*)event {}
- (void)flagsChanged:(NSEvent*)event {}
@end

@interface WillEQCocoaState : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) WillEQCocoaView* view;
@property(strong) CAMetalLayer* layer;
@property BOOL closeRequested;
@property BOOL resized;
@property BOOL polledSinceLastPresent;
@property double wheelAccum;
@end

@implementation WillEQCocoaState
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    self.closeRequested = YES;
    return NO;  // the device tears the window down in its destructor
}
- (void)windowDidResize:(NSNotification*)note { self.resized = YES; }
- (void)windowDidBecomeKey:(NSNotification*)note { fprintf(stderr, "[cocoa-window] key window: yes\n"); }
- (void)windowDidResignKey:(NSNotification*)note { fprintf(stderr, "[cocoa-window] key window: no\n"); }
@end

namespace
{
struct CocoaInputCounts {
    unsigned keyDown = 0, keyUp = 0, mouseDown = 0, mouseUp = 0, mouseMove = 0, wheel = 0, resize = 0;
};
CocoaInputCounts g_counts;
bool g_trace = false;

WillEQCocoaState* cocoaState(void* p) { return (__bridge WillEQCocoaState*)p; }
} // namespace

namespace irr
{

bool CIrrDeviceFB::initEGLX11()
{
    g_trace = getenv("WILLEQ_INPUT_TRACE") != nullptr;

    // A window needs a WindowServer (Aqua) session. ssh and launchd background
    // sessions have none: AppKit would still hand out an NSWindow with
    // windowNumber 0 (B1043), but nothing would ever appear on a screen.
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session) {
        os::Printer::log("EGL (Cocoa): no WindowServer session: run this at the console "
                         "(an ssh login cannot open a window)", ELL_ERROR);
        return false;
    }
    CFRelease(session);

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        WillEQCocoaState* st = [[WillEQCocoaState alloc] init];
        const NSRect rect = NSMakeRect(80, 80, Width > 0 ? Width : 800, Height > 0 ? Height : 600);
        st.window = [[NSWindow alloc] initWithContentRect:rect
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        st.window.releasedWhenClosed = NO;
        st.window.title = @"WillEQ";
        st.window.delegate = st;
        st.window.acceptsMouseMovedEvents = YES;

        st.view = [[WillEQCocoaView alloc] initWithFrame:NSMakeRect(0, 0, rect.size.width, rect.size.height)];
        st.layer = [CAMetalLayer layer];
        st.layer.contentsScale = 1.0;  // render at -r pixels; the compositor scales on Retina
        st.layer.frame = st.view.bounds;
        st.view.layer = st.layer;       // layer-HOSTING view: we own the layer
        st.view.wantsLayer = YES;
        st.window.contentView = st.view;
        [st.window makeFirstResponder:st.view];
        [st.window center];
        [st.window makeKeyAndOrderFront:nil];
        // A process started from Terminal is not a bundled app: ask for
        // activation every way macOS offers (14+ made activation cooperative,
        // and the deprecated call alone can leave Terminal the key app, so
        // keys keep going to Terminal until the window is clicked).
        if (@available(macOS 14.0, *)) [NSApp activate];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
        [[NSRunningApplication currentApplication]
            activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop

        x11Display_ = (__bridge_retained void*)st;  // the Cocoa state; released in closeX11Window()
        x11Window_ = (unsigned long)st.window.windowNumber;

        // A failure from here on closes the window and drops every EGL handle,
        // so the constructor's pbuffer fallback starts clean.
        auto fail = [this](const char* why) {
            os::Printer::log(why, ELL_ERROR);
            if (eglDisplay_ != EGL_NO_DISPLAY) {
                eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglSurface_);
                if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
            }
            eglSurface_ = EGL_NO_SURFACE;
            eglContext_ = EGL_NO_CONTEXT;
            eglDisplay_ = EGL_NO_DISPLAY;
            closeX11Window();
            return false;
        };

        typedef EGLDisplay (*GetPlatformDisplayEXT)(EGLenum, void*, const EGLint*);
        GetPlatformDisplayEXT getPlatformDisplay =
            (GetPlatformDisplayEXT)eglGetProcAddress("eglGetPlatformDisplayEXT");
        static const EGLint angleMetal[] = {
            0x3203 /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */, 0x3489 /* EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE */,
            EGL_NONE
        };
        if (getPlatformDisplay)
            eglDisplay_ = getPlatformDisplay(0x3202 /* EGL_PLATFORM_ANGLE_ANGLE */, nullptr, angleMetal);
        if (eglDisplay_ == EGL_NO_DISPLAY)
            eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay_ == EGL_NO_DISPLAY) {
            return fail("EGL (Cocoa): eglGetDisplay failed");
        }

        EGLint major = 0, minor = 0;
        if (!eglInitialize(eglDisplay_, &major, &minor)) {
            return fail("EGL (Cocoa): eglInitialize failed");
        }
        char verMsg[160];
        snprintf(verMsg, sizeof(verMsg), "EGL (Cocoa): Version %d.%d, vendor=%s", major, minor,
                 eglQueryString(eglDisplay_, EGL_VENDOR));
        os::Printer::log(verMsg, ELL_INFORMATION);

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            return fail("EGL (Cocoa): EGL_OPENGL_ES_API not available");
        }
        useGLES2_ = true;

        // B971: follow the client's stencil request (same as the X11 path).
        const bool wantStencil = CreationParams.Stencilbuffer;
        EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
            EGL_DEPTH_SIZE, wantStencil ? 24 : 16,
            EGL_STENCIL_SIZE, wantStencil ? 8 : 0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        EGLint numConfigs = 0;
        if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs < 1) {
            return fail("EGL (Cocoa): eglChooseConfig failed");
        }

        EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext_ == EGL_NO_CONTEXT) {
            return fail("EGL (Cocoa): eglCreateContext failed");
        }

        eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_,
                                             (EGLNativeWindowType)(__bridge void*)st.layer, nullptr);
        if (eglSurface_ == EGL_NO_SURFACE) {
            char m[96];
            snprintf(m, sizeof(m), "EGL (Cocoa): eglCreateWindowSurface failed 0x%x", eglGetError());
            return fail(m);
        }
        if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
            return fail("EGL (Cocoa): eglMakeCurrent failed");
        }

        EGLint sw = 0, sh = 0;
        eglQuerySurface(eglDisplay_, eglSurface_, EGL_WIDTH, &sw);
        eglQuerySurface(eglDisplay_, eglSurface_, EGL_HEIGHT, &sh);
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "EGL (Cocoa): window %u created %ux%u, surface %dx%d, backing scale %.1f (--egl-window)",
                 (unsigned)st.window.windowNumber, Width, Height, sw, sh, st.window.backingScaleFactor);
        os::Printer::log(msg, ELL_INFORMATION);
        fprintf(stderr, "[cocoa-window] created window=%u size=%ux%u surface=%dx%d scale=%.1f active=%d key=%d\n",
                (unsigned)st.window.windowNumber, Width, Height, sw, sh, st.window.backingScaleFactor,
                NSApp.active ? 1 : 0, st.window.keyWindow ? 1 : 0);
    }
    return true;
}

void CIrrDeviceFB::closeX11Window()
{
    if (!x11Display_) return;
    @autoreleasepool {
        WillEQCocoaState* st = (__bridge_transfer WillEQCocoaState*)x11Display_;
        x11Display_ = nullptr;
        x11Window_ = 0;
        st.window.delegate = nil;
        [st.window orderOut:nil];
        [st.window close];
        st.window = nil;
    }
    fprintf(stderr,
            "[cocoa-input] summary key_down=%u key_up=%u mouse_down=%u mouse_up=%u mouse_move=%u wheel=%u resize=%u\n",
            g_counts.keyDown, g_counts.keyUp, g_counts.mouseDown, g_counts.mouseUp, g_counts.mouseMove,
            g_counts.wheel, g_counts.resize);
}

void CIrrDeviceFB::setCocoaWindowCaption(const char* utf8)
{
    if (!x11Display_ || !utf8) return;
    @autoreleasepool {
        NSString* title = [NSString stringWithUTF8String:utf8];
        if (title) cocoaState(x11Display_).window.title = title;
    }
}

void CIrrDeviceFB::pumpCocoaIfIdle()
{
    if (!x11Display_) return;
    WillEQCocoaState* st = cocoaState(x11Display_);
    if (!st.polledSinceLastPresent)
        pollX11Events();
    st.polledSinceLastPresent = NO;
}

void CIrrDeviceFB::pollX11Events()
{
    if (!x11Display_) return;
    @autoreleasepool {
        WillEQCocoaState* st = cocoaState(x11Display_);
        st.polledSinceLastPresent = YES;

        auto buttonStates = [this]() {
            u32 s = 0;
            if (mouseButtonState_[0]) s |= EMBSM_LEFT;
            if (mouseButtonState_[1]) s |= EMBSM_RIGHT;
            if (mouseButtonState_[2]) s |= EMBSM_MIDDLE;
            return s;
        };
        // Window point -> render pixel, origin top-left (the X11 convention).
        // Returns false for a point outside the content view (the title bar).
        auto toPixel = [&](NSEvent* e) {
            const NSPoint p = [st.view convertPoint:e.locationInWindow fromView:nil];
            const NSRect b = st.view.bounds;
            if (b.size.width <= 0 || b.size.height <= 0 || !NSPointInRect(p, b)) return false;
            mousePos_.X = (s32)(p.x * Width / b.size.width);
            mousePos_.Y = (s32)((b.size.height - p.y) * Height / b.size.height);
            return true;
        };
        auto postKey = [&](EKEY_CODE key, bool down, wchar_t ch, unsigned short code) {
            SEvent ev;
            ev.EventType = EET_KEY_INPUT_EVENT;
            ev.KeyInput.Key = key;
            ev.KeyInput.PressedDown = down;
            ev.KeyInput.Shift = shiftDown_;
            ev.KeyInput.Control = ctrlDown_;
            ev.KeyInput.Char = ch;
            down ? ++g_counts.keyDown : ++g_counts.keyUp;
            if (g_trace)
                fprintf(stderr, "[cocoa-input] key %s code=0x%02x irr=%d char=%d shift=%d ctrl=%d alt=%d\n",
                        down ? "down" : "up", code, (int)key, (int)ch, shiftDown_, ctrlDown_, altDown_);
            postEventFromUser(ev);
        };
        auto postMouse = [&](EMOUSE_INPUT_EVENT kind, f32 wheel) {
            SEvent ev;
            ev.EventType = EET_MOUSE_INPUT_EVENT;
            ev.MouseInput.Event = kind;
            ev.MouseInput.X = mousePos_.X;
            ev.MouseInput.Y = mousePos_.Y;
            ev.MouseInput.Wheel = wheel;
            ev.MouseInput.Shift = shiftDown_;
            ev.MouseInput.Control = ctrlDown_;
            ev.MouseInput.ButtonStates = buttonStates();
            const char* name = "move";
            switch (kind) {
                case EMIE_MOUSE_MOVED: ++g_counts.mouseMove; break;
                case EMIE_MOUSE_WHEEL: ++g_counts.wheel; name = "wheel"; break;
                case EMIE_LMOUSE_PRESSED_DOWN: case EMIE_RMOUSE_PRESSED_DOWN: case EMIE_MMOUSE_PRESSED_DOWN:
                    ++g_counts.mouseDown; name = "down"; break;
                default: ++g_counts.mouseUp; name = "up"; break;
            }
            if (g_trace)
                fprintf(stderr, "[cocoa-input] mouse %s event=%d x=%d y=%d wheel=%.0f buttons=0x%x\n", name,
                        (int)kind, mousePos_.X, mousePos_.Y, wheel, ev.MouseInput.ButtonStates);
            postEventFromUser(ev);
        };

        NSEvent* e;
        while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil
                                          inMode:NSDefaultRunLoopMode dequeue:YES])) {
            const NSEventModifierFlags flags = e.modifierFlags;
            const NSEventType type = e.type;
            if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp || type == NSEventTypeFlagsChanged ||
                (type >= NSEventTypeLeftMouseDown && type <= NSEventTypeOtherMouseDragged)) {
                shiftDown_ = (flags & NSEventModifierFlagShift) != 0;
                ctrlDown_ = (flags & NSEventModifierFlagControl) != 0;
                altDown_ = (flags & NSEventModifierFlagOption) != 0;
            }
            bool forward = true;  // hand the event on to AppKit (window drag/resize/close)
            switch (type) {
                case NSEventTypeKeyDown:
                case NSEventTypeKeyUp: {
                    forward = false;  // translated here; AppKit would only beep
                    const bool down = (type == NSEventTypeKeyDown);
                    const unsigned short code = e.keyCode;
                    if (down && (flags & NSEventModifierFlagCommand) && code == 0x0C /* kVK_ANSI_Q */) {
                        Close = true;
                        break;
                    }
                    wchar_t ch = 0;
                    if (down && e.characters.length > 0) {
                        const unichar u = [e.characters characterAtIndex:0];
                        if (u < 0xF700 || u > 0xF8FF)  // AppKit's private-use function-key range -> no char
                            ch = (code == 0x33 && u == 0x7F) ? 0x08 : (wchar_t)u;  // backspace as X11's BS
                    }
                    postKey(cocoaKeycodeToIrrlicht(code), down, ch, code);
                    break;
                }
                case NSEventTypeFlagsChanged: {
                    forward = false;
                    const unsigned short code = e.keyCode;
                    NSUInteger bit = 0;
                    switch (code) {
                        case 0x38: bit = kDevLShift; break;
                        case 0x3C: bit = kDevRShift; break;
                        case 0x3B: bit = kDevLCtl; break;
                        case 0x3E: bit = kDevRCtl; break;
                        case 0x3A: bit = kDevLAlt; break;
                        case 0x3D: bit = kDevRAlt; break;
                        case 0x39:  // Caps Lock reports a toggle, not press/release: send both
                            postKey(KEY_CAPITAL, true, 0, code);
                            postKey(KEY_CAPITAL, false, 0, code);
                            break;
                        default: break;  // Command / Fn: unmapped, like X11's Super
                    }
                    if (bit) postKey(cocoaKeycodeToIrrlicht(code), (flags & bit) != 0, 0, code);
                    break;
                }
                case NSEventTypeLeftMouseDown: case NSEventTypeLeftMouseUp:
                case NSEventTypeRightMouseDown: case NSEventTypeRightMouseUp:
                case NSEventTypeOtherMouseDown: case NSEventTypeOtherMouseUp: {
                    const bool down = (type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
                                       type == NSEventTypeOtherMouseDown);
                    if (!toPixel(e) && down) break;  // a title-bar click belongs to AppKit
                    int idx = 0;
                    EMOUSE_INPUT_EVENT kind;
                    if (type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseUp) {
                        idx = 0; kind = down ? EMIE_LMOUSE_PRESSED_DOWN : EMIE_LMOUSE_LEFT_UP;
                    } else if (type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseUp) {
                        idx = 1; kind = down ? EMIE_RMOUSE_PRESSED_DOWN : EMIE_RMOUSE_LEFT_UP;
                    } else {
                        if (e.buttonNumber != 2) break;  // only the middle button, like X11 Button2
                        idx = 2; kind = down ? EMIE_MMOUSE_PRESSED_DOWN : EMIE_MMOUSE_LEFT_UP;
                    }
                    if (!down && !mouseButtonState_[idx]) break;  // a release whose press AppKit had
                    mouseButtonState_[idx] = down;
                    postMouse(kind, 0.0f);
                    break;
                }
                case NSEventTypeMouseMoved: case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged: case NSEventTypeOtherMouseDragged:
                    if (toPixel(e)) postMouse(EMIE_MOUSE_MOVED, 0.0f);
                    break;
                case NSEventTypeScrollWheel: {
                    if (!toPixel(e)) break;
                    // X11 delivers +1 per notch for a PHYSICAL wheel-up (Button4).
                    // Undo "natural scrolling" so the sign is the physical one.
                    double dy = e.scrollingDeltaY;
                    if (e.directionInvertedFromDevice) dy = -dy;
                    if (!e.hasPreciseScrollingDeltas) {
                        if (dy != 0.0) postMouse(EMIE_MOUSE_WHEEL, dy > 0 ? 1.0f : -1.0f);
                    } else {  // trackpad: pixel deltas, one notch per 20 points
                        st.wheelAccum += dy;
                        while (std::fabs(st.wheelAccum) >= 20.0) {
                            const f32 notch = st.wheelAccum > 0 ? 1.0f : -1.0f;
                            st.wheelAccum -= notch * 20.0;
                            postMouse(EMIE_MOUSE_WHEEL, notch);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            if (forward) [NSApp sendEvent:e];
        }
        [NSApp updateWindows];

        if (st.closeRequested) Close = true;
        if (st.resized) {
            st.resized = NO;
            const NSSize s = st.view.bounds.size;
            if (s.width >= 1 && s.height >= 1 && ((u32)s.width != Width || (u32)s.height != Height)) {
                st.layer.frame = st.view.bounds;
                Width = (u32)s.width;
                Height = (u32)s.height;
                CreationParams.WindowSize = core::dimension2d<u32>(Width, Height);
                if (VideoDriver) VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
                ++g_counts.resize;
                if (g_trace) fprintf(stderr, "[cocoa-input] resize %ux%u\n", Width, Height);
            }
        }
    }
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_FB_DEVICE_ && __APPLE__
