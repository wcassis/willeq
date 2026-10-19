// B1023 verification shim (v4, with constructor + getenv path for diagnosis).
// Proves: (1) the .so loads under LD_PRELOAD; (2) the client's first
// eglChooseConfig request carries DEPTH=16 STENCIL=0; (3) the driver returns
// only depth-24 (st 0/8) XRGB8888 configs -- no depth-16 8-bit-color config
// exists on Lima (Mesa dri_screen.c color_depth_match, mixed_color_depth_bits off).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EGL_SURFACE_TYPE 0x3033
#define EGL_RED_SIZE     0x3024
#define EGL_GREEN_SIZE   0x3023
#define EGL_BLUE_SIZE    0x3022
#define EGL_ALPHA_SIZE   0x3021
#define EGL_DEPTH_SIZE   0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_RENDERABLE   0x3040
#define EGL_VISUAL       0x302E
#define EGL_NONE         0x3038

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef int   EGLint;

static int log_fd = -1;
static const char* out_path(void){
    const char* p = getenv("B1023_OUT");
    return p ? p : "/tmp/b1023ev/verify.log";
}
static void ensure(void){
    if(log_fd>=0) return;
    log_fd = open(out_path(), O_CREAT|O_WRONLY|O_APPEND, 0644);
}
static void b1023log(const char* fmt, ...){
    if(log_fd<0) ensure();
    if(log_fd<0) return;
    char buf[2048]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    if(n>0) (void)!write(log_fd,buf,(size_t)n);
}

__attribute__((constructor))
static void b1023_ctor(void){
    ensure();
    b1023log("SHIM-LOADED pid=%d path=%s\n", (int)getpid(), out_path());
}

EGLint eglChooseConfig(EGLDisplay d, const EGLint* a, EGLConfig* c, EGLint n, EGLint* cn){
    typedef EGLint (*pfn)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
    static pfn real=0;
    if(!real) real=(pfn)dlsym(RTLD_NEXT,"eglChooseConfig");
    typedef EGLint (*gca)(EGLDisplay,EGLConfig,EGLint,EGLint*);
    static gca g=0;
    if(!g) g=(gca)dlsym(RTLD_NEXT,"eglGetConfigAttrib");

    static int calls=0; calls++;
    b1023log("=== eglChooseConfig #%d n=%d ===\n", calls,(int)n);
    for(int i=0; a && a[i]!=EGL_NONE && i<40; i+=2){
        const char* nm="ATTR";
        if(a[i]==EGL_SURFACE_TYPE) nm="SURFACE_TYPE";
        else if(a[i]==EGL_RED_SIZE) nm="RED";
        else if(a[i]==EGL_GREEN_SIZE) nm="GREEN";
        else if(a[i]==EGL_BLUE_SIZE) nm="BLUE";
        else if(a[i]==EGL_ALPHA_SIZE) nm="ALPHA";
        else if(a[i]==EGL_DEPTH_SIZE) nm="DEPTH";
        else if(a[i]==EGL_STENCIL_SIZE) nm="STENCIL";
        else if(a[i]==EGL_RENDERABLE) nm="RENDERABLE";
        b1023log("  0x%04x %s = %d\n", a[i], nm, a[i+1]);
    }
    EGLint r=real(d,a,c,n,cn);
    int got = cn?(int)*cn:-1;
    b1023log("  -> rc=%d numConfigs=%d\n",(int)r,got);
    if(calls==1 && c && got>0 && g){
        b1023log("  returned configs (first call, what the chooser sees):\n");
        for(int i=0;i<got;i++){
            EGLint v=0,dp=0,st=0;
            g(d,c[i],EGL_VISUAL,&v); g(d,c[i],EGL_DEPTH_SIZE,&dp); g(d,c[i],EGL_STENCIL_SIZE,&st);
            b1023log("    cfg[%d] visual=0x%08x depth=%d stencil=%d\n",i,(unsigned)v,(int)dp,(int)st);
        }
        b1023log("  (eglinfo cross-check: XRGB8888 visual 0x34325258 offers depth {0,24,24,24,24} -- no 16)\n");
    }
    return r;
}
