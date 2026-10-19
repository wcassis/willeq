// B1023 LD_PRELOAD shim: log every EGL config the client's eglChooseConfig
// returns, in order, with the attribs the client passed. Run:
//   LD_PRELOAD=/tmp/b1023_eglshim.so MESA_B1023_OUT=/tmp/b1023_shim.log <client>
// Writes to MESA_B1023_OUT (default /tmp/b1023_shim.log); fd kept open the
// whole run (the client may fork; logs go to the main process's fd).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// EGL constants (values verified against the GLVND egl.h on the Pi run).
#define EGL_SURFACE_TYPE 0x3031
#define EGL_RED_SIZE 0x3032
#define EGL_GREEN_SIZE 0x3033
#define EGL_BLUE_SIZE 0x3034
#define EGL_ALPHA_SIZE 0x3035
#define EGL_DEPTH_SIZE 0x3036
#define EGL_STENCIL_SIZE 0x3037
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_NATIVE_VISUAL_ID 0x3041

typedef void* EGLDisplay;
typedef void* EGLConfig;

static int log_fd = -1;

static void ensure_log(void) {
    if (log_fd >= 0)
        return;
    const char* out = getenv("MESA_B1023_OUT");
    if (!out)
        out = "/tmp/b1023_shim.log";
    log_fd = open(out, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (log_fd < 0)
        log_fd = -2;  // never retry
}

static void b1023logf(const char* fmt, ...) {
    if (log_fd < 0)
        ensure_log();
    if (log_fd < 0)
        return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        (void)!write(log_fd, buf, (size_t)n);
}

static void log_attribs(const int* a) {
    if (!a) {
        b1023logf("attribs=(null)\n");
        return;
    }
    b1023logf("attribs=[");
    for (int i = 0; a[i] != 0 && i < 64; ++i)
        b1023logf("%s0x%x=%d", i ? "," : "", a[i], a[i + 1]);
    b1023logf("]\n");
}

static void log_config(EGLDisplay d, int idx, EGLConfig cfg) {
    typedef int (*pGetAttr)(EGLDisplay, EGLConfig, int, int*);
    pGetAttr getAttr = (pGetAttr)dlsym(RTLD_NEXT, "eglGetConfigAttrib");
    int vis = 0, rt = 0, dep = 0, st = 0, ss = 0, rs = 0, gs = 0, bs = 0, as_ = 0;
    getAttr(d, cfg, EGL_NATIVE_VISUAL_ID, &vis);
    getAttr(d, cfg, EGL_RENDERABLE_TYPE, &rt);
    getAttr(d, cfg, EGL_DEPTH_SIZE, &dep);
    getAttr(d, cfg, EGL_STENCIL_SIZE, &st);
    getAttr(d, cfg, EGL_SURFACE_TYPE, &ss);
    getAttr(d, cfg, EGL_RED_SIZE, &rs);
    getAttr(d, cfg, EGL_GREEN_SIZE, &gs);
    getAttr(d, cfg, EGL_BLUE_SIZE, &bs);
    getAttr(d, cfg, EGL_ALPHA_SIZE, &as_);
    b1023logf("  cfg[%d] visual=0x%08x renderable=0x%x depth=%d stencil=%d "
         "surface=0x%x rgb=%d/%d/%d/%d\n", idx, vis, rt, dep, st, ss, rs, gs, bs, as_);
}

void* eglGetDisplay(void* n) {
    typedef void* (*p)(void*);
    p real = (p)dlsym(RTLD_NEXT, "eglGetDisplay");
    void* d = real(n);
    b1023logf("eglGetDisplay(native=%p) -> %p\n", n, d);
    return d;
}

int eglInitialize(EGLDisplay d, int* maj, int* min) {
    typedef int (*p)(EGLDisplay, int*, int*);
    p real = (p)dlsym(RTLD_NEXT, "eglInitialize");
    int r = real(d, maj, min);
    b1023logf("eglInitialize(%p) -> %d (%d.%d)\n", d, r, *maj, *min);
    return r;
}

int eglBindAPI(int api) {
    typedef int (*p)(int);
    p real = (p)dlsym(RTLD_NEXT, "eglBindAPI");
    int r = real(api);
    b1023logf("eglBindAPI(0x%x) -> %d\n", api, r);
    return r;
}

int eglChooseConfig(EGLDisplay d, const int* a, EGLConfig* cfgs, int n, int* got) {
    typedef int (*p)(EGLDisplay, const int*, EGLConfig*, int, int*);
    p real = (p)dlsym(RTLD_NEXT, "eglChooseConfig");
    int r = real(d, a, cfgs, n, got);
    int count = r && got ? *got : 0;
    static int call_no = 0;
    call_no++;
    b1023logf("eglChooseConfig #%d: ", call_no);
    log_attribs(a);
    b1023logf("  -> %d configs\n", count);
    if (r && cfgs && count > 0) {
        for (int i = 0; i < count; ++i)
            log_config(d, i, cfgs[i]);
    }
    return r;
}

int eglGetConfigAttrib(EGLDisplay d, EGLConfig c, int a, int* v) {
    typedef int (*p)(EGLDisplay, EGLConfig, int, int*);
    p real = (p)dlsym(RTLD_NEXT, "eglGetConfigAttrib");
    int r = real(d, c, a, v);
    b1023logf("eglGetConfigAttrib(0x%x) -> %d (%s)\n", a, r ? *v : -1,
         a == 0x3041 ? "NATIVE_VISUAL_ID"
         : a == 0x3040 ? "RENDERABLE_TYPE"
         : a == 0x3036 ? "DEPTH_SIZE"
         : a == 0x3037 ? "STENCIL_SIZE"
         : a == 0x3031 ? "SURFACE_TYPE"
         : a == 0x3032 ? "RED_SIZE"
         : a == 0x3033 ? "GREEN_SIZE"
         : a == 0x3034 ? "BLUE_SIZE"
         : a == 0x3035 ? "ALPHA_SIZE"
         : "?");
    return r;
}

void* eglCreateWindowSurface(EGLDisplay d, EGLConfig c, void* win, const int* a) {
    typedef void* (*p)(EGLDisplay, EGLConfig, void*, const int*);
    p real = (p)dlsym(RTLD_NEXT, "eglCreateWindowSurface");
    b1023logf("eglCreateWindowSurface(config=%p)\n", c);
    return real(d, c, win, a);
}

void* eglCreateContext(EGLDisplay d, EGLConfig c, void* share, const int* a) {
    typedef void* (*p)(EGLDisplay, EGLConfig, void*, const int*);
    p real = (p)dlsym(RTLD_NEXT, "eglCreateContext");
    b1023logf("eglCreateContext(config=%p)\n", c);
    return real(d, c, share, a);
}
